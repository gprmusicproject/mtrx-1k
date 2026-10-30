#include "fxOwn.h"

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

		// The chorus of a Boss CE-2: one bucket-brigade line, 2 to 12 ms, swept by a rounded
		// triangle, its input and output darkened by the filters the line needs, mixed with
		// the dry sound at equal level (Mix in the middle). The pedal is mono; here the right
		// side sweeps the other way, which spreads the chorus.
		// Controls: rate, depth, mix.
		class Chorus final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				for(auto& l : m_lines)
					l.resize(static_cast<size_t>(_rate * 0.02) + 8);
				// two poles each side of the line, as the filters around a BBD
				m_filterK = onePole(7500.0, _rate);
				m_smoothK = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _rate)));
				clear();
			}

			void clear() override
			{
				for(auto& l : m_lines) l.clear();
				for(auto& f : m_filters) f.fill(0.0f);
				m_phase = 0.0;
				m_depth = -1.0f;
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				const double step = 0.15 * std::pow(50.0, static_cast<double>(_v[0])) / m_rate;   // 0.15 to 7.5 Hz, cycles a sample
				const float mix = _v[2];
				if(m_depth < 0.0f)
					m_depth = _v[1];
				const double perMs = m_rate / 1000.0;
				for(int i = 0; i < _samples; ++i)
				{
					m_depth += m_smoothK * (_v[1] - m_depth);
					// a triangle with its corners rounded, as the pedal's LFO comes out
					double tri = 4.0 * std::fabs(m_phase - 0.5) - 1.0;
					tri *= 1.5 - 0.5 * tri * tri;
					m_phase += step;
					if(m_phase >= 1.0)
						m_phase -= 1.0;

					const float in[2] = {_inL[i], _inR[i]};
					float out[2];
					for(int ch = 0; ch < 2; ++ch)
					{
						auto& f = m_filters[ch];
						f[0] += m_filterK * (in[ch] - f[0]);
						f[1] += m_filterK * (f[0] - f[1]);
						m_lines[ch].write(f[1]);
						const double ms = 7.0 + 5.0 * m_depth * (ch ? -tri : tri);
						f[2] += m_filterK * (m_lines[ch].read(ms * perMs) - f[2]);
						f[3] += m_filterK * (f[2] - f[3]);
						out[ch] = in[ch] + mix * (f[3] - in[ch]);
					}
					_outL[i] = out[0];
					_outR[i] = out[1];
				}
			}

		private:
			double m_rate = 48000.0;
			std::array<Line, 2> m_lines;
			std::array<std::array<float, 4>, 2> m_filters{};
			float m_filterK = 0.5f;
			float m_smoothK = 0.001f;
			float m_depth = -1.0f;
			double m_phase = 0.0;
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

		// Octave up for the shimmer: two taps that run through a window at twice the speed of
		// the writing, crossfaded so that each restarts while the other is at full level.
		struct OctaveUp
		{
			Line line;
			double window = 1.0;
			double phase = 0.0;

			void resize(const double _window) { window = _window; line.resize(static_cast<size_t>(_window) + 8); phase = 0.0; }
			void clear() { line.clear(); phase = 0.0; }
			float process(const float _x)
			{
				line.write(_x);
				phase += 1.0 / window;
				if(phase >= 1.0)
					phase -= 1.0;
				const double other = phase >= 0.5 ? phase - 0.5 : phase + 0.5;
				const auto tap = [&](const double _p)
				{
					const double w = std::sin(Pi * _p);
					return static_cast<float>(w * w) * line.read(1.0 + (1.0 - _p) * (window - 2.0));
				};
				return tap(phase) + tap(other);
			}
		};

		// The reverberation of the hall, the room and the cloud: a predelay with early
		// reflections taken from it, allpass diffusers that blur the attack, and eight delay
		// lines feeding each other through a Householder matrix. Each line has a lowpass so
		// that the highs die first, a low cut, may have a diffuser of its own inside the loop,
		// and is moved slowly so that the tail does not ring; the shimmer puts the tail back
		// an octave up. Left and right are taken from different lines.
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
				bool inner;           // diffusers inside the loop
				bool early;           // early reflections
				bool shimmer;
			};

			struct Params
			{
				double rt60 = 2.0;
				double size = 1.0;
				double cutoff = 8000.0;
				double lowCut = 0.0;
				double predelay = 0.0;     // seconds
				float diffusion = 0.7f;    // of the diffusers in front, 0-1
				float innerDiffusion = 0.0f;
				double modMs = 0.3;
				float shimmer = 0.0f;
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
				{
					m_lines[i].resize(static_cast<size_t>((m_layout.lineMs[i] * m_layout.maxSize + m_layout.maxModMs) * perMs) + 16);
					m_inner[i].resize(InnerMs[i] * perMs);
				}
				m_diffusers.resize(m_layout.diffuseMs.size());
				for(size_t d = 0; d < m_diffusers.size(); ++d)
					m_diffusers[d].resize(m_layout.diffuseMs[d] * perMs);
				m_predelay.resize(static_cast<size_t>((m_layout.maxPredelay + 0.04) * _rate) + 16);
				m_octave.resize(0.08 * _rate);
				m_smooth = static_cast<float>(1.0 - std::exp(-1.0 / (0.08 * _rate)));
				clear();
			}

			void clear()
			{
				for(auto& l : m_lines) l.clear();
				for(auto& a : m_inner) a.line.clear();
				for(auto& d : m_diffusers) d.line.clear();
				m_predelay.clear();
				m_octave.clear();
				m_damp.fill(0.0f);
				m_low.fill(0.0f);
				m_length.fill(-1.0);
				m_shimmerTone = 0.0f;
				m_phase = 0.0;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const Params& _p)
			{
				const double perMs = m_rate / 1000.0;
				const float dampK = onePole(_p.cutoff, m_rate);
				const float lowK = _p.lowCut > 0.0 ? onePole(_p.lowCut, m_rate) : 0.0f;
				const double predelay = std::clamp(_p.predelay, 0.0, m_layout.maxPredelay) * m_rate + 1.0;
				const float diffusion = 0.75f * _p.diffusion;
				const float innerG = 0.6f * _p.innerDiffusion;
				const double modDepth = std::min(_p.modMs, m_layout.maxModMs) * perMs;
				const float shimmer = m_layout.shimmer ? 0.55f * _p.shimmer : 0.0f;
				const float shimmerK = onePole(6000.0, m_rate);

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
					if(m_layout.early && _p.early > 0.0f)
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
						if(m_layout.inner && innerG > 0.01f)
							y = m_inner[i].process(y, innerG);
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

					// the tail an octave up, darkened, back into the lines
					float lift = 0.0f;
					if(shimmer > 0.0f)
					{
						m_shimmerTone += shimmerK * (m_octave.process(sum * 0.25f) - m_shimmerTone);
						lift = shimmer * m_shimmerTone;
					}

					const float mixBack = sum * (2.0f / Lines);
					for(int i = 0; i < Lines; ++i)
						m_lines[i].write((x + lift) * InputSign[i] * 0.35f + gain[i] * (out[i] - mixBack));

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
			static constexpr double InnerMs[Lines] = {3.1, 4.7, 5.3, 6.1, 7.3, 8.9, 9.7, 11.3};
			static constexpr float InputSign[Lines] = {1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f};
			static constexpr double ModRate[Lines] = {0.31, 0.47, 0.67, 0.23, 0.53, 0.37, 0.79, 0.43};   // Hz
			static constexpr double EarlyMsL[6] = {4.3, 7.1, 11.9, 15.7, 21.1, 26.3};
			static constexpr double EarlyMsR[6] = {5.1, 8.9, 12.7, 17.3, 22.9, 28.1};
			static constexpr float EarlyGain[6] = {0.8f, -0.7f, 0.6f, -0.5f, 0.4f, -0.3f};

			double m_rate = 48000.0;
			Layout m_layout{};
			std::array<Line, Lines> m_lines;
			std::array<Allpass, Lines> m_inner;
			std::vector<Allpass> m_diffusers;
			Line m_predelay;
			OctaveUp m_octave;
			std::array<float, Lines> m_damp{}, m_low{};
			std::array<double, Lines> m_length{};
			float m_shimmerTone = 0.0f;
			float m_smooth = 0.001f;
			double m_phase = 0.0;
		};

		// A concert hall. Controls: decay (0.3 to 12 s), size, tone, predelay (to 200 ms), mix.
		class Hall final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_fdn.prepare(_rate, {{29.7, 37.1, 41.1, 43.7, 53.0, 59.3, 67.9, 73.1}, {4.77, 3.59, 12.73, 9.30}, 2.0, 0.2, 0.6, false, true, false});
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
				m_fdn.prepare(_rate, {{7.3, 9.1, 10.7, 12.3, 13.9, 15.7, 17.3, 19.1}, {2.3, 3.7}, 1.6, 0.03, 0.25, false, true, false});
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

		// The cloud: an enormous, dense ambient reverberation in the manner of the Cloud
		// of a Strymon BigSky. Six diffusers in front and one inside every line of a long
		// tank: with diffusion low the attacks come through grainy, raised the sound blooms
		// into a smooth, soft cloud. Low End sets how much bass the cloud keeps, Tone the
		// highs from dark (left) through warm (middle) to open (right), Mod a slow wander of
		// the lines, Shimmer the tail fed back an octave up.
		// Controls: decay (1 to 40 s), diffusion, low end, tone, mod, shimmer, predelay (to
		// 300 ms), mix.
		class Cloud final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_fdn.prepare(_rate, {{67.3, 79.1, 89.7, 101.3, 113.9, 127.1, 139.7, 151.3}, {4.1, 5.9, 7.7, 11.3, 15.1, 19.7}, 1.0, 0.3, 2.5, true, false, true});
				played = false;
			}
			void clear() override { m_fdn.clear(); played = false; }
			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				Fdn::Params p;
				p.rt60 = std::pow(40.0, static_cast<double>(_v[0]));
				p.diffusion = _v[1];
				p.innerDiffusion = _v[1];
				p.lowCut = 500.0 * std::pow(25.0, -static_cast<double>(_v[2]));   // 500 Hz down to 20
				p.cutoff = 1000.0 * std::pow(18.0, static_cast<double>(_v[3]));   // 1 kHz up to 18
				p.modMs = 2.5 * _v[4];
				p.shimmer = _v[5];
				p.predelay = 0.3 * _v[6];
				p.dry = dryShare(_v[7]);
				p.wet = wetShare(_v[7]);
				m_fdn.process(_inL, _inR, _outL, _outR, _samples, p);
			}
		private:
			Fdn m_fdn;
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

		// The tube spring reverb of 1963, as the Boss FRV-1 recreates it: a driver valve
		// sends the signal into a long tank of three springs, a recovery valve brings it back,
		// and the mixer adds it to the dry sound, which stays at full level. Dwell is how hard
		// the driver hits the springs: louder, more saturated and longer. Each spring disperses
		// a little, so a transient comes back with the splash and drip of the tank but the
		// notes stay in tune; the tank passes neither deep bass nor the top.
		// Controls: dwell, tone, mixer.
		class Spring final : public OwnEffect
		{
		public:
			void prepare(const double _rate) override
			{
				m_rate = _rate;
				for(int s = 0; s < Springs; ++s)
					m_springs[s].line.resize(static_cast<size_t>(SpringMs[s] * _rate / 1000.0) + 8);
				m_lowCutK = onePole(280.0, _rate);
				m_dampK = onePole(6500.0, _rate);
				m_smoothK = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * _rate)));
				clear();
			}

			void clear() override
			{
				for(auto& s : m_springs)
				{
					s.line.clear();
					s.x1.fill(0.0f);
					s.y1.fill(0.0f);
					s.damp = s.low = 0.0f;
				}
				m_tone.fill(0.0f);
				m_smooth.fill(-1.0f);
				played = false;
			}

			void process(const float* _inL, const float* _inR, float* _outL, float* _outR, const int _samples, const float* _v, double, double) override
			{
				for(int k = 0; k < 3; ++k)
					if(m_smooth[k] < 0.0f)
						m_smooth[k] = _v[k];
				// the tank's own decay, longer the harder it is driven: 1.6 to 3.8 s
				const double rt60 = (1.6 + 2.2 * static_cast<double>(_v[0])) * LoopLoss;
				std::array<float, Springs> gain{};
				for(int s = 0; s < Springs; ++s)
					gain[s] = static_cast<float>(std::pow(10.0, -3.0 * (SpringMs[s] / 1000.0) / rt60));
				const float toneK = onePole(1200.0 * std::pow(10.0, static_cast<double>(_v[1])), m_rate);   // 1.2 to 12 kHz

				for(int n = 0; n < _samples; ++n)
				{
					for(int k = 0; k < 3; ++k)
						m_smooth[k] += m_smoothK * (_v[k] - m_smooth[k]);
					const float dwell = m_smooth[0];
					const float mixer = m_smooth[2];

					// the driver valve: more dwell, more level and more grit into the tank
					const float drive = 1.0f + 5.0f * dwell;
					const float send = std::tanh(0.5f * (_inL[n] + _inR[n]) * drive) * (0.25f + 0.5f * dwell);

					std::array<float, Springs> y{};
					for(int s = 0; s < Springs; ++s)
					{
						auto& sp = m_springs[s];
						float x = sp.line.read(SpringMs[s] * m_rate / 1000.0);
						for(int i = 0; i < Stages; ++i)
						{
							const float z = Dispersion * x + sp.x1[i] - Dispersion * sp.y1[i];
							sp.x1[i] = x;
							sp.y1[i] = z;
							x = z;
						}
						sp.damp += m_dampK * (x - sp.damp);
						sp.low += m_lowCutK * (sp.damp - sp.low);
						y[s] = sp.damp - sp.low;
						sp.line.write(send + gain[s] * y[s]);
					}

					// the recovery valve, then the tone of the return; the third spring between the sides
					const std::array<float, 2> back = {y[0] + 0.6f * y[2], y[1] + 0.6f * y[2]};
					float out[2];
					const float in[2] = {_inL[n], _inR[n]};
					for(int ch = 0; ch < 2; ++ch)
					{
						const float wet = std::tanh(1.5f * back[ch]) / 1.5f;
						m_tone[ch] += toneK * (wet - m_tone[ch]);
						out[ch] = in[ch] + 2.0f * mixer * m_tone[ch];
					}
					_outL[n] = out[0];
					_outR[n] = out[1];
				}
			}

		private:
			static constexpr int Springs = 3;
			static constexpr int Stages = 10;
			static constexpr float Dispersion = 0.45f;
			static constexpr double SpringMs[Springs] = {33.7, 38.1, 41.9};
			// the filters of the loop take their share of the decay
			static constexpr double LoopLoss = 1.5;
			struct SpringState
			{
				Line line;
				std::array<float, Stages> x1{}, y1{};
				float damp = 0.0f, low = 0.0f;
			};
			double m_rate = 48000.0;
			std::array<SpringState, Springs> m_springs;
			std::array<float, 2> m_tone{};
			std::array<float, 3> m_smooth{-1.0f, -1.0f, -1.0f};
			float m_lowCutK = 0.02f, m_dampK = 0.3f, m_smoothK = 0.001f;
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
	std::unique_ptr<OwnEffect> createCloud() { return std::make_unique<Cloud>(); }
}
