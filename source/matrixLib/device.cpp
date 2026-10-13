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
		constexpr uint8_t Dco2KeyboardParam = 18;   // 1 = DCO 2 follows the keyboard with portamento

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

	Device::~Device() = default;

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
		return true;
	}

	void Device::runSeconds(const float _seconds)
	{
		std::vector<float> buf(512);
		auto remaining = static_cast<size_t>(_seconds * m_machine.getSamplerate());
		while(remaining)
		{
			const auto n = std::min(remaining, buf.size());
			m_machine.render(buf.data(), n);
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
			hw.midiIn(b);
		m_editHold = static_cast<uint32_t>(m_machine.getSamplerate() * EditSeconds);

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
			hw.expectEdit(static_cast<uint32_t>(rate * 0.03f), static_cast<uint32_t>(rate * 0.25f), static_cast<uint32_t>(rate * BusSeconds));
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
		auto& hw = m_machine.getHardware();

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
				for(const auto b : sx)
					hw.midiIn(b);
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
					if(const auto data = patch::decode(sx))
					{
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

		// The pitch the firmware writes next for a resting voice is this note, see Machine.
		// Not for an oscillator with portamento: its first step is the start of a glide.
		if((status & 0xf0) == 0x90 && (_ev.c & 0x7f) && m_paramsKnown)
			m_machine.expectNote(m_paramValue[Dco1PortaParam] == 0, m_paramValue[Dco2KeyboardParam] != 1);

		hw.midiIn(status);
		switch(status & 0xf0)
		{
		case 0xc0:
		case 0xd0:
			hw.midiIn(_ev.b & 0x7f);
			break;
		case 0xf0:
			if(status == 0xf1 || status == 0xf3)
				hw.midiIn(_ev.b & 0x7f);
			else if(status == 0xf2)
			{
				hw.midiIn(_ev.b & 0x7f);
				hw.midiIn(_ev.c & 0x7f);
			}
			break;
		default:
			hw.midiIn(_ev.b & 0x7f);
			hw.midiIn(_ev.c & 0x7f);
			break;
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		if(m_mono.size() < _samples)
			m_mono.resize(_samples);

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

		size_t pos = 0;
		size_t ev = 0;
		while(pos < _samples)
		{
			while(ev < m_pending.size() && m_pending[ev].offset <= pos)
				pushMidi(m_pending[ev++].event);

			size_t end = _samples;
			if(ev < m_pending.size())
				end = std::min<size_t>(end, std::max<size_t>(m_pending[ev].offset, pos + 1));

			m_machine.render(m_mono.data() + pos, end - pos);
			pos = end;
		}
		while(ev < m_pending.size())
			pushMidi(m_pending[ev++].event);
		m_pending.clear();

		m_sampleTime += _samples;
		if(m_lastClock && m_sampleTime - m_lastClock > static_cast<uint64_t>(m_machine.getSamplerate()))
			m_clockBpm.store(0.0f);

		const bool anySync = m_lfoSync[0].load() || m_lfoSync[1].load();
		if(m_lfoSyncDirty.exchange(false))
			sendLfoSync(true);
		else if(anySync && std::fabs(getHostBpm() - m_sentBpm) > 0.05f)
			sendLfoSync(false);

		for(size_t i = 0; i < _samples; ++i)
		{
			if(_outputs[0]) _outputs[0][i] = m_mono[i];
			if(_outputs[1]) _outputs[1][i] = m_mono[i];
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

		_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}
}
