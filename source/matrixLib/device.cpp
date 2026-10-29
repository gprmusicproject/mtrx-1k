#include "device.h"

#include "patch.h"
#include "patchNames.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#define MATRIXLIB_FP_X86 1
#endif

namespace matrixLib
{
	namespace
	{
		// bump when the voice model changes: the snapshot holds calibration done against it
		constexpr uint8_t SnapshotVersion = 8;
		constexpr char SnapshotMagic[] = "M1KSRAM";
		constexpr char SnapshotFile[] = "matrix1000.nvram";

		constexpr float BootRate = 48000.0f;

		// The first start, for whoever shows it. Machine time of the calibration with firmware
		// 1.20, the same at every cold start since the emulation has no chance in it, and the
		// share of the whole wait it takes when the factory banks are read after it.
		constexpr float ColdStartSeconds = 30.0f;
		constexpr float CalibrationShare = 0.32f;
		std::atomic<uint8_t> g_firstStartStage{0};
		std::atomic<float> g_firstStartProgress{0.0f};

		// the firmware keeps up with four times the MIDI rate, which keeps controller and
		// pitch bend streams from queueing up behind the 31.25 kbaud UART
		constexpr uint32_t UartCyclesPerByte = Hardware::MidiCyclesPerByte / 4;

		// spacing of remote edits, and how long after a patch load the first may follow
		constexpr float EditSeconds = 0.025f;
		constexpr float RebuildSeconds = 1.0f;      // without an edit before the precautionary rebuild
		constexpr float BusSeconds = 0.16f;         // after a matrix bus message before anything else is sent
		constexpr float PatchLoadSeconds = 0.15f;
		// an instrument that joins gets the edit buffer, 23 ms on the UART, and loads it
		constexpr float JoinSeconds = 0.2f;

		// The floating point mode of the calling thread. Hosts set theirs to flush denormal
		// numbers to zero; a thread of the device's own has to be told, or a filter that dies
		// away runs at a fraction of the speed.
		uint64_t fpMode()
		{
#if defined(MATRIXLIB_FP_X86)
			return _mm_getcsr();
#elif defined(__aarch64__)
			uint64_t r;
			__asm__ volatile("mrs %0, fpcr" : "=r"(r));
			return r;
#else
			return 0;
#endif
		}

		void setFpMode(const uint64_t _mode)
		{
#if defined(MATRIXLIB_FP_X86)
			_mm_setcsr(static_cast<unsigned int>(_mode));
#elif defined(__aarch64__)
			__asm__ volatile("msr fpcr, %0" : : "r"(_mode));
#else
			(void)_mode;
#endif
		}

		constexpr uint8_t KeyboardModeParam = 48;
		constexpr int8_t KeyboardUnison = 2;
		constexpr size_t MaxHeldNotes = 256;

		// continuous messages that only need their latest value per block
		uint32_t coalesceKey(const synthLib::SMidiEvent& _ev)
		{
			if(!_ev.sysex.empty() || _ev.a < 0x80)
				return 0;
			switch(_ev.a & 0xf0)
			{
			case 0xb0:
				// switches, bank select, data entry and mode messages carry state in every value
				if(_ev.b == 0 || _ev.b == 32 || _ev.b == 6 || _ev.b == 38 || (_ev.b >= 64 && _ev.b <= 69) || _ev.b >= 96)
					return 0;
				return 0x10000u | (_ev.a << 8) | _ev.b;
			case 0xd0:
			case 0xe0: return 0x10000u | (_ev.a << 8);
			default:   return 0;
			}
		}

		uint32_t firmwareSum(const std::vector<uint8_t>& _rom)
		{
			uint32_t s = 0;
			for(const auto b : _rom)
				s = s * 31 + b;
			return s;
		}

		// LFO rate per speed value, from the firmware's increment table at $AA1C
		constexpr uint16_t LfoIncrements[64] =
		{
			44, 62, 74, 88, 102, 116, 132, 148, 168, 172, 192, 214, 242, 276, 312, 356,
			410, 462, 520, 586, 656, 720, 780, 830, 886, 936, 992, 1058, 1130, 1192, 1262, 1338,
			1424, 1490, 1560, 1638, 1724, 1820, 1928, 2048, 2184, 2340, 2520, 2730, 2978, 3276, 3450, 3640,
			3856, 4096, 4370, 4682, 5042, 5462, 5958, 6554, 7282, 8192, 9364, 10924, 13108, 16386, 21846, 32770
		};
		// The firmware adds the increment to the phase on every other pass of its voice update,
		// and starts the cycle again when the sum passes 65536, dropping what is over. A cycle
		// is therefore a whole number of those steps, 65536 / increment rounded up: the rates
		// it can play are the step rate divided by 2, 3, 4 and so on, and two speed values
		// can share one. Measured on the firmware for every speed from 16 to 62. A pass takes
		// a little longer the more voices sound, 37.55 steps a second with one and 37.15 with
		// six: the figure here is the middle.
		constexpr double LfoStepsPerSecond = 37.35;

		constexpr double lfoHz(const int _speed)
		{
			const uint32_t inc = LfoIncrements[_speed];
			return LfoStepsPerSecond / static_cast<double>((65536u + inc - 1) / inc);
		}

		struct LfoDivision { const char* name; double beats; };
		constexpr LfoDivision LfoDivisions[] =
		{
			{"Off", 0.0},
			{"4/1", 16.0}, {"2/1", 8.0}, {"1/1", 4.0},
			{"1/2 Dotted", 3.0}, {"1/2", 2.0}, {"1/2 Triplet", 4.0 / 3.0},
			{"1/4 Dotted", 1.5}, {"1/4", 1.0}, {"1/4 Triplet", 2.0 / 3.0},
			{"1/8 Dotted", 0.75}, {"1/8", 0.5}, {"1/8 Triplet", 1.0 / 3.0},
			{"1/16 Dotted", 0.375}, {"1/16", 0.25}, {"1/16 Triplet", 1.0 / 6.0},
			{"1/32", 0.125},
		};
		constexpr uint32_t LfoDivisionCount = sizeof(LfoDivisions) / sizeof(LfoDivisions[0]);
		constexpr double LfoSyncTolerance = 0.06;
		constexpr uint8_t LfoSpeedParam[2] = {80, 90};
		constexpr uint8_t PortaRateParam = 44;
		constexpr uint8_t Dco1PortaParam = 8;       // 1 = DCO 1 glides
		constexpr uint8_t Dco2KeyboardParam = 18;   // bit 0: DCO 2 follows the keyboard with portamento (Porta, Porta+Keyb)

		// without a host playhead, MIDI clock or SysEx tempo (the standalone app)
		constexpr float DefaultBpm = 120.0f;

		// nearest firmware speed for a rate, and how far off it is (ratio - 1)
		int nearestLfoSpeed(const double _hz, double& _error)
		{
			int best = 0;
			double bestErr = 1e9;
			for(int i = 0; i < 64; ++i)
			{
				const double hz = lfoHz(i);
				const double err = std::fabs(std::log(hz / _hz));
				if(err < bestErr)
				{
					bestErr = err;
					best = i;
				}
			}
			_error = std::exp(bestErr) - 1.0;
			return best;
		}

		bool fileExists(const std::string& _path)
		{
			std::ifstream f(_path, std::ios::binary);
			return f.is_open();
		}

		// another process (the app and its AUv3 share the folder) never sees a partial file
		bool writeFileAtomic(const std::string& _path, const std::vector<uint8_t>& _data)
		{
			static std::atomic<uint32_t> s_counter{0};
			const auto id = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + s_counter.fetch_add(1);
			const auto tmp = _path + ".tmp" + std::to_string(id);
			if(!baseLib::filesystem::writeFile(tmp, _data))
				return false;
			// std::rename refuses to replace an existing file on Windows, which left the first
			// snapshot in place for good; the filesystem call replaces it on every platform
			std::error_code ec;
			std::filesystem::rename(tmp, _path, ec);
			if(!ec)
				return true;
			std::remove(tmp.c_str());
			return false;
		}

		std::string withSlash(const std::string& _path)
		{
			if(_path.empty() || _path.back() == '/' || _path.back() == '\\')
				return _path;
			return _path + '/';
		}
	}

	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		const auto& rom = _params.romData;
		if(rom.size() < 0x8000)
			return;

		if(!m_machine.setFirmware(std::vector<uint8_t>(rom.begin(), rom.begin() + 0x8000)))
			return;

		if(rom.size() >= 0x18000)
		{
			m_machine.setPatchRom(std::vector<uint8_t>(rom.begin() + 0x8000, rom.begin() + 0x18000));
			m_hasPatchRom = true;
		}

		m_homePath = withSlash(_params.homePath);
		m_valid = true;

		// with banks to read after it, the calibration is only the first part of the wait
		const bool readBanks = m_hasPatchRom && !m_homePath.empty() && romBanksMissing();
		m_stageSpan = readBanks ? CalibrationShare : 1.0f;

		boot();

		if(readBanks)
		{
			m_stageFrom = m_coldStart ? CalibrationShare : 0.0f;
			m_stageSpan = 1.0f - m_stageFrom;
			createRomBanks();
		}
		g_firstStartStage.store(static_cast<uint8_t>(FirstStartStage::None));

		m_machine.getHardware().getAcia().setCyclesPerByte(UartCyclesPerByte);

		// nothing the audio thread does with them later has to allocate
		m_extras.reserve(MaxMachines - 1);
		m_retired.reserve(MaxMachines - 1);
		m_held.reserve(MaxHeldNotes + 1);
		for(auto& lane : m_lanes)
		{
			lane.events.reserve(512);
			lane.bytes.reserve(8192);
		}
	}

	Device::FirstStartStage Device::getFirstStartStage()
	{
		return static_cast<FirstStartStage>(g_firstStartStage.load());
	}

	float Device::getFirstStartProgress()
	{
		return g_firstStartProgress.load();
	}

	void Device::firstStart(const FirstStartStage _stage, const float _fraction) const
	{
		g_firstStartProgress.store(m_stageFrom + m_stageSpan * std::min(1.0f, std::max(0.0f, _fraction)));
		g_firstStartStage.store(static_cast<uint8_t>(_stage));
	}

	bool Device::romBanksMissing() const
	{
		for(uint32_t bank = 2; bank <= 9; ++bank)
			if(!fileExists(m_homePath + romBankFileName(bank)))
				return true;
		return false;
	}

	Device::~Device()
	{
		m_quit.store(true);
		for(auto& go : m_go)
			go.release();
		for(auto& t : m_workers)
			t.join();
	}

	void Device::getSupportedSamplerates(std::vector<float>& _dst) const
	{
		_dst.push_back(44100.0f);
		_dst.push_back(48000.0f);
		_dst.push_back(88200.0f);
		_dst.push_back(96000.0f);
	}

	bool Device::setSamplerate(const float _samplerate)
	{
		if(!synthLib::Device::setSamplerate(_samplerate))
			return false;
		m_machine.setSamplerate(_samplerate);
		for(auto& e : m_extras)
			e.machine->setSamplerate(_samplerate);
		return true;
	}

	void Device::runSeconds(const float _seconds)
	{
		run(m_machine, _seconds);
	}

	void Device::run(Machine& _machine, const float _seconds)
	{
		std::vector<float> buf(512);
		auto remaining = static_cast<size_t>(_seconds * _machine.getSamplerate());
		while(remaining)
		{
			const auto n = std::min(remaining, buf.size());
			_machine.render(buf.data(), n);
			remaining -= n;
		}
	}

	bool Device::loadSnapshot()
	{
		if(m_homePath.empty())
			return false;

		std::vector<uint8_t> data;
		if(!baseLib::filesystem::readFile(data, m_homePath + SnapshotFile))
			return false;

		constexpr size_t header = sizeof(SnapshotMagic) + 1 + 4;
		auto& sram = m_machine.getHardware().getSram();
		if(data.size() != header + sram.size())
			return false;
		if(std::memcmp(data.data(), SnapshotMagic, sizeof(SnapshotMagic)) != 0 || data[sizeof(SnapshotMagic)] != SnapshotVersion)
			return false;

		uint32_t sum;
		std::memcpy(&sum, &data[sizeof(SnapshotMagic) + 1], 4);
		if(sum != firmwareSum(getDeviceCreateParams().romData))
			return false;

		std::memcpy(sram.data(), &data[header], sram.size());
		return true;
	}

	void Device::saveSnapshot() const
	{
		if(m_homePath.empty())
			return;

		baseLib::filesystem::createDirectory(m_homePath);

		const auto& sram = const_cast<Machine&>(m_machine).getHardware().getSram();
		std::vector<uint8_t> data(SnapshotMagic, SnapshotMagic + sizeof(SnapshotMagic));
		data.push_back(SnapshotVersion);
		const auto sum = firmwareSum(getDeviceCreateParams().romData);
		data.insert(data.end(), reinterpret_cast<const uint8_t*>(&sum), reinterpret_cast<const uint8_t*>(&sum) + 4);
		data.insert(data.end(), sram.begin(), sram.end());
		writeFileAtomic(m_homePath + SnapshotFile, data);
	}

	void Device::boot()
	{
		m_machine.setSamplerate(BootRate);

		const bool warm = loadSnapshot();
		if(warm)
			m_bootSram = m_machine.getHardware().getSram();
		m_machine.reset();

		if(warm)
		{
			runSeconds(1.0f);
			selectFirstProgram();
			return;
		}

		// cold start: the firmware initialises its RAM and calibrates every voice, which
		// takes about half a minute of machine time
		constexpr float step = 0.25f;
		float t = 0.0f;
		bool sawCalibration = false;
		m_coldStart = true;
		firstStart(FirstStartStage::Calibrating, 0.0f);
		while(t < 60.0f)
		{
			runSeconds(step);
			t += step;
			firstStart(FirstStartStage::Calibrating, 0.97f * t / ColdStartSeconds);
			if(m_machine.isCalibrating())
				sawCalibration = true;
			else if(sawCalibration || t > 5.0f)
				break;
		}
		runSeconds(1.0f);

		primeVoices();
		m_bootSram = m_machine.getHardware().getSram();
		saveSnapshot();
		selectFirstProgram();
		firstStart(FirstStartStage::Calibrating, 1.0f);
	}

	// Remote edits (opcodes 06 and 0B) are written where a pointer in the firmware's RAM
	// says. After power-up it does not point at the patch that plays, so the edits are
	// stored and never heard. A program change sets it and a patch sent to the edit buffer
	// leaves it alone: one program change after every boot is enough.
	void Device::selectFirstProgram()
	{
		auto& hw = m_machine.getHardware();
		hw.midiIn(0xc0);
		hw.midiIn(0x00);
		runSeconds(0.3f);
	}

	// The firmware only keeps the modulation sources up to date that the loaded patch uses.
	// After a cold start the words behind the others still hold power-up noise, and a patch
	// that reads one nobody computes gets that noise, flag byte included: three factory
	// patches feed the tracking generator from itself, and on some voices the stray flag
	// bits shut their filter. One note per voice with the tracking generator in use leaves
	// every voice with a valid value, as an instrument that has been played has.
	void Device::primeVoices()
	{
		patch::Data d{};
		patch::setName(d, "INIT");
		d[patch::findParam(48)->byte] = 1;                            // keyboard mode Rotate: six notes, six voices
		d[patch::findParam(33)->byte] = patch::SourceKeyboard;        // Track Input
		constexpr uint8_t points[5] = {0, 15, 31, 47, 63};
		for(uint8_t i = 0; i < 5; ++i)
			d[patch::findParam(static_cast<uint8_t>(34 + i))->byte] = points[i];
		// routed somewhere so that the generator runs
		d[patch::MatrixOffset] = patch::SourceTracking;
		d[patch::MatrixOffset + 1] = 1;
		d[patch::MatrixOffset + 2] = patch::DestVcfFrequency;

		auto& hw = m_machine.getHardware();
		for(const auto b : patch::encode(d, patch::OpEditBuffer, 0))
			hw.midiIn(b);
		runSeconds(0.3f);

		constexpr uint8_t firstNote = 57;
		for(uint8_t i = 0; i < Hardware::VoiceCount; ++i)
		{
			hw.midiIn(0x90);
			hw.midiIn(static_cast<uint8_t>(firstNote + i));
			hw.midiIn(64);
		}
		runSeconds(0.3f);
		for(uint8_t i = 0; i < Hardware::VoiceCount; ++i)
		{
			hw.midiIn(0x80);
			hw.midiIn(static_cast<uint8_t>(firstNote + i));
			hw.midiIn(0);
		}
		runSeconds(0.5f);
	}

	std::string Device::romBankFileName(const uint32_t _bank)
	{
		return "!ROM Bank " + std::to_string(_bank) + ".syx";
	}

	std::vector<uint8_t> Device::requestPatch(const uint32_t _number)
	{
		auto& hw = m_machine.getHardware();
		const uint8_t request[] = {0xf0, patch::IdOberheim, patch::IdMatrix, patch::OpRequest, 0x01, static_cast<uint8_t>(_number), 0xf7};
		for(const auto b : request)
			hw.midiIn(b);

		std::vector<uint8_t> reply;
		std::vector<uint8_t> tx;
		for(int i = 0; i < 200 && reply.size() < patch::SysexSize; ++i)
		{
			runSeconds(0.005f);
			tx.clear();
			hw.midiOut(tx);
			for(const auto b : tx)
			{
				if(b == 0xf0)
					reply.clear();
				if(b == 0xf0 || !reply.empty())
					reply.push_back(b);
			}
		}
		return reply;
	}

	void Device::createRomBanks()
	{
		if(!romBanksMissing())
			return;
		firstStart(FirstStartStage::ReadingBanks, 0.0f);

		auto& hw = m_machine.getHardware();

		// the dumps only need to be correct, not real time
		hw.getAcia().setCyclesPerByte(UartCyclesPerByte);

		for(uint32_t bank = 2; bank <= 9; ++bank)
		{
			const auto file = m_homePath + romBankFileName(bank);
			if(fileExists(file))
				continue;

			const uint8_t setBank[] = {0xf0, patch::IdOberheim, patch::IdMatrix, patch::OpSetBank, static_cast<uint8_t>(bank), 0xf7};
			for(const auto b : setBank)
				hw.midiIn(b);
			runSeconds(0.05f);

			std::vector<uint8_t> out;
			for(uint32_t n = 0; n < 100; ++n)
			{
				firstStart(FirstStartStage::ReadingBanks, static_cast<float>((bank - 2) * 100 + n) / 800.0f);
				const auto reply = requestPatch(n);
				auto data = patch::decode(reply);
				if(!data)
				{
					out.clear();
					break;
				}
				patch::setName(*data, g_factoryPatchNames[bank * 100 + n]);
				const auto msg = patch::encode(*data, patch::OpSinglePatch, static_cast<uint8_t>(n));
				out.insert(out.end(), msg.begin(), msg.end());
			}

			if(!out.empty())
				writeFileAtomic(file, out);
		}

		// return to the state the snapshot describes
		loadSnapshot();
		m_machine.reset();
		runSeconds(1.0f);
	}

	uint32_t Device::getLfoDivisionCount()
	{
		return LfoDivisionCount;
	}

	const char* Device::getLfoDivisionName(const uint32_t _division)
	{
		return _division < LfoDivisionCount ? LfoDivisions[_division].name : "";
	}

	void Device::setLfoSync(const uint32_t _lfo, const uint32_t _division)
	{
		m_lfoSync[_lfo & 1].store(_division < LfoDivisionCount ? _division : 0);
		m_lfoSyncDirty.store(true);
	}

	bool Device::hasTempoSource() const
	{
		return m_hostBpm.load() > 0.0f || m_clockBpm.load() > 0.0f || m_sysexBpm.load() > 0.0f;
	}

	float Device::getHostBpm() const
	{
		const float host = m_hostBpm.load();
		if(host > 0.0f)
			return host;
		const float clock = m_clockBpm.load();
		if(clock > 0.0f)
			return clock;
		const float sysex = m_sysexBpm.load();
		return sysex > 0.0f ? sysex : DefaultBpm;
	}

	bool Device::isLfoDivisionReachable(const uint32_t _division) const
	{
		const float bpm = getHostBpm();
		if(_division == 0 || _division >= LfoDivisionCount)
			return true;
		if(bpm <= 0.0f)
			return false;
		double err;
		nearestLfoSpeed(bpm / 60.0 / LfoDivisions[_division].beats, err);
		return err <= LfoSyncTolerance;
	}

	void Device::sendLfoSync(const bool _force)
	{
		const float bpm = getHostBpm();
		m_sentBpm = bpm;

		for(uint32_t lfo = 0; lfo < 2; ++lfo)
		{
			const auto division = m_lfoSync[lfo].load();
			if(division == 0 || bpm <= 0.0f)
			{
				// give the patch its own speed back
				if(m_sentSpeed[lfo] >= 0 && m_patchLfoSpeed[lfo] >= 0)
					queueEdit(patch::createParamChange(LfoSpeedParam[lfo], m_patchLfoSpeed[lfo]));
				m_sentSpeed[lfo] = -1;
				continue;
			}
			double err;
			const int speed = nearestLfoSpeed(bpm / 60.0 / LfoDivisions[division].beats, err);
			if(!_force && speed == m_sentSpeed[lfo])
				continue;
			m_sentSpeed[lfo] = speed;
			queueEdit(patch::createParamChange(LfoSpeedParam[lfo], speed));
		}
	}

	void Device::setVoiceCount(const uint32_t _voices)
	{
		if(!m_valid)
			return;
		const uint32_t machines = _voices >= 4 * VoicesPerMachine ? 4 : (_voices >= 2 * VoicesPerMachine ? 2 : 1);
		if(machines > 1)
			startWorkers();
		m_machineTarget.store(machines);

		// The audio thread holds the instruments it plays. What it left is freed here, spares
		// it will not need go too, and what it lacks is built.
		{
			std::lock_guard<std::mutex> lock(m_poolMutex);
			m_retired.clear();
			while(!m_spare.empty() && m_extras.size() + m_spare.size() + 1 > machines)
				m_spare.pop_back();
		}
		for(;;)
		{
			uint32_t index;
			{
				std::lock_guard<std::mutex> lock(m_poolMutex);
				if(m_extras.size() + m_spare.size() + 1 >= machines)
					break;
				index = ++m_built;
			}
			auto machine = createExtra(index);
			if(!machine)
				break;
			std::lock_guard<std::mutex> lock(m_poolMutex);
			m_spare.push_back(std::move(machine));
		}
		m_poolChanged.store(true);
	}

	// Another instrument from the same firmware, patch EPROM and calibrated memory: the state
	// the first one is in after a warm start.
	std::unique_ptr<Machine> Device::createExtra(const uint32_t _index) const
	{
		const auto& rom = getDeviceCreateParams().romData;
		if(m_bootSram.empty() || rom.size() < 0x8000)
			return {};

		auto machine = std::make_unique<Machine>();
		if(!machine->setFirmware(std::vector<uint8_t>(rom.begin(), rom.begin() + 0x8000)))
			return {};
		if(m_hasPatchRom)
			machine->setPatchRom(std::vector<uint8_t>(rom.begin() + 0x8000, rom.begin() + 0x18000));

		auto& sram = machine->getHardware().getSram();
		if(sram.size() != m_bootSram.size())
			return {};
		std::copy(m_bootSram.begin(), m_bootSram.end(), sram.begin());

		machine->setSamplerate(BootRate);
		machine->reset();
		// separate instruments do not share a noise source
		machine->setNoiseSeed(0x12345678u ^ (0x9e3779b9u * _index));
		run(*machine, 1.0f);

		// the program change of selectFirstProgram
		auto& hw = machine->getHardware();
		hw.midiIn(0xc0);
		hw.midiIn(0x00);
		run(*machine, 0.3f);

		hw.getAcia().setCyclesPerByte(UartCyclesPerByte);
		return machine;
	}

	void Device::takeExtras()
	{
		if(!m_poolChanged.load())
			return;
		std::unique_lock<std::mutex> lock(m_poolMutex, std::try_to_lock);
		if(!lock.owns_lock())
			return;

		const uint32_t want = m_machineTarget.load() - 1;
		while(m_extras.size() > want)
			dropExtra();

		while(m_extras.size() < want && !m_spare.empty())
		{
			Extra e;
			e.machine = std::move(m_spare.back());
			m_spare.pop_back();
			auto& machine = *e.machine;
			machine.setSamplerate(m_machine.getSamplerate());

			// the patch that plays and the pedals that are down
			auto& hw = machine.getHardware();
			if(m_editBuffer.size() == patch::DataSize)
			{
				patch::Data data;
				std::copy(m_editBuffer.begin(), m_editBuffer.end(), data.begin());
				for(const auto b : patch::encode(data, patch::OpEditBuffer, 0))
					hw.midiIn(b);
			}
			for(uint8_t ch = 0; ch < 16; ++ch)
			{
				if(m_pedalDown & (1u << ch))
				{
					hw.midiIn(static_cast<uint8_t>(0xb0 | ch));
					hw.midiIn(64);
					hw.midiIn(127);
				}
			}
			// and the detune the others have
			if(m_detuneSent)
			{
				hw.midiIn(0xb0);
				hw.midiIn(DetuneController);
				hw.midiIn(static_cast<uint8_t>(m_detuneSent));
			}
			e.readyIn = static_cast<uint32_t>(machine.getSamplerate() * JoinSeconds);
			m_extras.push_back(std::move(e));
		}
		m_poolChanged.store(m_extras.size() != want);
	}

	// the last one stops at once, with the notes it held
	void Device::dropExtra()
	{
		const auto index = static_cast<uint8_t>(m_extras.size());
		m_held.erase(std::remove_if(m_held.begin(), m_held.end(), [index](const HeldNote& _h) { return _h.machine == index; }), m_held.end());
		m_retired.push_back(std::move(m_extras.back().machine));
		m_extras.pop_back();
	}

	void Device::midiInAll(const uint8_t _byte)
	{
		m_machine.getHardware().midiIn(_byte);
		for(auto& e : m_extras)
			e.machine->getHardware().midiIn(_byte);
	}

	// one thread for each extra that can be, started once, before the first extra plays
	void Device::startWorkers()
	{
		std::lock_guard<std::mutex> lock(m_poolMutex);
		if(!m_workers.empty())
			return;
		for(uint32_t i = 0; i < MaxMachines - 1; ++i)
		{
			m_workers.emplace_back([this, i] { workerLoop(i); });
#ifdef _WIN32
			// they render audio for the audio thread, which waits for them
			SetThreadPriority(m_workers.back().native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
		}
	}

	void Device::workerLoop(const uint32_t _index)
	{
#ifdef _WIN32
		// An audio thread to Windows, as those of the host are: scheduled ahead of others and
		// kept off the slow cores and the power saving of a hybrid processor.
		if(const auto avrt = LoadLibraryW(L"avrt.dll"))
		{
			using SetCharacteristics = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
			if(const auto set = reinterpret_cast<SetCharacteristics>(reinterpret_cast<void*>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"))))
			{
				DWORD task = 0;
				set(L"Pro Audio", &task);
			}
		}
		THREAD_POWER_THROTTLING_STATE throttling{};
		throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
		throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
		throttling.StateMask = 0;
		SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof(throttling));

		// On a processor with performance and efficient cores, the performance ones: an
		// instrument rendered on an efficient core takes twice as long, and the audio thread
		// waits for it.
		ULONG length = 0;
		GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
		std::vector<uint8_t> info(length);
		if(length && GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(info.data()), length, &length, GetCurrentProcess(), 0))
		{
			std::vector<ULONG> fast;
			BYTE best = 0, worst = 255;
			for(ULONG at = 0; at < length;)
			{
				const auto* set = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(info.data() + at);
				if(set->Type == CpuSetInformation)
				{
					best = std::max(best, set->CpuSet.EfficiencyClass);
					worst = std::min(worst, set->CpuSet.EfficiencyClass);
				}
				at += set->Size;
			}
			for(ULONG at = 0; best != worst && at < length;)
			{
				const auto* set = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(info.data() + at);
				if(set->Type == CpuSetInformation && set->CpuSet.EfficiencyClass == best)
					fast.push_back(set->CpuSet.Id);
				at += set->Size;
			}
			if(!fast.empty())
				SetThreadSelectedCpuSets(GetCurrentThread(), fast.data(), static_cast<ULONG>(fast.size()));
		}
#endif
		for(;;)
		{
			m_go[_index].acquire();
			if(m_quit.load())
				return;
			// the audio thread may have taken it already
			if(!m_taken[_index + 1].exchange(true))
			{
				const auto mode = m_fpMode;
				if(fpMode() != mode)
					setFpMode(mode);
				renderLane(m_lanes[_index + 1], m_laneSamples);
			}
			m_done.release();
		}
	}

	void Device::emit(const int _machine, const uint8_t* _data, const size_t _count)
	{
		const uint32_t from = _machine < 0 ? 0 : static_cast<uint32_t>(_machine);
		const uint32_t to = _machine < 0 ? machineCount() : from + 1;
		for(uint32_t i = from; i < to; ++i)
		{
			auto& lane = m_lanes[i];
			lane.events.push_back({m_emitOffset, static_cast<uint32_t>(lane.bytes.size()), static_cast<uint32_t>(_count), -1, 0});
			lane.bytes.insert(lane.bytes.end(), _data, _data + _count);
		}
	}

	void Device::emitExpect(const uint32_t _machine, const bool _portaDco1, const bool _portaDco2, const int _interval)
	{
		m_lanes[_machine].events.push_back({m_emitOffset, 0, 0, static_cast<int8_t>((_portaDco1 ? 1 : 0) | (_portaDco2 ? 2 : 0)), static_cast<int16_t>(_interval)});
	}

	// renders a block, handing each event to the firmware at its sample
	void Device::renderLane(Lane& _lane, const size_t _samples)
	{
		auto& machine = *_lane.machine;
		auto& hw = machine.getHardware();
		size_t pos = 0;
		for(const auto& e : _lane.events)
		{
			const size_t at = std::min<size_t>(e.offset, _samples);
			if(at > pos)
			{
				if(_lane.outR)
					machine.render(_lane.out + pos, _lane.outR + pos, at - pos, _lane.gainL.data(), _lane.gainR.data());
				else
					machine.render(_lane.out + pos, at - pos);
				pos = at;
			}
			if(e.expect >= 0)
				machine.expectNote((e.expect & 1) != 0, (e.expect & 2) != 0, e.interval);
			for(uint32_t i = 0; i < e.count; ++i)
				hw.midiIn(_lane.bytes[e.first + i]);
		}
		if(pos < _samples)
		{
			if(_lane.outR)
				machine.render(_lane.out + pos, _lane.outR + pos, _samples - pos, _lane.gainL.data(), _lane.gainR.data());
			else
				machine.render(_lane.out + pos, _samples - pos);
		}
	}

	// Places in the stereo field for every voice of every instrument, evenly from left to
	// right. Voice v of instrument m is slot v * count + m, and slots one after another go
	// to opposite sides: 0 far left, 1 far right, 2 next to 0, 3 next to 1, and so on. The
	// gains keep the power of a voice the same wherever it is, at 1 in the middle.
	void Device::spreadGains(const float _spread, const uint32_t _count)
	{
		const uint32_t slots = VoicesPerMachine * _count;
		constexpr float quarterPi = 0.785398163f;
		constexpr float root2 = 1.41421356f;
		for(uint32_t m = 0; m < _count; ++m)
		{
			auto& lane = m_lanes[m];
			for(uint32_t v = 0; v < VoicesPerMachine; ++v)
			{
				const uint32_t s = v * _count + m;
				const uint32_t place = (s & 1) ? slots - 1 - s / 2 : s / 2;
				const float x = slots > 1 ? -1.0f + 2.0f * static_cast<float>(place) / static_cast<float>(slots - 1) : 0.0f;
				const float angle = (_spread * x + 1.0f) * quarterPi;
				lane.gainL[v] = std::cos(angle) * root2;
				lane.gainR[v] = std::sin(angle) * root2;
			}
		}
	}

	uint32_t Device::noteOn(const uint8_t _channel, const uint8_t _note)
	{
		// struck again before its note off: where it sounds
		for(auto& h : m_held)
		{
			if(h.channel == _channel && h.note == _note)
			{
				h.sustained = false;
				m_lastNoteOn[h.machine] = ++m_noteCount;
				return h.machine;
			}
		}

		uint32_t pick = 0;
		const bool unison = m_paramsKnown && m_paramValue[KeyboardModeParam] == KeyboardUnison;
		if(!unison)
		{
			size_t fewest = ~size_t(0);
			for(uint32_t m = 0; m < machineCount(); ++m)
			{
				if(m && m_extras[m - 1].readyIn)
					continue;
				const auto count = static_cast<size_t>(std::count_if(m_held.begin(), m_held.end(), [m](const HeldNote& _h) { return _h.machine == m; }));
				if(count < fewest || (count == fewest && m_lastNoteOn[m] < m_lastNoteOn[pick]))
				{
					fewest = count;
					pick = m;
				}
			}
		}

		// note offs that never came must not pile up
		if(m_held.size() >= MaxHeldNotes)
			m_held.erase(m_held.begin());
		m_held.push_back({_channel, _note, static_cast<uint8_t>(pick), false});
		m_lastNoteOn[pick] = ++m_noteCount;
		return pick;
	}

	// the instrument that plays the note, -1 when none is known to
	int Device::noteOff(const uint8_t _channel, const uint8_t _note)
	{
		for(auto it = m_held.begin(); it != m_held.end(); ++it)
		{
			if(it->channel != _channel || it->note != _note)
				continue;
			const int machine = it->machine;
			if(m_pedalDown & (1u << _channel))
				it->sustained = true;
			else
				m_held.erase(it);
			return machine;
		}
		return -1;
	}

	void Device::pedal(const uint8_t _channel, const bool _down)
	{
		if(_down)
		{
			m_pedalDown = static_cast<uint16_t>(m_pedalDown | (1u << _channel));
			return;
		}
		m_pedalDown = static_cast<uint16_t>(m_pedalDown & ~(1u << _channel));
		m_held.erase(std::remove_if(m_held.begin(), m_held.end(), [_channel](const HeldNote& _h) { return _h.channel == _channel && _h.sustained; }), m_held.end());
	}

	void Device::forgetNotes(const uint8_t _channel)
	{
		m_held.erase(std::remove_if(m_held.begin(), m_held.end(), [_channel](const HeldNote& _h) { return _h.channel == _channel; }), m_held.end());
	}

	// the lowest key held, which the firmware plays in unison, -1 when none is
	int Device::unisonNote() const
	{
		int lowest = -1;
		for(const auto& h : m_held)
			if(lowest < 0 || h.note < lowest)
				lowest = h.note;
		return lowest;
	}

	void Device::queueEdit(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex[3] == patch::OpMatrixParameter && _sysex.size() >= 9 && _sysex[4] == patch::MatrixBusCount - 1)
		{
			m_lastBus[0] = _sysex[5];
			m_lastBus[1] = static_cast<uint8_t>(static_cast<int8_t>(_sysex[6] << 1) >> 1);   // 7 bit two's complement
			m_lastBus[2] = _sysex[7];
		}

		// same opcode and same parameter or bus
		for(auto& waiting : m_editQueue)
		{
			if(waiting[3] == _sysex[3] && waiting[4] == _sysex[4])
			{
				waiting = _sysex;
				return;
			}
		}
		m_editQueue.push_back(_sysex);
	}

	void Device::feedEdits(const size_t _samples)
	{
		if(m_editHold > _samples)
		{
			m_editHold -= static_cast<uint32_t>(_samples);
			return;
		}
		m_editHold = 0;

		auto& hw = m_machine.getHardware();
		if(hw.midiInPending())
			return;

		if(m_editQueue.empty())
		{
			// the edits have stopped for a while: bring the modulation program up to date
			if(!m_rebuildPending)
				return;
			if(m_rebuildWait > _samples)
			{
				m_rebuildWait -= static_cast<uint32_t>(_samples);
				return;
			}
			m_editQueue.push_back(rebuildMessage());
		}

		const auto message = m_editQueue.front();
		m_editQueue.erase(m_editQueue.begin());
		for(const auto b : message)
			midiInAll(b);
		m_editHold = static_cast<uint32_t>(m_machine.getSamplerate() * EditSeconds);

		// what the firmware has now, for an instrument that joins later
		if(m_editBuffer.size() == patch::DataSize)
		{
			if(message[3] == patch::OpMatrixParameter && message.size() >= 9 && message[4] < patch::MatrixBusCount)
			{
				auto* bus = &m_editBuffer[patch::MatrixOffset + message[4] * 3];
				bus[0] = message[5];
				bus[1] = static_cast<uint8_t>(static_cast<int8_t>(message[6] << 1) >> 1);   // 7 bit two's complement
				bus[2] = message[7];
			}
			else if(message[3] == patch::OpParameter && message.size() >= 7)
			{
				if(const auto* p = patch::findParam(message[4]))
					m_editBuffer[p->byte] = static_cast<uint8_t>(static_cast<int8_t>(message[5] << 1) >> 1);
			}
		}

		if(message[3] == patch::OpMatrixParameter)
		{
			// Any bus edit rebuilds. The firmware brings the notes that are sounding up to
			// date about a tenth of a second later, and a second bus message inside that time
			// calls it off for good: the held note then never hears either. So the next
			// message waits until the firmware is through.
			m_rebuildPending = false;
			m_editHold = static_cast<uint32_t>(m_machine.getSamplerate() * BusSeconds);
			// the notes that sound take the new amount in one leap then: on the filter and the
			// pulse widths it is drawn out until the next message can go
			const float rate = m_machine.getSamplerate();
			for(uint32_t i = 0; i < machineCount(); ++i)
				machineAt(i).getHardware().expectEdit(static_cast<uint32_t>(rate * 0.03f), static_cast<uint32_t>(rate * 0.25f), static_cast<uint32_t>(rate * BusSeconds));
			return;
		}

		// A selector changes what the modulation program has to contain, and so does an amount
		// that leaves zero: until the rebuild it is plainly wrong, a vibrato depth that only
		// detunes. An amount that is already at work follows the edit as it is, like the plain
		// levels and times. Those get a rebuild all the same, as a precaution, but only when
		// the edits have stopped: for a tenth of a second after a rebuild the firmware keeps
		// parameter edits from the notes that are sounding, so one behind every step of a knob
		// would leave a held note deaf to the knob until it rests.
		m_rebuildPending = true;
		m_rebuildWait = static_cast<uint32_t>(m_machine.getSamplerate() * RebuildSeconds);
		const auto* p = patch::findParam(message[4]);
		bool programEdit = false;
		if(p)
		{
			const auto value = static_cast<int8_t>(static_cast<int8_t>(message[5] << 1) >> 1);   // 7 bit two's complement
			const int before = m_paramsKnown ? m_paramValue[p->number & 0x7f] : 0;
			// (the detune is bipolar too, but it is a level like the others; the amount of
			// envelope 2 on VCA 2 has no sign but is an amount)
			const bool amount = p->number != 12 && (p->min < 0 || p->number == 29);
			if(amount)
				programEdit = value != 0 && (before == 0 || (before < 0) != (value < 0));
			else
				programEdit = p->max <= 20;
			m_paramValue[p->number & 0x7f] = value;
		}
		const bool busEditNext = !m_editQueue.empty() && m_editQueue.front()[3] == patch::OpMatrixParameter;
		if(programEdit && !busEditNext)
			m_editQueue.insert(m_editQueue.begin(), rebuildMessage());
	}

	std::vector<uint8_t> Device::rebuildMessage() const
	{
		return patch::createMatrixChange(static_cast<uint8_t>(patch::MatrixBusCount - 1), m_lastBus[0], static_cast<int8_t>(m_lastBus[1]), m_lastBus[2]);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
	{
		// tempo for the LFO sync, never forwarded to the firmware
		const auto& sx = _ev.sysex;
		if(sx.size() == 7 && sx[0] == 0xf0 && sx[1] == 0x7d && sx[2] == 0x54 && sx[6] == 0xf7)
		{
			const uint32_t us = (static_cast<uint32_t>(sx[3] & 0x7f) << 14) | ((sx[4] & 0x7f) << 7) | (sx[5] & 0x7f);
			if(us > 0)
				m_sysexBpm.store(static_cast<float>(60000000.0 / us));
			return true;
		}

		if(_ev.sysex.empty() && _ev.a == 0xf8)
		{
			// MIDI clock, 24 per quarter note: the tempo is taken over a whole beat
			const uint64_t t = m_sampleTime + _ev.offset;
			const auto rate = static_cast<double>(m_machine.getSamplerate());
			if(m_lastClock == 0 || t - m_lastClock > static_cast<uint64_t>(rate))
			{
				m_clockCount = 0;
				m_clockBeatStart = t;
			}
			else if(++m_clockCount == 24)
			{
				m_clockBpm.store(static_cast<float>(60.0 * rate / static_cast<double>(t - m_clockBeatStart)));
				m_clockCount = 0;
				m_clockBeatStart = t;
			}
			m_lastClock = t;
			return true;
		}

		m_pending.push_back({_ev.offset, _ev});
		return true;
	}

	void Device::pushMidi(const synthLib::SMidiEvent& _ev)
	{
		if(!_ev.sysex.empty())
		{
			const bool matrix = _ev.sysex.size() > 5 && _ev.sysex[1] == patch::IdOberheim && _ev.sysex[2] == patch::IdMatrix;

			// With the keyboard in unison the firmware leaves two of its six voices behind
			// when the portamento rate is 0: they crawl after each note at the slowest rate
			// and the patch plays out of tune. Rate 1 arrives within one pitch update, so
			// that is what the firmware gets; the patch and the editor keep their 0.
			std::vector<uint8_t> replaced;
			if(matrix && _ev.sysex[3] == patch::OpParameter && _ev.sysex.size() >= 7 && _ev.sysex[4] == PortaRateParam && (_ev.sysex[5] & 0x7f) == 0)
			{
				replaced = patch::createParamChange(PortaRateParam, 1);
			}
			else if(matrix && (_ev.sysex[3] == patch::OpEditBuffer || _ev.sysex[3] == patch::OpSinglePatch))
			{
				if(auto data = patch::decode(_ev.sysex))
				{
					auto& rate = (*data)[patch::findParam(PortaRateParam)->byte];
					if(rate == 0)
					{
						rate = 1;
						replaced = patch::encode(*data, _ev.sysex[3], _ev.sysex[4]);
					}
				}
			}
			const auto& sx = replaced.empty() ? _ev.sysex : replaced;

			if(matrix && (sx[3] == patch::OpParameter || sx[3] == patch::OpMatrixParameter))
			{
				queueEdit(sx);
			}
			else
			{
				emit(-1, sx.data(), sx.size());
				if(matrix && (sx[3] == patch::OpEditBuffer || sx[3] == patch::OpSinglePatch))
				{
					// edits still waiting were meant for the patch this one replaces
					m_editQueue.clear();
					m_editHold = static_cast<uint32_t>(m_machine.getSamplerate() * PatchLoadSeconds);
				}
			}
			// a new patch or an edit of the speed itself replaces the synced value
			if(matrix)
			{
				if(sx[3] == patch::OpEditBuffer)
				{
					m_paramsKnown = false;
					m_editBuffer.clear();
					if(const auto data = patch::decode(sx))
					{
						m_editBuffer.assign(data->begin(), data->end());
						for(uint8_t n = 0; n < m_paramValue.size(); ++n)
							if(const auto* p = patch::findParam(n))
								m_paramValue[n] = static_cast<int8_t>((*data)[p->byte]);
						m_paramsKnown = true;
						for(uint32_t lfo = 0; lfo < 2; ++lfo)
							m_patchLfoSpeed[lfo] = (*data)[patch::findParam(LfoSpeedParam[lfo])->byte];
						const auto* bus = data->data() + patch::MatrixOffset + (patch::MatrixBusCount - 1) * 3;
						std::copy(bus, bus + 3, m_lastBus);
					}
					m_rebuildPending = false;
					m_lfoSyncDirty.store(true);
				}
				else if(sx[3] == patch::OpSinglePatch)
					m_lfoSyncDirty.store(true);
				else if(sx[3] == patch::OpParameter && (sx[4] == LfoSpeedParam[0] || sx[4] == LfoSpeedParam[1]))
				{
					m_patchLfoSpeed[sx[4] == LfoSpeedParam[0] ? 0 : 1] = sx[5] & 0x7f;
					m_lfoSyncDirty.store(true);
				}
			}
			return;
		}

		const uint8_t status = _ev.a;
		if(status < 0x80)
			return;

		// the firmware has no use for clock and active sensing, and every byte costs 320 us
		if(status >= 0xf8)
			return;

		// A program change would make the firmware load a patch from its own memory, which is
		// not the bank the plugin plays from: the sound would change behind the panel. The
		// plugin picks programs itself and sends the patch, so none is passed on.
		if((status & 0xf0) == 0xc0)
			return;

		const uint8_t bytes[3] = {status, static_cast<uint8_t>(_ev.b & 0x7f), static_cast<uint8_t>(_ev.c & 0x7f)};
		size_t length = 3;
		const uint8_t type = status & 0xf0;
		const uint8_t channel = status & 0x0f;
		if(type == 0xd0)
			length = 2;
		else if(type == 0xf0)
			length = (status == 0xf1 || status == 0xf3) ? 2 : (status == 0xf2 ? 3 : 1);

		// a note goes to one instrument, everything else to all of them
		int target = -1;
		if(type == 0x90 && bytes[2])
			target = static_cast<int>(noteOn(channel, bytes[1]));
		else if(type == 0x80 || type == 0x90)
			target = noteOff(channel, bytes[1]);
		else if(type == 0xb0)
		{
			if(bytes[1] == 64)
				pedal(channel, bytes[2] >= 64);
			else if(bytes[1] == 121)
				pedal(channel, false);
			else if(bytes[1] == 120 || bytes[1] == 123)
				forgetNotes(channel);
		}

		// The pitch the firmware writes next for a resting voice is this note, see Machine.
		// An oscillator with portamento may start a glide there instead, see Machine::expectNote.
		// In unison every voice moves to the lowest key held, the one the firmware plays, from
		// the note before: on a note on, and on the note off that leaves a lower key sounding.
		if((type == 0x90 || type == 0x80) && m_paramsKnown)
		{
			const bool on = type == 0x90 && bytes[2];
			int interval = 0;
			if(m_paramValue[KeyboardModeParam] == KeyboardUnison)
			{
				const int lowest = unisonNote();
				if(lowest >= 0 && m_unisonNote >= 0)
					interval = (lowest - m_unisonNote) * 100;
				if(lowest >= 0)
					m_unisonNote = lowest;
			}
			else
			{
				m_unisonNote = -1;
			}
			if(on || interval != 0)
				emitExpect(on ? static_cast<uint32_t>(target) : 0, m_paramValue[Dco1PortaParam] != 0, (m_paramValue[Dco2KeyboardParam] & 1) != 0, interval);
		}

		emit(target, bytes, length);
	}

	void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		takeExtras();

		if(m_mono.size() < _samples)
		{
			m_mono.resize(_samples);
			m_monoR.resize(_samples);
		}
		for(size_t i = 0; i < m_extras.size(); ++i)
		{
			if(m_extraOut[i].size() < _samples)
			{
				m_extraOut[i].resize(_samples);
				m_extraOutR[i].resize(_samples);
			}
		}

		// the spread glides to a new value, so that a knob turned does not step
		const float spreadTarget = m_spread.load();
		const float glide = std::min(1.0f, static_cast<float>(_samples) / (0.03f * m_machine.getSamplerate()));
		m_spreadNow += (spreadTarget - m_spreadNow) * glide;
		if(std::fabs(spreadTarget - m_spreadNow) < 0.002f)
			m_spreadNow = spreadTarget;
		const bool stereo = m_spreadNow > 0.0f;

		feedEdits(_samples);

		std::stable_sort(m_pending.begin(), m_pending.end(), [](const PendingEvent& _a, const PendingEvent& _b) { return _a.offset < _b.offset; });

		// drop continuous messages that a later one of the same kind replaces in this block
		for(size_t i = 0; i < m_pending.size(); ++i)
		{
			const auto key = coalesceKey(m_pending[i].event);
			if(!key)
				continue;
			for(size_t j = i + 1; j < m_pending.size(); ++j)
			{
				if(coalesceKey(m_pending[j].event) == key)
				{
					m_pending[i].event.a = 0;
					break;
				}
			}
		}

		// who gets what, and at which sample
		const uint32_t count = machineCount();
		for(uint32_t i = 0; i < count; ++i)
		{
			auto& lane = m_lanes[i];
			lane.machine = &machineAt(i);
			lane.out = i ? m_extraOut[i - 1].data() : m_mono.data();
			lane.outR = stereo ? (i ? m_extraOutR[i - 1].data() : m_monoR.data()) : nullptr;
			lane.events.clear();
			lane.bytes.clear();
		}
		if(stereo)
			spreadGains(m_spreadNow, count);
		// a new detune goes to every instrument before the events of the block
		const int detune = m_detune.load();
		if(detune != m_detuneSent)
		{
			m_detuneSent = detune;
			m_emitOffset = 0;
			const uint8_t cc[3] = {0xb0, DetuneController, static_cast<uint8_t>(detune)};
			emit(-1, cc, 3);
		}
		for(const auto& p : m_pending)
		{
			m_emitOffset = static_cast<uint32_t>(std::min<size_t>(p.offset, _samples));
			pushMidi(p.event);
		}
		m_pending.clear();

		// the extras on their threads, the first one here
		// Done with its own, the audio thread takes any that a worker has not started yet: one
		// that is slow to wake costs no more than the rendering itself.
		m_laneSamples = _samples;
		m_fpMode = fpMode();
		for(uint32_t i = 1; i < count; ++i)
		{
			m_taken[i].store(false);
			m_go[i - 1].release();
		}
		renderLane(m_lanes[0], _samples);
		for(uint32_t i = 1; i < count; ++i)
			if(!m_taken[i].exchange(true))
				renderLane(m_lanes[i], _samples);
		for(uint32_t i = 1; i < count; ++i)
			m_done.acquire();
		for(uint32_t i = 1; i < count; ++i)
		{
			const float* extra = m_extraOut[i - 1].data();
			for(size_t s = 0; s < _samples; ++s)
				m_mono[s] += extra[s];
			if(stereo)
			{
				const float* extraR = m_extraOutR[i - 1].data();
				for(size_t s = 0; s < _samples; ++s)
					m_monoR[s] += extraR[s];
			}
		}

		for(auto& e : m_extras)
			e.readyIn = e.readyIn > _samples ? e.readyIn - static_cast<uint32_t>(_samples) : 0;

		m_sampleTime += _samples;
		if(m_lastClock && m_sampleTime - m_lastClock > static_cast<uint64_t>(m_machine.getSamplerate()))
			m_clockBpm.store(0.0f);

		const bool anySync = m_lfoSync[0].load() || m_lfoSync[1].load();
		if(m_lfoSyncDirty.exchange(false))
			sendLfoSync(true);
		else if(anySync && std::fabs(getHostBpm() - m_sentBpm) > 0.05f)
			sendLfoSync(false);

		const float* right = stereo ? m_monoR.data() : m_mono.data();
		for(size_t i = 0; i < _samples; ++i)
		{
			if(_outputs[0]) _outputs[0][i] = m_mono[i];
			if(_outputs[1]) _outputs[1][i] = right[i];
		}
	}

	void Device::parseMidiOut(const uint8_t _byte)
	{
		if(_byte == 0xf0)
		{
			m_txSysex.assign(1, _byte);
			return;
		}
		if(!m_txSysex.empty())
		{
			m_txSysex.push_back(_byte);
			if(_byte == 0xf7)
			{
				synthLib::SMidiEvent e(synthLib::MidiEventSource::Device);
				e.sysex = std::move(m_txSysex);
				m_midiOut.push_back(std::move(e));
				m_txSysex.clear();
			}
			return;
		}
		if(_byte >= 0xf8)
			return;

		if(_byte & 0x80)
		{
			// F4-F7 carry no data
			if(_byte >= 0xf4)
			{
				if(_byte != 0xf7)
					m_midiOut.emplace_back(synthLib::MidiEventSource::Device, _byte, 0, 0);
				m_txRunningStatus = 0;
			}
			else
				m_txRunningStatus = _byte;
			m_txShort.clear();
			return;
		}
		if(!m_txRunningStatus)
			return;

		m_txShort.push_back(_byte);
		const auto type = m_txRunningStatus & 0xf0;
		const size_t len = (type == 0xc0 || type == 0xd0 || m_txRunningStatus == 0xf1 || m_txRunningStatus == 0xf3) ? 1 : 2;
		if(m_txShort.size() == len)
		{
			m_midiOut.emplace_back(synthLib::MidiEventSource::Device, m_txRunningStatus, m_txShort[0], len > 1 ? m_txShort[1] : 0);
			m_txShort.clear();
			if(type == 0xf0)
				m_txRunningStatus = 0;
		}
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_txBytes.clear();
		m_machine.getHardware().midiOut(m_txBytes);
		for(const auto b : m_txBytes)
			parseMidiOut(b);

		// the others answer too, and nobody listens
		for(auto& e : m_extras)
		{
			m_drain.clear();
			e.machine->getHardware().midiOut(m_drain);
		}

		_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}
}
