#include "fxOwn.h"
#include "fxChain.h"
#include "fxZitaRev.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fxLib
{
	namespace
	{
		constexpr double Pi = 3.141592653589793;
		constexpr double TwoPi = 2.0 * Pi;

		// one-pole coefficient for a cutoff
		float onePole(const double _hz, const double _rate)
		{
			return static_cast<float>(1.0 - std::exp(-TwoPi * std::min(_hz, _rate * 0.45) / _rate));
		}

		// a delay line read between samples
		struct Line
		{
			std::vector<float> data;
			size_t pos = 0;

			void resize(const size_t _size) { data.assign(std::max<size_t>(_size, 4), 0.0f); pos = 0; }
			void clear() { std::fill(data.begin(), data.end(), 0.0f); pos = 0; }
			void write(const float _x)
			{
				data[pos] = _x;
				if(++pos == data.size())
					pos = 0;
			}
			// _delay samples back from the last written, at least one
			float read(const double _delay) const
			{
				const double d = std::clamp(_delay, 1.0, static_cast<double>(data.size() - 2));
				const auto whole = static_cast<size_t>(d);
				const auto frac = static_cast<float>(d - static_cast<double>(whole));
				const size_t n = data.size();
				const size_t a = (pos + n - whole) % n;
				const size_t b = (a + n - 1) % n;
				return data[a] + frac * (data[b] - data[a]);
			}
		};

		// Schroeder allpass on a line: blurs without colouring
		struct Allpass
		{
			Line line;
			double length = 1.0;

			void resize(const double _length, const double _extra = 0.0)
			{
				length = _length;
				line.resize(static_cast<size_t>(_length + _extra) + 4);
			}
			float process(const float _x, const float _g, const double _offset = 0.0)
			{
				const float delayed = line.read(length + _offset);
				const float y = -_g * _x + delayed;
				line.write(_x + _g * y);
				return y;
			}
		};

		// ── Modulation ──────────────────────────────────────────────────────────

		// The chorus of a classic polysynth: two bucket-brigade lines swept in antiphase by one
		// LFO around 3.3 ms, each between the matched filters of the chip and with its soft
		// saturation, its gain following the clock as the charge transfer does. Ported from
		// KR106Chorus.h and BBDFilter.h of Ultramaster KR-106 by kayrockscreenprinting
		// (github.com/kayrockscreenprinting/ultramaster_kr106, GPL v3), whose rates, depths and
		// gains were measured on the hardware. Left out are its noise floor, the leakage noise,
		// the clicks of the clock and the mains ripple. The instrument is mono in and stereo
		// out; here each side takes its own line, so the spread of the voices stays.
		// Mode I as the instrument; II as the instrument but deeper, so that it stands apart from
		// I; I+II, unlike the instrument (whose fast vibrato is left out), plays I and II at once
		// on a second pair of lines, two choruses together. Mix in the middle is the instrument's own
		// balance of dry and wet, to the left the chorus fades, to the right the dry sound.
		// Controls: mode, mix.
		class Chorus final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = static_cast<float>(_rate);
				for(auto& l : m_lines)
					l.prepare(m_rate);
				m_fadeStep = 1.0f / (FadeMs * 0.001f * m_rate);
				m_smoothK = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _rate)));
				clear();
			}

			void clear() override
			{
				for(auto& l : m_lines)
					l.clear();
				m_phase = {0.0f, 0.0f};
				m_mode = -1;
				m_fade = 0.0f;
				m_hold = 0;
				m_mix = -1.0f;
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const int want = std::clamp(static_cast<int>(std::lround(_v[0] * 2.0f)), 0, 2);
				if(m_mode < 0)
					setMode(want);
				if(m_mix < 0.0f)
					m_mix = _v[1];

				for(int i = 0; i < _samples; ++i)
				{
					// another mode: the chorus fades out, changes and comes back
					const float target = want == m_mode && m_hold == 0 ? 1.0f : 0.0f;
					if(m_hold > 0)
						--m_hold;
					m_fade = target > m_fade ? std::min(target, m_fade + m_fadeStep) : std::max(target, m_fade - m_fadeStep);
					if(m_fade <= 0.0f && want != m_mode)
						setMode(want);
					for(int p = 0; p < 2; ++p)
						m_depth[p] += (m_depthTarget[p] - m_depth[p]) * m_fadeStep;
					m_mix += m_smoothK * (_v[1] - m_mix);

					// per pair one LFO, its lines in antiphase, each a little off as two chips are
					const float in[2] = {_inL[i], _inR[i]};
					float wet[2] = {0.0f, 0.0f};
					for(int p = 0; p < m_pairs; ++p)
					{
						m_phase[p] += m_lfoStep[p];
						if(m_phase[p] >= 1.0f)
							m_phase[p] -= 1.0f;
						const float lfo = 1.0f - 4.0f * std::fabs(m_phase[p] - 0.5f);
						const float ms[2] = {std::max((CentreMs + m_depth[p] * lfo) * (1.0f - ClockTrim), MinMs),
						                     std::max((CentreMs - m_depth[p] * lfo) * (1.0f + ClockTrim), MinMs)};
						for(int ch = 0; ch < 2; ++ch)
						{
							// the charge lost along the stages grows as the clock slows
							const float clock = std::max(Stages / (2.0f * ms[ch] * 0.001f), MinClock);
							const float gain = (ch ? 1.0f - GainTrim : 1.0f + GainTrim) * (1.0f - CteLoss * (1.0f / clock - 1.0f / CentreClock));
							wet[ch] += gain * m_lines[2 * p + ch].process(in[ch], ms[ch] * 0.001f * m_rate);
						}
					}
					// two pairs together as loud as one
					if(m_pairs == 2)
					{
						wet[0] *= 0.7071f;
						wet[1] *= 0.7071f;
					}

					// the summer of each output: dry and wet, at the share the mix gives, as loud
					// together as the dry sound alone; while the chorus fades the dry sound fills in
					const float dryShare = std::min(1.0f, 2.0f - 2.0f * m_mix);
					const float dry = dryShare * (1.0f - m_fade * (1.0f - DryGain)) + (1.0f - m_fade) * (1.0f - dryShare);
					const float wetGain = std::min(1.0f, 2.0f * m_mix) * m_fade * WetGain;
					const float norm = 1.0f / std::sqrt(std::max(dry * dry + wetGain * wetGain, 1.0e-6f));
					_outL[i] = norm * (dry * in[0] + wetGain * wet[0]);
					_outR[i] = norm * (dry * in[1] + wetGain * wet[1]);
				}
			}

		private:
			// the filter before and after the chip: a Butterworth pair at 9661 Hz and a pole at
			// 20 kHz, as the emitter followers of the instrument measure
			struct BbdFilter
			{
				float g = 0.0f, a1 = 0.0f, gPole = 0.0f;
				float ic1 = 0.0f, ic2 = 0.0f, pole = 0.0f;

				void prepare(const float _rate)
				{
					g = std::tan(static_cast<float>(Pi) * std::min(9661.0f, _rate * 0.45f) / _rate);
					a1 = 1.0f / (1.0f + g / 0.7071f + g * g);
					gPole = std::tan(static_cast<float>(Pi) * std::min(20000.0f, _rate * 0.45f) / _rate);
				}
				void clear() { ic1 = ic2 = pole = 0.0f; }
				float process(const float _x)
				{
					const float v3 = _x - ic2;
					const float v1 = a1 * ic1 + a1 * g * v3;
					const float v2 = ic2 + g * v1;
					ic1 = 2.0f * v1 - ic1;
					ic2 = 2.0f * v2 - ic2;
					const float v = (v2 - pole) * gPole / (1.0f + gPole);
					const float lp = pole + v;
					pole = lp + v;
					return lp;
				}
			};

			// one chip: filtered, softly saturated, delayed and read between samples (Hermite),
			// filtered again
			struct BbdLine
			{
				std::vector<float> buf;
				int mask = 0, pos = 0;
				BbdFilter pre, post;

				void prepare(const float _rate)
				{
					int len = 1;
					while(len < static_cast<int>(_rate * 0.02f) + 4)
						len <<= 1;
					buf.assign(static_cast<size_t>(len), 0.0f);
					mask = len - 1;
					pre.prepare(_rate);
					post.prepare(_rate);
				}
				void clear()
				{
					std::fill(buf.begin(), buf.end(), 0.0f);
					pos = 0;
					pre.clear();
					post.clear();
				}
				float process(const float _x, const float _delay)
				{
					const float filtered = pre.process(_x);
					buf[static_cast<size_t>(pos & mask)] = std::tanh(filtered * Drive) / Drive;
					float r = static_cast<float>(pos) - _delay;
					if(r < 0.0f)
						r += static_cast<float>(mask + 1);
					const int i1 = static_cast<int>(r);
					const float f = r - static_cast<float>(i1);
					const float y0 = buf[static_cast<size_t>((i1 - 1) & mask)], y1 = buf[static_cast<size_t>(i1 & mask)];
					const float y2 = buf[static_cast<size_t>((i1 + 1) & mask)], y3 = buf[static_cast<size_t>((i1 + 2) & mask)];
					const float c1 = 0.5f * (y2 - y0);
					const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
					const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
					const float wet = ((c3 * f + c2) * f + c1) * f + y1;
					pos = (pos + 1) & mask;
					return post.process(wet);
				}
			};

			// I and II each on the first pair of lines; I+II both, II on the second pair
			void setMode(const int _mode)
			{
				m_mode = _mode;
				const int first = _mode == 1 ? 1 : 0;
				m_pairs = _mode == 2 ? 2 : 1;
				for(int p = 0; p < m_pairs; ++p)
				{
					const int kind = p == 0 ? first : 1;
					m_lfoStep[p] = Rates[kind] / m_rate;
					m_depthTarget[p] = Depths[kind];
					if(m_fade <= 0.0f)
						m_depth[p] = m_depthTarget[p];
				}
				// the second pair starts empty, and is heard only once its lines are full
				if(m_pairs == 2)
				{
					m_lines[2].clear();
					m_lines[3].clear();
					m_hold = static_cast<int>(0.008f * m_rate);
				}
			}

			// I as the instrument measures; II faster as the instrument and deeper than its 1.71 ms
			static constexpr float Rates[2] = {0.514f, 0.842f};   // Hz
			static constexpr float Depths[2] = {2.13f, 2.4f};     // ms, half the swing

			static constexpr float CentreMs = 3.30f;
			static constexpr float MinMs = 0.1f;
			static constexpr float Stages = 256.0f;
			static constexpr float MinClock = 5000.0f;
			static constexpr float CentreClock = 40000.0f;
			static constexpr float CteLoss = 4468.0f;
			static constexpr float ClockTrim = 0.015f;
			static constexpr float GainTrim = 0.04f;
			static constexpr float DryGain = 0.863f;
			static constexpr float WetGain = 1.257f;
			static constexpr float Drive = 0.12f;
			static constexpr float FadeMs = 5.0f;

			float m_rate = 48000.0f;
			std::array<BbdLine, 4> m_lines;   // two pairs, left and right
			int m_pairs = 1;
			int m_hold = 0;   // samples before the chorus may fade in
			std::array<float, 2> m_phase{}, m_lfoStep{}, m_depth{}, m_depthTarget{};
			int m_mode = -1;
			float m_fade = 0.0f, m_fadeStep = 0.001f;
			float m_mix = -1.0f, m_smoothK = 0.001f;
		};

		// Six first-order allpass stages per side, swept by a sine LFO, a quarter cycle apart
		// left and right, with feedback around them.
		// Controls: rate, depth, feedback, mix.
		class Phaser final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				m_smoothK = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _rate)));
				clear();
			}

			void clear() override
			{
				for(auto& s : m_x1) s.fill(0.0f);
				for(auto& s : m_y1) s.fill(0.0f);
				m_last.fill(0.0f);
				m_phase = 0.0;
				m_depth = -1.0f;
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const double lfoHz = 0.05 * std::pow(160.0, static_cast<double>(_v[0]));   // 0.05 to 8 Hz
				const float feedback = 0.85f * _v[2];
				const float mix = _v[3];
				const double step = TwoPi * lfoHz / m_rate;
				std::array<float, 2> a{};
				if(m_depth < 0.0f)
					m_depth = _v[1];
				for(int i = 0; i < _samples; ++i)
				{
					// the depth glides, or a knob turned would step the notches
					m_depth += m_smoothK * (_v[1] - m_depth);
					const double depth = m_depth;
					// the notches sweep in octaves, from 200 Hz up to 6 octaves at full depth
					if((i & 15) == 0)
					{
						for(int ch = 0; ch < 2; ++ch)
						{
							const double lfo = 0.5 + 0.5 * std::sin(m_phase + ch * Pi * 0.5);
							const double hz = 200.0 * std::pow(2.0, 6.0 * depth * lfo);
							const double t = std::tan(Pi * std::min(hz, m_rate * 0.45) / m_rate);
							a[ch] = static_cast<float>((t - 1.0) / (t + 1.0));
						}
					}
					m_phase += step;
					if(m_phase > TwoPi)
						m_phase -= TwoPi;
					const float in[2] = {_inL[i], _inR[i]};
					float out[2];
					for(int ch = 0; ch < 2; ++ch)
					{
						float x = in[ch] + m_last[ch] * feedback;
						for(int s = 0; s < Stages; ++s)
						{
							const float y = a[ch] * x + m_x1[ch][s] - a[ch] * m_y1[ch][s];
							m_x1[ch][s] = x;
							m_y1[ch][s] = y;
							x = y;
						}
						m_last[ch] = x;
						out[ch] = in[ch] + mix * (x - in[ch]);
					}
					_outL[i] = out[0];
					_outR[i] = out[1];
				}
			}

		private:
			static constexpr int Stages = 6;
			double m_rate = 48000.0;
			std::array<std::array<float, Stages>, 2> m_x1{}, m_y1{};
			std::array<float, 2> m_last{};
			double m_phase = 0.0;
			float m_depth = -1.0f;
			float m_smoothK = 0.001f;
		};

		// A short delay swept by a raised-cosine LFO, a quarter cycle apart left and right,
		// with feedback that can be inverted: the jet of a tape or bucket-brigade flanger. The
		// sweep moves in octaves, as the ear hears the comb move.
		// Controls: rate, depth, manual (where the sweep starts), feedback (middle = none),
		// mix (the middle gives the deepest notches).
		class Flanger final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				for(auto& l : m_lines)
					l.resize(static_cast<size_t>(_rate * (MaxMs + 2.0) / 1000.0) + 8);
				m_smoothK = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _rate)));
				clear();
			}

			void clear() override
			{
				for(auto& l : m_lines) l.clear();
				m_last.fill(0.0f);
				m_phase = 0.0;
				m_smooth.fill(-1.0f);
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const double lfoHz = 0.02 * std::pow(250.0, static_cast<double>(_v[0]));   // 0.02 to 5 Hz
				const float mix = _v[4];
				const double step = TwoPi * lfoHz / m_rate;
				const double samplesPerMs = m_rate / 1000.0;
				for(int k = 0; k < 3; ++k)
					if(m_smooth[k] < 0.0f)
						m_smooth[k] = _v[1 + k];

				for(int i = 0; i < _samples; ++i)
				{
					// depth, manual and feedback glide, or a knob turned would make the delay jump
					for(int k = 0; k < 3; ++k)
						m_smooth[k] += m_smoothK * (_v[1 + k] - m_smooth[k]);
					const double depth = m_smooth[0];
					const double manual = m_smooth[1];
					const float feedback = 0.95f * (2.0f * m_smooth[2] - 1.0f);
					const float in[2] = {_inL[i], _inR[i]};
					float out[2];
					for(int ch = 0; ch < 2; ++ch)
					{
						const double lfo = 0.5 - 0.5 * std::cos(m_phase + ch * Pi * 0.5);
						const double ms = std::min(MaxMs, MinMs * std::pow(2.0, 5.0 * manual + 3.5 * depth * lfo));
						const float wet = m_lines[ch].read(ms * samplesPerMs);
						// the feedback is kept from running away by a soft limit
						m_lines[ch].write(in[ch] + std::tanh(feedback * m_last[ch]));
						m_last[ch] = wet;
						out[ch] = in[ch] + mix * (wet - in[ch]);
					}
					m_phase += step;
					if(m_phase > TwoPi)
						m_phase -= TwoPi;
					_outL[i] = out[0];
					_outR[i] = out[1];
				}
			}

		private:
			static constexpr double MinMs = 0.25;
			static constexpr double MaxMs = 20.0;
			double m_rate = 48000.0;
			std::array<Line, 2> m_lines;
			std::array<float, 2> m_last{};
			double m_phase = 0.0;
			std::array<float, 3> m_smooth{-1.0f, -1.0f, -1.0f};
			float m_smoothK = 0.001f;
		};

		// The ensemble of a string machine, as the Waldorf Crema has it after the Streichfett.
		// Strings: three bucket-brigade lines, each moved by a slow and a fast LFO at once, a
		// third of a cycle apart from line to line, the many players bowing slightly out of
		// step; left and right take different lines. Chorus: a slow two-tap chorus. Both: the
		// strings into the chorus.
		// Controls: mode (strings, chorus, both), depth, mix.
		class Ensemble final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				for(auto& l : m_lines)
					l.resize(static_cast<size_t>(_rate * 0.03) + 8);
				m_filterK = onePole(8000.0, _rate);
				m_smoothK = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _rate)));
				clear();
			}

			void clear() override
			{
				for(auto& l : m_lines) l.clear();
				for(auto& f : m_filters) f.fill(0.0f);
				m_slow = m_fast = m_chorus = 0.0;
				m_depth = -1.0f;
				m_strings = m_chorusOn = -1.0f;
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const int mode = std::clamp(static_cast<int>(std::lround(_v[0] * 2.0f)), 0, 2);
				// the stages fade in and out when the mode changes, so that it does not click
				const float stringsTarget = mode != 1 ? 1.0f : 0.0f;
				const float chorusTarget = mode != 0 ? 1.0f : 0.0f;
				if(m_depth < 0.0f)
				{
					m_depth = _v[1];
					m_strings = stringsTarget;
					m_chorusOn = chorusTarget;
				}
				const float mix = _v[2];
				const double perMs = m_rate / 1000.0;
				const double slowStep = TwoPi * 0.63 / m_rate;
				const double fastStep = TwoPi * 6.1 / m_rate;
				const double chorusStep = 0.5 / m_rate;   // cycles a sample
				constexpr double Third = TwoPi / 3.0;

				for(int i = 0; i < _samples; ++i)
				{
					m_depth += m_smoothK * (_v[1] - m_depth);
					m_strings += m_smoothK * (stringsTarget - m_strings);
					m_chorusOn += m_smoothK * (chorusTarget - m_chorusOn);
					const float in[2] = {_inL[i], _inR[i]};
					const float mono = 0.5f * (in[0] + in[1]);

					// the strings: three lines on the mono input
					auto& pre = m_filters[0];
					pre[0] += m_filterK * (mono - pre[0]);
					pre[1] += m_filterK * (pre[0] - pre[1]);
					std::array<float, 3> line{};
					for(int k = 0; k < 3; ++k)
					{
						m_lines[k].write(pre[1]);
						const double ms = 5.5 + m_depth * (2.2 * std::sin(m_slow + k * Third) + 0.35 * std::sin(m_fast + k * Third));
						auto& post = m_filters[1 + k];
						post[0] += m_filterK * (m_lines[k].read(ms * perMs) - post[0]);
						post[1] += m_filterK * (post[0] - post[1]);
						line[k] = post[1];
					}
					std::array<float, 2> s = {(line[0] + 0.5f * line[1]) / 1.5f, (line[2] + 0.5f * line[1]) / 1.5f};
					for(int ch = 0; ch < 2; ++ch)
						s[ch] = in[ch] + m_strings * (s[ch] - in[ch]);

					// the chorus after them: a triangle, the right side the other way
					double tri = 4.0 * std::fabs(m_chorus - 0.5) - 1.0;
					std::array<float, 2> out{};
					for(int ch = 0; ch < 2; ++ch)
					{
						m_lines[3 + ch].write(s[ch]);
						const double ms = 3.5 + 1.6 * m_depth * (ch ? -tri : tri);
						const float c = m_lines[3 + ch].read(ms * perMs);
						out[ch] = s[ch] + m_chorusOn * (0.5f * (s[ch] + c) - s[ch]);
					}

					m_slow += slowStep;
					if(m_slow > TwoPi) m_slow -= TwoPi;
					m_fast += fastStep;
					if(m_fast > TwoPi) m_fast -= TwoPi;
					m_chorus += chorusStep;
					if(m_chorus >= 1.0) m_chorus -= 1.0;

					_outL[i] = in[0] + mix * (out[0] - in[0]);
					_outR[i] = in[1] + mix * (out[1] - in[1]);
				}
			}

		private:
			double m_rate = 48000.0;
			std::array<Line, 5> m_lines;                       // three for the strings, two for the chorus
			std::array<std::array<float, 2>, 4> m_filters{};   // before the lines, and after each string line
			float m_filterK = 0.5f;
			float m_smoothK = 0.001f;
			float m_depth = -1.0f;
			float m_strings = -1.0f, m_chorusOn = -1.0f;
			double m_slow = 0.0, m_fast = 0.0, m_chorus = 0.0;
		};

		// ── Delay ───────────────────────────────────────────────────────────────

		// A tape echo. What goes on the tape, the input and what comes back, passes the tone
		// filter, a gentle low cut and the soft saturation of tape, so every repeat is a
		// little darker and rounder than the one before and loud repeats compress instead of
		// clipping. The time glides when it changes, as a tape speeds up or slows down; the
		// flutter wobbles it. In ping-pong the input, both sides together, goes to the left
		// tape, whose echo goes to the right one, whose echo comes back to the left.
		// Controls: time (mono) or time L and time R, then regen, tone, flutter, mix.
		class Echo final : public OwnEffect
		{
		public:
			explicit Echo(const EchoMode _mode) : m_mode(_mode) {}

			void prepare(const double _rate) override
			{
				m_rate = _rate;
				for(auto& l : m_lines)
					l.resize(static_cast<size_t>(_rate * (EchoLongest + 0.05)) + 8);
				m_glide = static_cast<float>(1.0 - std::exp(-1.0 / (0.05 * _rate)));
				m_lowCutK = onePole(60.0, _rate);
				clear();
			}

			void clear() override
			{
				for(auto& l : m_lines) l.clear();
				m_tone.fill(0.0f);
				m_low.fill(0.0f);
				m_time.fill(-1.0);
				m_phase = 0.0;
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, const double _timeL, const double _timeR) override
			{
				const double right = m_mode == EchoMode::Mono ? _timeL : _timeR;
				const std::array<double, 2> target = {std::clamp(_timeL, EchoShortest, EchoLongest) * m_rate, std::clamp(right, EchoShortest, EchoLongest) * m_rate};
				for(int s = 0; s < 2; ++s)
					if(m_time[s] < 0.0)
						m_time[s] = target[s];
				// the stereo delay has its second time first: the controls after it are the same
				const int k = m_mode == EchoMode::Mono ? 0 : 1;
				const float regen = 0.98f * _v[1 + k];
				const float toneK = onePole(800.0 * std::pow(22.0, static_cast<double>(_v[2 + k])), m_rate);
				const double flutter = static_cast<double>(_v[3 + k]) * m_rate / 1000.0;   // up to 1.2 ms of wow
				const float dry = dryShare(_v[4 + k]), wet = wetShare(_v[4 + k]);

				for(int i = 0; i < _samples; ++i)
				{
					for(int s = 0; s < 2; ++s)
						m_time[s] += (target[s] - m_time[s]) * m_glide;
					const double wobble = flutter * (1.2 * std::sin(m_phase * 0.7) + 0.12 * std::sin(m_phase * 6.3 + 1.0));
					m_phase += TwoPi / m_rate;
					if(m_phase > TwoPi * 100.0)
						m_phase -= TwoPi * 100.0;

					const float l = m_lines[0].read(m_time[0] + wobble);
					const float r = m_lines[1].read(m_time[1] - wobble);
					std::array<float, 2> onTape{};
					switch(m_mode)
					{
					case EchoMode::PingPong:
						onTape[0] = 0.5f * (_inL[i] + _inR[i]) + regen * r;
						onTape[1] = l;
						break;
					default:
						onTape[0] = _inL[i] + regen * l;
						onTape[1] = _inR[i] + regen * r;
						break;
					}
					for(int s = 0; s < 2; ++s)
					{
						m_tone[s] += toneK * (onTape[s] - m_tone[s]);
						m_low[s] += m_lowCutK * (m_tone[s] - m_low[s]);
						m_lines[s].write(std::tanh(m_tone[s] - m_low[s]));
					}
					_outL[i] = dry * _inL[i] + wet * l;
					_outR[i] = dry * _inR[i] + wet * r;
				}
			}

		private:
			EchoMode m_mode;
			double m_rate = 48000.0;
			std::array<Line, 2> m_lines;
			std::array<float, 2> m_tone{}, m_low{};
			std::array<double, 2> m_time{-1.0, -1.0};
			float m_glide = 0.001f;
			float m_lowCutK = 0.01f;
			double m_phase = 0.0;
		};

		// ── Reverb ──────────────────────────────────────────────────────────────

		// The reverberation of the hall and the room: a predelay with early reflections taken
		// from it, allpass diffusers that blur the attack, and eight delay lines feeding each
		// other through a Householder matrix. Each line has a lowpass so that the highs die
		// first and a low cut, and is moved slowly so that the tail does not ring. Left and
		// right are taken from different lines.
		class Fdn
		{
		public:
			static constexpr int Lines = 8;

			struct Layout
			{
				std::array<double, Lines> lineMs;
				std::vector<double> diffuseMs;
				double maxSize;
				double maxPredelay;   // seconds
				double maxModMs;
			};

			struct Params
			{
				double rt60 = 2.0;
				double size = 1.0;
				double cutoff = 8000.0;
				double lowCut = 0.0;
				double predelay = 0.0;     // seconds
				float diffusion = 0.7f;    // of the diffusers in front, 0-1
				double modMs = 0.3;
				float early = 0.0f;        // level of the early reflections
				float late = 1.0f;         // and of the tail
				float dry = 1.0f, wet = 0.5f;
			};

			void prepare(const double _rate, Layout _layout)
			{
				m_rate = _rate;
				m_layout = std::move(_layout);
				const double perMs = _rate / 1000.0;
				for(int i = 0; i < Lines; ++i)
					m_lines[i].resize(static_cast<size_t>((m_layout.lineMs[i] * m_layout.maxSize + m_layout.maxModMs) * perMs) + 16);
				m_diffusers.resize(m_layout.diffuseMs.size());
				for(size_t d = 0; d < m_diffusers.size(); ++d)
					m_diffusers[d].resize(m_layout.diffuseMs[d] * perMs);
				m_predelay.resize(static_cast<size_t>((m_layout.maxPredelay + 0.04) * _rate) + 16);
				m_smooth = static_cast<float>(1.0 - std::exp(-1.0 / (0.08 * _rate)));
				clear();
			}

			void clear()
			{
				for(auto& l : m_lines) l.clear();
				for(auto& d : m_diffusers) d.line.clear();
				m_predelay.clear();
				m_damp.fill(0.0f);
				m_low.fill(0.0f);
				m_length.fill(-1.0);
				m_phase = 0.0;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const Params& _p)
			{
				const double perMs = m_rate / 1000.0;
				const float dampK = onePole(_p.cutoff, m_rate);
				const float lowK = _p.lowCut > 0.0 ? onePole(_p.lowCut, m_rate) : 0.0f;
				const double predelay = std::clamp(_p.predelay, 0.0, m_layout.maxPredelay) * m_rate + 1.0;
				const float diffusion = 0.75f * _p.diffusion;
				const double modDepth = std::min(_p.modMs, m_layout.maxModMs) * perMs;

				// each line loses 60 dB over the reverberation time, in proportion to its length
				std::array<double, Lines> target{};
				std::array<float, Lines> gain{};
				for(int i = 0; i < Lines; ++i)
				{
					target[i] = m_layout.lineMs[i] * std::min(_p.size, m_layout.maxSize) * perMs;
					if(m_length[i] < 0.0)
						m_length[i] = target[i];
					gain[i] = static_cast<float>(std::pow(10.0, -3.0 * (m_length[i] + modDepth * 0.5) / (_p.rt60 * m_rate)));
				}

				for(int n = 0; n < _samples; ++n)
				{
					const float in = 0.5f * (_inL[n] + _inR[n]);
					m_predelay.write(in);
					float x = m_predelay.read(predelay);

					// early reflections, taps after the predelay, a different set each side
					float earlyL = 0.0f, earlyR = 0.0f;
					if(_p.early > 0.0f)
					{
						for(int t = 0; t < 6; ++t)
						{
							earlyL += EarlyGain[t] * m_predelay.read(predelay + EarlyMsL[t] * perMs);
							earlyR += EarlyGain[t] * m_predelay.read(predelay + EarlyMsR[t] * perMs);
						}
						earlyL *= _p.early;
						earlyR *= _p.early;
					}

					if(diffusion > 0.01f)
						for(auto& d : m_diffusers)
							x = d.process(x, diffusion);

					std::array<float, Lines> out{};
					float sum = 0.0f;
					for(int i = 0; i < Lines; ++i)
					{
						m_length[i] += (target[i] - m_length[i]) * m_smooth;
						// two slow sines a line, so that the movement does not repeat soon
						const double m = 0.5 + 0.25 * (std::sin(m_phase * ModRate[i] + i) + std::sin(m_phase * ModRate[i] * 1.618 + 2.0 * i));
						float y = m_lines[i].read(m_length[i] + modDepth * m);
						m_damp[i] += dampK * (y - m_damp[i]);
						y = m_damp[i];
						if(lowK > 0.0f)
						{
							m_low[i] += lowK * (y - m_low[i]);
							y -= m_low[i];
						}
						out[i] = y;
						sum += y;
					}

					const float mixBack = sum * (2.0f / Lines);
					for(int i = 0; i < Lines; ++i)
						m_lines[i].write(x * InputSign[i] * 0.35f + gain[i] * (out[i] - mixBack));

					m_phase += TwoPi / m_rate;
					if(m_phase > TwoPi * 1000.0)
						m_phase -= TwoPi * 1000.0;

					const float l = earlyL + _p.late * 0.45f * (out[0] - out[2] + out[4] - out[6]);
					const float r = earlyR + _p.late * 0.45f * (out[1] - out[3] + out[5] - out[7]);
					_outL[n] = _p.dry * _inL[n] + _p.wet * l;
					_outR[n] = _p.dry * _inR[n] + _p.wet * r;
				}
			}

		private:
			static constexpr float InputSign[Lines] = {1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f};
			static constexpr double ModRate[Lines] = {0.31, 0.47, 0.67, 0.23, 0.53, 0.37, 0.79, 0.43};   // Hz
			static constexpr double EarlyMsL[6] = {4.3, 7.1, 11.9, 15.7, 21.1, 26.3};
			static constexpr double EarlyMsR[6] = {5.1, 8.9, 12.7, 17.3, 22.9, 28.1};
			static constexpr float EarlyGain[6] = {0.8f, -0.7f, 0.6f, -0.5f, 0.4f, -0.3f};

			double m_rate = 48000.0;
			Layout m_layout{};
			std::array<Line, Lines> m_lines;
			std::vector<Allpass> m_diffusers;
			Line m_predelay;
			std::array<float, Lines> m_damp{}, m_low{};
			std::array<double, Lines> m_length{};
			float m_smooth = 0.001f;
			double m_phase = 0.0;
		};

		// A concert hall. Controls: decay (0.3 to 12 s), size, tone, predelay (to 200 ms), mix.
		class Hall final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_fdn.prepare(_rate, {{29.7, 37.1, 41.1, 43.7, 53.0, 59.3, 67.9, 73.1}, {4.77, 3.59, 12.73, 9.30}, 2.0, 0.2, 0.6});
				played = false;
			}
			void clear() override { m_fdn.clear(); played = false; }
			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				Fdn::Params p;
				p.rt60 = 0.3 * std::pow(40.0, static_cast<double>(_v[0]));
				p.size = 0.4 + 1.6 * _v[1];
				p.cutoff = 1500.0 * std::pow(12.0, static_cast<double>(_v[2]));
				p.lowCut = 40.0;
				p.predelay = 0.2 * _v[3];
				p.diffusion = 0.93f;
				p.modMs = 0.6;
				p.early = 0.25f;
				p.dry = dryShare(_v[4]);
				p.wet = wetShare(_v[4]);
				m_fdn.process(_inL, _inR, _outL, _outR, _samples, p);
			}
		private:
			Fdn m_fdn;
		};

		// A small room with its walls close by: the early reflections carry it. Position
		// goes from near the source (strong reflections, the room behind) to the back of the
		// room (later, weaker reflections, more of the room).
		// Controls: decay (0.15 to 2.5 s), tone, position, mix.
		class Room final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_fdn.prepare(_rate, {{7.3, 9.1, 10.7, 12.3, 13.9, 15.7, 17.3, 19.1}, {2.3, 3.7}, 1.6, 0.03, 0.25});
				played = false;
			}
			void clear() override { m_fdn.clear(); played = false; }
			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				Fdn::Params p;
				p.rt60 = 0.15 * std::pow(16.7, static_cast<double>(_v[0]));
				p.size = 1.0 + 0.6 * _v[2];
				p.cutoff = 2000.0 * std::pow(7.0, static_cast<double>(_v[1]));
				p.lowCut = 80.0;
				p.predelay = 0.002 + 0.016 * _v[2];
				p.diffusion = 0.7f;
				p.modMs = 0.2;
				p.early = 0.9f - 0.5f * _v[2];
				p.late = 0.6f + 0.4f * _v[2];
				p.dry = dryShare(_v[3]);
				p.wet = wetShare(_v[3]);
				m_fdn.process(_inL, _inR, _outL, _outR, _samples, p);
			}
		private:
			Fdn m_fdn;
		};

		// The transposer of the shimmer: a phase vocoder. Frames of 2048 samples, a new one
		// every 512, are taken to the spectrum; there every partial is moved to its frequency
		// times the ratio of the pitch, its phase carried on from frame to frame, and the
		// frames are brought back and added up. A held sound comes out held, without the
		// flutter of grains: Shimmer-Reverb transposes with grains, which here fluttered at
		// the rate they started (some 40 times a second) and crackled as the feedback piled
		// them up. The tail of a reverberation loses nothing by the smearing of its attacks.
		// The pitch glides to a new setting, a step every block. One for both sides.
		class SpectralShift
		{
		public:
			// _quarter: which quarter of a hop the frames of this one fall on, so that two of them
			// (and their two sides) never work out a frame in the same block
			void prepare(const int _quarter)
			{
				m_quarter = _quarter;
				for(int i = 0; i < Size; ++i)
					m_window[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(TwoPi * i / Size));
				for(int i = 0; i < Size / 2; ++i)
				{
					m_twiddleRe[static_cast<size_t>(i)] = static_cast<float>(std::cos(-TwoPi * i / Size));
					m_twiddleIm[static_cast<size_t>(i)] = static_cast<float>(std::sin(-TwoPi * i / Size));
				}
				for(int i = 0, j = 0; i < Size; ++i)
				{
					m_reverse[static_cast<size_t>(i)] = j;
					int bit = Size >> 1;
					while(j & bit) { j ^= bit; bit >>= 1; }
					j |= bit;
				}
				clear();
			}

			void clear()
			{
				for(auto& c : m_ch)
				{
					c.in.fill(0.0f);
					c.out.fill(0.0f);
					c.prevRe.fill(0.0f);
					c.prevIm.fill(0.0f);
					c.sumPhase.fill(0.0f);
				}
				// the sides half a hop apart
				m_ch[0].fill = m_quarter * Hop / 4;
				m_ch[1].fill = m_ch[0].fill + Hop / 2;
				m_glided = m_semitones;
				m_ratio = std::pow(2.0, m_semitones / 12.0);
			}

			void setSemitones(const float _st) { m_semitones = _st; }

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _n)
			{
				m_glided = 0.995f * m_glided + 0.005f * m_semitones;
				m_ratio = 0.995 * m_ratio + 0.005 * std::pow(2.0, m_glided / 12.0);
				const float* in[2] = {_inL, _inR};
				float* out[2] = {_outL, _outR};
				for(int i = 0; i < _n; ++i)
				{
					// the frame runs Size samples behind: a sample goes in where one comes out
					for(int ch = 0; ch < 2; ++ch)
					{
						auto& c = m_ch[static_cast<size_t>(ch)];
						c.in[static_cast<size_t>(Size - Hop + c.fill)] = in[ch][i];
						out[ch][i] = c.out[static_cast<size_t>(c.fill)];
						if(++c.fill == Hop)
						{
							c.fill = 0;
							frame(c);
						}
					}
				}
			}

		private:
			static constexpr int Size = 2048;
			static constexpr int Hop = Size / 4;
			static constexpr int Bins = Size / 2 + 1;

			struct Channel
			{
				std::array<float, Size> in{}, out{};
				std::array<float, Bins> prevRe{}, prevIm{}, sumPhase{};
				int fill = 0;   // samples into the hop
			};

			void fft(float* _re, float* _im, const bool _inverse) const
			{
				for(int i = 0; i < Size; ++i)
				{
					const int j = m_reverse[static_cast<size_t>(i)];
					if(j > i)
					{
						std::swap(_re[i], _re[j]);
						std::swap(_im[i], _im[j]);
					}
				}
				for(int len = 2; len <= Size; len <<= 1)
				{
					const int half = len >> 1, step = Size / len;
					for(int s = 0; s < Size; s += len)
					{
						for(int k = 0; k < half; ++k)
						{
							const float wr = m_twiddleRe[static_cast<size_t>(k * step)];
							const float wi = _inverse ? -m_twiddleIm[static_cast<size_t>(k * step)] : m_twiddleIm[static_cast<size_t>(k * step)];
							const int a = s + k, b = a + half;
							const float tr = _re[b] * wr - _im[b] * wi;
							const float ti = _re[b] * wi + _im[b] * wr;
							_re[b] = _re[a] - tr;
							_im[b] = _im[a] - ti;
							_re[a] += tr;
							_im[a] += ti;
						}
					}
				}
			}

			void frame(Channel& _c)
			{
				constexpr float expected = static_cast<float>(TwoPi * Hop / Size);   // a bin's phase step a hop
				constexpr float twoPi = static_cast<float>(TwoPi);
				std::array<float, Size> re{}, im{};
				for(int i = 0; i < Size; ++i)
					re[static_cast<size_t>(i)] = _c.in[static_cast<size_t>(i)] * m_window[static_cast<size_t>(i)];
				fft(re.data(), im.data(), false);

				// the peaks of the spectrum, by their power
				std::array<float, Bins> power{};
				float loudest = 0.0f;
				for(int k = 0; k < Bins; ++k)
				{
					power[k] = re[k] * re[k] + im[k] * im[k];
					loudest = std::max(loudest, power[k]);
				}
				std::array<int, Bins> peaks{};
				int count = 0;
				for(int k = 1; k < Bins - 1; ++k)
					if(power[k] > power[k - 1] && power[k] >= power[k + 1] && power[k] > loudest * 1.0e-10f)
						peaks[count++] = k;

				// Moved by the ratio, each peak with the bins around it as one piece, so that the
				// shape of every partial stays whole: the bins of a piece keep their phases against
				// its peak (Laroche and Dolson's phase locking), and the peak's phase runs on at its
				// new frequency from where that bin had it a frame before. The true frequency of a
				// peak comes from how far its phase moved since the frame before; a piece is turned
				// by one rotation, so that only the peaks need an angle.
				const auto ratio = static_cast<float>(m_ratio);
				std::array<float, Size> outRe{}, outIm{};
				std::array<float, Bins> carried = _c.sumPhase;
				for(int i = 0; i < count; ++i)
				{
					const int p = peaks[i];
					const int lo = i == 0 ? 0 : (peaks[i - 1] + p + 1) / 2;
					const int hi = i == count - 1 ? Bins - 1 : (p + peaks[i + 1]) / 2;
					const float phase = std::atan2(im[p], re[p]);
					float moved = phase - std::atan2(_c.prevIm[p], _c.prevRe[p]) - p * expected;
					moved -= twoPi * std::round(moved / twoPi);
					const float target = (p + moved / expected) * ratio;
					const int q = static_cast<int>(std::lround(target));
					if(q < 0 || q >= Bins)
						continue;
					float peakPhase = _c.sumPhase[q] + target * expected;
					peakPhase -= twoPi * std::floor(peakPhase / twoPi);
					const float turn = peakPhase - phase;
					const float c = std::cos(turn), s = std::sin(turn);
					const int shift = q - p;
					for(int k = std::max(lo, -shift); k <= hi && k + shift < Bins; ++k)
					{
						const int o = k + shift;
						outRe[o] += re[k] * c - im[k] * s;
						outIm[o] += re[k] * s + im[k] * c;
						carried[o] = peakPhase;
					}
				}
				_c.sumPhase = carried;
				std::copy(re.begin(), re.begin() + Bins, _c.prevRe.begin());
				std::copy(im.begin(), im.begin() + Bins, _c.prevIm.begin());

				// back to sound
				for(int k = 1; k < Size / 2; ++k)
				{
					outRe[Size - k] = outRe[k];
					outIm[Size - k] = -outIm[k];
				}
				fft(outRe.data(), outIm.data(), true);

				// windowed again and added; Hann twice at a quarter hop adds up to 1.5
				constexpr float scale = 1.0f / (Size * 1.5f);
				std::copy(_c.out.begin() + Hop, _c.out.end(), _c.out.begin());
				std::fill(_c.out.end() - Hop, _c.out.end(), 0.0f);
				for(int i = 0; i < Size; ++i)
					_c.out[static_cast<size_t>(i)] += outRe[static_cast<size_t>(i)] * m_window[static_cast<size_t>(i)] * scale;
				std::copy(_c.in.begin() + Hop, _c.in.end(), _c.in.begin());
			}

			std::array<Channel, 2> m_ch;
			std::array<float, Size> m_window{};
			std::array<float, Size / 2> m_twiddleRe{}, m_twiddleIm{};
			std::array<int, Size> m_reverse{};
			int m_quarter = 0;
			float m_semitones = 12.0f, m_glided = 12.0f;
			double m_ratio = 2.0;
		};

		// A filter of two poles, as JUCE's IIR high and low pass with Q 0.707, for both sides
		struct Biquad
		{
			float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
			std::array<float, 2> z1{}, z2{};

			void set(const bool _high, const double _hz, const double _rate)
			{
				const double n = 1.0 / std::tan(Pi * std::min(_hz, _rate * 0.45) / _rate);
				const double n2 = n * n, q = 1.4142135623730951;   // 1 / Q
				const double c1 = 1.0 / (1.0 + q * n + n2);
				b0 = static_cast<float>(_high ? c1 * n2 : c1);
				b1 = static_cast<float>(_high ? -2.0 * c1 * n2 : 2.0 * c1);
				b2 = b0;
				a1 = static_cast<float>(c1 * 2.0 * (1.0 - n2));
				a2 = static_cast<float>(c1 * (1.0 - q * n + n2));
			}
			void clear() { z1.fill(0.0f); z2.fill(0.0f); }
			float process(const int _ch, const float _x)
			{
				const float y = b0 * _x + z1[_ch];
				z1[_ch] = b1 * _x - a1 * y + z2[_ch];
				z2[_ch] = b2 * _x - a2 * y;
				return y;
			}
		};

		// The shimmer of Shimmer-Reverb by Danial Kooshki (MIT licence, see fxZitaRev.h): the
		// zita-rev1 reverberator, whose tail is filtered (low cut, high cut), transposed in two
		// layers, A and B, each at its own level (by a phase vocoder here, by grains there),
		// softly limited and fed back into the reverberator with the feedback, block by block,
		// so that it climbs interval by interval. The dry sound is mixed outside the reverberator. Layers C and D of the
		// original, off there by default, are left out.
		// Controls: decay (1 to 8 s), low decay (1 to 8 s), tone (the damping of the highs,
		// 1.5 to 12 kHz), pitch A, level A, pitch B, level B (-24 to +24 semitones each),
		// feedback, low cut (50 to 600 Hz), high cut (3 to 12 kHz), mix.
		class Shimmer final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				if(!m_zita)
					m_zita = std::make_unique<zita::ZitaRev>();
				m_zita->init(static_cast<int>(_rate));
				m_zita->setWetDry(-1.0f);       // the reverberation alone
				m_zita->setOutputLevel(0.0f);
				m_layers[0].prepare(0);
				m_layers[1].prepare(1);
				clear();
			}

			void clear() override
			{
				if(m_zita)
					m_zita->instanceClear();
				for(auto& t : m_layers)
					t.clear();
				m_lowCut.clear();
				m_highCut.clear();
				m_backL.fill(0.0f);
				m_backR.fill(0.0f);
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				m_zita->setDecay(1.0f + 7.0f * _v[0]);
				m_zita->setLowRT60(1.0f + 7.0f * _v[1]);
				m_zita->setHFDamping(1500.0f + 10500.0f * _v[2]);
				m_layers[0].setSemitones(static_cast<float>(semitonesOf(_v[3])));
				m_layers[1].setSemitones(static_cast<float>(semitonesOf(_v[5])));
				const float gain[2] = {_v[4], _v[6]};
				const float feedback = FeedbackMost * _v[7];
				m_lowCut.set(true, 50.0 + 550.0 * _v[8], m_rate);
				m_highCut.set(false, 3000.0 + 9000.0 * _v[9], m_rate);
				const float dry = dryShare(_v[10]), wet = wetShare(_v[10]);

				// in blocks, as the original goes round once a block
				for(int done = 0; done < _samples; done += Block)
				{
					const int n = std::min(Block, _samples - done);
					std::array<float, Block> inL, inR, wetL, wetR, loopL, loopR, layL, layR;
					for(int i = 0; i < n; ++i)
					{
						inL[i] = _inL[done + i] + feedback * m_backL[i];
						inR[i] = _inR[done + i] + feedback * m_backR[i];
					}
					float* ins[2] = {inL.data(), inR.data()};
					float* outs[2] = {wetL.data(), wetR.data()};
					m_zita->compute(n, ins, outs);

					// the tail, low and high cut, through the two layers and back next block
					for(int i = 0; i < n; ++i)
					{
						loopL[i] = m_highCut.process(0, m_lowCut.process(0, wetL[i]));
						loopR[i] = m_highCut.process(1, m_lowCut.process(1, wetR[i]));
					}
					m_backL.fill(0.0f);
					m_backR.fill(0.0f);
					for(int k = 0; k < 2; ++k)
					{
						m_layers[k].process(loopL.data(), loopR.data(), layL.data(), layR.data(), n);
						for(int i = 0; i < n; ++i)
						{
							m_backL[i] += gain[k] * layL[i];
							m_backR[i] += gain[k] * layR[i];
						}
					}
					for(int i = 0; i < n; ++i)
					{
						m_backL[i] = std::tanh(m_backL[i] * Makeup * 0.85f * 1.5f);
						m_backR[i] = std::tanh(m_backR[i] * Makeup * 0.85f * 1.5f);
						_outL[done + i] = dry * _inL[done + i] + wet * wetL[i];
						_outR[done + i] = dry * _inR[done + i] + wet * wetR[i];
					}
				}
			}

		private:
			static constexpr int Block = 256;
			// The loop of the original gives back so little (the reverberator about a third, its
			// grains about a third, the feedback at most 0.35) that the shimmer stays 25 to 35 dB
			// under the sound: here the feedback reaches 0.7 and the layers are raised by 1.8, so
			// that it is heard, and still dies away at every setting.
			static constexpr float FeedbackMost = 0.7f;
			static constexpr float Makeup = 1.8f;
			double m_rate = 48000.0;
			std::unique_ptr<zita::ZitaRev> m_zita;   // large: its lines are inside it
			std::array<SpectralShift, 2> m_layers;
			Biquad m_lowCut, m_highCut;
			std::array<float, Block> m_backL{}, m_backR{};
		};

		// A plate: the figure-of-eight tank Jon Dattorro published in "Effect Design" (1997),
		// a bright, dense sound with no early echoes, as of a sheet of steel. Two modulated
		// allpasses keep the tail from ringing.
		// Controls: decay, tone, predelay (to 150 ms), mix.
		class Plate final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				const double k = _rate / 29761.0;
				m_scale = k;
				m_predelay.resize(static_cast<size_t>(0.16 * _rate) + 8);
				const double input[4] = {142, 107, 379, 277};
				for(int i = 0; i < 4; ++i)
					m_input[i].resize(input[i] * k);
				m_modL.resize(672 * k, 20 * k);
				m_modR.resize(908 * k, 20 * k);
				m_delayL1.resize(static_cast<size_t>(4453 * k) + 8);
				m_delayR1.resize(static_cast<size_t>(4217 * k) + 8);
				m_apL2.resize(1800 * k);
				m_apR2.resize(2656 * k);
				m_delayL2.resize(static_cast<size_t>(3720 * k) + 8);
				m_delayR2.resize(static_cast<size_t>(3163 * k) + 8);
				clear();
			}

			void clear() override
			{
				m_predelay.clear();
				for(auto& a : m_input) a.line.clear();
				for(auto* a : {&m_modL, &m_modR, &m_apL2, &m_apR2}) a->line.clear();
				for(auto* l : {&m_delayL1, &m_delayR1, &m_delayL2, &m_delayR2}) l->clear();
				m_bandwidth = m_dampL = m_dampR = m_endL = m_endR = 0.0f;
				m_phase = 0.0;
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const double k = m_scale;
				const float decay = 0.25f + 0.72f * _v[0];
				const float damping = 0.75f - 0.72f * _v[1];        // share the lowpass lets through is 1 - this
				const double predelay = 0.15 * _v[2] * m_rate + 1.0;
				const float dry = dryShare(_v[3]), wet = wetShare(_v[3]);
				const double excursion = 16.0 * k;
				const auto tap = [k](const Line& _l, const double _at) { return _l.read(_at * k); };

				for(int n = 0; n < _samples; ++n)
				{
					m_predelay.write(0.5f * (_inL[n] + _inR[n]));
					float x = m_predelay.read(predelay);
					m_bandwidth += 0.9995f * (x - m_bandwidth);
					x = m_bandwidth;
					x = m_input[0].process(x, 0.75f);
					x = m_input[1].process(x, 0.75f);
					x = m_input[2].process(x, 0.625f);
					x = m_input[3].process(x, 0.625f);

					const double modL = excursion * (1.0 + std::sin(m_phase));
					const double modR = excursion * (1.0 + std::cos(m_phase));
					m_phase += TwoPi * 1.0 / m_rate;
					if(m_phase > TwoPi)
						m_phase -= TwoPi;

					// left half of the tank
					float l = m_modL.process(x + decay * m_endR, -0.7f, modL);
					const float l1 = m_delayL1.read(4453 * k);
					m_delayL1.write(l);
					m_dampL += (1.0f - damping) * (l1 - m_dampL);
					l = m_apL2.process(m_dampL * decay, 0.5f);
					const float l2 = m_delayL2.read(3720 * k);
					m_delayL2.write(l);

					// right half
					float r = m_modR.process(x + decay * m_endL, -0.7f, modR);
					const float r1 = m_delayR1.read(4217 * k);
					m_delayR1.write(r);
					m_dampR += (1.0f - damping) * (r1 - m_dampR);
					r = m_apR2.process(m_dampR * decay, 0.5f);
					const float r2 = m_delayR2.read(3163 * k);
					m_delayR2.write(r);

					m_endL = l2;
					m_endR = r2;

					const float outL = 0.6f * (tap(m_delayR1, 266) + tap(m_delayR1, 2974) - tap(m_apR2.line, 1913) + tap(m_delayR2, 1996)
					                         - tap(m_delayL1, 1990) - tap(m_apL2.line, 187) - tap(m_delayL2, 1066));
					const float outR = 0.6f * (tap(m_delayL1, 353) + tap(m_delayL1, 3627) - tap(m_apL2.line, 1228) + tap(m_delayL2, 2673)
					                         - tap(m_delayR1, 2111) - tap(m_apR2.line, 335) - tap(m_delayR2, 121));
					_outL[n] = dry * _inL[n] + wet * outL;
					_outR[n] = dry * _inR[n] + wet * outR;
				}
			}

		private:
			double m_rate = 48000.0;
			double m_scale = 1.0;
			Line m_predelay;
			std::array<Allpass, 4> m_input;
			Allpass m_modL, m_modR, m_apL2, m_apR2;
			Line m_delayL1, m_delayR1, m_delayL2, m_delayR2;
			float m_bandwidth = 0.0f, m_dampL = 0.0f, m_dampR = 0.0f, m_endL = 0.0f, m_endR = 0.0f;
			double m_phase = 0.0;
		};

		// The spring reverb of Selector Spring Reverb by jennyrave-sys
		// (github.com/jennyrave-sys/SELECTOR_SPRING_REVERB_VST, GPL v3), its SpringReverbEngine
		// without JUCE, one engine a side as there. A triple-spring tank: the signal, driven
		// into a soft saturation, goes into three springs (delays of 29, 37 and 43 ms, the right
		// side 1.5 % longer) that feed back on themselves through a low-pass each and wobble
		// on slow LFOs, the right side in the other phase; their sum is dispersed by two
		// allpasses, shaped by a bass and a treble shelf and freed of DC. Decay sets the
		// feedback of the springs, Feedback adds more, past 90 % until they ring on by
		// themselves. Unlike the original, what goes into a spring passes a soft limit, so that
		// ringing on stays loud but bounded: there the loop could grow without end.
		// Controls: drive (0 to +24 dB), decay (1.2 to 4 s), feedback, bass, treble (-12 to
		// +12 dB each), mix.
		class Spring final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				for(int ch = 0; ch < 2; ++ch)
				{
					auto& e = m_engines[static_cast<size_t>(ch)];
					const double spread = ch == 1 ? 1.015 : 1.0;   // stereo width
					for(int s = 0; s < Springs; ++s)
					{
						e.spring[s].resize(static_cast<size_t>(_rate * 0.15));
						e.delay[s] = SpringSeconds[s] * spread * _rate;
						e.damp[s] = static_cast<float>(std::exp(-TwoPi * DampHz[s] / _rate));
						e.phaseStep[s] = TwoPi * LfoHz[s] / _rate;
					}
					e.ap[0].assign(static_cast<size_t>(_rate * 0.0050 * spread), 0.0f);
					e.ap[1].assign(static_cast<size_t>(_rate * 0.0017 * spread), 0.0f);
				}
				clear();
			}

			void clear() override
			{
				for(int ch = 0; ch < 2; ++ch)
				{
					auto& e = m_engines[static_cast<size_t>(ch)];
					const double phase = ch == 1 ? Pi : 0.0;
					for(int s = 0; s < Springs; ++s)
					{
						e.spring[s].clear();
						e.lp[s] = 0.0f;
						e.phase[s] = phase + 0.5 * s;
					}
					for(auto& a : e.ap)
						std::fill(a.begin(), a.end(), 0.0f);
					e.apPos.fill(0);
					e.bass.clear();
					e.treble.clear();
					e.dcIn = e.dcOut = 0.0f;
				}
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const float drive = static_cast<float>(std::pow(10.0, 24.0 * _v[0] / 20.0));
				// the feedback of the springs: decay 1.2 to 4 s maps to 0.80 to 0.98, the feedback adds up to 0.15
				const float decaySeconds = 1.2f + 2.8f * _v[1];
				const float feedback = std::min(1.05f, 0.80f + (decaySeconds - 1.2f) / 2.8f * 0.18f + 0.15f * _v[2]);
				const float dry = dryShare(_v[5]), wet = wetShare(_v[5]);
				const float* in[2] = {_inL, _inR};
				float* out[2] = {_outL, _outR};
				for(int ch = 0; ch < 2; ++ch)
				{
					auto& e = m_engines[static_cast<size_t>(ch)];
					e.bass.setShelf(false, 250.0, 24.0 * _v[3] - 12.0, m_rate);
					e.treble.setShelf(true, 4000.0, 24.0 * _v[4] - 12.0, m_rate);
					for(int i = 0; i < _samples; ++i)
					{
						const float x = in[ch][i];
						const float driven = std::tanh(x * drive);
						float sum = 0.0f;
						for(int s = 0; s < Springs; ++s)
						{
							const float wobble = static_cast<float>(std::sin(e.phase[s])) * LfoSamples[s];
							e.phase[s] += e.phaseStep[s];
							if(e.phase[s] >= TwoPi)
								e.phase[s] -= TwoPi;
							const float y = e.spring[s].read(std::max(1.0, e.delay[s] + wobble));
							e.lp[s] = y * (1.0f - e.damp[s]) + e.lp[s] * e.damp[s];
							e.spring[s].write(std::tanh(driven + e.lp[s] * feedback));
							sum += y;
						}
						float w = sum * 0.4f;
						for(int a = 0; a < 2; ++a)
						{
							auto& buf = e.ap[static_cast<size_t>(a)];
							auto& pos = e.apPos[static_cast<size_t>(a)];
							const float held = buf[static_cast<size_t>(pos)];
							buf[static_cast<size_t>(pos)] = w + held * 0.6f + 1.0e-18f;
							w = held - w;
							if(++pos >= static_cast<int>(buf.size()))
								pos = 0;
						}
						w = e.treble.process(e.bass.process(w));
						const float dc = w - e.dcIn + 0.995f * e.dcOut;
						e.dcIn = w;
						e.dcOut = dc;
						// the output clipper of the original
						w = dc > 1.5f ? 1.5f + std::tanh(dc - 1.5f) : dc < -1.5f ? -1.5f + std::tanh(dc + 1.5f) : dc;
						out[ch][i] = dry * x + wet * w;
					}
				}
			}

		private:
			// a shelf of the RBJ cookbook, Q 0.707, as the original's
			struct Shelf
			{
				float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
				float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

				void setShelf(const bool _high, const double _hz, const double _db, const double _rate)
				{
					const double a = std::pow(10.0, _db / 40.0);
					const double w0 = TwoPi * _hz / _rate, c = std::cos(w0), s = std::sin(w0);
					const double alpha = s / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / 0.707 - 1.0) + 2.0);
					const double beta = 2.0 * std::sqrt(a) * alpha;
					double nb0, nb1, nb2, na0, na1, na2;
					if(!_high)
					{
						nb0 = a * ((a + 1) - (a - 1) * c + beta); nb1 = 2 * a * ((a - 1) - (a + 1) * c); nb2 = a * ((a + 1) - (a - 1) * c - beta);
						na0 = (a + 1) + (a - 1) * c + beta;       na1 = -2 * ((a - 1) + (a + 1) * c);   na2 = (a + 1) + (a - 1) * c - beta;
					}
					else
					{
						nb0 = a * ((a + 1) + (a - 1) * c + beta); nb1 = -2 * a * ((a - 1) + (a + 1) * c); nb2 = a * ((a + 1) + (a - 1) * c - beta);
						na0 = (a + 1) - (a - 1) * c + beta;       na1 = 2 * ((a - 1) - (a + 1) * c);     na2 = (a + 1) - (a - 1) * c - beta;
					}
					b0 = static_cast<float>(nb0 / na0); b1 = static_cast<float>(nb1 / na0); b2 = static_cast<float>(nb2 / na0);
					a1 = static_cast<float>(na1 / na0); a2 = static_cast<float>(na2 / na0);
				}
				void clear() { x1 = x2 = y1 = y2 = 0.0f; }
				float process(const float _x)
				{
					const float y = b0 * _x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
					x2 = x1; x1 = _x; y2 = y1; y1 = y;
					return y;
				}
			};

			static constexpr int Springs = 3;
			static constexpr double SpringSeconds[Springs] = {0.029, 0.037, 0.043};
			static constexpr double DampHz[Springs] = {4500.0, 4100.0, 3800.0};
			static constexpr double LfoHz[Springs] = {1.5, 2.2, 0.8};
			static constexpr float LfoSamples[Springs] = {8.0f, 6.0f, 10.0f};

			struct Engine
			{
				std::array<Line, Springs> spring;
				std::array<double, Springs> delay{}, phase{}, phaseStep{};
				std::array<float, Springs> damp{}, lp{};
				std::array<std::vector<float>, 2> ap;
				std::array<int, 2> apPos{};
				Shelf bass, treble;
				float dcIn = 0.0f, dcOut = 0.0f;
			};
			double m_rate = 48000.0;
			std::array<Engine, 2> m_engines;
		};
	}

	std::unique_ptr<OwnEffect> createChorus() { return std::make_unique<Chorus>(); }
	std::unique_ptr<OwnEffect> createFlanger() { return std::make_unique<Flanger>(); }
	std::unique_ptr<OwnEffect> createPhaser() { return std::make_unique<Phaser>(); }
	std::unique_ptr<OwnEffect> createEnsemble() { return std::make_unique<Ensemble>(); }
	std::unique_ptr<OwnEffect> createEcho(const EchoMode _mode) { return std::make_unique<Echo>(_mode); }
	std::unique_ptr<OwnEffect> createHall() { return std::make_unique<Hall>(); }
	std::unique_ptr<OwnEffect> createRoom() { return std::make_unique<Room>(); }
	std::unique_ptr<OwnEffect> createPlate() { return std::make_unique<Plate>(); }
	std::unique_ptr<OwnEffect> createSpring() { return std::make_unique<Spring>(); }
	std::unique_ptr<OwnEffect> createShimmer() { return std::make_unique<Shimmer>(); }
}
