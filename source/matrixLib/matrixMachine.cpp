#include "matrixMachine.h"

#include <algorithm>
#include <cmath>

namespace matrixLib
{
	namespace
	{
		// timing resistor times capacitor per converter range: with the firmware's nominal
		// waveshape calibration a 2.5 V ramp for shape 0 (saw), 5 V for shape 63 (triangle)
		constexpr float RtCtLow = 7.34e-4f;
		constexpr float RtCtHigh = RtCtLow / 13.8f;

		constexpr float OutputGain = 0.18f;

		// A count of 1 leaves the square wave output of an 8253 without an edge for
		// milliseconds, so the converter gets no discharge pulse. The firmware does this for
		// the first 12 ms of a note when the key click of the DCO is on.
		constexpr uint32_t StoppedCount = 1;

		// Pitch steps up to three semitones are glided, see Machine::PitchGlide, and steps up
		// to an octave when they follow the one before at the pace of the firmware: a fast LFO
		// or a quick portamento, not a new note.
		constexpr float GlideRatio = 1.19f;
		constexpr float GlideStreamRatio = 2.0f;
		constexpr float GlideUsual = 0.0134f;       // seconds between two steps of the firmware
		constexpr float GlideShortest = 0.002f;
		constexpr float GlideLongest = 0.020f;
		constexpr float CountStays = 0.00025f;      // seconds before a new timer count is believed
		constexpr float NoteArrives = 0.04f;        // seconds from a note on to the pitch the firmware writes for it
		constexpr float HeightLag = 0.006f;         // seconds, each of the two stages that follow the ramp height
		constexpr float RangeSettles = 0.012f;      // seconds the waveshape voltage needs after a change of range
	}

	Machine::Machine()
	{
		setSamplerate(48000.0f);
	}

	void Machine::setSamplerate(const float _rate)
	{
		m_rate = _rate;
		m_cyclesPerSample = static_cast<double>(Hardware::CpuClock) / _rate;
		m_dcCoeff = 1.0f - 2.0f * 3.14159265f * 5.0f / _rate;
		m_cvK = 1.0f - std::exp(-1.0f / (Hardware::CvTimeConstant * _rate));
		m_heightK = 1.0f - std::exp(-1.0f / (HeightLag * _rate));
		m_hw.setCvJoin(static_cast<uint32_t>(GlideShortest * _rate), static_cast<uint32_t>(GlideLongest * _rate));
		for(auto& v : m_voices)
			v.setSamplerate(_rate * Oversampling);
	}

	void Machine::reset()
	{
		for(auto& v : m_voices)
			v.reset();
		m_glides = {};
		m_decimator.reset();
		m_cycleAcc = 0.0;
		m_dcIn = m_dcOut = m_lastSum = 0.0f;
		m_hw.reset();
		m_sampleCycle = m_hw.getCycles();
	}

	void Machine::expectNote(const bool _dco1, const bool _dco2)
	{
		const auto window = static_cast<uint32_t>(NoteArrives * m_rate);
		m_noteWindow[0] = _dco1 ? window : 0;
		m_noteWindow[1] = _dco2 ? window : 0;
	}

	float Machine::noise()
	{
		m_noiseState ^= m_noiseState << 13;
		m_noiseState ^= m_noiseState >> 17;
		m_noiseState ^= m_noiseState << 5;
		return static_cast<float>(static_cast<int32_t>(m_noiseState)) * (1.0f / 2147483648.0f);
	}

	float Machine::glide(PitchGlide& _g, uint32_t _count, const bool _noteDue)
	{
		// While the firmware rewrites a timer it stands still for some 20 microseconds and then
		// shows the stretch that ends the old cycle for some 80 more. Samples that fall there
		// must not sound: a count is taken once it has stayed a quarter of a millisecond.
		if(_count != _g.seen)
		{
			_g.seen = _count;
			_g.seenFor = 0;
		}
		else if(_g.seenFor < 0x7fffffffu)
		{
			++_g.seenFor;
		}
		if(_g.seenFor < static_cast<uint32_t>(CountStays * m_rate) && !isCalibrating())
			_count = _g.target;

		if(_count != _g.target)
		{
			const auto to = static_cast<float>(_count);
			const float ratio = _g.sinceMove <= static_cast<uint32_t>(GlideLongest * m_rate) ? GlideStreamRatio : GlideRatio;
			const auto longest = static_cast<uint32_t>(GlideLongest * m_rate);
			bool joined = _g.target > StoppedCount && _count > StoppedCount
				&& to < _g.value * ratio && _g.value < to * ratio;
			// A voice at rest that moves just after a note on is taking that note, however
			// near the one it played before: it starts in tune, as a lever or an LFO setting
			// out does not have to.
			if(_noteDue && _g.sinceMove > longest)
				joined = false;
			if(joined && !isCalibrating())
			{
				// arrive when the next step is due: after the time the last one took, or the
				// usual time between two steps when this is the first one after a rest
				const auto span = _g.sinceMove > longest ? static_cast<uint32_t>(GlideUsual * m_rate)
				                                         : std::max(_g.sinceMove, static_cast<uint32_t>(GlideShortest * m_rate));
				_g.left = std::max<uint32_t>(span, 1);
				_g.step = (to - _g.value) / static_cast<float>(_g.left);
			}
			else
			{
				_g.value = to;
				_g.left = 0;
			}
			_g.target = _count;
			_g.sinceMove = 0;
		}
		else if(_g.sinceMove < 0x7fffffffu)
		{
			++_g.sinceMove;
		}

		if(_g.left)
		{
			_g.value += _g.step;
			if(--_g.left == 0)
				_g.value = static_cast<float>(_g.target);
		}
		return _g.value;
	}

	void Machine::buildControls(const uint32_t _voice, Cem3396::Controls& _c)
	{
		const auto& cv = m_hw.getCvVolts();

		const uint32_t pA = m_hw.getDcoPeriod(_voice, 0);
		const uint32_t pB = m_hw.getDcoPeriod(_voice, 1);
		_c.periodA = glide(m_glides[_voice][0], pA, m_noteWindow[0] != 0) / Hardware::VoiceTimerClock;
		_c.periodB = glide(m_glides[_voice][1], pB, m_noteWindow[1] != 0) / Hardware::VoiceTimerClock;
		// the calibration keeps the converters as it has always seen them
		const bool calibrating = isCalibrating();
		_c.holdA = m_glides[_voice][0].target == StoppedCount && !calibrating;
		_c.holdB = m_glides[_voice][1].target == StoppedCount && !calibrating;
		_c.rtCtA = m_hw.getRangeHigh(_voice, 0) ? RtCtHigh : RtCtLow;
		_c.rtCtB = m_hw.getRangeHigh(_voice, 1) ? RtCtHigh : RtCtLow;

		// The firmware sets the waveshape voltage for the pitch it has just written, so that
		// voltage times period, the height of the ramp, stays what the wave shape asks for.
		// The voltage gets there a few milliseconds after the timer, through its sample and
		// hold, and the converter is still gliding to the new pitch besides: taken as they
		// are, the two give the ramp a dent at every step of the pitch, 75 times a second, a
		// hum under any fast vibrato. So the height the firmware means is followed with a
		// short lag instead, and the voltage is worked back from it for the pitch the
		// converter runs at.
		const auto waveshape = [&](PitchGlide& _g, const float _volts, const float _rtCt)
		{
			if(calibrating || _g.target <= StoppedCount || _g.value <= 0.0f)
			{
				_g.height1 = _g.height2 = _g.pitch = _g.rtCt = 0.0f;
				return _volts;
			}
			// When the converter changes range the voltage has to move by the ratio of the
			// two ranges, which again takes it some milliseconds: the height is held over
			// them, and the ramp does not leap.
			if(_rtCt != _g.rtCt)
			{
				if(_g.rtCt > 0.0f)
					_g.rangeHold = static_cast<uint32_t>(RangeSettles * m_rate);
				_g.rtCt = _rtCt;
			}
			// the voltage follows the pitch through its sample and hold: compare it with the
			// pitch seen through the same lag
			const float pitch = 1.0f / static_cast<float>(_g.target);
			if(_g.pitch <= 0.0f)
				_g.pitch = pitch;
			_g.pitch += (pitch - _g.pitch) * m_cvK;
			const float height = _volts / (_g.pitch * _rtCt);
			if(_g.height2 <= 0.0f)
				_g.height1 = _g.height2 = height;
			if(_g.rangeHold)
			{
				--_g.rangeHold;
			}
			else
			{
				_g.height1 += (height - _g.height1) * m_heightK;
				_g.height2 += (_g.height1 - _g.height2) * m_heightK;
			}
			return _g.height2 * _rtCt / _g.value;
		};
		_c.wsA = waveshape(m_glides[_voice][0], cv[Hardware::voiceCv(_voice, Hardware::CvWsA)], _c.rtCtA);
		_c.wsB = waveshape(m_glides[_voice][1], cv[Hardware::voiceCv(_voice, Hardware::CvWsB)], _c.rtCtB);
		m_rampPeak[_voice] = _c.rtCtA > 0.0f ? _c.wsA * _c.periodA / _c.rtCtA : 0.0f;
		_c.pwA = cv[Hardware::voiceCv(_voice, Hardware::CvPwA)];
		_c.pwB = cv[Hardware::voiceCv(_voice, Hardware::CvPwB)];
		_c.freq = cv[Hardware::voiceCv(_voice, Hardware::CvFreq)];
		_c.logGain = cv[Hardware::voiceCv(_voice, Hardware::CvLog)];
		_c.linGain = cv[Hardware::voiceCv(_voice, Hardware::CvLin)];
		_c.mod = cv[Hardware::voiceCv(_voice, Hardware::CvMod)];
		_c.balance = cv[Hardware::balanceCv(_voice)];
		_c.resonance = cv[Hardware::resonanceCv(_voice)];

		// $1D00: bits 5-4 drive the wave select level; bit 3 low restarts the timer of DCO 1
		// with DCO 2 (SYNC1), bit 2 low discharges its capacitor (SYNC2), see Cem3396
		const uint8_t ctl = m_hw.getLatch(0x1d00);
		switch((ctl >> 4) & 3)
		{
		case 3:  _c.slopedA = true;  _c.slopedB = false; break;
		case 2:  _c.slopedA = false; _c.slopedB = false; break;
		case 1:  _c.slopedA = false; _c.slopedB = true;  break;
		default: _c.slopedA = true;  _c.slopedB = true;  break;
		}
		_c.sync = static_cast<uint8_t>(3 - ((ctl >> 2) & 3));
		_c.noiseB = m_hw.getLatch(0x1c06) != 0;
	}

	void Machine::render(float* _out, const size_t _count)
	{
		auto& acia = m_hw.getAcia();
		Cem3396::Controls c;

		for(size_t i = 0; i < _count; ++i)
		{
			// the sample grid runs on its own clock, the CPU overshoots it by part of an instruction
			const uint64_t start = m_sampleCycle;
			m_cycleAcc += m_cyclesPerSample;
			const auto cycles = static_cast<uint64_t>(m_cycleAcc);
			m_cycleAcc -= static_cast<double>(cycles);
			const uint64_t end = start + cycles;
			m_sampleCycle = end;

			// voices run at twice the rate, the CPU and CVs at the output rate
			m_hw.advanceCv(m_cvK);
			for(auto& w : m_noteWindow)
				if(w)
					--w;
			const float n0 = noise();
			const float n1 = noise();
			float sum0 = 0.0f, sum1 = 0.0f;
			for(uint32_t v = 0; v < Hardware::VoiceCount; ++v)
			{
				buildControls(v, c);
				sum0 += m_voices[v].process(c, n0);
				sum1 += m_voices[v].process(c, n1);
			}
			const float sum = m_decimator.process(sum0, sum1);

			// output coupling capacitor
			const float y = sum - m_dcIn + m_dcCoeff * m_dcOut;
			m_dcIn = sum;
			m_dcOut = y;

			// the calibration comparator sees this sample ahead of the CPU, with the
			// zero crossing placed between the two samples
			const bool calibrating = isCalibrating();
			if(calibrating)
			{
				// comparator with a little hysteresis so the noise floor alone does not switch it
				constexpr float hysteresis = 2e-3f;
				const bool before = m_comparator;
				bool after = before;
				float threshold = 0.0f;
				if(!before && y > hysteresis) { after = true; threshold = hysteresis; }
				else if(before && y < -hysteresis) { after = false; threshold = -hysteresis; }

				uint64_t at = end;
				if(before != after && m_lastSum != y)
				{
					const float frac = std::clamp((m_lastSum - threshold) / (m_lastSum - y), 0.0f, 1.0f);
					at = start + static_cast<uint64_t>(frac * static_cast<float>(cycles));
				}
				acia.setCts(before, at, after);
				m_comparator = after;
			}
			else
			{
				acia.setCts(false, 0, false);
			}
			m_lastSum = y;
			if(onComparatorSample)
				onComparatorSample(y);

			m_hw.runUntil(end);

			_out[i] = calibrating ? 0.0f : std::tanh(y * OutputGain);
		}
	}
}
