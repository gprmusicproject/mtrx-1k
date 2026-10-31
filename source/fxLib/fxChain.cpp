#include "fxChain.h"
#include "fxOwn.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fxLib
{
	namespace
	{
		constexpr auto K = ControlKind::Knob;
		constexpr auto S = ControlKind::Switch;
		constexpr auto P = ControlUnit::Percent;
		constexpr auto T = ControlUnit::Time;
		constexpr auto B = ControlUnit::Bipolar;

		constexpr auto St = ControlUnit::Semitones;

		enum ModType { ModChorus, ModFlanger, ModPhaser, ModEnsemble };
		const char* const ChorusModes[] = {"I", "II", "I+II"};
		const char* const EnsembleModes[] = {"Strings", "Chorus", "Both"};
		enum DelayType { DelayMono, DelayStereo };
		constexpr int PingPongControl = 6;   // of the stereo delay
		enum ReverbType { VerbHall, VerbRoom, VerbPlate, VerbSpring, VerbShimmer };

		const TypeInfo g_modulation[] =
		{
			{"Chorus",  2, {{{"Mode", 0.0f, K, P, 3, ChorusModes}, {"Mix", 0.5f, K, P}}}},
			{"Flanger", 5, {{{"Rate", 0.25f, K, P}, {"Depth", 0.6f, K, P}, {"Manual", 0.3f, K, P}, {"Feedback", 0.75f, K, B}, {"Mix", 0.5f, K, P}}}},
			{"Phaser",  4, {{{"Rate", 0.3f, K, P}, {"Depth", 0.7f, K, P}, {"Feedback", 0.5f, K, P}, {"Mix", 0.5f, K, P}}}},
			{"Ensemble", 3, {{{"Mode", 0.0f, K, P, 3, EnsembleModes}, {"Depth", 0.6f, K, P}, {"Mix", 0.7f, K, P}}}},
		};
		const TypeInfo g_delay[] =
		{
			{"Mono",   6, {{{"Time", 0.6f, K, T}, {"Feedback", 0.3f, K, P}, {"Tone", 0.5f, K, P}, {"Flutter", 0.0f, K, P}, {"Mix", 0.35f, K, P}, {"Sync", 0.0f, S, P}}}},
			{"Stereo", 8, {{{"Time L", 0.55f, K, T}, {"Time R", 0.65f, K, T}, {"Feedback", 0.3f, K, P}, {"Tone", 0.5f, K, P}, {"Flutter", 0.0f, K, P}, {"Mix", 0.35f, K, P}, {"Ping Pong", 0.0f, S, P}, {"Sync", 0.0f, S, P}}}},
		};
		const TypeInfo g_reverb[] =
		{
			{"Hall",    5, {{{"Decay", 0.5f, K, P}, {"Size", 0.6f, K, P}, {"Tone", 0.6f, K, P}, {"Predelay", 0.15f, K, P}, {"Mix", 0.3f, K, P}}}},
			{"Room",    4, {{{"Decay", 0.4f, K, P}, {"Tone", 0.5f, K, P}, {"Position", 0.5f, K, P}, {"Mix", 0.3f, K, P}}}},
			{"Plate",   4, {{{"Decay", 0.5f, K, P}, {"Tone", 0.6f, K, P}, {"Predelay", 0.0f, K, P}, {"Mix", 0.25f, K, P}}}},
			{"Spring",  3, {{{"Dwell", 0.5f, K, P}, {"Tone", 0.5f, K, P}, {"Mixer", 0.3f, K, P}}}},
			// the starting values GPR Music Project chose: decay 7.3 s, low decay 3.9 s, damping
			// 6 kHz, A and B both +12 at half level, feedback 76, the loop cut at 250 Hz and 6 kHz
			{"Shimmer", 11, {{{"Decay", 0.9f, K, P}, {"Low Decay", 0.42f, K, P}, {"Tone", 4.5f / 10.5f, K, P},
			                  {"Pitch A", 36.0f / 48.0f, K, St, 49}, {"Level A", 0.5f, K, P}, {"Pitch B", 36.0f / 48.0f, K, St, 49}, {"Level B", 0.5f, K, P},
			                  {"Feedback", 0.76f, K, P}, {"Low Cut", 200.0f / 550.0f, K, P}, {"High Cut", 0.33f, K, P}, {"Mix", 0.6f, K, P}}}},
		};
		// a pitch of the shimmer: -24 to +24 semitones
		constexpr int SemitoneRange = 24;

		struct Division { const char* name; double beats; };
		const Division g_divisions[] =
		{
			{"1/1", 4.0}, {"1/2 D", 3.0}, {"1/2", 2.0}, {"1/2 T", 4.0 / 3.0},
			{"1/4 D", 1.5}, {"1/4", 1.0}, {"1/4 T", 2.0 / 3.0},
			{"1/8 D", 0.75}, {"1/8", 0.5}, {"1/8 T", 1.0 / 3.0},
			{"1/16 D", 0.375}, {"1/16", 0.25}, {"1/16 T", 1.0 / 6.0}, {"1/32", 0.125},
		};
		constexpr int DivisionCount = static_cast<int>(sizeof(g_divisions) / sizeof(g_divisions[0]));

		constexpr float FadeSeconds = 0.01f;

		// 14 bits of a value in two bytes of 7
		uint16_t toBits(const float _v) { return static_cast<uint16_t>(std::lround(std::clamp(_v, 0.0f, 1.0f) * 16383.0f)); }
		// 7: the shimmer of Shimmer-Reverb, with other controls; 6: the mix of the chain, the
		// chorus with modes, the pitch and feedback of the shimmer; 5: the ensemble came; 4: the spring of 3 had other controls; 1 and 2 were partly others' effects
		constexpr uint8_t StateVersion = 7;
		constexpr uint8_t OldestReadable = 5;

		// how many controls a type had in a version of the settings
		int controlsIn(const int _version, const int _rack, const int _type)
		{
			if(_version == 5 && _rack == RackModulation && _type == ModChorus)
				return 3;
			if(_version == 5 && _rack == RackReverb && _type == VerbShimmer)
				return 8;
			return typeInfo(_rack, _type).controlCount;
		}

		// where a control of a version of the settings is now; -1 when it is gone
		int placeNow(const int _version, const int _rack, const int _type, const int _control)
		{
			if(_version == 5 && _rack == RackModulation && _type == ModChorus)
				return _control == 2 ? 1 : -1;    // rate and depth went, the mix stays
			// of another shimmer only the mix, the last control, carries over
			if(_version < 7 && _rack == RackReverb && _type == VerbShimmer)
				return _control == controlsIn(_version, _rack, _type) - 1 ? typeInfo(_rack, _type).controlCount - 1 : -1;
			return _control;
		}
	}

	// semitones of a value 0-1
	int semitonesOf(const float _value) { return static_cast<int>(std::lround(_value * 2.0f * SemitoneRange)) - SemitoneRange; }

	const char* rackName(const int _rack)
	{
		static const char* const names[RackCount] = {"Modulation", "Delay", "Reverb"};
		return _rack >= 0 && _rack < RackCount ? names[_rack] : "";
	}

	int typeCount(const int _rack)
	{
		switch(_rack)
		{
		case RackModulation: return 4;
		case RackDelay:      return 2;
		case RackReverb:     return 5;
		default:             return 0;
		}
	}

	const TypeInfo& typeInfo(const int _rack, const int _type)
	{
		const int t = std::clamp(_type, 0, typeCount(_rack) - 1);
		switch(_rack)
		{
		case RackModulation: return g_modulation[t];
		case RackDelay:      return g_delay[t];
		default:             return g_reverb[t];
		}
	}

	int delaySyncControl(const int _type) { return typeInfo(RackDelay, _type).controlCount - 1; }
	int delayDivisionCount() { return DivisionCount; }
	const char* delayDivisionName(const int _division) { return g_divisions[std::clamp(_division, 0, DivisionCount - 1)].name; }
	double delayDivisionBeats(const int _division) { return g_divisions[std::clamp(_division, 0, DivisionCount - 1)].beats; }
	int delayDivisionOf(const float _value) { return std::clamp(static_cast<int>(std::lround(_value * (DivisionCount - 1))), 0, DivisionCount - 1); }

	struct Chain::RackState
	{
		int playing = -1;      // engine that sounds, -1 none
		float gain = 0.0f;     // its share against the dry signal while fading
		float input = 0.0f;    // how much of the signal reaches it: it starts empty, and a signal
		                       // that set in at full level would come back as a click in every echo
	};

	Chain::Chain()
	{
		m_engines[RackModulation][ModChorus] = createChorus();
		m_engines[RackModulation][ModFlanger] = createFlanger();
		m_engines[RackModulation][ModPhaser] = createPhaser();
		m_engines[RackModulation][ModEnsemble] = createEnsemble();
		m_engines[RackDelay][DelayMono] = createEcho(EchoMode::Mono);
		m_engines[RackDelay][DelayStereo] = createEcho(EchoMode::Stereo);
		m_engines[RackDelay][PingPongEngine] = createEcho(EchoMode::PingPong);
		m_engines[RackReverb][VerbHall] = createHall();
		m_engines[RackReverb][VerbRoom] = createRoom();
		m_engines[RackReverb][VerbPlate] = createPlate();
		m_engines[RackReverb][VerbSpring] = createSpring();
		m_engines[RackReverb][VerbShimmer] = createShimmer();
		for(auto& r : m_racks)
			r = std::make_unique<RackState>();
		reset();
	}

	Chain::~Chain() = default;

	void Chain::reset()
	{
		m_chainOn.store(false);
		m_mix.store(1.0f);
		for(int r = 0; r < RackCount; ++r)
		{
			m_rackOn[r].store(false);
			m_type[r].store(0);
			for(int t = 0; t < MaxTypes; ++t)
			{
				for(int c = 0; c < MaxControls; ++c)
				{
					const bool exists = t < typeCount(r) && c < typeInfo(r, t).controlCount;
					m_values[r][t][c].store(exists ? typeInfo(r, t).controls[c].initial : 0.0f);
				}
			}
		}
	}

	void Chain::prepare(const double _sampleRate, const int _maxBlock)
	{
		const bool newRate = _sampleRate != m_rate.load();
		m_rate.store(_sampleRate);
		m_maxBlock = std::max(_maxBlock, 64);
		for(auto* b : {&m_wetL, &m_wetR, &m_dryL, &m_dryR, &m_inL, &m_inR, &m_srcL, &m_srcR})
			b->assign(static_cast<size_t>(m_maxBlock), 0.0f);
		m_fadeStep = 1.0f / (FadeSeconds * static_cast<float>(_sampleRate));
		m_mixGlide = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _sampleRate)));
		m_mixNow = m_mix.load();

		// buffers for this rate, nothing playing
		if(newRate)
		{
			for(int r = 0; r < RackCount; ++r)
			{
				for(auto& e : m_engines[r])
					if(e)
						e->prepare(_sampleRate);
				*m_racks[r] = RackState{};
			}
		}
	}

	void Chain::setType(const int _rack, const int _type)
	{
		m_type[_rack].store(std::clamp(_type, 0, typeCount(_rack) - 1));
	}

	void Chain::setValue(const int _rack, const int _type, const int _control, const float _value)
	{
		if(_rack < 0 || _rack >= RackCount || _type < 0 || _type >= MaxTypes || _control < 0 || _control >= MaxControls)
			return;
		m_values[_rack][_type][_control].store(std::clamp(_value, 0.0f, 1.0f));
	}

	float Chain::getValue(const int _rack, const int _type, const int _control) const
	{
		if(_rack < 0 || _rack >= RackCount || _type < 0 || _type >= MaxTypes || _control < 0 || _control >= MaxControls)
			return 0.0f;
		return m_values[_rack][_type][_control].load();
	}

	// the time of a delay control in seconds: a division of the beat when synced, otherwise
	// the knob over the range of the delay, evenly in octaves
	double Chain::delaySeconds(const int _type, const int _control, const double _bpm) const
	{
		const float v = getValue(RackDelay, _type, _control);
		if(getValue(RackDelay, _type, delaySyncControl(_type)) >= 0.5f)
			return delayDivisionBeats(delayDivisionOf(v)) * 60.0 / std::max(20.0, _bpm);
		return EchoShortest * std::pow(EchoLongest / EchoShortest, static_cast<double>(v));
	}

	bool Chain::isDelayDivisionReachable(const int _type, const int _control, const double _bpm) const
	{
		const double s = delayDivisionBeats(delayDivisionOf(getValue(RackDelay, _type, _control))) * 60.0 / std::max(20.0, _bpm);
		return s >= EchoShortest * 0.999 && s <= EchoLongest * 1.001;
	}

	std::string Chain::valueText(const int _rack, const int _type, const int _control) const
	{
		const auto& info = typeInfo(_rack, _type);
		if(_control < 0 || _control >= info.controlCount)
			return {};
		const auto& c = info.controls[_control];
		const float v = getValue(_rack, _type, _control);
		char text[32];
		if(c.kind == ControlKind::Switch)
			return v >= 0.5f ? "On" : "Off";
		if(c.steps > 1 && c.labels)
			return c.labels[std::clamp(static_cast<int>(std::lround(v * (c.steps - 1))), 0, c.steps - 1)];
		if(c.unit == ControlUnit::Semitones)
		{
			const int n = semitonesOf(v);
			std::snprintf(text, sizeof(text), n ? "%+d st" : "%d st", n);
			return text;
		}
		if(c.unit == ControlUnit::Time)
		{
			if(getValue(_rack, _type, delaySyncControl(_type)) >= 0.5f)
				return delayDivisionName(delayDivisionOf(v));
			const double s = delaySeconds(_type, _control, 120.0);
			if(s >= 1.0)
				std::snprintf(text, sizeof(text), "%.2fs", s);
			else
				std::snprintf(text, sizeof(text), "%.0fms", s * 1000.0);
			return text;
		}
		if(c.unit == ControlUnit::Bipolar)
		{
			const int percent = static_cast<int>(std::lround((v - 0.5f) * 200.0f));
			std::snprintf(text, sizeof(text), percent ? "%+d" : "%d", percent);
			return text;
		}
		std::snprintf(text, sizeof(text), "%.0f", v * 100.0f);
		return text;
	}

	int Chain::engineFor(const int _rack) const
	{
		const int type = getType(_rack);
		if(_rack == RackDelay && type == DelayStereo && getValue(RackDelay, DelayStereo, PingPongControl) >= 0.5f)
			return PingPongEngine;
		return type;
	}

	int Chain::typeOfEngine(const int _rack, const int _engine) const
	{
		return _rack == RackDelay && _engine == PingPongEngine ? DelayStereo : _engine;
	}

	void Chain::renderEngine(const int _rack, const int _engine, float* _l, float* _r, const int _samples, const double _bpm)
	{
		auto* fx = m_engines[_rack][_engine].get();
		if(!fx)
		{
			std::copy(m_inL.begin(), m_inL.begin() + _samples, _l);
			std::copy(m_inR.begin(), m_inR.begin() + _samples, _r);
			return;
		}
		const int type = typeOfEngine(_rack, _engine);
		std::array<float, MaxControls> v{};
		for(int c = 0; c < MaxControls; ++c)
			v[static_cast<size_t>(c)] = getValue(_rack, type, c);
		double timeL = 0.0, timeR = 0.0;
		if(_rack == RackDelay)
		{
			timeL = delaySeconds(type, 0, _bpm);
			timeR = type == DelayStereo ? delaySeconds(type, 1, _bpm) : timeL;
		}
		fx->played = true;
		fx->process(m_inL.data(), m_inR.data(), _l, _r, _samples, v.data(), timeL, timeR);
	}

	void Chain::process(float* _left, float* _right, const int _samples, const double _bpm)
	{
		if(m_rate.load() <= 0.0 || m_maxBlock == 0)
			return;

		// longer blocks than prepared for go in pieces
		if(_samples > m_maxBlock)
		{
			for(int done = 0; done < _samples; done += m_maxBlock)
				process(_left + done, _right + done, std::min(m_maxBlock, _samples - done), _bpm);
			return;
		}

		// the instrument's own sound, for the mix of the chain at the end
		const float mix = m_mix.load();
		const bool mixing = mix < 1.0f || m_mixNow < 1.0f;
		if(mixing)
		{
			std::copy(_left, _left + _samples, m_srcL.data());
			std::copy(_right, _right + _samples, m_srcR.data());
		}

		const bool chainOn = m_chainOn.load();
		for(int r = 0; r < RackCount; ++r)
		{
			auto& state = *m_racks[r];
			const int want = chainOn && m_rackOn[r].load() ? engineFor(r) : -1;

			// another engine, or none: the one that plays fades out first; one that has
			// played before starts again from silence
			if(state.playing != want && state.gain <= 0.0f)
			{
				state.playing = want;
				state.input = 0.0f;
				if(want >= 0)
					if(auto* fx = m_engines[r][want].get(); fx && fx->played)
						fx->clear();
			}
			if(state.playing < 0)
				continue;

			const float target = state.playing == want ? 1.0f : 0.0f;
			std::copy(_left, _left + _samples, m_dryL.data());
			std::copy(_right, _right + _samples, m_dryR.data());
			float in = state.input;
			for(int i = 0; i < _samples; ++i)
			{
				in = std::min(1.0f, in + m_fadeStep);
				m_inL[i] = m_dryL[i] * in;
				m_inR[i] = m_dryR[i] * in;
			}
			state.input = in;
			renderEngine(r, state.playing, m_wetL.data(), m_wetR.data(), _samples, _bpm);

			float g = state.gain;
			for(int i = 0; i < _samples; ++i)
			{
				g = target > g ? std::min(target, g + m_fadeStep) : std::max(target, g - m_fadeStep);
				_left[i] = m_dryL[i] + g * (m_wetL[i] - m_dryL[i]);
				_right[i] = m_dryR[i] + g * (m_wetR[i] - m_dryR[i]);
			}
			state.gain = g;
		}

		if(mixing)
		{
			float m = m_mixNow;
			for(int i = 0; i < _samples; ++i)
			{
				m += m_mixGlide * (mix - m);
				_left[i] = m_srcL[i] + m * (_left[i] - m_srcL[i]);
				_right[i] = m_srcR[i] + m * (_right[i] - m_srcR[i]);
			}
			// close enough is there, so that a full mix costs nothing again
			m_mixNow = std::fabs(mix - m) < 1.0e-4f ? mix : m;
		}
	}

	// version, chain on, its mix in 14 bits, then per rack: on, type, and every control of
	// every type in 14 bits. Version 5 had no mix.
	std::vector<uint8_t> Chain::write() const
	{
		std::vector<uint8_t> d;
		d.push_back(StateVersion);
		d.push_back(m_chainOn.load() ? 1 : 0);
		const auto mixBits = toBits(m_mix.load());
		d.push_back(static_cast<uint8_t>(mixBits >> 7));
		d.push_back(static_cast<uint8_t>(mixBits & 0x7f));
		for(int r = 0; r < RackCount; ++r)
		{
			d.push_back(m_rackOn[r].load() ? 1 : 0);
			d.push_back(static_cast<uint8_t>(m_type[r].load()));
			for(int t = 0; t < typeCount(r); ++t)
			{
				for(int c = 0; c < typeInfo(r, t).controlCount; ++c)
				{
					const auto bits = toBits(getValue(r, t, c));
					d.push_back(static_cast<uint8_t>(bits >> 7));
					d.push_back(static_cast<uint8_t>(bits & 0x7f));
				}
			}
		}
		return d;
	}

	bool Chain::read(const uint8_t* _data, const size_t _size)
	{
		if(_size < 1 || _data[0] < OldestReadable || _data[0] > StateVersion)
			return false;
		const int version = _data[0];
		const bool hasMix = version >= 6;
		size_t expected = hasMix ? 4 : 2;
		for(int r = 0; r < RackCount; ++r)
		{
			expected += 2;
			for(int t = 0; t < typeCount(r); ++t)
				expected += 2 * static_cast<size_t>(controlsIn(version, r, t));
		}
		if(_size != expected)
			return false;
		for(size_t i = 0; i < _size; ++i)
			if(_data[i] > 0x7f)
				return false;

		// what the settings do not hold starts where it starts
		reset();
		size_t p = 1;
		m_chainOn.store(_data[p++] != 0);
		if(hasMix)
		{
			m_mix.store(static_cast<float>((_data[p] << 7) | _data[p + 1]) / 16383.0f);
			p += 2;
		}
		for(int r = 0; r < RackCount; ++r)
		{
			m_rackOn[r].store(_data[p++] != 0);
			m_type[r].store(std::min<int>(_data[p++], typeCount(r) - 1));
			for(int t = 0; t < typeCount(r); ++t)
			{
				for(int c = 0; c < controlsIn(version, r, t); ++c)
				{
					const int bits = (_data[p] << 7) | _data[p + 1];
					p += 2;
					if(const int now = placeNow(version, r, t, c); now >= 0)
						m_values[r][t][now].store(static_cast<float>(bits) / 16383.0f);
				}
			}
		}
		return true;
	}
}
