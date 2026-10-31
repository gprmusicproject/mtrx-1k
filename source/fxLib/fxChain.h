#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fxLib
{
	class OwnEffect;

	// The effects that follow the instrument: three racks in a row, modulation, delay and
	// reverb, each playing one type at a time, a switch for the whole chain and its mix with
	// the instrument's own sound. The effects are in fxOwn.h.
	//
	// Settings are written from any thread and read by the audio thread at the start of a
	// block. Switching a rack, the chain or a type fades over 10 ms, so nothing clicks; an
	// effect that comes back starts from silence, not from what it held when it stopped.
	enum Rack : int { RackModulation, RackDelay, RackReverb, RackCount };

	constexpr int MaxControls = 11;
	constexpr int MaxTypes = 5;

	enum class ControlKind : uint8_t { Knob, Switch };
	// Semitones: a knob that clicks over its steps around a centre of 0
	enum class ControlUnit : uint8_t { Percent, Time, Bipolar, Semitones };

	struct ControlInfo
	{
		const char* name;
		float initial;          // 0-1
		ControlKind kind;
		ControlUnit unit;
		int steps = 0;                         // a knob that clicks between this many settings
		const char* const* labels = nullptr;   // and their names
	};

	struct TypeInfo
	{
		const char* name;
		int controlCount;
		std::array<ControlInfo, MaxControls> controls;
	};

	const char* rackName(int _rack);
	int typeCount(int _rack);
	const TypeInfo& typeInfo(int _rack, int _type);

	// the delay's Sync switch: the last control of a delay type
	int delaySyncControl(int _type);
	// the divisions a synced delay time steps through, the time knob picks one
	int delayDivisionCount();
	const char* delayDivisionName(int _division);
	double delayDivisionBeats(int _division);
	int delayDivisionOf(float _value);
	// what a Semitones control says: -24 to +24 semitones
	int semitonesOf(float _value);

	class Chain
	{
	public:
		Chain();
		~Chain();

		// Message thread, or with the audio stopped: the effects get their buffers for this
		// rate and the largest block.
		void prepare(double _sampleRate, int _maxBlock);

		// Audio thread: processes the stereo output in place. _bpm is the tempo for a synced
		// delay.
		void process(float* _left, float* _right, int _samples, double _bpm);

		// settings, any thread
		void setChainOn(bool _on) { m_chainOn.store(_on); }
		bool isChainOn() const { return m_chainOn.load(); }
		// the share of the chain's output against the instrument's own sound, 0-1
		void setMix(float _mix) { m_mix.store(_mix < 0.0f ? 0.0f : _mix > 1.0f ? 1.0f : _mix); }
		float getMix() const { return m_mix.load(); }
		void setRackOn(int _rack, bool _on) { m_rackOn[_rack].store(_on); }
		bool isRackOn(int _rack) const { return m_rackOn[_rack].load(); }
		void setType(int _rack, int _type);
		int getType(int _rack) const { return m_type[_rack].load(); }
		// the value of a control of a type, 0-1; every type of a rack keeps its own
		void setValue(int _rack, int _type, int _control, float _value);
		float getValue(int _rack, int _type, int _control) const;
		// of the type the rack plays
		void setValue(int _rack, int _control, float _value) { setValue(_rack, getType(_rack), _control, _value); }
		float getValue(int _rack, int _control) const { return getValue(_rack, getType(_rack), _control); }

		// what the value of a control means: a percentage, a time in ms or a division
		std::string valueText(int _rack, int _type, int _control) const;
		// a synced division the delay cannot reach at this tempo
		bool isDelayDivisionReachable(int _type, int _control, double _bpm) const;

		// Everything off and every control at its starting value: what a patch without
		// effects of its own plays.
		void reset();

		// The settings as bytes 0-127, so that they fit a SysEx message; read returns false
		// and changes nothing when the bytes are not settings. Settings of versions 5 (1.0.0
		// alpha) to 7 are read too: what they lack takes its starting value.
		std::vector<uint8_t> write() const;
		bool read(const uint8_t* _data, size_t _size);
		bool read(const std::vector<uint8_t>& _data) { return read(_data.data(), _data.size()); }

	private:
		struct RackState;

		// What plays a type: its index, but for the stereo delay with Ping Pong on, which is
		// another engine, PingPongEngine.
		static constexpr int PingPongEngine = 2;
		int engineFor(int _rack) const;
		int typeOfEngine(int _rack, int _engine) const;
		void renderEngine(int _rack, int _engine, float* _l, float* _r, int _samples, double _bpm);
		double delaySeconds(int _type, int _control, double _bpm) const;

		std::atomic<bool> m_chainOn{false};
		std::atomic<float> m_mix{1.0f};
		float m_mixNow = 1.0f;     // the mix as it glides to the one set
		float m_mixGlide = 0.001f;
		std::array<std::atomic<bool>, RackCount> m_rackOn{};
		std::array<std::atomic<int>, RackCount> m_type{};
		std::array<std::array<std::array<std::atomic<float>, MaxControls>, MaxTypes>, RackCount> m_values;

		std::array<std::array<std::unique_ptr<OwnEffect>, MaxTypes>, RackCount> m_engines;
		std::array<std::unique_ptr<RackState>, RackCount> m_racks;

		std::atomic<double> m_rate{0.0};
		int m_maxBlock = 0;
		std::vector<float> m_wetL, m_wetR, m_dryL, m_dryR, m_inL, m_inR, m_srcL, m_srcR;
		float m_fadeStep = 0.0f;
	};
}
