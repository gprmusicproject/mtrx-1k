#include "matrixHardware.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace matrixLib
{
	Hardware::Hardware() : m_cpu(*this), m_acia(MidiCyclesPerByte)
	{
		m_rom.assign(0x8000, 0xff);
		m_patchRom.assign(0x10000, 0xff);
		m_sram.resize(0x8000);

		// an SRAM powers up with noise, the firmware treats an all-zero cold-start flag as valid RAM
		uint32_t seed = 0x1234567;
		for(auto& b : m_sram)
		{
			seed = seed * 1103515245u + 12345u;
			b = static_cast<uint8_t>(seed >> 16);
		}
	}

	bool Hardware::setFirmware(const std::vector<uint8_t>& _rom)
	{
		if(_rom.size() != 0x8000)
			return false;
		m_rom = _rom;
		return true;
	}

	void Hardware::setPatchRom(const std::vector<uint8_t>& _rom)
	{
		m_patchRom.assign(0x10000, 0xff);
		std::memcpy(m_patchRom.data(), _rom.data(), std::min<size_t>(_rom.size(), m_patchRom.size()));
	}

	void Hardware::reset()
	{
		for(auto& t : m_timers)
			t.reset();
		m_sysTimer.reset();
		m_acia.reset();
		m_bankLatch = 0;
		m_cpu.reset();
	}

	uint8_t* Hardware::sramPtr(const uint16_t _addr)
	{
		if(_addr >= 0x4000 && _addr < 0x6000)
			return &m_sram[_addr - 0x4000];
		if(_addr >= 0x6000 && _addr < 0x8000)
		{
			const uint32_t page = ((m_bankLatch + 1) & 3) * 0x2000;
			return &m_sram[page + (_addr - 0x6000)];
		}
		return nullptr;
	}

	uint8_t Hardware::getLatch(const uint16_t _addr) const
	{
		return m_latches[_addr & 0x3ff];
	}

	uint8_t Hardware::read(const uint16_t _addr)
	{
		if(_addr >= 0x8000)
			return m_rom[_addr - 0x8000];

		if(auto* p = sramPtr(_addr))
			return *p;

		if(_addr >= 0x2000)
			return m_patchRom[(m_bankLatch & 7) * 0x2000 + (_addr - 0x2000)];

		const uint64_t t = m_cpu.getCycles();
		uint8_t v = 0;

		if(_addr < 0x1000)
			v = m_timers[(_addr >> 10) & 3].read(_addr & 3, t * 2);
		else if(_addr >= 0x1400 && _addr < 0x1800)
		{
			if(_addr & 0x0200)
				v = m_sysTimer.read(_addr & 3, t);
			else
			{
				m_acia.advance(t);
				v = m_acia.read(_addr & 1, t);
			}
		}
		else if(_addr >= 0x1800 && _addr < 0x1c00)
			v = (_addr & 1) ? m_switch1 : m_switch0;
		else
			v = 0xff;

		if(onIoAccess)
			onIoAccess(_addr, v, false);
		return v;
	}

	void Hardware::write(const uint16_t _addr, const uint8_t _val)
	{
		if(_addr >= 0x8000)
			return;

		if(auto* p = sramPtr(_addr))
		{
			*p = _val;
			return;
		}

		if(onIoAccess)
			onIoAccess(_addr, _val, true);

		if(_addr >= 0x2000)
			return;

		const uint64_t t = m_cpu.getCycles();

		if(_addr < 0x1000)
		{
			m_timers[(_addr >> 10) & 3].write(_addr & 3, _val, t * 2);
		}
		else if(_addr < 0x1400)
		{
			// every write latches the S&H address, even = DAC high byte, odd = low byte.
			// Bit 2 of the low byte inhibits the multiplexer, the selected S&H tracks the DAC otherwise
			m_cvChannel = cvChannel(_addr);
			m_cvRange = static_cast<uint8_t>((_addr >> 7) & 3);
			if(_addr & 1)
			{
				m_dacLow = _val;
				m_cvInhibit = (_val & 0x04) != 0;
			}
			else
			{
				m_dacHigh = _val;
			}

			if(!m_cvInhibit)
			{
				const auto raw = static_cast<uint16_t>((m_dacHigh << 8) | m_dacLow);
				const auto code = static_cast<uint16_t>(raw >> 3);
				// the DAC sees the low 15 bits: 0 to +5 V
				float volts = static_cast<float>(raw & 0x7ff8) * (5.0f / 32768.0f);
				if(m_cvRange & 2)
				{
					const bool filter = m_cvChannel < VoiceCount * 8 && (m_cvChannel & 7) == CvFreq;
					volts = filter ? volts * (FilterRangeSpan / 5.0f) + FilterRangeLow : volts - 2.5f;
				}
				m_cv[m_cvChannel] = code;
				// The first multiplexer of a channel charges the hold capacitor. Each voice has
				// a second one on the pin side of the 1 MOhm, enabled while bit 7 of the high
				// latch (FAST*) is low: the firmware clears it on the write that starts a note
				// or steps an LFO, so those land at once, and sets it on every other refresh,
				// which then reaches the pin through the RC. Balance and resonance have no
				// second multiplexer.
				// The firmware refreshes every voltage about 75 times a second, and a modulation
				// arrives as a step each time. Through the RC that is a staircase with rounded
				// corners: on the filter and on the pulse widths one hears it as a hum under a
				// deep or fast modulation. Small steps that follow one another at that pace are
				// drawn out into a line to the next one; the first step of a note and anything
				// large still land as they are. Not while the firmware calibrates.
				const auto kind = m_cvChannel & 7;
				const bool voice = m_cvChannel < VoiceCount * 8;
				const bool joinable = voice && (kind == CvFreq || kind == CvPwA || kind == CvPwB) && m_cvJoinUsual && !(m_latches[0x100] & 0x02);
				auto& since = m_cvSinceWrite[m_cvChannel];
				if(joinable)
				{
					// An edit of a modulation amount moves a held note in one leap, a little
					// after the message. While one is expected, the first step that is out of
					// line with those before it is taken for that leap: the voltage keeps its
					// course and takes the leap in over the time until the next edit can come.
					const float delta = volts - m_cvRaw[m_cvChannel];
					const float usual = since <= m_cvJoinLongest ? m_cvDelta[m_cvChannel] : 0.0f;
					if(delta != 0.0f)
					{
						float kept = delta;
						if(m_cvEditUntil && !m_cvEditFrom && !m_cvEditDone[m_cvChannel] && (m_dacHigh & 0x80))
						{
							const float leap = delta - usual;
							if(std::fabs(leap) > EditLeapLeast && std::fabs(leap) > 3.0f * std::fabs(usual))
							{
								m_cvOffset[m_cvChannel] += leap;
								m_cvOffsetStep[m_cvChannel] = std::fabs(m_cvOffset[m_cvChannel]) / static_cast<float>(std::max<uint32_t>(1, m_cvEditRamp));
								m_cvEditDone[m_cvChannel] = true;
								kept = usual;
							}
						}
						m_cvDelta[m_cvChannel] = kept;
					}
					m_cvRaw[m_cvChannel] = volts;
					volts -= m_cvOffset[m_cvChannel];
				}
				const float step = volts - m_cvTarget[m_cvChannel];
				const bool joined = joinable && since <= m_cvJoinLongest && std::fabs(step) < (kind == CvFreq ? FilterJoinLimit : PulseJoinLimit);
				if(joined && step != 0.0f)
				{
					const uint32_t span = std::max(since, m_cvJoinShortest);
					m_cvRampLeft[m_cvChannel] = span;
					m_cvRampStep[m_cvChannel] = (volts - m_cvHold[m_cvChannel]) / static_cast<float>(span);
				}
				else if(!joined)
				{
					m_cvHold[m_cvChannel] = volts;
					m_cvRampLeft[m_cvChannel] = 0;
					if(!(m_dacHigh & 0x80) && voice)
						m_cvVolts[m_cvChannel] = volts;
				}
				else if(!m_cvRampLeft[m_cvChannel])
				{
					// a refresh that changes nothing tops up the hold capacitor
					m_cvHold[m_cvChannel] = volts;
				}
				m_cvTarget[m_cvChannel] = volts;
				// the pace is that of the changes: a refresh in between does not count
				if(std::fabs(step) > 1e-6f)
					since = 0;
				if(onCvWrite)
					onCvWrite(_addr & 0xfffe, code);
			}
		}
		else if(_addr < 0x1800)
		{
			if(_addr & 0x0200)
				m_sysTimer.write(_addr & 3, _val, t);
			else
			{
				m_acia.advance(t);
				m_acia.write(_addr & 1, _val, t);
			}
		}
		else if(_addr >= 0x1c00)
		{
			m_latches[_addr & 0x3ff] = _val;
			if(_addr == 0x1d80)
				m_bankLatch = _val;
		}
	}

	void Hardware::expectEdit(const uint32_t _from, const uint32_t _until, const uint32_t _ramp)
	{
		m_cvEditFrom = _from;
		m_cvEditUntil = _until;
		m_cvEditRamp = _ramp;
		m_cvEditDone.fill(false);
	}

	void Hardware::advanceCv(const float _k)
	{
		if(m_cvEditFrom)
			--m_cvEditFrom;
		if(m_cvEditUntil)
			--m_cvEditUntil;
		constexpr float toPin = CvHoldCap / (CvHoldCap + CvPinCap);
		for(uint32_t i = 0; i < CvChannelCount; ++i)
		{
			if(m_cvSinceWrite[i] < 0x7fffffffu)
				++m_cvSinceWrite[i];
			if(m_cvOffset[i] != 0.0f)
			{
				// the leap of an edit comes in: what is held back shrinks, the voltage follows
				const float before = m_cvOffset[i];
				m_cvOffset[i] = std::fabs(before) <= m_cvOffsetStep[i] ? 0.0f : before - std::copysign(m_cvOffsetStep[i], before);
				const float released = before - m_cvOffset[i];
				m_cvHold[i] += released;
				m_cvTarget[i] += released;
			}
			if(m_cvRampLeft[i])
			{
				// the hold capacitor on its way to the step the firmware asked for
				m_cvHold[i] += m_cvRampStep[i];
				if(--m_cvRampLeft[i] == 0)
					m_cvHold[i] = m_cvTarget[i];
			}
			const float moved = (m_cvHold[i] - m_cvVolts[i]) * _k;
			m_cvVolts[i] += moved * toPin;
			m_cvHold[i] -= moved * (1.0f - toPin);
		}
	}

	uint32_t Hardware::getDcoPeriod(const uint32_t _voice, const uint32_t _dco)
	{
		auto& timer = m_timers[(_dco ? 2 : 0) + (_voice >= 3 ? 1 : 0)];
		// To move a pitch without a jump in phase the firmware restarts the timer with what is
		// left of the running period, scaled to the new one, and writes the new period behind
		// it. The converter model keeps its own phase, so it needs the period that was
		// written last, not the stretch that finishes the old cycle.
		return timer.getWrittenPeriod(_voice % 3, m_cpu.getCycles() * 2);
	}

	void Hardware::updateIrq()
	{
		const uint64_t t = m_cpu.getCycles();
		m_acia.advance(t);
		m_cpu.setFirq(m_acia.getIrq());
		m_cpu.setIrq(!m_sysTimer.getOut(2, t));
	}

	void Hardware::runUntil(const uint64_t _targetCycle)
	{
		while(m_cpu.getCycles() < _targetCycle)
		{
			updateIrq();
			if(onInstruction)
				onInstruction(m_cpu.getPC());
			m_cpu.step();
		}
	}
}
