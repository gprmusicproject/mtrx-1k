#include "EditPanel.h"

#include "ParameterPool.h"
#include "matrixLib/patch.h"

#include <array>
#include <cmath>

namespace retromulator
{
    namespace
    {
        struct Style
        {
            juce::uint32 backgroundTop, backgroundBottom;
            juce::uint32 frame, frameOutline;
            juce::uint32 title;          // section names and their rule
            juce::uint32 label, value;
            juce::uint32 knobTrack, knobArc, knobFaceTop, knobFaceBottom;
            juce::uint32 boxFill, boxOutline;
        };

        // Shared by both finishes: the keys are black on either panel, and the graphs sit
        // in a window like the one of the display.
        constexpr juce::uint32 kKeyTop      = 0xff303236;
        constexpr juce::uint32 kKeyBottom   = 0xff101112;
        constexpr juce::uint32 kKeyText     = 0xffececec;
        constexpr juce::uint32 kLedOn       = 0xffff4a3d;
        constexpr juce::uint32 kLedOff      = 0xff4a0e0e;
        constexpr juce::uint32 kScreenTop   = 0xff1c0607;
        constexpr juce::uint32 kScreenBottom = 0xff0d0203;
        constexpr juce::uint32 kTrace       = 0xffff3324;

        const Style& styleFor(const rackPanel::Theme theme)
        {
            static const Style black
            {
                0xff1c1d20, 0xff121315,
                0xff0f1012, 0xff2b2d32,
                rackPanel::kLabelBlue,
                0xffb9bbc0, 0xfff0f0f0,
                0xff2f3136, rackPanel::kLabelBlue, 0xff34363b, 0xff1a1b1e,
                0xff0c0d0f, 0xff34373c
            };
            static const Style white
            {
                0xffe6e5e1, 0xffd6d5d0,
                0xffefeeea, 0xffb4b3ae,
                0xff141414,
                0xff2c2c2c, 0xff101010,
                0xffc2c1bc, 0xff1a1a1a, 0xfffafaf7, 0xffdcdbd6,
                0xfff8f7f3, 0xffa9a8a3
            };
            return theme == rackPanel::Theme::White ? white : black;
        }

        juce::Font sans(const float height, const bool bold = false)
        {
            return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), height,
                                                bold ? juce::Font::bold : juce::Font::plain));
        }

        const char* const kSources[matrixLib::patch::SourceCount] =
        {
            "None", "Env 1", "Env 2", "Env 3", "LFO 1", "LFO 2", "Vibrato", "Ramp 1", "Ramp 2", "Keyboard", "Portamento",
            "Tracking", "Key Gate", "Velocity", "Release Vel", "Pressure", "Pedal 1", "Pedal 2", "Lever 1", "Lever 2", "Lever 3"
        };

        const char* const kDestinations[matrixLib::patch::DestinationCount] =
        {
            "None", "DCO 1 Freq", "DCO 1 PW", "DCO 1 Shape", "DCO 2 Freq", "DCO 2 PW", "DCO 2 Shape", "Mix", "VCF FM", "VCF Freq",
            "VCF Reso", "VCA 1", "VCA 2", "Env 1 Delay", "Env 1 Attack", "Env 1 Decay", "Env 1 Release", "Env 1 Amp",
            "Env 2 Delay", "Env 2 Attack", "Env 2 Decay", "Env 2 Release", "Env 2 Amp", "Env 3 Delay", "Env 3 Attack",
            "Env 3 Decay", "Env 3 Release", "Env 3 Amp", "LFO 1 Speed", "LFO 1 Amp", "LFO 2 Speed", "LFO 2 Amp", "Porta Rate"
        };

        // The firmware's law for a modulation amount, measured on it value by value with the
        // amount in a loaded patch: the share of the full depth that a fixed modulation or a
        // matrix bus reaches at 0 to 63. It stays under 2.5 % up to 31, then about doubles
        // every five steps, and runs straight from 52 up.
        constexpr float kDepthLaw[64] =
        {
            0.0000f, 0.0000f, 0.0002f, 0.0005f, 0.0008f, 0.0010f, 0.0012f, 0.0017f,
            0.0022f, 0.0027f, 0.0032f, 0.0037f, 0.0045f, 0.0050f, 0.0057f, 0.0065f,
            0.0072f, 0.0087f, 0.0097f, 0.0107f, 0.0117f, 0.0124f, 0.0134f, 0.0144f,
            0.0154f, 0.0164f, 0.0174f, 0.0184f, 0.0194f, 0.0204f, 0.0214f, 0.0224f,
            0.0244f, 0.0281f, 0.0321f, 0.0371f, 0.0418f, 0.0478f, 0.0545f, 0.0625f,
            0.0712f, 0.0809f, 0.0916f, 0.1053f, 0.1200f, 0.1250f, 0.1404f, 0.1648f,
            0.1875f, 0.2079f, 0.2500f, 0.2694f, 0.3125f, 0.3750f, 0.4375f, 0.5000f,
            0.5625f, 0.6250f, 0.6875f, 0.7500f, 0.8125f, 0.8750f, 0.9375f, 1.0000f
        };
        // The same for an LFO on the pitch of a DCO: the swing from lowest to highest, in
        // octaves. The last three are taken from the audio, the timers cannot be read there.
        constexpr float kPitchLaw[64] =
        {
            0.0000f, 0.0000f, 0.0000f, 0.0007f, 0.0007f, 0.0013f, 0.0013f, 0.0020f,
            0.0026f, 0.0032f, 0.0040f, 0.0045f, 0.0052f, 0.0065f, 0.0072f, 0.0085f,
            0.0091f, 0.0104f, 0.0111f, 0.0124f, 0.0137f, 0.0150f, 0.0162f, 0.0175f,
            0.0189f, 0.0206f, 0.0222f, 0.0238f, 0.0254f, 0.0271f, 0.0287f, 0.0309f,
            0.0333f, 0.0364f, 0.0397f, 0.0427f, 0.0459f, 0.0499f, 0.0538f, 0.0580f,
            0.0661f, 0.0739f, 0.0818f, 0.1026f, 0.1234f, 0.1440f, 0.1647f, 0.1745f,
            0.1954f, 0.2263f, 0.2471f, 0.2672f, 0.3085f, 0.3391f, 0.3705f, 0.4320f,
            0.5041f, 0.5765f, 0.6687f, 0.7204f, 0.8332f, 0.9800f, 1.1500f, 1.3300f
        };
        // an envelope at full level on the filter: semitones at the full amount
        constexpr float kFilterFullDepth = 115.6f;

        // Travel of an amount control. Spread evenly, half of it would cover values that do
        // next to nothing, so the first eighth covers 0 to 30 and the rest covers 30 to 63.
        constexpr float kQuietTravel = 0.125f;
        constexpr int   kQuietAmount = 30;

        // -63..63 to -1..1
        float amountToTravel(const int amount)
        {
            const auto a = static_cast<float>(juce::jlimit(0, 63, std::abs(amount)));
            const float t = a <= kQuietAmount ? kQuietTravel * a / kQuietAmount
                                              : kQuietTravel + (1.0f - kQuietTravel) * (a - kQuietAmount) / (63 - kQuietAmount);
            return amount < 0 ? -t : t;
        }

        int travelToAmount(const float travel)
        {
            const float t = juce::jlimit(0.0f, 1.0f, std::abs(travel));
            const float a = t <= kQuietTravel ? t / kQuietTravel * kQuietAmount
                                              : kQuietAmount + (t - kQuietTravel) / (1.0f - kQuietTravel) * (63 - kQuietAmount);
            const int rounded = juce::roundToInt(a);
            return travel < 0.0f ? -rounded : rounded;
        }

        juce::String shareText(const float share)
        {
            const float percent = share * 100.0f;
            if(percent < 0.05f)
                return "0%";
            return percent < 10.0f ? juce::String(percent, 1) + "%" : juce::String(juce::roundToInt(percent)) + "%";
        }

        // What an amount does, for the readout next to the value: an LFO on a DCO as the
        // pitch swing each way, envelope 1 on the filter in semitones, the others as the
        // share of their full depth. number < 0 is a matrix bus.
        juce::String depthText(const int number, const int amount)
        {
            const int a = juce::jlimit(0, 63, std::abs(amount));
            if(a == 0)
                return {};
            if(number == 1 || number == 11)
            {
                const float cents = kPitchLaw[a] * 600.0f;
                const auto plusMinus = juce::String::fromUTF8("\xc2\xb1");
                if(cents < 0.5f)
                    return "<1c";
                return cents < 100.0f ? plusMinus + juce::String(juce::roundToInt(cents)) + "c"
                                      : plusMinus + juce::String(cents / 100.0f, 1) + "st";
            }
            if(number == 22)
            {
                const float semitones = kDepthLaw[a] * kFilterFullDepth;
                return semitones < 10.0f ? juce::String(semitones, 1) + "st" : juce::String(juce::roundToInt(semitones)) + "st";
            }
            return shareText(kDepthLaw[a]);
        }

        // Glide time of every portamento rate in milliseconds, measured on the firmware. Mode 0
        // glides at a constant speed (the time for an octave, three octaves take three times
        // as long), mode 1 in a constant time whatever the interval, modes 2 and 3 slow down
        // as they arrive (the time to come within a third of a semitone, over an octave).
        const float kGlideMs[3][64] =
        {
            { 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.9f, 0.9f, 0.9f,
              0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 4.7f, 4.7f, 5.0f,
              5.0f, 9.6f, 9.6f, 13.6f, 23.2f, 26.2f, 35.8f, 35.8f,
              35.8f, 39.6f, 41.1f, 44.3f, 46.3f, 48.3f, 52.0f, 52.6f,
              57.7f, 61.9f, 61.9f, 66.7f, 70.7f, 76.4f, 82.6f, 87.5f,
              93.3f, 102.0f, 110.0f, 119.0f, 128.0f, 137.0f, 147.0f, 159.0f,
              178.0f, 195.0f, 216.0f, 242.0f, 272.0f, 463.0f, 787.0f, 1579.0f,
              2660.0f, 3990.0f, 6669.0f, 7997.0f, 10007.0f, 13333.0f, 20061.0f, 40111.0f },
            { 2.6f, 8.2f, 8.2f, 14.9f, 14.9f, 14.9f, 14.9f, 14.9f,
              21.6f, 27.8f, 27.8f, 28.0f, 41.8f, 41.8f, 41.8f, 67.4f,
              81.2f, 100.0f, 112.0f, 170.0f, 230.0f, 294.0f, 376.0f, 395.0f,
              408.0f, 425.0f, 448.0f, 469.0f, 490.0f, 512.0f, 542.0f, 569.0f,
              604.0f, 638.0f, 675.0f, 716.0f, 758.0f, 807.0f, 858.0f, 916.0f,
              988.0f, 1056.0f, 1133.0f, 1222.0f, 1328.0f, 1429.0f, 1569.0f, 1705.0f,
              1873.0f, 2068.0f, 2306.0f, 2543.0f, 2834.0f, 4897.0f, 8256.0f, 16024.0f,
              26690.0f, 40149.0f, 60184.0f, 78603.0f, 80717.0f, 120670.0f, 120670.0f, 241341.0f },
            { 3.0f, 5.9f, 5.9f, 9.8f, 42.1f, 42.6f, 49.2f, 69.0f,
              80.1f, 104.0f, 107.0f, 130.0f, 142.0f, 171.0f, 209.0f, 267.0f,
              328.0f, 402.0f, 521.0f, 704.0f, 929.0f, 1247.0f, 1532.0f, 1561.0f,
              1676.0f, 1716.0f, 1768.0f, 1930.0f, 1960.0f, 2042.0f, 2246.0f, 2304.0f,
              2384.0f, 2479.0f, 2719.0f, 2811.0f, 2919.0f, 3031.0f, 3412.0f, 3564.0f,
              3774.0f, 3902.0f, 4524.0f, 4727.0f, 5026.0f, 5281.0f, 5522.0f, 7351.0f,
              7746.0f, 8256.0f, 9021.0f, 9979.0f, 11036.0f, 18447.0f, 30077.0f, 56472.0f,
              87572.0f, 137015.0f, 179510.0f, 286548.0f, 286548.0f, 286548.0f, 286882.0f, 286882.0f }
        };

        juce::String glideText(const int mode, const int rate)
        {
            const float ms = kGlideMs[juce::jlimit(0, 2, mode)][juce::jlimit(0, 63, rate)];
            if(ms < 10.0f)
                return "<10ms";
            if(ms < 1000.0f)
                return juce::String(juce::roundToInt(ms)) + "ms";
            if(ms < 10000.0f)
                return juce::String(ms / 1000.0f, 1) + "s";
            return ms < 100000.0f ? juce::String(juce::roundToInt(ms / 1000.0f)) + "s" : juce::String(juce::roundToInt(ms / 60000.0f)) + "min";
        }

        void showMenu(juce::Component& target, const juce::StringArray& items, const int current, std::function<void(int)> onPick)
        {
            juce::PopupMenu menu;
            for(int i = 0; i < items.size(); ++i)
                menu.addItem(i + 1, items[i], true, i == current);

            juce::Component::SafePointer<juce::Component> safe(&target);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
                [safe, onPick = std::move(onPick)](const int result)
                {
                    if(safe && result > 0)
                        onPick(result - 1);
                });
        }

        void drawBox(juce::Graphics& g, const Style& st, const juce::Rectangle<float> r, const juce::String& text, const bool arrow, const bool enabled)
        {
            g.setColour(juce::Colour(st.boxFill));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(juce::Colour(st.boxOutline));
            g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);

            auto textArea = r.reduced(4.0f, 0.0f);
            if(arrow)
            {
                const auto a = textArea.removeFromRight(7.0f);
                juce::Path p;
                p.addTriangle(a.getX(), a.getCentreY() - 2.0f, a.getRight(), a.getCentreY() - 2.0f, a.getCentreX(), a.getCentreY() + 2.5f);
                g.setColour(juce::Colour(st.knobArc).withAlpha(enabled ? 0.9f : 0.3f));
                g.fillPath(p);
            }
            g.setColour(juce::Colour(st.value).withAlpha(enabled ? 1.0f : 0.35f));
            g.setFont(sans(10.0f));
            g.drawFittedText(text, textArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }

        void drawScreen(juce::Graphics& g, const juce::Rectangle<float> r)
        {
            g.setColour(juce::Colour(0xff030303));
            g.fillRoundedRectangle(r, 3.0f);
            g.setGradientFill(juce::ColourGradient(juce::Colour(kScreenTop), 0.0f, r.getY(),
                                                   juce::Colour(kScreenBottom), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle(r.reduced(1.5f), 2.0f);
        }

        void drawLed(juce::Graphics& g, const float cx, const float cy, const bool lit)
        {
            constexpr float r = 2.6f;
            if(lit)
            {
                g.setGradientFill(juce::ColourGradient(juce::Colour(kLedOn).withAlpha(0.5f), cx, cy,
                                                       juce::Colour(kLedOn).withAlpha(0.0f), cx + 8.0f, cy, true));
                g.fillEllipse(cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
            }
            g.setColour(juce::Colours::black);
            g.fillEllipse(cx - r - 0.8f, cy - r - 0.8f, (r + 0.8f) * 2, (r + 0.8f) * 2);
            g.setColour(juce::Colour(lit ? kLedOn : kLedOff));
            g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
        }

        // Black key with a legend, as on the panel.
        class TextKey final : public juce::Button
        {
        public:
            explicit TextKey(const juce::String& legend) : juce::Button(legend)
            {
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }

        private:
            void paintButton(juce::Graphics& g, const bool highlighted, const bool down) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour(juce::Colour(0xff020202));
                g.fillRoundedRectangle(r, 3.5f);
                r = r.reduced(1.5f);
                if(down)
                    r = r.translated(0.0f, 0.8f);
                g.setGradientFill(juce::ColourGradient(juce::Colour(down ? 0xff161718 : (highlighted ? 0xff3a3c40 : kKeyTop)), 0.0f, r.getY(),
                                                       juce::Colour(down ? 0xff0a0a0b : kKeyBottom), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle(r, 2.5f);
                g.setColour(juce::Colour(kKeyText).withAlpha(isEnabled() ? 1.0f : 0.4f));
                g.setFont(sans(11.0f, true));
                g.drawText(getName(), r.toNearestInt(), juce::Justification::centred, false);
                // a key that stands for a choice carries a lamp
                if(getToggleState())
                    drawLed(g, r.getX() + 11.0f, r.getCentreY(), true);
            }
        };
    }

    struct EditPanel::Context
    {
        HeadlessProcessor& proc;
        const Style* style;

        ParameterPool* pool() const { return proc.getParameterPool(); }
    };

    class EditPanel::Control : public juce::Component
    {
    public:
        explicit Control(Context& context) : ctx(context) {}

        // Looks its parameters up again after the core changed.
        virtual void rebind() {}
        // Repaints when what it shows has moved.
        virtual void refresh() = 0;

    protected:
        const Style& style() const { return *ctx.style; }

        Context& ctx;
    };

    namespace
    {
        constexpr int kLabelHeight = 11;

        // A control on one patch parameter, through its host parameter slot.
        class ParamControl : public EditPanel::Control
        {
        public:
            ParamControl(EditPanel::Context& context, const int native, const juce::String& label)
                : Control(context), m_native(native), m_label(label)
            {
                setTitle(label);
            }

            void rebind() override
            {
                auto* pool = ctx.pool();
                m_param = pool ? pool->findNativeSlot(m_native) : nullptr;
                m_shown = -1000;
                repaint();
            }

            void refresh() override
            {
                const int v = usable() ? value() : -999;
                if(v != m_shown)
                {
                    m_shown = v;
                    repaint();
                }
            }

        protected:
            bool usable() const { return m_param && m_param->binding().desc; }
            int  value() const  { return m_param->getMidiValue(); }
            int  low() const    { return juce::roundToInt(m_param->binding().range.start); }
            int  high() const   { return juce::roundToInt(m_param->binding().range.end); }
            bool bipolar() const { return m_param->binding().desc->isBipolar; }
            // A modulation amount, -63 to +63 around the middle of the slot. The detune is
            // bipolar too but is a plain setting.
            bool isAmount() const { return bipolar() && m_native != 12 && high() - low() == 126; }
            int  centre() const { return (low() + high()) / 2; }
            juce::String text() const { return m_param->getText(m_param->getValue(), 0); }
            juce::String textFor(const int v) const
            {
                return m_param->getText(m_param->binding().range.convertTo0to1(static_cast<float>(v)), 0);
            }

            void set(const int v)
            {
                const int limited = juce::jlimit(low(), high(), v);
                if(limited != value())
                    m_param->setValueNotifyingHost(m_param->binding().range.convertTo0to1(static_cast<float>(limited)));
            }

            // one complete edit, for the host's undo and automation
            void setOnce(const int v)
            {
                m_param->beginChangeGesture();
                set(v);
                m_param->endChangeGesture();
            }

            void drawLabel(juce::Graphics& g, juce::Rectangle<int> area) const
            {
                g.setColour(juce::Colour(style().label).withAlpha(usable() ? 1.0f : 0.4f));
                g.setFont(sans(9.5f));
                g.drawFittedText(m_label, area, juce::Justification::centred, 1, 0.7f);
            }

            const int m_native;
            const juce::String m_label;
            SlotParameter* m_param = nullptr;
            int m_shown = -1000;
        };

        class Knob final : public ParamControl
        {
        public:
            using ParamControl::ParamControl;

            void refresh() override
            {
                // the glide time under the portamento rate follows the portamento mode as well
                if(m_native == kPortaRate)
                {
                    const int mode = portaMode();
                    if(mode != m_portaMode)
                    {
                        m_portaMode = mode;
                        repaint();
                    }
                }
                ParamControl::refresh();
            }

            void paint(juce::Graphics& g) override
            {
                const auto& st = style();
                auto b = getLocalBounds();
                drawLabel(g, b.removeFromTop(kLabelHeight));

                const auto area = b.toFloat();
                const float r  = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 2.0f;
                const float cx = area.getCentreX();
                const float cy = area.getCentreY() + 1.0f;
                constexpr float sweep = juce::MathConstants<float>::pi * 0.75f;

                juce::Path track;
                track.addCentredArc(cx, cy, r, r, 0.0f, -sweep, sweep, true);
                g.setColour(juce::Colour(st.knobTrack));
                g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const bool on = usable();
                const bool amount = on && isAmount();
                if(on)
                {
                    // an amount turns by its travel, which gives the values that matter more room
                    const float p = static_cast<float>(value() - low()) / static_cast<float>(juce::jmax(1, high() - low()));
                    const float angle = amount ? amountToTravel(value() - centre()) * sweep : -sweep + p * sweep * 2.0f;
                    const float from  = bipolar() ? 0.0f : -sweep;
                    if(std::abs(angle - from) > 0.01f)
                    {
                        juce::Path arc;
                        arc.addCentredArc(cx, cy, r, r, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
                        g.setColour(juce::Colour(st.knobArc));
                        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    }
                }

                const float face = r - 4.5f;
                g.setGradientFill(juce::ColourGradient(juce::Colour(st.knobFaceTop), cx, cy - face,
                                                       juce::Colour(st.knobFaceBottom), cx, cy + face, false));
                g.fillEllipse(cx - face, cy - face, face * 2, face * 2);

                // an amount shows what it does under its value, the portamento rate its glide time
                const auto depth = amount ? depthText(m_native, value() - centre())
                                 : on && m_native == kPortaRate ? glideText(portaMode(), value()) : juce::String();
                const float valueY = depth.isEmpty() ? cy - 7.0f : cy - 11.0f;
                g.setColour(juce::Colour(st.value).withAlpha(on ? 1.0f : 0.35f));
                g.setFont(sans(depth.isEmpty() ? 11.0f : 10.5f, true));
                g.drawText(on ? text() : juce::String("-"), juce::Rectangle<float>(cx - face, valueY, face * 2, 14.0f).toNearestInt(),
                           juce::Justification::centred, false);
                if(depth.isNotEmpty())
                {
                    g.setColour(juce::Colour(st.knobArc));
                    g.setFont(sans(8.5f));
                    g.drawFittedText(depth, juce::Rectangle<float>(cx - face + 1.0f, cy + 1.0f, face * 2 - 2.0f, 10.0f).toNearestInt(),
                                     juce::Justification::centred, 1, 0.75f);
                }
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                if(!usable())
                    return;
                m_dragStart = value();
                m_dragStartTravel = isAmount() ? amountToTravel(value() - centre()) : 0.0f;
                m_dragging = true;
                m_param->beginChangeGesture();
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                if(!m_dragging)
                    return;
                // the whole range over 130 pixels, a quarter of that speed with shift
                const auto moved = static_cast<float>(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
                const float pixels = e.mods.isShiftDown() ? 520.0f : 130.0f;
                if(isAmount())
                    set(centre() + travelToAmount(m_dragStartTravel + moved * 2.0f / pixels));
                else
                    set(m_dragStart + juce::roundToInt(moved * static_cast<float>(high() - low()) / pixels));
            }

            void mouseUp(const juce::MouseEvent&) override
            {
                if(m_dragging)
                    m_param->endChangeGesture();
                m_dragging = false;
            }

            void mouseDoubleClick(const juce::MouseEvent&) override
            {
                if(usable() && bipolar())
                    setOnce((low() + high()) / 2);
            }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if(usable() && wheel.deltaY != 0.0f)
                    setOnce(value() + (wheel.deltaY > 0.0f ? 1 : -1));
            }

        private:
            static constexpr int kPortaRate = 44;
            static constexpr int kPortaMode = 46;

            int portaMode() const
            {
                auto* pool = ctx.pool();
                auto* mode = pool ? pool->findNativeSlot(kPortaMode) : nullptr;
                return mode && mode->binding().desc ? mode->getMidiValue() : 0;
            }

            int   m_dragStart = 0;
            float m_dragStartTravel = 0.0f;
            bool  m_dragging = false;
            int   m_portaMode = -1;
        };

        class Choice final : public ParamControl
        {
        public:
            using ParamControl::ParamControl;

            void paint(juce::Graphics& g) override
            {
                auto b = getLocalBounds();
                drawLabel(g, b.removeFromTop(kLabelHeight));
                drawBox(g, style(), b.withTrimmedTop(2).toFloat(), usable() ? text() : juce::String("-"), true, usable());
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                if(!usable())
                    return;
                juce::StringArray items;
                for(int v = low(); v <= high(); ++v)
                    items.add(textFor(v));
                const int lo = low();
                showMenu(*this, items, value() - lo, [this, lo](const int picked)
                {
                    if(usable())
                        setOnce(lo + picked);
                });
            }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if(usable() && wheel.deltaY != 0.0f)
                    setOnce(value() + (wheel.deltaY > 0.0f ? -1 : 1));
            }
        };

        class Toggle final : public ParamControl
        {
        public:
            using ParamControl::ParamControl;

            void paint(juce::Graphics& g) override
            {
                auto b = getLocalBounds();
                drawLabel(g, b.removeFromTop(kLabelHeight));

                auto r = b.withTrimmedTop(2).toFloat().withSizeKeepingCentre(30.0f, 18.0f);
                g.setColour(juce::Colour(0xff020202));
                g.fillRoundedRectangle(r, 3.5f);
                r = r.reduced(1.5f);
                g.setGradientFill(juce::ColourGradient(juce::Colour(kKeyTop), 0.0f, r.getY(), juce::Colour(kKeyBottom), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle(r, 2.5f);
                drawLed(g, r.getCentreX(), r.getCentreY(), usable() && value() > low());
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                if(usable())
                    setOnce(value() > low() ? low() : high());
            }
        };

        // Curve drawn from several parameters, in a window like the display's.
        class Graph : public EditPanel::Control
        {
        public:
            Graph(EditPanel::Context& context, const std::array<int, 5>& natives) : Control(context), m_natives(natives)
            {
                setInterceptsMouseClicks(false, false);
            }

            void rebind() override
            {
                auto* pool = ctx.pool();
                for(size_t i = 0; i < m_params.size(); ++i)
                    m_params[i] = pool ? pool->findNativeSlot(m_natives[i]) : nullptr;
                m_shown.fill(-1);
                repaint();
            }

            void refresh() override
            {
                std::array<int, 5> now;
                for(size_t i = 0; i < m_params.size(); ++i)
                    now[i] = m_params[i] && m_params[i]->binding().desc ? m_params[i]->getMidiValue() : 0;
                if(now != m_shown)
                {
                    m_shown = now;
                    repaint();
                }
            }

            void paint(juce::Graphics& g) override
            {
                const auto r = getLocalBounds().toFloat();
                drawScreen(g, r);
                const auto path = curve(r.reduced(6.0f, 5.0f));
                g.setColour(juce::Colour(kTrace).withAlpha(0.22f));
                g.strokePath(path, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(juce::Colour(kTrace));
                g.strokePath(path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

        protected:
            virtual juce::Path curve(juce::Rectangle<float> area) const = 0;

            // value of parameter i as 0 to 1 of its range of 0 to 63
            float unit(const size_t i) const { return static_cast<float>(juce::jlimit(0, 63, m_shown[i])) / 63.0f; }

        private:
            const std::array<int, 5> m_natives;
            std::array<SlotParameter*, 5> m_params{};
            std::array<int, 5> m_shown{};
        };

        // delay, attack, decay, sustain, release
        class EnvelopeGraph final : public Graph
        {
        public:
            using Graph::Graph;

        private:
            juce::Path curve(const juce::Rectangle<float> a) const override
            {
                // four timed stages of up to a fifth of the width each, the held level takes the rest
                const float stage = a.getWidth() * 0.2f;
                const float x0 = a.getX();
                const float x1 = x0 + unit(0) * stage;
                const float x2 = x1 + juce::jmax(1.5f, unit(1) * stage);
                const float x3 = x2 + juce::jmax(1.5f, unit(2) * stage);
                const float x5 = a.getRight();
                const float x4 = x5 - juce::jmax(1.5f, unit(4) * stage);
                const float level = a.getBottom() - unit(3) * a.getHeight();

                juce::Path p;
                p.startNewSubPath(x0, a.getBottom());
                p.lineTo(x1, a.getBottom());
                p.lineTo(x2, a.getY());
                p.lineTo(x3, level);
                p.lineTo(juce::jmax(x3, x4), level);
                p.lineTo(x5, a.getBottom());
                return p;
            }
        };

        // the five points of the tracking generator
        class TrackingGraph final : public Graph
        {
        public:
            using Graph::Graph;

        private:
            juce::Path curve(const juce::Rectangle<float> a) const override
            {
                juce::Path p;
                for(size_t i = 0; i < 5; ++i)
                {
                    const float x = a.getX() + a.getWidth() * static_cast<float>(i) / 4.0f;
                    const float y = a.getBottom() - unit(i) * a.getHeight();
                    if(i == 0) p.startNewSubPath(x, y);
                    else       p.lineTo(x, y);
                }
                return p;
            }
        };

        // One bus of the modulation matrix: source, amount, destination.
        class MatrixRow final : public EditPanel::Control
        {
        public:
            MatrixRow(EditPanel::Context& context, const int bus) : Control(context), m_bus(bus)
            {
                setTitle("Modulation " + juce::String(bus + 1));
            }

            void resized() override
            {
                auto b = getLocalBounds().reduced(0, 3);
                m_number = b.removeFromLeft(16);
                m_source = b.removeFromLeft(90);
                b.removeFromLeft(4);
                m_amount = b.removeFromLeft(72);
                b.removeFromLeft(4);
                m_destination = b;
            }

            void refresh() override
            {
                const int now = usable() ? packed() : -1;
                if(now != m_shown)
                {
                    m_shown = now;
                    repaint();
                }
            }

            void paint(juce::Graphics& g) override
            {
                const auto& st = style();
                const bool on = usable();
                const auto bus = on ? get() : ParameterPool::MatrixBus{};

                g.setColour(juce::Colour(st.label).withAlpha(on ? 1.0f : 0.4f));
                g.setFont(sans(9.5f));
                g.drawText(juce::String(m_bus + 1), m_number, juce::Justification::centredLeft, false);

                drawBox(g, st, m_source.toFloat(), on ? juce::String(kSources[bus.source]) : juce::String("-"), true, on);
                drawBox(g, st, m_destination.toFloat(), on ? juce::String(kDestinations[bus.destination]) : juce::String("-"), true, on);

                // amount: a bar from the middle
                const auto r = m_amount.toFloat();
                g.setColour(juce::Colour(st.boxFill));
                g.fillRoundedRectangle(r, 3.0f);
                if(on && bus.amount != 0)
                {
                    // the bar follows the travel, like the knobs of the fixed modulations
                    const auto inner = r.reduced(2.0f);
                    const float half = inner.getWidth() * 0.5f * std::abs(amountToTravel(bus.amount));
                    const float x = bus.amount > 0 ? inner.getCentreX() : inner.getCentreX() - half;
                    g.setColour(juce::Colour(st.knobArc).withAlpha(0.55f));
                    g.fillRoundedRectangle(x, inner.getY(), half, inner.getHeight(), 1.5f);
                }
                g.setColour(juce::Colour(st.boxOutline));
                g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
                g.fillRect(r.getCentreX() - 0.5f, r.getY() + 2.0f, 1.0f, r.getHeight() - 4.0f);

                // the value on the left, the share of the full depth it gives on the right
                const auto depth = on ? depthText(-1, bus.amount) : juce::String();
                const auto valueArea = depth.isEmpty() ? m_amount : m_amount.withWidth(m_amount.getWidth() / 2);
                g.setColour(juce::Colour(st.value).withAlpha(on ? 1.0f : 0.35f));
                g.setFont(sans(10.5f, true));
                g.drawText(on ? (bus.amount > 0 ? "+" : "") + juce::String(static_cast<int>(bus.amount)) : juce::String("-"),
                           valueArea, juce::Justification::centred, false);
                if(depth.isNotEmpty())
                {
                    g.setFont(sans(9.0f));
                    g.drawFittedText(depth, m_amount.withTrimmedLeft(m_amount.getWidth() / 2).reduced(2, 0), juce::Justification::centred, 1, 0.75f);
                }
            }

            void mouseDown(const juce::MouseEvent& e) override
            {
                if(!usable())
                    return;
                m_dragging = false;
                if(m_amount.contains(e.getPosition()))
                {
                    m_dragging = true;
                    m_dragStartTravel = amountToTravel(get().amount);
                }
                else if(m_source.contains(e.getPosition()))
                {
                    showMenu(*this, juce::StringArray(kSources, matrixLib::patch::SourceCount), get().source, [this](const int picked)
                    {
                        auto b = get();
                        b.source = static_cast<uint8_t>(picked);
                        set(b);
                    });
                }
                else if(m_destination.contains(e.getPosition()))
                {
                    showMenu(*this, juce::StringArray(kDestinations, matrixLib::patch::DestinationCount), get().destination, [this](const int picked)
                    {
                        auto b = get();
                        b.destination = static_cast<uint8_t>(picked);
                        set(b);
                    });
                }
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                if(!m_dragging || !usable())
                    return;
                // from none to full over 90 pixels, a quarter of that speed with shift
                const auto moved = static_cast<float>(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
                setAmount(travelToAmount(m_dragStartTravel + moved / (e.mods.isShiftDown() ? 360.0f : 90.0f)));
            }

            void mouseUp(const juce::MouseEvent&) override { m_dragging = false; }

            void mouseDoubleClick(const juce::MouseEvent& e) override
            {
                if(usable() && m_amount.contains(e.getPosition()))
                    setAmount(0);
            }

            void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
            {
                if(usable() && wheel.deltaY != 0.0f && m_amount.contains(e.getPosition()))
                    setAmount(get().amount + (wheel.deltaY > 0.0f ? 1 : -1));
            }

        private:
            bool usable() const
            {
                const auto* pool = ctx.pool();
                return pool && pool->getCore() == SynthType::Matrix;
            }

            ParameterPool::MatrixBus get() const { return ctx.pool()->getMatrixBus(m_bus); }
            void set(const ParameterPool::MatrixBus& b)
            {
                if(usable())
                    ctx.pool()->setMatrixBus(m_bus, b);
                refresh();
            }

            void setAmount(const int amount)
            {
                auto b = get();
                const auto limited = static_cast<int8_t>(juce::jlimit(-63, 63, amount));
                if(limited == b.amount)
                    return;
                b.amount = limited;
                set(b);
            }

            int packed() const
            {
                const auto b = get();
                return b.source | (static_cast<uint8_t>(b.amount) << 8) | (b.destination << 16);
            }

            const int m_bus;
            juce::Rectangle<int> m_number, m_source, m_amount, m_destination;
            int  m_shown = -2;
            float m_dragStartTravel = 0.0f;
            bool m_dragging = false;
        };
    }

    // Where a save goes: over a position of a writable bank, the RAM bank first, or into a
    // new bank file. Covers the panel while it is open.
    class EditPanel::SaveOverlay final : public juce::Component
    {
    public:
        SaveOverlay(HeadlessProcessor& proc, std::function<void()> onSaved)
            : m_proc(proc), m_onSaved(std::move(onSaved))
        {
            const auto styleEditor = [](juce::TextEditor& e, const juce::String& title)
            {
                e.setFont(sans(15.0f, true));
                e.setJustification(juce::Justification::centredLeft);
                e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0c0d0f));
                e.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0f0f0));
                e.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff34373c));
                e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(rackPanel::kLabelBlue));
                e.setTitle(title);
            };

            // the characters a Matrix patch name can hold; lower case is stored as upper
            m_name.setInputRestrictions(static_cast<int>(matrixLib::patch::NameLength),
                " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_abcdefghijklmnopqrstuvwxyz");
            styleEditor(m_name, "Patch name");
            m_name.onReturnKey = [this] { save(); };
            m_name.onEscapeKey = [this] { setVisible(false); };
            addAndMakeVisible(m_name);

            m_bankName.setInputRestrictions(40);
            styleEditor(m_bankName, "Bank name");
            m_bankName.onReturnKey = [this] { save(); };
            m_bankName.onEscapeKey = [this] { setVisible(false); };
            addChildComponent(m_bankName);

            m_bank.setTitle("Save to bank");
            m_bank.onChange = [this] { fillPositions(-1); };
            addAndMakeVisible(m_bank);
            m_position.setTitle("Save to position");
            addAndMakeVisible(m_position);

            m_overwrite.onClick = [this] { setNewBank(false); };
            m_newBank.onClick   = [this] { setNewBank(true); };
            addAndMakeVisible(m_overwrite);
            addAndMakeVisible(m_newBank);

            m_save.onClick   = [this] { save(); };
            m_cancel.onClick = [this] { setVisible(false); };
            addAndMakeVisible(m_save);
            addAndMakeVisible(m_cancel);
        }

        void open(const juce::String& currentName)
        {
            m_error.clear();
            m_name.setText(currentName.trim().substring(0, static_cast<int>(matrixLib::patch::NameLength)).toUpperCase(), false);
            m_bankName.setText({}, false);

            // the bank the patch came from when it can be written, otherwise the RAM bank
            m_banks = HeadlessProcessor::getMatrixWritableBanks();
            const juce::File loaded(m_proc.getSysexFilePath());
            int current = -1;
            m_bank.clear(juce::dontSendNotification);
            for(int i = 0; i < static_cast<int>(m_banks.size()); ++i)
            {
                const juce::File file(m_banks[static_cast<size_t>(i)]);
                m_bank.addItem(i == 0 ? juce::String("RAM  (000-199)") : file.getFileNameWithoutExtension(), i + 1);
                if(file == loaded)
                    current = i;
            }
            m_bank.setSelectedId(juce::jmax(0, current) + 1, juce::dontSendNotification);
            fillPositions(current >= 0 ? m_proc.getCurrentProgram() : -1);

            setNewBank(false);
            setVisible(true);
            toFront(false);
            m_name.grabKeyboardFocus();
            m_name.selectAll();
        }

        void resized() override
        {
            m_box = getLocalBounds().withSizeKeepingCentre(560, 250);
            const int x = m_box.getX() + 22;
            int y = m_box.getY() + 50;

            m_overwrite.setBounds(x, y, 170, 24);
            m_newBank.setBounds(x + 178, y, 120, 24);
            y += 40;
            m_name.setBounds(x + kCaption, y, 150, 26);
            y += 36;
            m_bank.setBounds(x + kCaption, y, 250, 26);
            m_bankName.setBounds(x + kCaption, y, 250, 26);
            y += 36;
            m_position.setBounds(x + kCaption, y, 250, 26);

            m_cancel.setBounds(m_box.getRight() - 22 - 84, m_box.getBottom() - 18 - 24, 84, 24);
            m_save.setBounds(m_cancel.getX() - 8 - 84, m_cancel.getY(), 84, 24);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.62f));

            const auto box = m_box.toFloat();
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c1d20), 0.0f, box.getY(), juce::Colour(0xff111214), 0.0f, box.getBottom(), false));
            g.fillRoundedRectangle(box, 6.0f);
            g.setColour(juce::Colour(0xff3a3d43));
            g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);

            g.setColour(juce::Colour(rackPanel::kLabelBlue));
            g.fillRect(box.getX() + 22.0f, box.getY() + 14.0f, 150.0f, 3.5f);
            g.setFont(sans(13.0f, true));
            g.setColour(juce::Colour(0xfff0f0f0));
            g.drawText("Save patch", m_box.getX() + 22, m_box.getY() + 22, 300, 18, juce::Justification::centredLeft, false);

            g.setFont(sans(11.5f));
            g.setColour(juce::Colour(0xffb9bbc0));
            const int x = m_box.getX() + 22;
            g.drawText("Name", x, m_name.getY(), kCaption, 26, juce::Justification::centredLeft, false);
            g.drawText(m_toNewBank ? "Bank name" : "Bank", x, m_bank.getY(), kCaption, 26, juce::Justification::centredLeft, false);
            if(!m_toNewBank)
                g.drawText("Position", x, m_position.getY(), kCaption, 26, juce::Justification::centredLeft, false);

            g.setFont(sans(10.5f));
            g.drawText("8 characters", m_name.getRight() + 10, m_name.getY(), 120, 26, juce::Justification::centredLeft, false);
            if(m_toNewBank)
                g.drawText("A new bank file next to the others; the patch goes to its first position.",
                           x, m_position.getY(), m_box.getWidth() - 44, 26, juce::Justification::centredLeft, false);
            else
                g.drawText("Replaces the patch stored there.", m_position.getRight() + 10, m_position.getY(), 180, 26, juce::Justification::centredLeft, false);

            if(m_error.isNotEmpty())
            {
                g.setColour(juce::Colour(kLedOn));
                g.setFont(sans(11.0f));
                g.drawText(m_error, x, m_cancel.getY(), m_save.getX() - x - 10, 24, juce::Justification::centredLeft, false);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if(!m_box.contains(e.getPosition()))
                setVisible(false);
        }

    private:
        static constexpr int kCaption = 82;

        void setNewBank(const bool toNewBank)
        {
            m_toNewBank = toNewBank;
            m_error.clear();
            // the lamp on the key shows which way is chosen
            m_overwrite.setToggleState(!toNewBank, juce::dontSendNotification);
            m_newBank.setToggleState(toNewBank, juce::dontSendNotification);
            m_bank.setVisible(!toNewBank);
            m_position.setVisible(!toNewBank);
            m_bankName.setVisible(toNewBank);
            if(toNewBank && isShowing())
                m_bankName.grabKeyboardFocus();
            repaint();
        }

        // Positions of the chosen bank with their patch names. select < 0 picks the first
        // initialised one, so a save does not land on a sound by default.
        void fillPositions(const int select)
        {
            m_position.clear(juce::dontSendNotification);
            const int bank = m_bank.getSelectedId() - 1;
            if(bank < 0 || bank >= static_cast<int>(m_banks.size()))
                return;

            const auto names = HeadlessProcessor::getMatrixBankPatchNames(m_banks[static_cast<size_t>(bank)]);
            int firstFree = -1;
            for(int i = 0; i < static_cast<int>(names.size()); ++i)
            {
                const juce::String name(names[static_cast<size_t>(i)]);
                m_position.addItem(juce::String(i).paddedLeft('0', 3) + "  " + name, i + 1);
                if(firstFree < 0 && name.trim() == "INIT")
                    firstFree = i;
            }
            const int chosen = select >= 0 && select < static_cast<int>(names.size()) ? select : juce::jmax(0, firstFree);
            m_position.setSelectedId(chosen + 1, juce::dontSendNotification);
        }

        void save()
        {
            const auto name = m_name.getText().toStdString();
            if(m_toNewBank)
            {
                m_error = m_proc.createMatrixBank(m_bankName.getText().toStdString(), name);
            }
            else
            {
                const int bank = m_bank.getSelectedId() - 1;
                const int position = m_position.getSelectedId() - 1;
                if(bank < 0 || bank >= static_cast<int>(m_banks.size()) || position < 0)
                    return;
                m_error = m_proc.saveMatrixPatch(m_banks[static_cast<size_t>(bank)], position, name)
                    ? juce::String() : juce::String("The bank file could not be written.");
            }

            if(m_error.isNotEmpty())
            {
                repaint();
                return;
            }
            setVisible(false);
            if(m_onSaved)
                m_onSaved();
        }

        HeadlessProcessor& m_proc;
        std::function<void()> m_onSaved;
        juce::Rectangle<int> m_box;
        std::vector<std::string> m_banks;
        bool m_toNewBank = false;

        juce::TextEditor m_name, m_bankName;
        juce::ComboBox m_bank, m_position;
        TextKey m_overwrite { "Overwrite a position" };
        TextKey m_newBank { "New bank" };
        TextKey m_save { "Save" };
        TextKey m_cancel { "Cancel" };
        juce::String m_error;
    };

    // ── EditPanel ─────────────────────────────────────────────────────────────

    namespace
    {
        constexpr int kHeaderHeight = 36;
        constexpr int kSectionTop   = 42;
        constexpr int kColumnWidth  = 256;
        constexpr int kColumnGap    = 6;
        constexpr int kLeft         = 10;
        constexpr int kRightX       = kLeft + 3 * (kColumnWidth + kColumnGap);   // 796
        constexpr int kRightWidth   = rackPanel::kWidth - kLeft - kRightX;       // 294
        constexpr int kRowHeight    = 186;
        constexpr int kLastRowHeight = 128;
        constexpr int kSectionHead  = 24;
        constexpr int kKnobWidth    = 48;
        constexpr int kKnobHeight   = 54;
        constexpr int kKnobRow      = 56;
        constexpr int kChoiceHeight = 32;
        constexpr int kChoiceRow    = 38;
        constexpr int kToggleWidth  = 40;
    }

    EditPanel::EditPanel(HeadlessProcessor& proc)
        : m_proc(proc), m_context(new Context{proc, &styleFor(rackPanel::Theme::Black)})
    {
        addSections();

        auto save = std::make_unique<TextKey>("Save");
        save->onClick = [this] { showSave(); };
        m_saveBtn = std::move(save);
        addAndMakeVisible(*m_saveBtn);

        auto revert = std::make_unique<TextKey>("Revert");
        revert->onClick = [this] { this->revert(); };
        m_revertBtn = std::move(revert);
        addAndMakeVisible(*m_revertBtn);

        auto init = std::make_unique<TextKey>("Init");
        init->onClick = [this] { this->init(); };
        m_initBtn = std::move(init);
        addAndMakeVisible(*m_initBtn);

        m_saveOverlay = std::make_unique<SaveOverlay>(m_proc, [this]
        {
            if(onProgramChanged)
                onProgramChanged();
        });
        addChildComponent(*m_saveOverlay);

        setSize(rackPanel::kWidth, kHeight);
        refresh();
    }

    EditPanel::~EditPanel() = default;

    void EditPanel::addSections()
    {
        auto& ctx = *m_context;

        const auto section = [this](const juce::String& title, const int x, const int y, const int w, const int h)
        {
            m_sections.push_back({juce::Rectangle<int>(x, y, w, h), title});
            return juce::Point<int>(x + 8, y + kSectionHead);
        };
        const auto add = [this](std::unique_ptr<Control> c, const juce::Rectangle<int> bounds)
        {
            c->setBounds(bounds);
            addAndMakeVisible(*c);
            m_controls.push_back(std::move(c));
        };
        const auto knob = [&](const int native, const char* label, const int x, const int y)
        {
            add(std::make_unique<Knob>(ctx, native, label), {x, y, kKnobWidth, kKnobHeight});
        };
        const auto knobs = [&](const juce::Point<int> at, std::initializer_list<std::pair<int, const char*>> list)
        {
            int x = at.x;
            for(const auto& [native, label] : list)
            {
                knob(native, label, x, at.y);
                x += kKnobWidth;
            }
        };
        const auto choice = [&](const int native, const char* label, const int x, const int y, const int w)
        {
            add(std::make_unique<Choice>(ctx, native, label), {x, y, w, kChoiceHeight});
        };
        // choices side by side with a gap of 4, returns the x after the last one
        const auto choices = [&](const juce::Point<int> at, std::initializer_list<std::tuple<int, const char*, int>> list)
        {
            int x = at.x - 2;
            for(const auto& [native, label, w] : list)
            {
                choice(native, label, x, at.y, w);
                x += w + 4;
            }
            return x;
        };
        const auto toggle = [&](const int native, const char* label, const int x, const int y)
        {
            add(std::make_unique<Toggle>(ctx, native, label), {x, y, kToggleWidth, kChoiceHeight});
        };

        const int col[3] = {kLeft, kLeft + kColumnWidth + kColumnGap, kLeft + 2 * (kColumnWidth + kColumnGap)};
        const int rowA = kSectionTop;
        const int rowB = rowA + kRowHeight + kColumnGap;
        const int rowC = rowB + kRowHeight + kColumnGap;

        // ── oscillators and filter ──
        {
            const auto at = section("DCO 1", col[0], rowA, kColumnWidth, kRowHeight);
            knobs(at, {{0, "Freq"}, {1, "< LFO 1"}, {3, "PW"}, {4, "< LFO 2"}, {5, "Shape"}});
            choices(at.translated(0, kKnobRow), {{6, "Wave", 78}, {7, "Levers", 78}, {2, "Sync", 76}});
            toggle(8, "Porta", at.x, at.y + kKnobRow + kChoiceRow);
            toggle(9, "Click", at.x + kToggleWidth + 8, at.y + kKnobRow + kChoiceRow);
        }
        {
            const auto at = section("DCO 2", col[1], rowA, kColumnWidth, kRowHeight);
            knobs(at, {{10, "Freq"}, {11, "< LFO 1"}, {13, "PW"}, {14, "< LFO 2"}, {15, "Shape"}});
            choices(at.translated(0, kKnobRow), {{16, "Wave", 86}, {17, "Levers", 66}, {18, "Keyb / Porta", 80}});
            const int y = at.y + kKnobRow + kChoiceRow;
            knobs({at.x, y}, {{12, "Detune"}, {20, "Mix"}});
            toggle(19, "Click", at.x + 2 * kKnobWidth + 8, y + 10);
        }
        {
            const auto at = section("FILTER / AMPLIFIER", col[2], rowA, kColumnWidth, kRowHeight);
            knobs(at, {{21, "Freq"}, {22, "< Env 1"}, {23, "< Press"}, {24, "Reso"}, {30, "FM"}});
            choices(at.translated(0, kKnobRow), {{25, "Levers", 78}, {26, "Keyb / Porta", 86}});
            knobs({at.x, at.y + kKnobRow + kChoiceRow}, {{31, "FM<Env 3"}, {32, "FM<Press"}, {27, "VCA 1"}, {28, "VCA1<Vel"}, {29, "VCA2<Env2"}});
        }

        // ── envelopes ──
        static const char* const envNames[3] = {"ENV 1  (FILTER)", "ENV 2  (AMPLIFIER)", "ENV 3  (FM)"};
        for(int e = 0; e < 3; ++e)
        {
            const int n = 50 + e * 10;
            const auto at = section(envNames[e], col[e], rowB, kColumnWidth, kRowHeight);
            knobs(at, {{n, "Delay"}, {n + 1, "Attack"}, {n + 2, "Decay"}, {n + 3, "Sustain"}, {n + 4, "Release"}});
            choices(at.translated(0, kKnobRow), {{n + 7, "Trigger", 100}, {n + 8, "Mode", 72}, {n + 9, "LFO Trig", 64}});
            const int y = at.y + kKnobRow + kChoiceRow;
            knobs({at.x, y}, {{n + 5, "Amp"}, {n + 6, "< Velocity"}});
            add(std::make_unique<EnvelopeGraph>(ctx, std::array<int, 5>{n, n + 1, n + 2, n + 3, n + 4}),
                {at.x + 2 * kKnobWidth + 8, y + 4, kColumnWidth - 16 - 2 * kKnobWidth - 8, 48});
        }

        // ── modulation matrix ──
        {
            const auto at = section("MATRIX MODULATION", kRightX, rowA, kRightWidth, 2 * kRowHeight + kColumnGap);
            for(int bus = 0; bus < ParameterPool::MatrixBusCount; ++bus)
                add(std::make_unique<MatrixRow>(ctx, bus), {at.x - 2, at.y + 18 + bus * 33, kRightWidth - 12, 30});
        }

        // ── LFOs, tracking, ramps ──
        for(int l = 0; l < 2; ++l)
        {
            const int n = 80 + l * 10;
            const auto at = section(l == 0 ? "LFO 1" : "LFO 2", col[l], rowC, kColumnWidth, kLastRowHeight);
            knobs(at, {{n, "Speed"}, {n + 1, l == 0 ? "< Press" : "< Keyb"}, {n + 4, "Amp"}, {n + 5, l == 0 ? "< Ramp 1" : "< Ramp 2"}, {n + 3, "Retrig"}});
            const int x = choices(at.translated(0, kKnobRow), {{n + 2, "Wave", 68}, {n + 6, "Trigger", 58}, {n + 8, "Sample", 68}});
            toggle(n + 7, "Lag", x - 2, at.y + kKnobRow);
        }
        {
            const auto at = section("TRACKING", col[2], rowC, kColumnWidth, kLastRowHeight);
            knobs(at, {{34, "Point 1"}, {35, "Point 2"}, {36, "Point 3"}, {37, "Point 4"}, {38, "Point 5"}});
            const int x = choices(at.translated(0, kKnobRow), {{33, "Input", 86}});
            add(std::make_unique<TrackingGraph>(ctx, std::array<int, 5>{34, 35, 36, 37, 38}),
                {x + 4, at.y + kKnobRow + 2, col[2] + kColumnWidth - 8 - (x + 4), kChoiceHeight - 2});
        }
        {
            const auto at = section("RAMP / PORTAMENTO / KEYBOARD", kRightX, rowC, kRightWidth, kLastRowHeight);
            knobs(at, {{40, "Ramp 1"}, {42, "Ramp 2"}, {44, "Porta"}, {45, "< Velocity"}});
            toggle(47, "Legato", at.x + 4 * kKnobWidth + 14, at.y + 10);
            choices(at.translated(0, kKnobRow), {{41, "Ramp 1 Trig", 66}, {43, "Ramp 2 Trig", 66}, {46, "Porta Mode", 70}, {48, "Keyboard", 68}});
        }
    }

    void EditPanel::setTheme(const rackPanel::Theme theme)
    {
        m_context->style = &styleFor(theme);
        repaint();
    }

    void EditPanel::setPatchTitle(const juce::String& title)
    {
        if(title == m_patchTitle)
            return;
        m_patchTitle = title;
        repaint(0, 0, getWidth(), kHeaderHeight);
    }

    void EditPanel::refresh()
    {
        const auto* pool = m_proc.getParameterPool();
        const auto core = pool ? pool->getCore() : SynthType::None;
        if(core != m_boundCore)
        {
            m_boundCore = core;
            for(auto& c : m_controls)
                c->rebind();
        }
        for(auto& c : m_controls)
            c->refresh();

        const bool usable = core == SynthType::Matrix && !m_proc.m_isBooting.load() && m_proc.getProgramCount() > 0;
        m_saveBtn->setEnabled(usable);
        m_revertBtn->setEnabled(usable);
        m_initBtn->setEnabled(usable);

        // with no edit left the program plays again, and the processor says so
        const bool edited = m_proc.hasMatrixPatchEdits();
        const bool initialised = m_proc.isMatrixInitPatch();
        if(edited != m_edited || initialised != m_initialised)
        {
            m_edited = edited;
            m_initialised = initialised;
            repaint(0, 0, getWidth(), kHeaderHeight);
        }
    }

    void EditPanel::showSave()
    {
        // the name is what follows the number in the title
        const auto name = m_patchTitle.fromFirstOccurrenceOf("  ", false, false);
        m_saveOverlay->open(m_initialised ? juce::String("INIT") : (name.isNotEmpty() ? name : juce::String(m_proc.getPatchName())));
    }

    void EditPanel::init()
    {
        if(!m_proc.initMatrixPatch())
            return;
        refresh();
        repaint(0, 0, getWidth(), kHeaderHeight);
    }

    void EditPanel::revert()
    {
        // loading the program again drops every edit
        m_proc.selectProgram(m_proc.getCurrentProgram());
        repaint(0, 0, getWidth(), kHeaderHeight);
        if(onProgramChanged)
            onProgramChanged();
    }

    void EditPanel::resized()
    {
        m_saveBtn->setBounds(getWidth() - kLeft - 84, 7, 84, 22);
        m_revertBtn->setBounds(m_saveBtn->getX() - 8 - 84, 7, 84, 22);
        m_initBtn->setBounds(m_revertBtn->getX() - 8 - 84, 7, 84, 22);
        m_saveOverlay->setBounds(getLocalBounds());
    }

    void EditPanel::paint(juce::Graphics& g)
    {
        const auto& st = *m_context->style;
        const auto bounds = getLocalBounds().toFloat();

        g.setGradientFill(juce::ColourGradient(juce::Colour(st.backgroundTop), 0.0f, 0.0f,
                                               juce::Colour(st.backgroundBottom), 0.0f, bounds.getBottom(), false));
        g.fillRect(bounds);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.fillRect(0.0f, 0.0f, bounds.getWidth(), 1.5f);

        // header: title, loaded patch, edited lamp
        g.setColour(juce::Colour(st.title));
        g.setFont(sans(12.5f, true));
        g.drawText("PATCH EDIT", kLeft + 4, 7, 100, 22, juce::Justification::centredLeft, false);

        const juce::Rectangle<float> screen(118.0f, 6.0f, 240.0f, 24.0f);
        drawScreen(g, screen);
        g.setColour(juce::Colour(kTrace));
        g.setFont(sans(13.5f, true));
        g.drawText(m_initialised ? juce::String("INIT   (not saved)") : m_patchTitle,
                   screen.reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, false);

        drawLed(g, 378.0f, 18.0f, m_edited);
        g.setColour(juce::Colour(st.label));
        g.setFont(sans(9.5f));
        g.drawText("Edited", 388, 7, 60, 22, juce::Justification::centredLeft, false);

        for(const auto& s : m_sections)
        {
            const auto r = s.bounds.toFloat();
            g.setColour(juce::Colour(st.frame));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(juce::Colour(st.frameOutline));
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);

            g.setColour(juce::Colour(st.title));
            g.setFont(sans(10.5f, true));
            g.drawText(s.title, s.bounds.getX() + 10, s.bounds.getY() + 5, s.bounds.getWidth() - 20, 13, juce::Justification::centredLeft, false);
            g.setColour(juce::Colour(st.title).withAlpha(0.55f));
            g.fillRect(r.getX() + 10.0f, r.getY() + 19.5f, r.getWidth() - 20.0f, 1.2f);
        }

        // column heads of the modulation matrix
        g.setColour(juce::Colour(st.label));
        g.setFont(sans(9.0f));
        const int y = kSectionTop + kSectionHead + 4;
        g.drawText("Source",      kRightX + 22,  y, 90, 11, juce::Justification::centred, false);
        g.drawText("Amount",      kRightX + 116, y, 72, 11, juce::Justification::centred, false);
        g.drawText("Destination", kRightX + 192, y, 96, 11, juce::Justification::centred, false);
    }
}
