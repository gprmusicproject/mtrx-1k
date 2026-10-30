#pragma once

#include "synthLib/device.h"

#include "matrixMachine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace matrixLib
{
	// Original firmware on an emulated 6809 board driving six CEM3396 models. romData = 32 KB system
	// EPROM followed by the optional 64 KB patch EPROM, homePath = calibration snapshot and ROM banks
	class Device : public synthLib::Device
	{
	public:
		explicit Device(const synthLib::DeviceCreateParams& _params);
		~Device() override;

		float getSamplerate() const override { return m_machine.getSamplerate(); }
		void getSupportedSamplerates(std::vector<float>& _dst) const override;
		bool setSamplerate(float _samplerate) override;
		bool isValid() const override { return m_valid; }

#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return false; }
#endif

		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }

		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return Hardware::CpuClock; }

		// The first start with a firmware takes seconds: the firmware initialises its RAM and
		// calibrates every voice, and the factory banks are read out of the patch EPROM. Both are
		// done once and kept in files. While one of them runs, this says which (FirstStartStage)
		// and how much of the whole is done, 0 to 1; at any other time the stage is None. The
		// same for every instance of the process.
		enum class FirstStartStage : uint8_t { None, Calibrating, ReadingBanks };
		static FirstStartStage getFirstStartStage();
		static float getFirstStartProgress();

		// file names of the banks built from the patch EPROM, 2-9
		static std::string romBankFileName(uint32_t _bank);

		// LFO speed locked to the host tempo, division 0 = off. The nearest of the 64 firmware
		// speeds is sent after every tempo change and patch load
		static uint32_t getLfoDivisionCount();
		static const char* getLfoDivisionName(uint32_t _division);
		void setLfoSync(uint32_t _lfo, uint32_t _division);
		uint32_t getLfoSync(uint32_t _lfo) const { return m_lfoSync[_lfo & 1].load(); }
		void setHostBpm(float _bpm) { m_hostBpm.store(_bpm); }
		// host playhead tempo, else incoming MIDI clock, else the last SysEx tempo, else 120
		// (F0 7D 54 t1 t2 t3 F7: microseconds per quarter note, three 7 bit bytes, high first)
		float getHostBpm() const;
		bool hasTempoSource() const;
		// false when no speed step lands within a few percent of the division at this tempo
		bool isLfoDivisionReachable(uint32_t _division) const;

		// Polyphony. Six voices are one Matrix-1000. For 12 or 24 the device runs two or four
		// whole instruments, firmware and voice board each, as modules chained over MIDI play,
		// and gives every note to one of them; the firmware is the same and does not know.
		// Everything that is not a note goes to all of them, and only the first one answers
		// on MIDI. Building one takes a moment of machine time: call this off the audio
		// thread. The audio thread takes them over at the start of a block.
		static constexpr uint32_t VoicesPerMachine = Hardware::VoiceCount;
		static constexpr uint32_t MaxMachines = 4;
		void setVoiceCount(uint32_t _voices);
		uint32_t getVoiceCount() const { return m_machineTarget.load() * VoicesPerMachine; }

		// Stereo spread of the voices, 0 = every voice in the middle as on the instrument,
		// which has one output, to 1 = from hard left to hard right. Each voice of each
		// instrument has a place of its own; voices one after another are far apart, so that
		// the notes of a chord, and of a line in Rotate, go to both sides. Any thread.
		void setSpread(float _amount) { m_spread.store(std::min(1.0f, std::max(0.0f, _amount))); }
		float getSpread() const { return m_spread.load(); }

		// Detune of the voices against each other, the firmware's own: controller 94, which
		// gives the six voices of an instrument offsets spread evenly around the note, about
		// 1.2 cents per step for the outermost (measured on firmware 1.20). Up to 40, beyond
		// which the firmware no longer spreads them evenly. 0 = every voice on the note, as
		// after power-up. Sent to every instrument when it changes. Any thread.
		static constexpr int MaxDetune = 40;
		static constexpr uint8_t DetuneController = 94;
		void setDetune(int _amount) { m_detune.store(std::min(MaxDetune, std::max(0, _amount))); }
		int getDetune() const { return m_detune.load(); }

		// Mono or poly, with the keyboard mode of the patch:
		//   Unison, mono: the firmware's unison, all six voices on one note, as the instrument.
		//   Unison, poly: every note on two voices, so three notes per instrument (6, 12 with
		//     12 voices, 24 with 24...); the firmware plays Reassign and gets each note twice,
		//     which gives it two voices; past three notes the oldest is let go.
		//   Reassign, Rotate, mono: one voice, one note at a time. The firmware plays it in
		//     unison and one voice of the six is heard, see Machine::setSolo; no detune, no
		//     spread.
		//   Reassign, Rotate, poly: as the instrument.
		// MonoAsPatch is what the instrument does: mono in Unison, poly otherwise. The patch
		// keeps its own keyboard mode; the firmware is given the one that plays it. Any thread.
		static constexpr int MonoAsPatch = -1;
		void setMono(int _mono) { m_monoSetting.store(_mono < 0 ? MonoAsPatch : (_mono ? 1 : 0)); }
		int getMonoSetting() const { return m_monoSetting.load(); }
		bool isMono() const { return isMonoWith(m_monoSetting.load()); }
		// whether a setting plays mono with the keyboard mode of the patch that plays
		bool isMonoWith(int _setting) const;

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		void boot();
		bool romBanksMissing() const;
		void firstStart(FirstStartStage _stage, float _fraction) const;
		void primeVoices();
		void selectFirstProgram();
		bool loadSnapshot();
		void saveSnapshot() const;
		void runSeconds(float _seconds);
		static void run(Machine& _machine, float _seconds);

		// the part of the first start the stage that runs covers
		float m_stageFrom = 0.0f;
		float m_stageSpan = 1.0f;
		bool m_coldStart = false;

		// asks the firmware for every patch of banks 2-9 and writes them as .syx files
		void createRomBanks();
		std::vector<uint8_t> requestPatch(uint32_t _number);

		void pushMidi(const synthLib::SMidiEvent& _ev);
		void sendLfoSync(bool _force);

		// Remote edits (opcodes 06 and 0B) wait here. The firmware takes 10 to 20 ms for
		// each and loses what arrives meanwhile, so they go out one at a time, and a newer
		// value of a parameter replaces the one still waiting.
		void queueEdit(const std::vector<uint8_t>& _sysex);
		void feedEdits(size_t _samples);

		// A parameter edit (06) changes the value but not the firmware's modulation program,
		// which is only built when a patch is loaded or a matrix bus is edited. A modulation
		// that was at zero then stays a fixed offset, and a selector that brings another
		// source into play does nothing. Sending the last bus again unchanged makes the
		// firmware rebuild: at once after edits of that kind, otherwise when the edits pause.
		std::vector<uint8_t> rebuildMessage() const;
		void parseMidiOut(uint8_t _byte);

		// the instruments beside the first, see setVoiceCount
		struct Extra
		{
			std::unique_ptr<Machine> machine;
			uint32_t readyIn = 0;   // samples until its patch is in and it takes notes
		};
		std::unique_ptr<Machine> createExtra(uint32_t _index) const;
		void takeExtras();
		void dropExtra();
		uint32_t machineCount() const { return 1 + static_cast<uint32_t>(m_extras.size()); }
		Machine& machineAt(uint32_t _index) { return _index ? *m_extras[_index - 1].machine : m_machine; }
		void midiInAll(uint8_t _byte);

		// One instrument's share of a block: what reaches it at which sample, and where its
		// audio goes. The block is first shared out, in the order of its events; then the
		// extras render their lanes on threads of their own while the audio thread renders
		// the first, so that more voices do not make the audio thread's block longer.
		struct LaneEvent
		{
			uint32_t offset;
			uint32_t first;   // into the lane's bytes
			uint32_t count;
			int8_t expect;    // -1, or a note with the flags of expectNote: 1 = DCO 1 has portamento, 2 = DCO 2
			int16_t interval; // and how far it moves the pitch in cents, 0 when not known
		};
		struct Lane
		{
			Machine* machine = nullptr;
			float* out = nullptr;
			float* outR = nullptr;     // set when the voices are spread
			std::array<float, VoicesPerMachine> gainL{};
			std::array<float, VoicesPerMachine> gainR{};
			std::vector<LaneEvent> events;
			std::vector<uint8_t> bytes;
		};
		void spreadGains(float _spread, uint32_t _count);
		// to one instrument, or to all of them with -1, at the sample of the event being shared out
		void emit(int _machine, const uint8_t* _data, size_t _count);
		void emitExpect(uint32_t _machine, bool _portaDco1, bool _portaDco2, int _interval);
		static void renderLane(Lane& _lane, size_t _samples);
		void startWorkers();
		void workerLoop(uint32_t _index);
		std::array<Lane, MaxMachines> m_lanes;
		uint32_t m_emitOffset = 0;
		size_t m_laneSamples = 0;
		uint64_t m_fpMode = 0;   // of the audio thread, for the workers
		std::vector<std::thread> m_workers;
		std::array<std::binary_semaphore, MaxMachines - 1> m_go{std::binary_semaphore{0}, std::binary_semaphore{0}, std::binary_semaphore{0}};
		std::counting_semaphore<MaxMachines> m_done{0};
		std::atomic<bool> m_quit{false};
		std::array<std::atomic<bool>, MaxMachines> m_taken{};   // a lane of this block is being rendered

		// Which instrument gets a note: the one that holds the fewest, keys down and notes the
		// sustain pedal keeps, of those the one whose last note is oldest, so that release
		// tails ring on. A note off goes where its note went. In unison the first one plays
		// alone, as a single Matrix-1000 does. (Whether the firmware holds a note for the
		// pedal depends on the patch: counting one it lets go only spreads the notes wider.)
		uint32_t noteOn(uint8_t _channel, uint8_t _note);
		int noteOff(uint8_t _channel, uint8_t _note);
		void pedal(uint8_t _channel, bool _down);
		void forgetNotes(uint8_t _channel);
		struct HeldNote
		{
			uint8_t channel;
			uint8_t note;
			uint8_t machine;
			bool sustained;   // released while the pedal is down
		};
		std::vector<HeldNote> m_held;
		std::array<uint64_t, MaxMachines> m_lastNoteOn{};
		uint64_t m_noteCount = 0;
		int m_unisonNote = -1;   // the note the voices play in unison, -1 when not known
		int unisonNote() const;

		// how the notes are played, see setMono
		enum class KeyMode : uint8_t { Poly, PairedUnison, Unison, Mono };
		KeyMode keyModeFor(int _patchKeyboard) const;
		static int firmwareKeyboard(KeyMode _mode, int _patchKeyboard);
		bool firmwareUnison() const { return m_keyMode == KeyMode::Unison || m_keyMode == KeyMode::Mono; }
		void setKeyMode(KeyMode _mode);
		void releaseHeld();
		std::atomic<int> m_monoSetting{MonoAsPatch};
		std::atomic<int> m_patchKeyboard{-1};   // the keyboard mode of the patch, -1 before one is known
		KeyMode m_keyMode = KeyMode::Poly;      // audio thread
		uint16_t m_pedalDown = 0;   // one bit per channel

		Machine m_machine;
		std::vector<Extra> m_extras;            // audio thread
		std::array<std::vector<float>, MaxMachines - 1> m_extraOut;
		std::array<std::vector<float>, MaxMachines - 1> m_extraOutR;
		std::vector<float> m_monoR;
		std::atomic<float> m_spread{0.0f};
		std::atomic<int> m_detune{0};
		int m_detuneSent = 0;       // what the firmware has, 0 after a start
		float m_spreadNow = 0.0f;   // follows m_spread over a few hundredths of a second
		std::vector<uint8_t> m_drain;
		std::vector<uint8_t> m_bootSram;        // calibrated memory every instrument starts from
		std::mutex m_poolMutex;
		std::vector<std::unique_ptr<Machine>> m_spare;     // built, waiting for the audio thread
		std::vector<std::unique_ptr<Machine>> m_retired;   // left by it, freed outside it
		std::atomic<uint32_t> m_machineTarget{1};
		std::atomic<bool> m_poolChanged{false};
		uint32_t m_built = 0;                   // extras ever built, to tell their noise apart
		// the edit buffer as the firmware has it, for an instrument that joins
		std::vector<uint8_t> m_editBuffer;
		std::string m_homePath;
		bool m_valid = false;
		bool m_hasPatchRom = false;

		struct PendingEvent
		{
			uint32_t offset;
			synthLib::SMidiEvent event;
		};
		std::vector<PendingEvent> m_pending;
		std::vector<float> m_mono;

		std::vector<std::vector<uint8_t>> m_editQueue;
		uint32_t m_editHold = 0;   // samples until the next edit may go out
		uint8_t m_lastBus[3] = {0, 0, 0};   // source, amount and destination of bus 10 as the firmware has it
		bool m_rebuildPending = false;
		uint32_t m_rebuildWait = 0;   // samples without an edit until the pending rebuild goes out
		// the value of every patch parameter as the firmware has it, unknown after a program
		// change it answers from its own memory
		std::array<int8_t, 128> m_paramValue{};
		bool m_paramsKnown = false;

		std::vector<uint8_t> m_txBytes;
		std::vector<uint8_t> m_txSysex;
		std::vector<uint8_t> m_txShort;
		uint8_t m_txRunningStatus = 0;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		std::atomic<uint32_t> m_lfoSync[2] = {0, 0};
		std::atomic<float> m_hostBpm{0.0f};
		std::atomic<float> m_clockBpm{0.0f};
		std::atomic<float> m_sysexBpm{0.0f};
		uint64_t m_sampleTime = 0;
		uint64_t m_lastClock = 0;
		uint64_t m_clockBeatStart = 0;
		uint32_t m_clockCount = 0;
		std::atomic<bool> m_lfoSyncDirty{false};
		float m_sentBpm = 0.0f;
		int m_sentSpeed[2] = {-1, -1};
		int m_patchLfoSpeed[2] = {-1, -1};
	};
}
