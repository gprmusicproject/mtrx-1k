#include "EditPanel.h"

#include "MidiLearn.h"
#include "ParameterPool.h"
#include "matrixLib/patch.h"
#include "matrixLib/device.h"

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
        constexpr juce::uint32 kLearn       = 0xffffb020;   // what belongs to the MIDI learn

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
        MidiLearn* learn() const { return proc.getMidiLearn(); }
    };

    class EditPanel::Control : public juce::Component
    {
    public:
        explicit Control(Context& context) : ctx(context) {}

        // Looks its parameters up again after the core changed.
        virtual void rebind() {}
        // Repaints when what it shows has moved.
        virtual void refresh() = 0;

        // A part of the control that a control of the player's keyboard can be assigned to:
        // the target as the MIDI learn names it, and where it is in the control.
        struct LearnTarget
        {
            int target;
            juce::Rectangle<int> area;
        };
        virtual void learnTargets(std::vector<LearnTarget>&) const {}

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

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                if(usable())
                    out.push_back({m_native, getLocalBounds()});
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
                for(int v = low(); v <= top(); ++v)
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
                    setOnce(juce::jlimit(low(), top(), value() + (wheel.deltaY > 0.0f ? -1 : 1)));
            }

        private:
            // The wave of DCO 2 is three bits, but with noise on the firmware switches pulse and
            // wave off: 5 to 7 sound as 4, and no factory patch uses them. The menu offers what
            // the instrument plays; a patch that carries 5 to 7 still shows Noise.
            static constexpr int kDco2Wave = 16;
            static constexpr int kNoise = 4;
            int top() const { return m_native == kDco2Wave ? juce::jmin(high(), kNoise) : high(); }
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

        // The voices: one Matrix-1000, or two or four of them chained. Not a patch parameter
        // but a setting of the instance, so it stays when the sound changes.
        class VoicesChoice final : public EditPanel::Control
        {
        public:
            explicit VoicesChoice(EditPanel::Context& context) : Control(context)
            {
                setTitle("Voices");
            }

            void refresh() override
            {
                const int v = ctx.proc.getMatrixVoices();
                if(v != m_shown)
                {
                    m_shown = v;
                    repaint();
                }
            }

            void paint(juce::Graphics& g) override
            {
                auto b = getLocalBounds();
                g.setColour(juce::Colour(style().label));
                g.setFont(sans(9.5f));
                g.drawFittedText("Voices", b.removeFromTop(kLabelHeight), juce::Justification::centred, 1, 0.7f);
                drawBox(g, style(), b.withTrimmedTop(2).toFloat(), juce::String(ctx.proc.getMatrixVoices()), true, true);
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                const int current = ctx.proc.getMatrixVoices();
                showMenu(*this, {"6", "12", "24"}, current >= 24 ? 2 : (current >= 12 ? 1 : 0), [this](const int picked)
                {
                    set(picked == 2 ? 24 : (picked == 1 ? 12 : 6));
                });
            }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if(wheel.deltaY == 0.0f)
                    return;
                const int current = ctx.proc.getMatrixVoices();
                if(wheel.deltaY > 0.0f)
                    set(current >= 24 ? 12 : 6);
                else
                    set(current <= 6 ? 12 : 24);
            }

        private:
            void set(const int voices)
            {
                ctx.proc.setMatrixVoices(voices);
                refresh();
            }

            int m_shown = -1;
        };

        // Mono or poly, with the keyboard mode: Unison mono is the instrument's, Unison poly
        // gives every note two voices, Reassign or Rotate mono plays one voice. Shows what
        // plays, which before a choice is what the instrument does with the keyboard mode.
        class MonoChoice final : public EditPanel::Control
        {
        public:
            explicit MonoChoice(EditPanel::Context& context) : Control(context)
            {
                setTitle("Mono / Poly");
            }

            void refresh() override
            {
                const int m = ctx.proc.isMatrixMono() ? 1 : 0;
                if(m != m_shown)
                {
                    m_shown = m;
                    repaint();
                }
            }

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                out.push_back({MidiLearn::kMonoTarget, getLocalBounds()});
            }

            void paint(juce::Graphics& g) override
            {
                auto b = getLocalBounds();
                g.setColour(juce::Colour(style().label));
                g.setFont(sans(9.5f));
                g.drawFittedText("Mono/Poly", b.removeFromTop(kLabelHeight), juce::Justification::centred, 1, 0.6f);
                drawBox(g, style(), b.withTrimmedTop(2).toFloat(), ctx.proc.isMatrixMono() ? "Mono" : "Poly", false, true);
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                showMenu(*this, {"Mono", "Poly"}, ctx.proc.isMatrixMono() ? 0 : 1, [this](const int picked)
                {
                    set(picked == 0);
                });
            }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if(wheel.deltaY != 0.0f)
                    set(wheel.deltaY > 0.0f);
            }

        private:
            void set(const bool mono)
            {
                ctx.proc.setMatrixMono(mono ? 1 : 0);
                refresh();
            }

            int m_shown = -1;
        };

        // A knob on a setting of the instance rather than a patch parameter: Spread and
        // Detune. Saved with the instance and with a patch saved from the editor; a control of
        // the player's keyboard can be assigned to it through its MIDI learn target.
        class SettingKnob final : public EditPanel::Control
        {
        public:
            struct Setting
            {
                juce::String name;
                int learnTarget;
                int maximum;
                std::function<int()> get;
                std::function<void(int)> set;
                std::function<juce::String(int)> valueText;   // in the middle of the knob
                std::function<juce::String(int)> depthText;   // the small line under it, may be empty
            };

            SettingKnob(EditPanel::Context& context, Setting setting) : Control(context), m_setting(std::move(setting))
            {
                setTitle(m_setting.name);
            }

            void refresh() override
            {
                const int v = m_setting.get();
                if(v != m_shown)
                {
                    m_shown = v;
                    repaint();
                }
            }

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                out.push_back({m_setting.learnTarget, getLocalBounds()});
            }

            void paint(juce::Graphics& g) override
            {
                const auto& st = style();
                auto b = getLocalBounds();
                g.setColour(juce::Colour(st.label));
                g.setFont(sans(9.5f));
                g.drawFittedText(m_setting.name, b.removeFromTop(kLabelHeight), juce::Justification::centred, 1, 0.7f);

                const auto area = b.toFloat();
                const float r  = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 2.0f;
                const float cx = area.getCentreX();
                const float cy = area.getCentreY() + 1.0f;
                constexpr float sweep = juce::MathConstants<float>::pi * 0.75f;

                juce::Path track;
                track.addCentredArc(cx, cy, r, r, 0.0f, -sweep, sweep, true);
                g.setColour(juce::Colour(st.knobTrack));
                g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const int value = m_setting.get();
                if(value > 0)
                {
                    juce::Path arc;
                    arc.addCentredArc(cx, cy, r, r, 0.0f, -sweep, -sweep + static_cast<float>(value) / static_cast<float>(m_setting.maximum) * sweep * 2.0f, true);
                    g.setColour(juce::Colour(st.knobArc));
                    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }

                const float face = r - 4.5f;
                g.setGradientFill(juce::ColourGradient(juce::Colour(st.knobFaceTop), cx, cy - face,
                                                       juce::Colour(st.knobFaceBottom), cx, cy + face, false));
                g.fillEllipse(cx - face, cy - face, face * 2, face * 2);

                const auto text = m_setting.valueText(value);
                const auto depth = m_setting.depthText ? m_setting.depthText(value) : juce::String();
                g.setColour(juce::Colour(st.value));
                g.setFont(sans(text.length() > 3 ? 9.5f : (depth.isEmpty() ? 11.0f : 10.5f), true));
                g.drawText(text, juce::Rectangle<float>(cx - face, depth.isEmpty() ? cy - 7.0f : cy - 11.0f, face * 2, 14.0f).toNearestInt(),
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
                m_dragStart = m_setting.get();
                m_dragging = true;
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                if(!m_dragging)
                    return;
                // the whole range over 130 pixels, a quarter of that speed with shift, as the other knobs
                const auto moved = static_cast<float>(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
                const float pixels = e.mods.isShiftDown() ? 520.0f : 130.0f;
                set(m_dragStart + juce::roundToInt(moved * static_cast<float>(m_setting.maximum) / pixels));
            }

            void mouseUp(const juce::MouseEvent&) override
            {
                m_dragging = false;
            }

            // back to 0, the instrument's own
            void mouseDoubleClick(const juce::MouseEvent&) override
            {
                set(0);
            }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if(wheel.deltaY != 0.0f)
                    set(m_setting.get() + (wheel.deltaY > 0.0f ? 1 : -1));
            }

        private:
            void set(const int value)
            {
                m_setting.set(juce::jlimit(0, m_setting.maximum, value));
                refresh();
            }

            Setting m_setting;
            int  m_shown = -1;
            int  m_dragStart = 0;
            bool m_dragging = false;
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

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                if(!usable())
                    return;
                const int first = MidiLearn::kBusTargetFirst + m_bus * 3;
                out.push_back({first + MidiLearn::BusSource, m_source});
                out.push_back({first + MidiLearn::BusAmount, m_amount});
                out.push_back({first + MidiLearn::BusDestination, m_destination});
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

    // Lies over the controls while controller assignments are learned. A click picks the
    // parameter that the next control to move on the player's keyboard is assigned to, a
    // right click clears what a parameter is assigned to. Every assigned parameter carries
    // the name of its controller.
    class EditPanel::LearnLayer final : public juce::Component
    {
    public:
        explicit LearnLayer(EditPanel& panel) : m_panel(panel)
        {
            setTitle("MIDI learn");
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void paint(juce::Graphics& g) override
        {
            const auto* learn = m_panel.m_proc.getMidiLearn();
            if(!learn)
                return;
            g.fillAll(juce::Colours::black.withAlpha(0.16f));

            const int armed = learn->getArmed();
            const juce::Colour mark(kLearn);
            forEachTarget([&](const int target, const juce::Rectangle<int> area)
            {
                const auto r = area.toFloat();
                const auto* m = learn->findForTarget(target);
                if(target == armed)
                {
                    g.setColour(mark.withAlpha(0.26f));
                    g.fillRoundedRectangle(r, 4.0f);
                    g.setColour(mark);
                    g.drawRoundedRectangle(r.reduced(1.0f), 4.0f, 2.0f);
                }
                else if(m)
                {
                    g.setColour(mark.withAlpha(0.6f));
                    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
                }
                if(m)
                {
                    // the controller, on a tag: under a knob, in the corner of anything flat
                    const auto text = MidiLearn::controllerName(*m, true);
                    g.setFont(sans(8.5f, true));
                    const float w = juce::jmin(r.getWidth(), juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) + 7.0f);
                    const auto tag = r.getHeight() >= 40.0f
                        ? juce::Rectangle<float>(r.getCentreX() - w * 0.5f, r.getBottom() - 11.0f, w, 11.0f)
                        : juce::Rectangle<float>(r.getRight() - w, r.getY(), w, 11.0f);
                    g.setColour(juce::Colour(0xff17120a));
                    g.fillRoundedRectangle(tag, 2.5f);
                    g.setColour(mark);
                    g.drawFittedText(text, tag.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                }
                return false;
            });
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            auto* learn = m_panel.m_proc.getMidiLearn();
            if(!learn)
                return;
            int hit = -1;
            forEachTarget([&](const int target, const juce::Rectangle<int> area)
            {
                if(!area.contains(e.getPosition()))
                    return false;
                hit = target;
                return true;
            });
            if(hit >= 0 && e.mods.isPopupMenu())
                learn->removeTarget(hit);
            else
                learn->arm(hit);
            m_panel.refresh();
        }

    private:
        // every assignable part of every control with its place on this layer, until fn says stop
        template<typename Fn> void forEachTarget(Fn&& fn) const
        {
            std::vector<Control::LearnTarget> targets;
            for(const auto& c : m_panel.pageControls())
            {
                targets.clear();
                c->learnTargets(targets);
                for(const auto& t : targets)
                    if(fn(t.target, t.area.translated(c->getX() - getX(), c->getY() - getY())))
                        return;
            }
        }

        EditPanel& m_panel;
    };

    // The profiles of controller assignments: choosing one, saving the assignments in use
    // under a name, deleting one, and the list of the assignments, where one can be removed
    // and the range of an NRPN control set. Covers the panel while it is open.
    class EditPanel::MidiOverlay final : public juce::Component, private juce::ListBoxModel
    {
    public:
        explicit MidiOverlay(HeadlessProcessor& proc) : m_proc(proc)
        {
            m_profile.setTitle("MIDI profile");
            m_profile.onChange = [this] { pickProfile(); };
            addAndMakeVisible(m_profile);

            m_name.setFont(sans(14.0f, true));
            m_name.setJustification(juce::Justification::centredLeft);
            m_name.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0c0d0f));
            m_name.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0f0f0));
            m_name.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff34373c));
            m_name.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(rackPanel::kLabelBlue));
            m_name.setTitle("Profile name");
            m_name.setInputRestrictions(40);
            m_name.onReturnKey = [this] { saveAs(); };
            m_name.onEscapeKey = [this] { setVisible(false); };
            addAndMakeVisible(m_name);

            m_save.onClick   = [this] { save(); };
            m_saveAs.onClick = [this] { saveAs(); };
            m_delete.onClick = [this] { remove(); };
            m_close.onClick  = [this] { setVisible(false); };
            m_clear.onClick  = [this] { clearAll(); };
            m_import.onClick = [this] { importFile(); };
            m_export.onClick = [this] { exportFile(); };
            for(auto* b : {&m_save, &m_saveAs, &m_delete, &m_close, &m_clear, &m_import, &m_export})
                addAndMakeVisible(*b);

            m_list.setModel(this);
            m_list.setRowHeight(kRow);
            m_list.setTitle("Assignments");
            m_list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff0c0d0f));
            m_list.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff34373c));
            m_list.setOutlineThickness(1);
            addAndMakeVisible(m_list);
        }

        void open()
        {
            m_error.clear();
            m_confirmDelete = m_confirmClear = false;
            if(const auto* learn = m_proc.getMidiLearn())
                m_name.setText(MidiLearn::isBuiltIn(learn->getProfileName()) ? juce::String() : learn->getProfileName(), false);
            sync();
            setVisible(true);
            toFront(false);
        }

        // Shows what the MIDI learn holds now.
        void sync()
        {
            const auto* learn = m_proc.getMidiLearn();
            if(!learn)
                return;
            const auto names = learn->getProfileNames();
            m_profile.clear(juce::dontSendNotification);
            for(int i = 0; i < names.size(); ++i)
                m_profile.addItem(names[i], i + 1);
            const auto& current = learn->getProfileName();
            if(learn->isModified())
                m_profile.setText(current + "   (changed, not saved)", juce::dontSendNotification);
            else
                m_profile.setSelectedId(names.indexOf(current, true) + 1, juce::dontSendNotification);

            const bool own = !MidiLearn::isBuiltIn(current);
            m_save.setEnabled(own && learn->isModified());
            m_delete.setEnabled(own);
            m_clear.setEnabled(!learn->getMappings().empty());
            m_export.setEnabled(!learn->getMappings().empty());
            m_list.updateContent();
            m_list.repaint();
            repaint();
        }

        void resized() override
        {
            m_box = getLocalBounds().withSizeKeepingCentre(660, 470);
            const int x = m_box.getX() + 22;
            int y = m_box.getY() + 50;

            m_profile.setBounds(x + kCaption, y, 300, 26);
            m_import.setBounds(m_profile.getRight() + 10, y + 1, 78, 24);
            m_export.setBounds(m_import.getRight() + 8, y + 1, 78, 24);
            y += 36;
            m_name.setBounds(x + kCaption, y, 220, 26);
            m_saveAs.setBounds(m_name.getRight() + 10, y + 1, 78, 24);
            m_save.setBounds(m_saveAs.getRight() + 8, y + 1, 64, 24);
            m_delete.setBounds(m_save.getRight() + 8, y + 1, 78, 24);
            y += 66;
            m_close.setBounds(m_box.getRight() - 22 - 84, m_box.getBottom() - 18 - 24, 84, 24);
            m_list.setBounds(x, y, m_box.getWidth() - 44, m_close.getY() - 60 - y);
            m_clear.setBounds(m_list.getRight() - 72, m_list.getY() - 28, 72, 22);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.62f));

            const auto box = m_box.toFloat();
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c1d20), 0.0f, box.getY(), juce::Colour(0xff111214), 0.0f, box.getBottom(), false));
            g.fillRoundedRectangle(box, 6.0f);
            g.setColour(juce::Colour(0xff3a3d43));
            g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);

            g.setColour(juce::Colour(kLearn));
            g.fillRect(box.getX() + 22.0f, box.getY() + 14.0f, 150.0f, 3.5f);
            g.setFont(sans(13.0f, true));
            g.setColour(juce::Colour(0xfff0f0f0));
            g.drawText("MIDI controller profiles", m_box.getX() + 22, m_box.getY() + 22, 300, 18, juce::Justification::centredLeft, false);

            const int x = m_box.getX() + 22;
            g.setFont(sans(11.5f));
            g.setColour(juce::Colour(0xffb9bbc0));
            g.drawText("Profile", x, m_profile.getY(), kCaption, 26, juce::Justification::centredLeft, false);
            g.drawText("Name", x, m_name.getY(), kCaption, 26, juce::Justification::centredLeft, false);

            // column heads of the list
            g.setFont(sans(10.0f));
            const int y = m_list.getY() - 15;
            g.drawText("Control", m_list.getX() + kColControl, y, 110, 13, juce::Justification::centredLeft, false);
            g.drawText("Parameter", m_list.getX() + kColTarget, y, 200, 13, juce::Justification::centredLeft, false);
            g.drawText("Values it sends", m_list.getX() + kColRange, y, 110, 13, juce::Justification::centredLeft, false);

            // what the assigned controls last sent and what it was taken for
            if(const auto* learn = m_proc.getMidiLearn())
            {
                g.setFont(sans(10.5f, true));
                g.setColour(juce::Colour(kLearn));
                g.drawText("Last received", x, m_list.getBottom() + 5, 86, 14, juce::Justification::centredLeft, false);
                g.setFont(sans(10.5f));
                g.setColour(juce::Colour(0xfff0f0f0));
                g.drawText(learn->getMonitorText(), x + 86, m_list.getBottom() + 5, m_box.getWidth() - 44 - 86, 14, juce::Justification::centredLeft, true);
            }

            g.setColour(juce::Colour(0xffb9bbc0));
            g.setFont(sans(10.5f));
            const auto notes = juce::Rectangle<int>(x, m_list.getBottom() + 25, m_box.getWidth() - 44, 28);
            g.drawFittedText("To assign: close this page, press Learn, click a parameter and move a control of your keyboard. "
                             "A right click on a parameter clears it. How far a control goes is learned from it: turn it fully once. A click on the values here makes it learn again.",
                             notes, juce::Justification::topLeft, 2, 0.9f);

            if(m_error.isNotEmpty())
            {
                g.setColour(juce::Colour(kLedOn));
                g.setFont(sans(11.0f));
                g.drawText(m_error, x, m_close.getY(), m_close.getX() - x - 10, 24, juce::Justification::centredLeft, false);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if(!m_box.contains(e.getPosition()))
                setVisible(false);
        }

    private:
        static constexpr int kCaption = 70;
        static constexpr int kRow = 22;
        static constexpr int kColControl = 10;
        static constexpr int kColTarget  = 130;
        static constexpr int kColRange   = 400;
        static constexpr int kRemoveWidth = 34;

        int getNumRows() override
        {
            const auto* learn = m_proc.getMidiLearn();
            return learn ? static_cast<int>(learn->getMappings().size()) : 0;
        }

        void paintListBoxItem(const int row, juce::Graphics& g, const int width, const int height, bool) override
        {
            const auto* learn = m_proc.getMidiLearn();
            if(!learn || row < 0 || row >= static_cast<int>(learn->getMappings().size()))
                return;
            const auto& m = learn->getMappings()[static_cast<size_t>(row)];
            if(row & 1)
                g.fillAll(juce::Colour(0xff131417));

            g.setFont(sans(11.5f, true));
            g.setColour(juce::Colour(kLearn));
            g.drawText(MidiLearn::controllerName(m), kColControl, 0, kColTarget - kColControl - 6, height, juce::Justification::centredLeft, false);
            g.setFont(sans(11.5f));
            g.setColour(juce::Colour(0xfff0f0f0));
            g.drawText(MidiLearn::targetName(m.target), kColTarget, 0, kColRange - kColTarget - 6, height, juce::Justification::centredLeft, false);
            // how far the control goes, as learned from it; nothing yet after a click here
            g.setColour(juce::Colour(m.max > 0 ? 0xfff0f0f0 : kLearn));
            g.drawText(m.max > 0 ? "0 - " + juce::String(static_cast<int>(m.max)) : juce::String("turn it fully"),
                       kColRange, 0, 110, height, juce::Justification::centredLeft, false);

            g.setColour(juce::Colour(0xffb9bbc0));
            g.setFont(sans(10.5f));
            g.drawText("Remove", width - 70, 0, 62, height, juce::Justification::centredRight, false);
        }

        void listBoxItemClicked(const int row, const juce::MouseEvent& e) override
        {
            auto* learn = m_proc.getMidiLearn();
            if(!learn || row < 0)
                return;
            m_error.clear();
            m_confirmDelete = m_confirmClear = false;
            if(e.x >= m_list.getVisibleContentWidth() - 70)
                learn->removeMapping(static_cast<size_t>(row));
            else if(e.x >= kColRange && e.x < kColRange + 110)
                learn->relearnRange(static_cast<size_t>(row));
            sync();
        }

        void pickProfile()
        {
            auto* learn = m_proc.getMidiLearn();
            const int index = m_profile.getSelectedItemIndex();
            if(!learn || index < 0)
                return;
            const auto name = m_profile.getItemText(index);
            m_confirmDelete = m_confirmClear = false;
            m_error = learn->selectProfile(name) ? juce::String() : juce::String("That profile could not be read.");
            if(m_error.isEmpty())
                m_name.setText(MidiLearn::isBuiltIn(name) ? juce::String() : name, false);
            sync();
        }

        void save()
        {
            auto* learn = m_proc.getMidiLearn();
            if(!learn)
                return;
            m_confirmDelete = m_confirmClear = false;
            m_error = learn->saveProfile(learn->getProfileName());
            sync();
        }

        void saveAs()
        {
            auto* learn = m_proc.getMidiLearn();
            if(!learn)
                return;
            m_confirmDelete = m_confirmClear = false;
            m_error = learn->saveProfile(m_name.getText());
            if(m_error.isEmpty())
                m_name.setText(learn->getProfileName(), false);
            sync();
        }

        // The assignments in the list go to a JSON file of the player's choice.
        void exportFile()
        {
            const auto* learn = m_proc.getMidiLearn();
            if(!learn || learn->getMappings().empty())
                return;
            m_error.clear();
            m_confirmDelete = m_confirmClear = false;
            repaint();

            const auto start = m_folder.getChildFile(juce::File::createLegalFileName(learn->getProfileName()) + ".json");
            m_chooser = std::make_unique<juce::FileChooser>("Export the MIDI profile", start, "*.json");
            juce::Component::SafePointer<MidiOverlay> safe(this);
            m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [safe](const juce::FileChooser& fc)
            {
                auto chosen = fc.getResult();
                auto* learn = safe ? safe->m_proc.getMidiLearn() : nullptr;
                if(!learn || chosen == juce::File())
                    return;
                if(!chosen.hasFileExtension("json"))
                    chosen = chosen.withFileExtension("json");
                safe->m_folder = chosen.getParentDirectory();
                safe->m_error = learn->exportProfile(chosen);
                if(safe->m_error.isEmpty())
                    safe->m_error = "Exported to " + chosen.getFileName() + ".";
                safe->sync();
            });
        }

        // A profile from a JSON file joins the saved ones and becomes the one in use.
        void importFile()
        {
            if(!m_proc.getMidiLearn())
                return;
            m_error.clear();
            m_confirmDelete = m_confirmClear = false;
            repaint();

            m_chooser = std::make_unique<juce::FileChooser>("Import a MIDI profile", m_folder, "*.json");
            juce::Component::SafePointer<MidiOverlay> safe(this);
            m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [safe](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                auto* learn = safe ? safe->m_proc.getMidiLearn() : nullptr;
                if(!learn || chosen == juce::File())
                    return;
                safe->m_folder = chosen.getParentDirectory();
                safe->m_error = learn->importProfile(chosen);
                if(safe->m_error.isEmpty())
                {
                    safe->m_name.setText(learn->getProfileName(), false);
                    safe->m_error = "Imported as \"" + learn->getProfileName() + "\".";
                }
                safe->sync();
            });
        }

        // Every assignment at once. The first press asks, the second clears; the profile on
        // file stays as it is until it is saved.
        void clearAll()
        {
            auto* learn = m_proc.getMidiLearn();
            if(!learn || learn->getMappings().empty())
                return;
            m_error.clear();
            m_confirmDelete = false;
            if(!m_confirmClear)
            {
                m_confirmClear = true;
                m_error = "Press Clear again to remove all " + juce::String(static_cast<int>(learn->getMappings().size())) + " assignments.";
            }
            else
            {
                m_confirmClear = false;
                learn->clearAll();
            }
            sync();
        }

        // the first press asks, the second deletes
        void remove()
        {
            auto* learn = m_proc.getMidiLearn();
            if(!learn || MidiLearn::isBuiltIn(learn->getProfileName()))
                return;
            m_error.clear();
            if(!m_confirmDelete)
            {
                m_confirmDelete = true;
                m_confirmClear = false;
                m_error = "Press Delete again to delete the profile \"" + learn->getProfileName() + "\".";
            }
            else
            {
                m_confirmDelete = m_confirmClear = false;
                if(!learn->deleteProfile(learn->getProfileName()))
                    m_error = "The profile could not be deleted.";
                else
                    m_name.setText({}, false);
            }
            sync();
        }

        HeadlessProcessor& m_proc;
        juce::Rectangle<int> m_box;
        juce::ComboBox m_profile;
        juce::TextEditor m_name;
        juce::ListBox m_list;
        TextKey m_saveAs { "Save as" };
        TextKey m_save { "Save" };
        TextKey m_delete { "Delete" };
        TextKey m_close { "Close" };
        TextKey m_clear { "Clear" };
        TextKey m_import { "Import" };
        TextKey m_export { "Export" };
        // where the last file was exported to or imported from
        juce::File m_folder { juce::File::getSpecialLocation(juce::File::userDocumentsDirectory) };
        std::unique_ptr<juce::FileChooser> m_chooser;
        juce::String m_error;
        bool m_confirmDelete = false;
        bool m_confirmClear = false;
    };

    // ── Effects page ──────────────────────────────────────────────────────────

    namespace
    {
        // Layout of the effects page: the chain on top, then the three racks in the order the
        // sound goes through them, each drawn as a rack unit with ears.
        constexpr int kFxLeft     = 10;
        constexpr int kFxWidth    = rackPanel::kWidth - 2 * kFxLeft;
        constexpr int kFxBarY     = 42;
        constexpr int kFxBarH     = 40;
        constexpr int kRackTop    = 90;
        constexpr int kRackHeight = 148;
        constexpr int kRackGap    = 8;
        constexpr int kRackEar    = 28;
        constexpr int kFxNameX    = kFxLeft + kRackEar + 16;
        constexpr int kFxTypeX    = kFxLeft + 132;
        constexpr int kFxTypeW    = 80;
        // room for the most controls a type has, ending where eight of 62 once did
        constexpr int kFxKnobW    = 58;
        constexpr int kFxKnobX    = kFxLeft + 1044 - fxLib::MaxControls * kFxKnobW;
        // in the bar of the chain: where the order of the racks starts, and its mix
        constexpr int kFxFlowX    = kFxLeft + 196;
        constexpr int kFxMixX     = kFxLeft + 610;
        // in a rack: the knobs from this height, their centres at the other; the keys of the
        // types and the switch of the rack are centred on the same line
        constexpr int kFxKnobTop    = 42;
        constexpr int kFxKnobCentre = 88;
        constexpr int kFxKeyHeight  = 26;
        constexpr int kFxKeyGap     = 8;

        // where the keys of the types start in a rack, in rows of three
        int typeKeysTop(const int rack)
        {
            const int rows = (fxLib::typeCount(rack) + 2) / 3;
            return kFxKnobCentre - (rows * kFxKeyHeight + (rows - 1) * kFxKeyGap) / 2;
        }
        constexpr int kFxKnobH    = 100;

        int rackY(const int rack) { return kRackTop + rack * (kRackHeight + kRackGap); }

        fxLib::Chain* chainOf(const EditPanel::Context& ctx) { return ctx.proc.getFxChain(); }

        // the tempo a synced delay follows, as the LFO sync has it
        double fxTempo(HeadlessProcessor& proc)
        {
            const auto* dev = proc.getMatrixDevice();
            return dev ? static_cast<double>(dev->getHostBpm()) : 120.0;
        }

        // A key with a lamp, the look of the keys of the header
        void drawLampKey(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& legend, const bool lit, const bool enabled)
        {
            g.setColour(juce::Colour(0xff020202));
            g.fillRoundedRectangle(r, 3.5f);
            r = r.reduced(1.5f);
            g.setGradientFill(juce::ColourGradient(juce::Colour(kKeyTop), 0.0f, r.getY(), juce::Colour(kKeyBottom), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle(r, 2.5f);
            drawLed(g, r.getX() + 10.0f, r.getCentreY(), lit && enabled);
            g.setColour(juce::Colour(kKeyText).withAlpha(enabled ? 1.0f : 0.4f));
            g.setFont(sans(10.5f, true));
            g.drawFittedText(legend, r.withTrimmedLeft(18.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }

        // A knob of the effects in an area: its track, the arc of the value, the face and the
        // pointer
        void drawFxKnob(juce::Graphics& g, const Style& st, const juce::Rectangle<float> area, const float v, const float stroke)
        {
            const float r = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 3.0f;
            const float cx = area.getCentreX();
            const float cy = area.getCentreY();
            constexpr float sweep = juce::MathConstants<float>::pi * 0.75f;

            juce::Path track;
            track.addCentredArc(cx, cy, r, r, 0.0f, -sweep, sweep, true);
            g.setColour(juce::Colour(st.knobTrack));
            g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if(v > 0.0f)
            {
                juce::Path arc;
                arc.addCentredArc(cx, cy, r, r, 0.0f, -sweep, -sweep + v * sweep * 2.0f, true);
                g.setColour(juce::Colour(st.knobArc));
                g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            const float face = r - stroke * 1.6f;
            g.setGradientFill(juce::ColourGradient(juce::Colour(st.knobFaceTop), cx, cy - face, juce::Colour(st.knobFaceBottom), cx, cy + face, false));
            g.fillEllipse(cx - face, cy - face, face * 2, face * 2);
            // the pointer
            const float angle = -sweep + v * sweep * 2.0f;
            g.setColour(juce::Colour(st.value).withAlpha(0.8f));
            g.drawLine(cx + std::sin(angle) * face * 0.55f, cy - std::cos(angle) * face * 0.55f,
                       cx + std::sin(angle) * face * 0.92f, cy - std::cos(angle) * face * 0.92f, stroke > 3.0f ? 2.0f : 1.6f);
        }

        // The switch of the chain or of one rack: a key with a lamp. Assignable.
        class FxPowerKey final : public EditPanel::Control
        {
        public:
            FxPowerKey(EditPanel::Context& context, const int rack) : Control(context), m_rack(rack)
            {
                setTitle(rack < 0 ? juce::String("FX chain") : juce::String(fxLib::rackName(rack)) + " on");
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }

            void refresh() override
            {
                const int v = on() ? 1 : 0;
                if(v != m_shown)
                {
                    m_shown = v;
                    repaint();
                }
            }

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                out.push_back({m_rack < 0 ? MidiLearn::kFxChainTarget : MidiLearn::fxRackOnTarget(m_rack), getLocalBounds()});
            }

            void paint(juce::Graphics& g) override
            {
                drawLampKey(g, getLocalBounds().toFloat(), "On", on(), chainOf(ctx) != nullptr);
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                if(auto* c = chainOf(ctx))
                {
                    if(m_rack < 0)
                        c->setChainOn(!c->isChainOn());
                    else
                        c->setRackOn(m_rack, !c->isRackOn(m_rack));
                    refresh();
                }
            }

        private:
            bool on() const
            {
                const auto* c = chainOf(ctx);
                return c && (m_rack < 0 ? c->isChainOn() : c->isRackOn(m_rack));
            }

            int m_rack;
            int m_shown = -1;
        };

        // One type of a rack: a key, lit when the rack plays it
        class FxTypeKey final : public EditPanel::Control
        {
        public:
            FxTypeKey(EditPanel::Context& context, const int rack, const int type) : Control(context), m_rack(rack), m_type(type)
            {
                setTitle(juce::String(fxLib::rackName(rack)) + " " + fxLib::typeInfo(rack, type).name);
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }

            void refresh() override
            {
                const auto* c = chainOf(ctx);
                const int v = c && c->getType(m_rack) == m_type ? 1 : 0;
                if(v != m_shown)
                {
                    m_shown = v;
                    repaint();
                }
            }

            void paint(juce::Graphics& g) override
            {
                const auto* c = chainOf(ctx);
                drawLampKey(g, getLocalBounds().toFloat(), fxLib::typeInfo(m_rack, m_type).name, c && c->getType(m_rack) == m_type, c != nullptr);
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                if(auto* c = chainOf(ctx))
                {
                    c->setType(m_rack, m_type);
                    if(auto* p = getParentComponent())
                        p->repaint(kFxLeft, rackY(m_rack), kFxWidth, kRackHeight);
                }
            }

        private:
            int m_rack, m_type;
            int m_shown = -1;
        };

        // One control place of a rack: what it is follows the type the rack plays, a knob or a
        // switch, or nothing when the type has fewer controls. Assignable by its place.
        class FxControl final : public EditPanel::Control
        {
        public:
            FxControl(EditPanel::Context& context, const int rack, const int control) : Control(context), m_rack(rack), m_control(control)
            {
                setTitle(juce::String(fxLib::rackName(rack)) + " control " + juce::String(control + 1));
            }

            void refresh() override
            {
                const auto* c = chainOf(ctx);
                if(!c)
                    return;
                const int type = c->getType(m_rack);
                const auto text = exists() ? c->valueText(m_rack, type, m_control) : std::string();
                const float v = c->getValue(m_rack, type, m_control);
                const bool reachable = reachableDivision();
                if(type != m_shownType || v != m_shownValue || text != m_shownText || reachable != m_shownReachable)
                {
                    m_shownType = type;
                    m_shownValue = v;
                    m_shownText = text;
                    m_shownReachable = reachable;
                    setMouseCursor(exists() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                    repaint();
                }
            }

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                if(exists())
                    out.push_back({MidiLearn::fxControlTarget(m_rack, m_control), getLocalBounds()});
            }

            void paint(juce::Graphics& g) override
            {
                const auto* c = chainOf(ctx);
                if(!c || !exists())
                    return;
                const auto& st = style();
                const int type = c->getType(m_rack);
                const auto& info = fxLib::typeInfo(m_rack, type).controls[static_cast<size_t>(m_control)];
                auto b = getLocalBounds();
                g.setColour(juce::Colour(st.label));
                g.setFont(sans(10.0f));
                g.drawFittedText(info.name, b.removeFromTop(14), juce::Justification::centred, 1, 0.7f);
                const float v = c->getValue(m_rack, type, m_control);

                if(info.kind == fxLib::ControlKind::Switch)
                {
                    drawLampKey(g, juce::Rectangle<float>(52.0f, 22.0f).withCentre({b.toFloat().getCentreX(), static_cast<float>(kFxKnobCentre - kFxKnobTop)}), v >= 0.5f ? "On" : "Off", v >= 0.5f, true);
                    return;
                }

                drawFxKnob(g, st, b.removeFromTop(64).toFloat(), v, 3.5f);

                // the value under it; a synced delay time the delay cannot reach in red
                g.setColour(reachableDivision() ? juce::Colour(st.value) : juce::Colour(kLedOn));
                g.setFont(sans(10.5f, true));
                g.drawFittedText(juce::String(m_shownText.empty() ? c->valueText(m_rack, type, m_control) : m_shownText), b.removeFromTop(16), juce::Justification::centred, 1, 0.7f);
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                auto* c = chainOf(ctx);
                if(!c || !exists())
                    return;
                if(isSwitch())
                {
                    c->setValue(m_rack, m_control, c->getValue(m_rack, m_control) >= 0.5f ? 0.0f : 1.0f);
                    refresh();
                    return;
                }
                m_dragStart = c->getValue(m_rack, m_control);
                m_dragging = true;
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                auto* c = chainOf(ctx);
                if(!c || !m_dragging)
                    return;
                // the whole range over 160 pixels, a quarter of that speed with shift
                const auto moved = static_cast<float>(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
                const float pixels = e.mods.isShiftDown() ? 640.0f : 160.0f;
                set(m_dragStart + moved / pixels);
            }

            void mouseUp(const juce::MouseEvent&) override
            {
                m_dragging = false;
            }

            void mouseDoubleClick(const juce::MouseEvent&) override
            {
                auto* c = chainOf(ctx);
                if(!c || !exists() || isSwitch())
                    return;
                set(fxLib::typeInfo(m_rack, c->getType(m_rack)).controls[static_cast<size_t>(m_control)].initial);
            }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                auto* c = chainOf(ctx);
                if(!c || !exists() || isSwitch() || wheel.deltaY == 0.0f)
                    return;
                // a synced time steps by one division, anything else by a hundredth
                const int steps = info().steps;
                const float step = isSyncedTime() ? 1.0f / static_cast<float>(fxLib::delayDivisionCount() - 1)
                                 : (steps > 1 ? 1.0f / static_cast<float>(steps - 1) : 0.01f);
                set(c->getValue(m_rack, m_control) + (wheel.deltaY > 0.0f ? step : -step));
            }

        private:
            bool exists() const
            {
                const auto* c = chainOf(ctx);
                return c && m_control < fxLib::typeInfo(m_rack, c->getType(m_rack)).controlCount;
            }

            const fxLib::ControlInfo& info() const
            {
                const auto* c = chainOf(ctx);
                return fxLib::typeInfo(m_rack, c ? c->getType(m_rack) : 0).controls[static_cast<size_t>(m_control)];
            }

            bool isSwitch() const
            {
                const auto* c = chainOf(ctx);
                return c && fxLib::typeInfo(m_rack, c->getType(m_rack)).controls[static_cast<size_t>(m_control)].kind == fxLib::ControlKind::Switch;
            }

            bool isSyncedTime() const
            {
                const auto* c = chainOf(ctx);
                if(!c || m_rack != fxLib::RackDelay || !exists())
                    return false;
                const int type = c->getType(m_rack);
                return fxLib::typeInfo(m_rack, type).controls[static_cast<size_t>(m_control)].unit == fxLib::ControlUnit::Time
                    && c->getValue(m_rack, type, fxLib::delaySyncControl(type)) >= 0.5f;
            }

            bool reachableDivision() const
            {
                const auto* c = chainOf(ctx);
                return !isSyncedTime() || c->isDelayDivisionReachable(c->getType(m_rack), m_control, fxTempo(ctx.proc));
            }

            void set(const float v)
            {
                if(auto* c = chainOf(ctx))
                {
                    float value = juce::jlimit(0.0f, 1.0f, v);
                    // a synced time rests on a division
                    if(isSyncedTime())
                        value = static_cast<float>(fxLib::delayDivisionOf(value)) / static_cast<float>(fxLib::delayDivisionCount() - 1);
                    // and a knob that clicks on a setting
                    else if(const int steps = info().steps; steps > 1)
                        value = std::round(value * static_cast<float>(steps - 1)) / static_cast<float>(steps - 1);
                    c->setValue(m_rack, m_control, value);
                    refresh();
                }
            }

            int m_rack, m_control;
            int m_shownType = -1;
            float m_shownValue = -1.0f;
            std::string m_shownText;
            bool m_shownReachable = true;
            float m_dragStart = 0.0f;
            bool m_dragging = false;
        };

        // The tempo a synced delay follows
        class FxTempo final : public EditPanel::Control
        {
        public:
            explicit FxTempo(EditPanel::Context& context) : Control(context) {}

            void refresh() override
            {
                const int bpm = juce::roundToInt(fxTempo(ctx.proc) * 10.0);
                if(bpm != m_shown)
                {
                    m_shown = bpm;
                    repaint();
                }
            }

            void paint(juce::Graphics& g) override
            {
                g.setColour(juce::Colour(style().label));
                g.setFont(sans(9.5f));
                g.drawText("Delay sync tempo", getLocalBounds().removeFromTop(12), juce::Justification::centredRight, false);
                g.setColour(juce::Colour(style().value));
                g.setFont(sans(12.0f, true));
                g.drawText(juce::String(m_shown / 10.0, 1) + " BPM", getLocalBounds().withTrimmedTop(12), juce::Justification::centredRight, false);
            }

        private:
            int m_shown = -1;
        };

        // The mix of the chain against the instrument's own sound: its name, a small knob and
        // the value, in a row. Assignable.
        class FxChainMix final : public EditPanel::Control
        {
        public:
            explicit FxChainMix(EditPanel::Context& context) : Control(context)
            {
                setTitle("FX chain mix");
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }

            void refresh() override
            {
                const auto* c = chainOf(ctx);
                const float v = c ? c->getMix() : -1.0f;
                if(v != m_shown)
                {
                    m_shown = v;
                    repaint();
                }
            }

            void learnTargets(std::vector<LearnTarget>& out) const override
            {
                out.push_back({MidiLearn::kFxChainMixTarget, getLocalBounds()});
            }

            void paint(juce::Graphics& g) override
            {
                const auto* c = chainOf(ctx);
                const auto& st = style();
                auto b = getLocalBounds();
                g.setColour(juce::Colour(st.label).withAlpha(c ? 1.0f : 0.4f));
                g.setFont(sans(10.0f));
                g.drawText("Mix", b.removeFromLeft(26), juce::Justification::centredLeft, false);
                if(!c)
                    return;
                const float v = c->getMix();
                drawFxKnob(g, st, b.removeFromLeft(b.getHeight()).toFloat(), v, 2.5f);
                g.setColour(juce::Colour(st.value));
                g.setFont(sans(10.5f, true));
                g.drawText(juce::String(juce::roundToInt(v * 100.0f)), b.withTrimmedLeft(6), juce::Justification::centredLeft, false);
            }

            void mouseDown(const juce::MouseEvent&) override
            {
                if(const auto* c = chainOf(ctx))
                    m_dragStart = c->getMix();
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                // as the knobs of the racks: the whole range over 160 pixels, a quarter of that speed with shift
                const auto moved = static_cast<float>(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
                set(m_dragStart + moved / (e.mods.isShiftDown() ? 640.0f : 160.0f));
            }

            void mouseDoubleClick(const juce::MouseEvent&) override { set(1.0f); }

            void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
            {
                if(const auto* c = chainOf(ctx); c && wheel.deltaY != 0.0f)
                    set(c->getMix() + (wheel.deltaY > 0.0f ? 0.01f : -0.01f));
            }

        private:
            void set(const float v)
            {
                if(auto* c = chainOf(ctx))
                {
                    c->setMix(juce::jlimit(0.0f, 1.0f, v));
                    refresh();
                }
            }

            float m_shown = -2.0f;
            float m_dragStart = 1.0f;
        };
    }

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
        addEffects();

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

        auto learn = std::make_unique<TextKey>("Learn");
        learn->setClickingTogglesState(true);
        learn->onClick = [this] { setLearning(m_learnBtn->getToggleState()); };
        m_learnBtn = std::move(learn);
        addAndMakeVisible(*m_learnBtn);

        auto midi = std::make_unique<TextKey>("MIDI");
        midi->onClick = [this]
        {
            setLearning(false);
            m_midiOverlay->open();
        };
        m_midiBtn = std::move(midi);
        addAndMakeVisible(*m_midiBtn);

        // over the controls, under the two pages
        m_learnLayer = std::make_unique<LearnLayer>(*this);
        addChildComponent(*m_learnLayer);

        m_saveOverlay = std::make_unique<SaveOverlay>(m_proc, [this]
        {
            if(onProgramChanged)
                onProgramChanged();
        });
        addChildComponent(*m_saveOverlay);

        m_midiOverlay = std::make_unique<MidiOverlay>(m_proc);
        addChildComponent(*m_midiOverlay);

        setSize(rackPanel::kWidth, kHeight);
        refresh();
    }

    EditPanel::~EditPanel()
    {
        // nobody is left to pick a parameter
        if(auto* learn = m_proc.getMidiLearn())
            learn->setLearning(false);
    }

    void EditPanel::visibilityChanged()
    {
        if(!isVisible())
            setLearning(false);
    }

    void EditPanel::setLearning(const bool on)
    {
        if(auto* learn = m_proc.getMidiLearn())
            learn->setLearning(on);
        refresh();
    }

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
            // six knobs, Legato and Mono/Poly in a row: the knobs of this section are narrower
            constexpr int w = 36;
            static const std::pair<int, const char*> patchKnobs[] = {{40, "Ramp 1"}, {42, "Ramp 2"}, {44, "Porta"}, {45, "< Velocity"}};
            int kx = at.x;
            for(const auto& [native, label] : patchKnobs)
            {
                add(std::make_unique<Knob>(ctx, native, label), {kx, at.y, w, kKnobHeight});
                kx += w;
            }
            // the spread and the detune of the voices, settings kept with the patch
            add(std::make_unique<SettingKnob>(ctx, SettingKnob::Setting{"Spread", MidiLearn::kSpreadTarget, 100,
                    [this] { return m_proc.getMatrixSpread(); },
                    [this](const int v) { m_proc.setMatrixSpread(v); },
                    [](const int v) { return juce::String(v) + "%"; }, {}}),
                {kx, at.y, w, kKnobHeight});
            kx += w;
            add(std::make_unique<SettingKnob>(ctx, SettingKnob::Setting{"V. Detune", MidiLearn::kDetuneTarget, matrixLib::Device::MaxDetune,
                    [this] { return m_proc.getMatrixDetune(); },
                    [this](const int v) { m_proc.setMatrixDetune(v); },
                    [](const int v) { return juce::String(v); },
                    // the outermost voices, measured on the firmware: about 1.2 cents a step
                    [](const int v) { return v > 0 ? juce::String(juce::CharPointer_UTF8("\xc2\xb1")) + juce::String(juce::roundToInt(v * 1.19)) + "c" : juce::String(); }}),
                {kx, at.y, w, kKnobHeight});
            kx += w;
            add(std::make_unique<Toggle>(ctx, 47, "Legato"), {kx, at.y + 10, 31, kChoiceHeight});
            add(std::make_unique<MonoChoice>(ctx), {kx + 32, at.y + 10, 34, kChoiceHeight});
            // the voices next to the keyboard mode
            const int x = choices(at.translated(0, kKnobRow), {{41, "Ramp 1 Trig", 54}, {43, "Ramp 2 Trig", 54}, {46, "Porta Mode", 58}, {48, "Keyboard", 62}});
            add(std::make_unique<VoicesChoice>(ctx), {x, at.y + kKnobRow, 38, kChoiceHeight});
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
        for(auto& c : pageControls())
            c->refresh();

        // the chain drawn on the effects page lights the racks that play
        if(m_page == Page::Effects)
        {
            int lit = 0;
            if(const auto* chain = m_proc.getFxChain())
                lit = (chain->isChainOn() ? 1 : 0) | (chain->isRackOn(0) ? 2 : 0) | (chain->isRackOn(1) ? 4 : 0) | (chain->isRackOn(2) ? 8 : 0);
            if(lit != m_fxLit)
            {
                m_fxLit = lit;
                repaint(0, kFxBarY, getWidth(), kFxBarH);
            }
        }

        const bool usable = core == SynthType::Matrix && !m_proc.m_isBooting.load() && m_proc.getProgramCount() > 0;
        m_saveBtn->setEnabled(usable);
        m_revertBtn->setEnabled(usable);
        m_initBtn->setEnabled(usable);
        m_learnBtn->setEnabled(usable);
        m_midiBtn->setEnabled(usable);

        // what the MIDI learn waits for, has assigned, or which profile it holds
        if(const auto* learn = m_proc.getMidiLearn())
        {
            if(!usable && learn->isLearning())
                m_proc.getMidiLearn()->setLearning(false);
            if(learn->getRevision() != m_learnRevision)
            {
                m_learnRevision = learn->getRevision();
                m_learnStatus = learn->getStatus();
                m_profileText = learn->getProfileName() + (learn->isModified() ? " *" : "");
                m_learnBtn->setToggleState(learn->isLearning(), juce::dontSendNotification);
                m_learnLayer->setVisible(learn->isLearning());
                m_learnLayer->repaint();
                if(m_midiOverlay->isVisible())
                    m_midiOverlay->sync();
                repaint(0, 0, getWidth(), kHeaderHeight);
            }
            // the monitor of the page of the profiles follows what comes in
            if(learn->getMonitorCount() != m_monitorCount)
            {
                m_monitorCount = learn->getMonitorCount();
                if(m_midiOverlay->isVisible())
                    m_midiOverlay->repaint();
            }
        }

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
        m_learnBtn->setBounds(m_initBtn->getX() - 20 - 72, 7, 72, 22);
        m_midiBtn->setBounds(m_learnBtn->getX() - 8 - 60, 7, 60, 22);
        m_learnLayer->setBounds(0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight);
        m_saveOverlay->setBounds(getLocalBounds());
        m_midiOverlay->setBounds(getLocalBounds());
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
        g.drawText(m_page == Page::Patch ? "PATCH EDIT" : "EFFECTS", kLeft + 4, 7, 100, 22, juce::Justification::centredLeft, false);

        const juce::Rectangle<float> screen(118.0f, 6.0f, 240.0f, 24.0f);
        drawScreen(g, screen);
        // while assignments are learned the screen says what the learning waits for or did
        g.setColour(juce::Colour(m_learnStatus.isNotEmpty() ? kLearn : kTrace));
        g.setFont(sans(13.5f, true));
        g.drawFittedText(m_learnStatus.isNotEmpty() ? m_learnStatus : (m_initialised ? juce::String("INIT   (not saved)") : m_patchTitle),
                         screen.reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

        drawLed(g, 378.0f, 18.0f, m_edited);
        g.setColour(juce::Colour(st.label));
        g.setFont(sans(9.5f));
        g.drawText("Edited", 388, 7, 60, 22, juce::Justification::centredLeft, false);

        // the profile of controller assignments in use, with a star when it was changed
        if(m_midiBtn != nullptr && m_profileText.isNotEmpty())
        {
            const int x = 440;
            const int w = m_midiBtn->getX() - 8 - x;
            g.setFont(sans(9.5f));
            g.drawText("MIDI profile", x, 4, w, 11, juce::Justification::centredRight, false);
            g.setColour(juce::Colour(st.value));
            g.setFont(sans(11.0f, true));
            g.drawFittedText(m_profileText, x, 15, w, 15, juce::Justification::centredRight, 1, 0.7f);
        }

        if(m_page == Page::Effects)
        {
            paintEffects(g);
            return;
        }

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

    void EditPanel::setPage(const Page page)
    {
        if(page == m_page)
            return;
        m_page = page;
        for(auto& c : m_controls)
            c->setVisible(page == Page::Patch);
        for(auto& c : m_fxControls)
            c->setVisible(page == Page::Effects);
        for(auto& c : pageControls())
            c->refresh();
        m_learnLayer->repaint();
        repaint();
    }

    void EditPanel::addEffects()
    {
        auto& ctx = *m_context;
        const auto add = [this](std::unique_ptr<Control> c, const juce::Rectangle<int> bounds)
        {
            c->setBounds(bounds);
            addChildComponent(*c);
            m_fxControls.push_back(std::move(c));
        };

        add(std::make_unique<FxPowerKey>(ctx, -1), {kFxLeft + 110, kFxBarY + 9, 56, 22});
        add(std::make_unique<FxChainMix>(ctx), {kFxMixX, kFxBarY + 2, 130, kFxBarH - 4});
        add(std::make_unique<FxTempo>(ctx), {kFxLeft + kFxWidth - 190, kFxBarY + 6, 176, 28});

        for(int r = 0; r < fxLib::RackCount; ++r)
        {
            const int y = rackY(r);
            add(std::make_unique<FxPowerKey>(ctx, r), {kFxNameX, y + kFxKnobCentre - kFxKeyHeight / 2, 70, kFxKeyHeight});
            // the types in rows of three
            for(int t = 0; t < fxLib::typeCount(r); ++t)
                add(std::make_unique<FxTypeKey>(ctx, r, t), {kFxTypeX + (t % 3) * (kFxTypeW + 6), y + typeKeysTop(r) + (t / 3) * (kFxKeyHeight + kFxKeyGap), kFxTypeW, kFxKeyHeight});
            for(int c = 0; c < fxLib::MaxControls; ++c)
                add(std::make_unique<FxControl>(ctx, r, c), {kFxKnobX + c * kFxKnobW, y + kFxKnobTop, kFxKnobW, kFxKnobH});
        }
    }

    // The chain and three rack units, in the finish of the panel
    void EditPanel::paintEffects(juce::Graphics& g)
    {
        const auto& st = *m_context->style;
        const auto* chain = m_proc.getFxChain();

        // the chain: the order of the racks and its switch
        const juce::Rectangle<float> bar(static_cast<float>(kFxLeft), static_cast<float>(kFxBarY), static_cast<float>(kFxWidth), static_cast<float>(kFxBarH));
        g.setColour(juce::Colour(st.frame));
        g.fillRoundedRectangle(bar, 5.0f);
        g.setColour(juce::Colour(st.frameOutline));
        g.drawRoundedRectangle(bar.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(juce::Colour(st.title));
        g.setFont(sans(10.5f, true));
        g.drawText("FX CHAIN", kFxLeft + 12, kFxBarY, 90, kFxBarH, juce::Justification::centredLeft, false);

        // instrument > modulation > delay > reverb > out, the racks that play lit
        const char* const stages[] = {"MTRX-1K", "MODULATION", "DELAY", "REVERB", "OUT"};
        float x = static_cast<float>(kFxFlowX);
        const float cy = static_cast<float>(kFxBarY + kFxBarH / 2);
        g.setFont(sans(10.0f, true));
        for(int i = 0; i < 5; ++i)
        {
            const bool lit = i == 0 || i == 4 || (chain && chain->isChainOn() && chain->isRackOn(i - 1));
            const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), stages[i]) + 16.0f;
            const juce::Rectangle<float> box(x, cy - 10.0f, w, 20.0f);
            g.setColour(juce::Colour(lit ? st.knobArc : st.knobTrack).withAlpha(lit ? 0.9f : 0.6f));
            g.drawRoundedRectangle(box, 3.0f, 1.2f);
            g.setColour(juce::Colour(lit ? st.value : st.label).withAlpha(lit ? 1.0f : 0.5f));
            g.drawText(stages[i], box.toNearestInt(), juce::Justification::centred, false);
            x += w;
            if(i < 4)
            {
                juce::Path arrow;
                arrow.startNewSubPath(x + 6.0f, cy);
                arrow.lineTo(x + 22.0f, cy);
                g.setColour(juce::Colour(st.label).withAlpha(0.7f));
                g.strokePath(arrow, juce::PathStrokeType(1.2f));
                juce::Path head;
                head.addTriangle(x + 23.0f, cy, x + 18.0f, cy - 3.5f, x + 18.0f, cy + 3.5f);
                g.fillPath(head);
                x += 28.0f;
            }
        }

        // the racks
        for(int r = 0; r < fxLib::RackCount; ++r)
        {
            const auto y = static_cast<float>(rackY(r));
            const juce::Rectangle<float> unit(static_cast<float>(kFxLeft), y, static_cast<float>(kFxWidth), static_cast<float>(kRackHeight));

            // the face and its ears with two screws each
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRoundedRectangle(unit.translated(0.0f, 2.0f), 4.0f);
            g.setGradientFill(juce::ColourGradient(juce::Colour(st.knobFaceTop).interpolatedWith(juce::Colour(st.frame), 0.6f), 0.0f, y,
                                                   juce::Colour(st.frame), 0.0f, y + kRackHeight, false));
            g.fillRoundedRectangle(unit, 4.0f);
            g.setColour(juce::Colour(st.frameOutline));
            g.drawRoundedRectangle(unit.reduced(0.5f), 4.0f, 1.0f);
            for(const float ex : {unit.getX(), unit.getRight() - kRackEar})
            {
                const juce::Rectangle<float> ear(ex, y, static_cast<float>(kRackEar), static_cast<float>(kRackHeight));
                g.setColour(juce::Colour(st.backgroundBottom).withAlpha(0.8f));
                g.fillRect(ear.reduced(0.0f, 1.0f));
                for(const float sy : {y + 22.0f, y + kRackHeight - 22.0f})
                {
                    const float sx = ear.getCentreX();
                    g.setColour(juce::Colours::black.withAlpha(0.6f));
                    g.fillRoundedRectangle(sx - 6.0f, sy - 4.0f, 12.0f, 8.0f, 4.0f);
                    g.setColour(juce::Colour(st.knobTrack));
                    g.fillEllipse(sx - 3.0f, sy - 3.0f, 6.0f, 6.0f);
                }
            }

            // name and rule, the label of the types
            g.setColour(juce::Colour(st.title));
            g.setFont(sans(15.0f, true));
            g.drawText(juce::String(fxLib::rackName(r)).toUpperCase(), kFxNameX, static_cast<int>(y) + 12, 200, 22, juce::Justification::centredLeft, false);
            g.setColour(juce::Colour(st.title).withAlpha(0.55f));
            g.fillRect(static_cast<float>(kFxNameX), y + 35.0f, static_cast<float>(kFxWidth - 2 * (kRackEar + 16)), 1.2f);
            g.setColour(juce::Colour(st.label));
            g.setFont(sans(9.5f));
            g.drawText("Type", kFxTypeX, static_cast<int>(y) + typeKeysTop(r) - 17, 80, 14, juce::Justification::centredLeft, false);
            g.drawText(juce::String(r + 1), kFxLeft + kFxWidth - kRackEar - 40, static_cast<int>(y) + 14, 24, 14, juce::Justification::centredRight, false);
        }
    }
}
