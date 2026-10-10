#include "cem3396.h"

#include <algorithm>
#include <cmath>

namespace matrixLib
{
	namespace
	{
		constexpr float ShaperPeak = 2.5f;      // 5/24 Vcc: sloped output at maximum
		constexpr float ShaperEnd = 5.0f;       // 5/12 Vcc: sloped output back at minimum
		constexpr float RampLimit = 10.0f;      // where a capacitor that is not discharged stops
		constexpr double MaxRampPeriods = 64.0; // the same for a ramp counted in periods
		constexpr float Pi = 3.14159265358979f;

		// sloped waveform for a ramp voltage, 0..1
		float shaper(const float _v)
		{
			if(_v <= 0.0f) return 0.0f;
			if(_v <= ShaperPeak) return _v * (1.0f / ShaperPeak);
			if(_v <= ShaperEnd) return (ShaperEnd - _v) * (1.0f / ShaperPeak);
			return 0.0f;
		}

		float shaperSlope(const float _v)
		{
			if(_v < ShaperPeak) return 1.0f / ShaperPeak;
			if(_v < ShaperEnd) return -1.0f / ShaperPeak;
			return 0.0f;
		}

		// cubic B-spline BLEP and BLAMP residuals, _t = samples from the event, nonzero within +-2
		float blep(const float _t)
		{
			const float a = std::fabs(_t);
			if(a >= 2.0f) return 0.0f;
			float i;
			if(a < 1.0f)
				i = 0.5f + (4.0f * a - 2.0f * a * a * a + 0.75f * a * a * a * a) * (1.0f / 6.0f);
			else
			{
				const float u = 2.0f - a;
				i = 1.0f - u * u * u * u * (1.0f / 24.0f);
			}
			return _t < 0.0f ? 1.0f - i : i - 1.0f;
		}

		float blamp(const float _t)
		{
			const float a = std::fabs(_t);
			if(a >= 2.0f) return 0.0f;
			if(a < 1.0f)
				return 7.0f / 30.0f - 0.5f * a + (2.0f * a * a - 0.5f * a * a * a * a + 0.15f * a * a * a * a * a) * (1.0f / 6.0f);
			const float u = 2.0f - a;
			return u * u * u * u * u * (1.0f / 120.0f);
		}

		// corrections for the samples n-2 .. n+1 around the current sample n
		struct Corrections
		{
			float t[4] = {};

			void step(const float _h, const float _d)
			{
				for(int k = 0; k < 4; ++k)
					t[k] += _h * blep(_d + static_cast<float>(k - 2));
			}
			void kink(const float _slopeDelta, const float _d)
			{
				for(int k = 0; k < 4; ++k)
					t[k] += _slopeDelta * blamp(_d + static_cast<float>(k - 2));
			}
		};

		struct ConverterOut
		{
			float sloped = 0.0f;
			float pulse = 0.0f;
			bool wrapped = false;
			float wrapD = 0.0f;       // samples since the discharge
			bool fell = false;        // the timer output went low, half way through its period
			float fallD = 0.0f;       // samples since that edge
		};

		// advances one converter by one sample and returns band-limited sloped and pulse outputs
		template<typename TConverter>
		ConverterOut runConverter(TConverter& _state, const float _period, const bool _hold, const float _rtCt, const float _ws, const float _pw,
								  const float _invRate, Corrections& _sloped, Corrections& _pulse)
		{
			ConverterOut out;
			double& _phase = _state.phase;
			if(_period <= 0.0f || _rtCt <= 0.0f)
				return out;

			if(_hold)
			{
				// No discharge pulse: the capacitor charges on at the rate the waveshape input
				// sets, past the top of the ramp. The sloped output runs through its peak and
				// back to its minimum and rests there, the pulse output falls once.
				const float dv = std::max(0.0f, _ws) / _rtCt * _invRate;
				const float v0 = _state.lastRamp;
				const float v1 = std::min(v0 + dv, RampLimit);
				if(v1 > v0)
				{
					auto at = [&](const float _v) { return std::min(0.9999f, (v1 - _v) / dv); };
					if(v0 < ShaperPeak && v1 >= ShaperPeak)
						_sloped.kink(-2.0f / ShaperPeak * dv, at(ShaperPeak));
					if(v0 < ShaperEnd && v1 >= ShaperEnd)
						_sloped.kink(1.0f / ShaperPeak * dv, at(ShaperEnd));
					if(_pw > 0.0f && v0 < _pw && v1 >= _pw)
						_pulse.step(-2.0f, at(_pw));
				}
				_state.lastRamp = v1;
				_state.held = true;
				out.sloped = shaper(v1) * 2.0f - 1.0f;
				out.pulse = (v1 < _pw ? 2.0f : 0.0f) - 2.0f * _state.duty;
				return out;
			}

			const double dphase = static_cast<double>(_invRate) / _period;
			if(dphase >= 0.5)
			{
				// discharged faster than the capacitor can charge: the ramp stays near 0 V
				_phase = 0.0;
				_state.offset = 0.0;
				_state.lastRamp = 0.0f;
				_state.held = false;
				return out;
			}
			const float vp = std::max(0.0f, _ws) * _period / _rtCt;    // ramp peak for this period
			const float dvPerSample = vp * static_cast<float>(dphase);

			if(_state.held)
			{
				// the pulses are back: the first one discharges whatever the capacitor holds
				const float v = _state.lastRamp;
				_sloped.step(-shaper(v), 0.9999f);
				_sloped.kink((shaperSlope(0.0f) - shaperSlope(v)) * dvPerSample, 0.9999f);
				if(_pw > 0.0f && v >= _pw)
					_pulse.step(2.0f, 0.9999f);
				_phase = 0.0;
				_state.offset = 0.0;
				_state.held = false;
			}

			const double start = _phase;
			double end = start + dphase;
			// the ramp runs with the timer unless a sync edge has moved one against the other
			double offset = _state.offset;

			// shaper corners and the pulse edge between two phases of the same ramp,
			// _after = samples from the segment end to the output sample
			auto crossings = [&](const double _p0, const double _p1, const float _after)
			{
				const float v0 = vp * static_cast<float>(_p0 + offset);
				const float v1 = vp * static_cast<float>(_p1 + offset);
				auto at = [&](const float _v) { return std::min(0.9999f, (v1 - _v) / std::max(1e-9f, dvPerSample) + _after); };

				if(v0 < ShaperPeak && v1 >= ShaperPeak)
					_sloped.kink(-2.0f / ShaperPeak * dvPerSample, at(ShaperPeak));
				if(v0 < ShaperEnd && v1 >= ShaperEnd)
					_sloped.kink(1.0f / ShaperPeak * dvPerSample, at(ShaperEnd));
				if(_pw > 0.0f && v0 < _pw && v1 >= _pw)
					_pulse.step(-2.0f, at(_pw));
			};

			if(end >= 1.0)
			{
				end -= 1.0;
				const float d = static_cast<float>(end / dphase);
				crossings(start, 1.0, d);

				const float vEnd = std::min(vp * static_cast<float>(1.0 + offset), RampLimit);

				_sloped.step(-shaper(vEnd), d);
				_sloped.kink((shaperSlope(0.0f) - shaperSlope(vEnd)) * dvPerSample, d);
				if(_pw > 0.0f && vEnd >= _pw)
					_pulse.step(2.0f, d);

				// its own pulse brings ramp and timer together again
				offset = 0.0;
				crossings(0.0, end, 0.0f);
				out.wrapped = true;
				out.wrapD = d;
			}
			else
			{
				crossings(start, end, 0.0f);
				if(start < 0.5 && end >= 0.5)
				{
					out.fell = true;
					out.fallD = static_cast<float>((end - 0.5) / dphase);
				}
			}

			_phase = end;
			_state.offset = offset;
			const float v = std::min(vp * static_cast<float>(end + offset), RampLimit);
			out.sloped = shaper(v) * 2.0f - 1.0f;
			// Centred on its own duty cycle, so changing or muting the width does not step the DC
			// level. A comparator that never trips holds the output high, or low with the width at zero.
			const float duty = _pw <= 0.0f ? 0.0f : (_pw < vp ? _pw / vp : 1.0f);
			out.pulse = (_pw > 0.0f && v < _pw ? 2.0f : 0.0f) - 2.0f * duty;
			_state.lastRamp = v;
			_state.duty = duty;
			return out;
		}
	}

	void Cem3396::setSamplerate(const float _rate)
	{
		m_rate = _rate;
		m_invRate = 1.0f / _rate;
		// OB-Xd style first stage damping; kept light, a strong limit on the state turns into a
		// limit cycle at Nyquist when the cutoff is near the top
		m_damp = 0.2f * std::sqrt(44000.0f / _rate);
		m_cvCoeff = 1.0f - std::exp(-1.0f / (0.001f * _rate));
		m_dampInv = 1.0f / m_damp;
		// The noise is one random value per sample, so its level per hertz falls as the rate
		// rises. Kept at what it is at 96 kHz, the rate the voices run at for a 48 kHz host.
		m_noiseGain = std::sqrt(_rate / 96000.0f);
	}

	void Cem3396::reset()
	{
		m_a = {};
		m_b = {};
		for(int i = 0; i < 3; ++i)
			m_histA[i] = m_histB[i] = 0.0f;
		m_smoothInit = false;
		for(auto& s : m_s)
			s = 0.0f;
	}

	float Cem3396::filter(const float _in, const float _cutoff, const float _k)
	{
		const float g = std::tan(Pi * _cutoff * m_invRate);
		const float G = g / (1.0f + g);
		const float inv = 1.0f / (1.0f + g);

		const float S = G * G * G * m_s[0] * inv + G * G * m_s[1] * inv + G * m_s[2] * inv + m_s[3] * inv;
		const float G4 = G * G * G * G;

		// zero delay feedback solve with the input stage saturating, then OB-Xd style damping
		// of the first stage state inside the loop
		float x = std::tanh((_in - _k * S) / (1.0f + _k * G4));
		for(size_t i = 0; i < 4; ++i)
		{
			auto& s = m_s[i];
			const float v = (x - s) * G;
			const float y = v + s;
			s = y + v;
			if(i == 0)
				s = std::atan(s * m_damp) * m_dampInv;
			x = y;
		}
		return x;
	}

	float Cem3396::process(const Controls& _in, const float _noise)
	{
		// The control inputs sit on the sample and hold network, which the hardware model
		// already runs. Only the two gain inputs have a further 1 MOhm and 1 nF at the pin.
		if(!m_smoothInit)
		{
			m_smooth = _in;
			m_smoothInit = true;
		}
		auto& c = m_smooth;
		const float cvK = m_cvCoeff;
		auto slew = [cvK](float& _v, const float _target) { _v += (_target - _v) * cvK; };
		c.wsA = _in.wsA;
		c.wsB = _in.wsB;
		c.pwA = _in.pwA;
		c.pwB = _in.pwB;
		c.balance = _in.balance;
		c.freq = _in.freq;
		c.resonance = _in.resonance;
		c.mod = _in.mod;
		slew(c.logGain, _in.logGain);
		slew(c.linGain, _in.linGain);
		c.periodA = _in.periodA;
		c.periodB = _in.periodB;
		c.holdA = _in.holdA;
		c.holdB = _in.holdB;
		c.rtCtA = _in.rtCtA;
		c.rtCtB = _in.rtCtB;
		c.slopedA = _in.slopedA;
		c.slopedB = _in.slopedB;
		c.noiseB = _in.noiseB;
		c.sync = _in.sync;
		const Controls& _c = c;

		Corrections slopedA, pulseA, slopedB, pulseB;

		// converter B first: with sync its timer acts on A
		ConverterOut b;
		if(_c.noiseB)
		{
			b.sloped = _noise * m_noiseGain;
			b.pulse = 0.0f;
			// the timer runs on under the noise
			const double dB = _c.periodB > 0.0f && !_c.holdB ? static_cast<double>(m_invRate) / _c.periodB : 1.0;
			if(dB < 0.5)
			{
				const double end = m_b.phase + dB;
				if(m_b.phase < 0.5 && end >= 0.5)
				{
					b.fell = true;
					b.fallD = static_cast<float>((end - 0.5) / dB);
				}
				m_b.phase = end >= 1.0 ? end - 1.0 : end;
			}
		}
		else
		{
			b = runConverter(m_b, _c.periodB, _c.holdB, _c.rtCtB, _c.wsB, _c.pwB, m_invRate, slopedB, pulseB);
		}

		// Sync, as the voice board does it. The falling edge of timer B, half way through the
		// period of DCO 2, reaches DCO 1 by two ways, one per control bit. One discharges the
		// timing capacitor of A and leaves its timer alone. The other pulses the GATE of timer
		// A, which starts its period again; its output is forced high, and only if it was low
		// (the second half of the period) is that a rising edge that discharges the capacitor
		// too. Otherwise the ramp keeps climbing through the restarted period.
		if(b.fell && _c.sync && !_c.holdA && _c.periodA > 0.0f && _c.rtCtA > 0.0f)
		{
			const double dA = static_cast<double>(m_invRate) / _c.periodA;
			const double timer = m_a.phase + dA * (1.0 - b.fallD);        // of A, at the edge
			// past 1 the pulse of A itself came a moment before the edge and has done the work
			if(dA < 0.5 && timer < 1.0)
			{
				const bool restart = (_c.sync & 2) != 0;
				const bool discharge = (_c.sync & 1) != 0 || timer >= 0.5;
				const double ramp = timer + m_a.offset;
				if(discharge && ramp > 0.0)
				{
					// band-limited like the converter's own discharge
					const float vp = std::max(0.0f, _c.wsA) * _c.periodA / _c.rtCtA;
					const float v = std::min(vp * static_cast<float>(ramp), RampLimit);
					slopedA.step(-shaper(v), b.fallD);
					slopedA.kink((shaperSlope(0.0f) - shaperSlope(v)) * vp * static_cast<float>(dA), b.fallD);
					if(_c.pwA > 0.0f && v >= _c.pwA)
						pulseA.step(2.0f, b.fallD);
				}
				if(restart)
				{
					m_a.phase = -dA * (1.0 - b.fallD);
					// a ramp that is never discharged ends at the top of the capacitor's range
					m_a.offset = discharge ? 0.0 : std::min(ramp, MaxRampPeriods);
				}
				else
				{
					m_a.offset = -timer;
				}
			}
		}

		const ConverterOut a = runConverter(m_a, _c.periodA, _c.holdA, _c.rtCtA, _c.wsA, _c.pwA, m_invRate, slopedA, pulseA);

		auto delay = [](float* _h, const float _naive, const Corrections& _pulse, const Corrections& _sloped, const bool _useSloped)
		{
			float c[4];
			for(int k = 0; k < 4; ++k)
				c[k] = _pulse.t[k] + (_useSloped ? _sloped.t[k] * 2.0f : 0.0f);
			const float out = _h[0] + c[0];
			_h[0] = _h[1] + c[1];
			_h[1] = _h[2] + _naive + c[2];
			_h[2] = c[3];
			return out;
		};
		const float convA = delay(m_histA, a.pulse + (_c.slopedA ? a.sloped : 0.0f), pulseA, slopedA, _c.slopedA);
		const float convB = delay(m_histB, b.pulse + (_c.slopedB ? b.sloped : 0.0f), pulseB, slopedB, _c.slopedB);

		// balance: differential pair, 80 dB of the other converter at about +-2 V
		const float gA = 1.0f / (1.0f + std::exp(-4.6f * _c.balance));
		const float mix = convA * gA + convB * (1.0f - gA);

		// cutoff: -0.5 V per octave; FM from converter A up to 2x. The datasheet gives 500 to
		// 980 Hz at 0 V (700 typical) for the 33 nF and 470 pF capacitors the voice board uses.
		// The figure here goes with Hardware::FilterRangeLow: together they put the filter
		// where the firmware's default calibration table expects it.
		const float depth = std::clamp(_c.mod * (1.0f / 4.5f), 0.0f, 1.1f) * 2.0f;
		float fc = 974.0f * std::exp2(-_c.freq * 2.0f) * std::max(0.0f, 1.0f + depth * convA * 0.5f);
		fc = std::clamp(fc, 5.0f, m_rate * 0.45f);

		const float k = std::clamp(_c.resonance, 0.0f, 5.0f);
		// the analog noise floor seeds self-oscillation near the resonance threshold
		const float y = filter(mix * 0.5f + _noise * 1e-4f, fc, k) * 2.0f * (1.0f + 0.25f * std::min(k, 4.0f));

		// audio taper VCA: exponential to -15 dB at 3.3 V, then linear to 0 dB at 5 V
		float log;
		if(_c.logGain <= 0.3f)
			log = 0.0f;
		else if(_c.logGain < 3.3f)
			log = std::pow(10.0f, (-90.0f + (_c.logGain - 0.3f) * 25.0f) * 0.05f);
		else
			log = 0.178f + (std::min(_c.logGain, 5.0f) - 3.3f) * (0.822f / 1.7f);

		// linear VCA. The firmware adds the matrix modulation of VCA 2 to its control voltage
		// even while the envelope rests, so patches that route velocity or tracking there
		// (OBXA-A7 and others) leave it at 0.02 to 0.07 V for good. The cell has to be shut
		// at that level or those patches drone with no key down. The datasheet puts the "on"
		// threshold of the linear input between 0 and 150 mV (90 dB attenuation below it).
		constexpr float linOff = 0.1f;
		const float lin = std::clamp((_c.linGain - linOff) * (1.0f / (4.5f - linOff)), 0.0f, 1.1f);

		return y * log * lin;
	}
}
