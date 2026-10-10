#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "matrixHardware.h"
#include "cem3396.h"
#include "halfband2x.h"

namespace matrixLib
{
	// Processor board and voice board, run in lockstep one audio sample at a time
	class Machine
	{
	public:
		Machine();

		bool setFirmware(const std::vector<uint8_t>& _rom) { return m_hw.setFirmware(_rom); }
		void setPatchRom(const std::vector<uint8_t>& _rom) { m_hw.setPatchRom(_rom); }

		void setSamplerate(float _rate);
		float getSamplerate() const { return m_rate; }

		void reset();

		// renders _count mono samples, the CPU runs alongside
		void render(float* _out, size_t _count);

		Hardware& getHardware() { return m_hw; }

		// A note on has just gone to the firmware: the pitch step that takes a resting voice
		// to it within the next few hundredths of a second is not joined to the old pitch.
		// One flag per oscillator: false leaves it joined, for one that glides to its notes.
		void expectNote(bool _dco1, bool _dco2);

		std::function<void(float)> onComparatorSample;   // debugging
		// debugging: the height in volts of the ramp converter A of a voice is asked for
		float getRampPeak(uint32_t _voice) const { return m_rampPeak[_voice]; }

		// true while $1D00 bit 1 mutes the output and routes the voice sum to the ACIA CTS pin
		bool isCalibrating() const { return (m_hw.getLatch(0x1d00) & 0x02) != 0; }

		static constexpr uint32_t Oversampling = 2;

	private:
		// The firmware moves the pitch of a voice about 75 times a second, in one step each
		// time. Small steps, and larger ones in a run of steps, are joined by a straight line so
		// that vibrato, the levers, an LFO and a portamento do not come out as a staircase; a
		// new note still jumps.
		struct PitchGlide
		{
			uint32_t target = 0;       // timer count the firmware asked for
			uint32_t seen = 0;         // count read last, taken as the target once it stays
			uint32_t seenFor = 0;      // samples it has stayed
			float height1 = 0.0f;      // waveshape voltage times timer count, followed in two stages
			float height2 = 0.0f;
			float pitch = 0.0f;        // one over the count, lagged like the waveshape voltage
			float rtCt = 0.0f;         // range the converter was in
			uint32_t rangeHold = 0;    // samples the height is held after a change of range
			float value = 0.0f;        // timer count the converter runs at
			float step = 0.0f;
			uint32_t left = 0;         // samples until value arrives at target
			uint32_t sinceMove = 0;    // samples since the firmware last moved it
		};

		void buildControls(uint32_t _voice, Cem3396::Controls& _c);
		float glide(PitchGlide& _g, uint32_t _count, bool _noteDue);
		float noise();

		Hardware m_hw;
		std::array<Cem3396, Hardware::VoiceCount> m_voices;
		std::array<std::array<PitchGlide, 2>, Hardware::VoiceCount> m_glides;
		Halfband2x m_decimator;

		float m_rate = 48000.0f;
		double m_cyclesPerSample = Hardware::CpuClock / 48000.0;
		double m_cycleAcc = 0.0;
		uint64_t m_sampleCycle = 0;

		float m_dcIn = 0.0f;
		float m_dcOut = 0.0f;
		float m_dcCoeff = 0.9995f;
		float m_cvK = 0.0f;       // per sample share of the charge the S&H capacitors exchange
		float m_heightK = 0.0f;   // per sample step of the stages that follow the ramp height
		float m_lastSum = 0.0f;
		bool m_comparator = false;

		std::array<uint32_t, 2> m_noteWindow{};   // per oscillator: samples left in which a resting voice takes a new note
		uint32_t m_noiseState = 0x12345678;
		std::array<float, Hardware::VoiceCount> m_rampPeak{};
	};
}
