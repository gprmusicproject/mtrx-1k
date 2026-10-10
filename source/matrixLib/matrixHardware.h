#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "m6809.h"
#include "pit8254.h"
#include "acia6850.h"

namespace matrixLib
{
	// Oberheim Matrix-1000 processor board
	class Hardware : public M6809Bus
	{
	public:
		static constexpr uint32_t CpuClock = 2000000;              // E clock, 8 MHz XTAL / 4
		static constexpr uint32_t VoiceTimerClock = 4000000;       // DCO timers count at twice the E clock
		static constexpr uint32_t MidiCyclesPerByte = CpuClock * 10 / 31250;
		static constexpr uint32_t VoiceCount = 6;
		static constexpr uint32_t CvChannelCount = 64;             // 8 multiplexers x 8 sample and holds

		// sample and hold network of every channel: the multiplexer charges a 33 nF hold
		// capacitor, which feeds 6.8 nF at the chip pin through 1 MOhm
		static constexpr float CvHoldCap = 33e-9f;
		static constexpr float CvPinCap = 6.8e-9f;
		static constexpr float CvSeriesR = 1e6f;
		// time constant with which the two capacitors settle on a common voltage
		static constexpr float CvTimeConstant = CvSeriesR * CvHoldCap * CvPinCap / (CvHoldCap + CvPinCap);

		// Filter frequency range. The firmware corrects every cutoff with a nine point table
		// per voice that its calibration fills in. Before calibrating it loads the table with
		// defaults, 2841 falling by 200 per point, and the lowest point keeps its default for
		// good: those figures describe the instrument its authors had on the bench. With them
		// the DAC range runs from 60 Hz to 69 kHz, 10.2 octaves, where the cutoff the firmware
		// computes runs from 33 Hz to 53 kHz: a Matrix-1000 does not close its filter below
		// about 60 Hz, and has a third of an octave to spare at the top.
		// The range here is set so that the calibration lands on those defaults. Away from
		// them the lowest table segment gets a kink, since one end is fixed, and with a filter
		// that cannot reach the top the corrections go negative and the firmware, taking the
		// sum for an overflow, shuts the filter on any patch that opens it fully.
		// Only the product of range and filter scale shows; the split between the two is
		// loose. The schematic's bipolar range starts at -3.08 V, which puts the filter's 0 V
		// frequency at the top of the datasheet's 500 to 980 Hz, see Cem3396. The balance
		// shares the range bit but keeps +-2.5 V.
		static constexpr float FilterRangeSpan = 5.095f;
		static constexpr float FilterRangeLow = -3.08f;

		Hardware();

		bool setFirmware(const std::vector<uint8_t>& _rom);
		void setPatchRom(const std::vector<uint8_t>& _rom);

		void reset();

		// runs the CPU until its cycle counter reaches _targetCycle
		void runUntil(uint64_t _targetCycle);

		uint64_t getCycles() const { return m_cpu.getCycles(); }

		void midiIn(uint8_t _byte) { m_acia.pushRx(_byte); }
		void midiOut(std::vector<uint8_t>& _dst) { m_acia.readTx(_dst); }
		size_t midiInPending() const { return m_acia.rxQueueSize(); }

		uint8_t read(uint16_t _addr) override;
		void write(uint16_t _addr, uint8_t _val) override;

		M6809& getCpu() { return m_cpu; }
		Acia6850& getAcia() { return m_acia; }

		// DCO1 and DCO2 period of a voice in voice timer ticks, 0 while stopped
		uint32_t getDcoPeriod(uint32_t _voice, uint32_t _dco);
		bool getRangeHigh(uint32_t _voice, uint32_t _dco) const { return m_latches[(_dco ? 0x80 : 0x00) + _voice] != 0; }
		Pit8254& getVoiceTimer(uint32_t _index) { return m_timers[_index]; }
		Pit8254& getSystemTimer() { return m_sysTimer; }

		std::vector<uint8_t>& getSram() { return m_sram; }

		// S&H mux 0-5 feed the voices, mux 6-7 hold balance and resonance; A8 selects the
		// bipolar range. The DAC takes bits 14-3 of the word: 0 V to +5 V. Bit 15 is not a DAC bit
		// but FAST*, see write()
		enum CvIndex : uint8_t { CvWsA, CvFreq, CvLog, CvPwA, CvLin, CvPwB, CvMod, CvWsB };

		static constexpr uint32_t cvChannel(uint16_t _addr) { return (_addr >> 1) & 0x3f; }
		static constexpr uint32_t voiceCv(uint32_t _voice, CvIndex _cv) { return _voice * 8 + _cv; }
		static constexpr uint32_t balanceCv(uint32_t _voice) { return 48 + _voice; }
		static constexpr uint32_t resonanceCv(uint32_t _voice) { return 54 + _voice; }

		// lets the hold and pin capacitors of every channel exchange charge for one audio
		// sample, _k = 1 - exp(-1 / (CvTimeConstant * rate))
		void advanceCv(float _k);

		// Sets how the steps of an LFO on the filter and pulse width voltages are joined, in
		// calls of advanceCv: the shortest and longest time between two steps that still
		// counts as a run of steps. 0 for _longest leaves the steps as the firmware makes them.
		void setCvJoin(uint32_t _shortest, uint32_t _longest) { m_cvJoinShortest = _shortest; m_cvJoinLongest = _longest; m_cvJoinUsual = _longest != 0; }

		// An edit that will move sounding notes in one leap has just gone to the firmware: the
		// leap is expected between _from and _until calls of advanceCv from now and is taken
		// in over _ramp calls. Filter and pulse width voltages only.
		void expectEdit(uint32_t _from, uint32_t _until, uint32_t _ramp);
		static constexpr float EditLeapLeast = 0.03f;   // volts: less is not worth drawing out

		// largest step that is joined, in volts at the pin: seven semitones of cutoff, a
		// tenth of the pulse width range
		static constexpr float FilterJoinLimit = 0.3f;
		static constexpr float PulseJoinLimit = 0.5f;

		// voltage at the chip pin per channel
		const std::array<float, CvChannelCount>& getCvVolts() const { return m_cvVolts; }
		float getCvVolts(uint32_t _channel) const { return m_cvVolts[_channel]; }

		// last 13 bit DAC code per channel, for tracing
		uint16_t getCv(uint16_t _addr) const { return m_cv[cvChannel(_addr)]; }
		uint8_t getLatch(uint16_t _addr) const;

		std::function<void(uint16_t, uint8_t, bool)> onIoAccess;
		std::function<void(uint16_t, uint16_t)> onCvWrite;
		std::function<void(uint16_t)> onInstruction;     // debugging, called before each instruction

	private:
		uint8_t* sramPtr(uint16_t _addr);
		void updateIrq();

		M6809 m_cpu;
		Pit8254 m_timers[4];
		Pit8254 m_sysTimer;
		Acia6850 m_acia;

		std::vector<uint8_t> m_rom;          // 32 KB system EPROM at $8000
		std::vector<uint8_t> m_patchRom;     // 64 KB patch EPROM, 8 KB pages at $2000
		std::vector<uint8_t> m_sram;         // 32 KB battery-backed RAM

		uint8_t m_bankLatch = 0;             // $1D80
		std::array<uint8_t, 0x400> m_latches{};
		std::array<uint16_t, CvChannelCount> m_cv{};
		std::array<float, CvChannelCount> m_cvVolts{};   // pin side, 6.8 nF
		std::array<float, CvChannelCount> m_cvHold{};    // multiplexer side, 33 nF
		std::array<float, CvChannelCount> m_cvRampStep{};
		std::array<uint32_t, CvChannelCount> m_cvRampLeft{};
		std::array<uint32_t, CvChannelCount> m_cvSinceWrite{};
		std::array<float, CvChannelCount> m_cvTarget{};       // voltage the DAC last gave the channel
		std::array<float, CvChannelCount> m_cvRaw{};          // the same before the leap of an edit is held back
		std::array<float, CvChannelCount> m_cvDelta{};        // its last step
		std::array<float, CvChannelCount> m_cvOffset{};       // what of an edit's leap is still held back
		std::array<float, CvChannelCount> m_cvOffsetStep{};
		std::array<bool, CvChannelCount> m_cvEditDone{};
		uint32_t m_cvEditFrom = 0;
		uint32_t m_cvEditUntil = 0;
		uint32_t m_cvEditRamp = 0;
		uint32_t m_cvJoinShortest = 0;
		uint32_t m_cvJoinLongest = 0;
		bool m_cvJoinUsual = false;
		uint8_t m_dacHigh = 0;
		uint8_t m_dacLow = 0;
		uint32_t m_cvChannel = 0;
		uint8_t m_cvRange = 0;
		bool m_cvInhibit = true;

		uint8_t m_switch0 = 0;
		uint8_t m_switch1 = 0;
	};
}
