#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

// Front panel widgets for the GPL editor: a 1U rack unit drawn in code, laid out
// after the Oberheim Matrix-1000. Nothing here knows about the processor.

namespace retromulator
{
    namespace rackPanel
    {
        static constexpr int kWidth       = 1100;
        static constexpr int kUnitHeight  = 100;   // the rack unit itself
        static constexpr int kStripHeight = 58;    // browser strip under it
        static constexpr int kHeight      = kUnitHeight + kStripHeight;

        static constexpr int kLedCount = 6;

        // Positions shared by the cached background (labels) and the live overlay (LEDs).
        static constexpr int kLedFirstX = 226;
        static constexpr int kLedStepX  = 34;
        static constexpr int kLedY      = 67;

        // Browser strip layout
        static constexpr int kStripControlY = kUnitHeight + 23;
        static constexpr int kStripControlH = 26;
        static constexpr int kStripEditX    = 16;
        static constexpr int kStripEditW    = 72;
        static constexpr int kStripBankX    = 104;
        static constexpr int kStripBankW    = 300;
        static constexpr int kStripNavX     = 420;
        static constexpr int kStripPatchX   = 480;
        static constexpr int kStripPatchW   = 516;

        // About legend, in the top right corner between the case screw and the rack ear
        static constexpr int kAboutX        = 1037;
        static constexpr int kAboutY        = 6;
        static constexpr int kAboutW        = 33;
        static constexpr int kAboutH        = 13;

        // Panel finish switch, left of the Power legend
        static constexpr int kThemeSwitchX  = 878;
        static constexpr int kThemeSwitchY  = 46;
        static constexpr int kThemeSwitchW  = 26;
        static constexpr int kThemeSwitchH  = 12;

        static constexpr juce::uint32 kLabelBlue = 0xff3c8be6;
        static constexpr juce::uint32 kLabelRed  = 0xffe0303a;

        // The Matrix-1000 came in two finishes: black with blue and red legends, and
        // the later white one with black legends and no stripe.
        enum class Theme { Black, White };

        // Brushed panel with ears, screws and every fixed legend, at 2x for
        // high-DPI hosts. ledLabels may contain a line break.
        juce::Image createBackground(Theme theme, const std::array<juce::String, kLedCount>& ledLabels);

        // Name at the top left, under a stripe on the black panel, drawn as an outline
        // like the model name on the hardware.
        void drawTitle(juce::Graphics& g, Theme theme, const juce::String& name);

        void drawLed(juce::Graphics& g, int index, bool lit);
    }

    class RackLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        RackLookAndFeel();
    };

    // Black key with an optional white legend.
    class PanelButton final : public juce::Button
    {
    public:
        explicit PanelButton(const juce::String& legend = {});

    private:
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

        juce::String m_legend;
    };

    // Rocker switch; the toggle state is the "on" position.
    class PowerSwitch final : public juce::Button
    {
    public:
        PowerSwitch();

    private:
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    // Small legend printed on the panel that can be clicked.
    class LegendButton final : public juce::Button
    {
    public:
        explicit LegendButton(const juce::String& legend);

        void setTheme(rackPanel::Theme theme);

    private:
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

        rackPanel::Theme m_theme = rackPanel::Theme::Black;
    };

    // Slide switch between the two panel finishes; the toggle state is the white one.
    class ThemeSwitch final : public juce::Button
    {
    public:
        ThemeSwitch();

    private:
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    class VolumeKnob final : public juce::Slider
    {
    public:
        VolumeKnob();
        void paint(juce::Graphics& g) override;
    };

    // Covers the editor with the credits; a click anywhere but on the link closes it.
    class AboutPanel final : public juce::Component
    {
    public:
        AboutPanel(const juce::String& name, const juce::String& version);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override { setVisible(false); }

    private:
        juce::String m_name, m_version;
        juce::HyperlinkButton m_link;
    };

    // Three red digits. Understands 0-9, '-', ' ' and the letters b E r o n.
    class SevenSegmentDisplay final : public juce::Component
    {
    public:
        SevenSegmentDisplay();

        void setText(const juce::String& text);
        // Decimal point of the first digit, lit while the bank is locked.
        void setDot(bool lit);
        void paint(juce::Graphics& g) override;

    private:
        juce::String m_text{"   "};
        bool m_dot = false;
    };
}
