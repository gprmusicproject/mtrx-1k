#include "RackPanel.h"

#include <cmath>

namespace retromulator
{
    namespace
    {
        constexpr juce::uint32 kLegendWhite = 0xffececec;
        constexpr juce::uint32 kLegendBlack = 0xff141414;

        struct Palette
        {
            juce::uint32 panelTop, panelBottom;
            juce::uint32 legend;        // Volume, LED captions
            juce::uint32 keyLegend;     // Select, Bank Lock, Enter
            juce::uint32 powerLegend;
            juce::uint32 titleFill, titleOutline;
            bool         stripe;
            float        topEdge, bottomEdge;   // highlight and shadow strength along the unit's edges
            float        streaks;               // strength of the brushing; the white finish is painted
        };

        const Palette& paletteFor(const rackPanel::Theme theme)
        {
            static const Palette black
            {
                0xff222326, 0xff151617,
                kLegendWhite, rackPanel::kLabelBlue, rackPanel::kLabelRed,
                0xfff4f4f4, 0xffd8d8d8,
                true, 0.16f, 0.7f, 1.0f
            };
            static const Palette white
            {
                0xffe6e5e1, 0xffd6d5d0,
                kLegendBlack, kLegendBlack, kLegendBlack,
                0xff101010, 0xff101010,
                false, 0.55f, 0.35f, 0.3f
            };
            return theme == rackPanel::Theme::White ? white : black;
        }
        constexpr juce::uint32 kSegmentLit  = 0xffff3324;
        constexpr juce::uint32 kSegmentOff  = 0xff2b0808;

        juce::Font sansFont(const float height, const bool bold = true)
        {
            return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), height,
                                                bold ? juce::Font::bold : juce::Font::plain));
        }

        juce::Path textPath(const juce::Font& font, const juce::String& text, const float x, const float baseline)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(font, text, x, baseline);
            juce::Path p;
            glyphs.createPath(p);
            return p;
        }

        // The model name on the hardware is set in a wide, heavy serif face with small flared
        // serifs, as an outline, the letters close enough to touch. That face cannot be
        // shipped, so this takes the nearest one the system has and widens and tightens it.
        juce::Path logoPath(const juce::String& text, const float height, const float x, const float baseline)
        {
            static const juce::String face = []
            {
                const auto installed = juce::Font::findAllTypefaceNames();
                for(const char* name : { "Palatino Linotype", "Palatino", "Book Antiqua" })
                    if(installed.contains(name))
                        return juce::String(name);
                return juce::Font::getDefaultSerifFontName();
            }();

            const auto font = juce::Font(juce::FontOptions(face, height, juce::Font::bold))
                                  .withHorizontalScale(1.3f)
                                  .withExtraKerningFactor(-0.035f);
            return textPath(font, text, x, baseline);
        }

        // The maker's symbol: a knob with its scale, the letters G, P and R around it and a
        // waveform under it. Drawn in black, recoloured for the finish it goes on.
        const char* const kSymbolSvg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="76" height="96" viewBox="22 10 76 96"><g transform="translate(0 13)">
<path d="M38.79 71.21L35.25 74.75" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M31.47 59.27L26.71 60.82" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M30.37 45.31L25.43 44.52" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M35.73 32.37L31.68 29.43" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M46.38 23.27L44.11 18.81" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M60.00 20.00L60.00 15.00" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M73.62 23.27L75.89 18.81" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M84.27 32.37L88.32 29.43" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M89.63 45.31L94.57 44.52" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M88.53 59.27L93.29 60.82" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<path d="M81.21 71.21L84.75 74.75" stroke="#000000" stroke-width="3" stroke-linecap="round"/>
<circle cx="60" cy="50" r="24" stroke="#000000" stroke-width="3" fill="none"/>
<path d="M60 50L69.00 34.41" stroke="#000000" stroke-width="5" stroke-linecap="round"/>
<circle cx="60" cy="50" r="4" fill="#000000"/>
<path d="M30.288 85.615H30.183Q30.048 85.93 29.86 86.222Q29.673 86.515 29.395 86.74Q29.118 86.965 28.728 87.1Q28.338 87.235 27.828 87.235Q26.193 87.235 25.368 85.84Q24.543 84.445 24.543 81.895Q24.543 79.225 25.488 77.815Q26.433 76.405 28.383 76.405Q29.148 76.405 29.725 76.615Q30.303 76.825 30.708 77.177Q31.113 77.53 31.383 78.002Q31.653 78.475 31.818 79L30.303 79.51Q30.183 79.165 30.033 78.857Q29.883 78.55 29.665 78.31Q29.448 78.07 29.14 77.935Q28.833 77.8 28.398 77.8Q27.303 77.8 26.815 78.647Q26.328 79.495 26.328 80.935V82.6Q26.328 83.32 26.433 83.92Q26.538 84.52 26.785 84.947Q27.033 85.375 27.43 85.615Q27.828 85.855 28.398 85.855Q29.313 85.855 29.8 85.307Q30.288 84.76 30.288 83.905V82.96H28.263V81.7H31.818V87.055H30.288Z" fill="#000000"/>
<path d="M56.505 10.235V-0.235H60.42Q61.92 -0.235 62.707 0.605Q63.495 1.445 63.495 2.915Q63.495 4.385 62.707 5.225Q61.92 6.065 60.42 6.065H58.185V10.235ZM58.185 4.67H60.165Q61.725 4.67 61.725 3.23V2.585Q61.725 1.16 60.165 1.16H58.185Z" fill="#000000"/>
<path d="M89.915 87.055H88.25V76.585H92.165Q93.665 76.585 94.452 77.425Q95.24 78.265 95.24 79.735Q95.24 80.935 94.655 81.715Q94.07 82.495 92.975 82.675L95.39 87.055H93.53L91.31 82.825H89.915ZM91.925 81.475Q93.485 81.475 93.485 80.05V79.405Q93.485 77.98 91.925 77.98H89.915V81.475Z" fill="#000000"/>
<path d="M41 84.50h7l3.5-6 4.5 12 4.5-9 3.5 4.5 3.5-1.5h11.5" stroke="#000000" stroke-width="3" fill="none" stroke-linecap="round" stroke-linejoin="round"/></g></svg>)svg";

        void drawSymbol(juce::Graphics& g, const juce::Colour colour, const juce::Rectangle<float> area)
        {
            static const std::unique_ptr<juce::Drawable> symbol = []() -> std::unique_ptr<juce::Drawable>
            {
                const auto xml = juce::parseXML(kSymbolSvg);
                return xml ? juce::Drawable::createFromSVG(*xml) : nullptr;
            }();
            if(!symbol)
                return;
            const auto copy = symbol->createCopy();
            copy->replaceColour(juce::Colours::black, colour);
            copy->drawWithin(g, area, juce::RectanglePlacement::centred, 1.0f);
        }

        // where the symbol and the name stand on the unit
        constexpr float kSymbolX = 82.0f;
        constexpr float kNameX   = 113.0f;

        void drawScrew(juce::Graphics& g, const float cx, const float cy, const float r, const float slotAngle)
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillEllipse(cx - r - 0.5f, cy - r + 0.5f, r * 2 + 1, r * 2 + 1);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4c50), cx - r, cy - r,
                                                   juce::Colour(0xff141516), cx + r, cy + r, false));
            g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

            g.setColour(juce::Colour(0xff060606));
            for(int i = 0; i < 2; ++i)
            {
                const float a = slotAngle + static_cast<float>(i) * juce::MathConstants<float>::halfPi;
                const float dx = std::cos(a) * r * 0.62f;
                const float dy = std::sin(a) * r * 0.62f;
                g.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, 1.3f);
            }
        }

        void drawEarSlot(juce::Graphics& g, const float cx, const float cy)
        {
            const juce::Rectangle<float> r(cx - 8.0f, cy - 4.5f, 16.0f, 9.0f);
            g.setColour(juce::Colour(0xff030303));
            g.fillRoundedRectangle(r, 4.5f);
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.drawRoundedRectangle(r.translated(0.0f, 0.6f), 4.5f, 0.8f);
        }

        // Fine horizontal streaks, drawn in device pixels so they stay one pixel thin.
        void drawBrushedMetal(juce::Graphics& g, const juce::Rectangle<int> area, const float strength)
        {
            juce::Random rng(0x1000);
            for(int y = area.getY(); y < area.getBottom(); ++y)
            {
                for(int i = 0; i < 7; ++i)
                {
                    const int x   = area.getX() + rng.nextInt(area.getWidth());
                    const int len = 60 + rng.nextInt(700);
                    const float alpha = (0.012f + rng.nextFloat() * 0.035f) * strength;
                    g.setColour((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha(alpha));
                    g.fillRect(x, y, juce::jmin(len, area.getRight() - x), 1);
                }
            }
        }

        juce::Path horizontalSegment(const float cx, const float cy, const float len, const float t)
        {
            const float h = len * 0.5f, k = t * 0.5f;
            juce::Path p;
            p.startNewSubPath(cx - h, cy);
            p.lineTo(cx - h + k, cy - k);
            p.lineTo(cx + h - k, cy - k);
            p.lineTo(cx + h, cy);
            p.lineTo(cx + h - k, cy + k);
            p.lineTo(cx - h + k, cy + k);
            p.closeSubPath();
            return p;
        }

        juce::Path verticalSegment(const float cx, const float cy, const float len, const float t)
        {
            const float h = len * 0.5f, k = t * 0.5f;
            juce::Path p;
            p.startNewSubPath(cx, cy - h);
            p.lineTo(cx + k, cy - h + k);
            p.lineTo(cx + k, cy + h - k);
            p.lineTo(cx, cy + h);
            p.lineTo(cx - k, cy + h - k);
            p.lineTo(cx - k, cy - h + k);
            p.closeSubPath();
            return p;
        }

        // Bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
        int segmentsFor(const juce::juce_wchar c)
        {
            switch(c)
            {
                case '0': return 63;
                case '1': return 6;
                case '2': return 91;
                case '3': return 79;
                case '4': return 102;
                case '5': return 109;
                case '6': return 125;
                case '7': return 7;
                case '8': return 127;
                case '9': return 111;
                case '-': return 64;
                case 'b': return 124;
                case 'E': return 121;
                case 'r': return 80;
                case 'o': return 92;
                case 'n': return 84;
                default:  return 0;
            }
        }
    }

    namespace rackPanel
    {
        juce::Image createBackground(const Theme theme, const std::array<juce::String, kLedCount>& ledLabels, const int scale)
        {
            const Palette& pal = paletteFor(theme);

            juce::Image image(juce::Image::RGB, kWidth * scale, kHeight * scale, false);
            juce::Graphics g(image);

            const juce::Rectangle<int> unitPx(0, 0, kWidth * scale, kUnitHeight * scale);

            g.setGradientFill(juce::ColourGradient(juce::Colour(pal.panelTop), 0.0f, 0.0f,
                                                   juce::Colour(pal.panelBottom), 0.0f, static_cast<float>(unitPx.getHeight()), false));
            g.fillRect(unitPx);
            drawBrushedMetal(g, unitPx, pal.streaks);

            g.addTransform(juce::AffineTransform::scale(static_cast<float>(scale)));

            const float w = static_cast<float>(kWidth);
            const float h = static_cast<float>(kUnitHeight);

            // Edges of the unit
            g.setColour(juce::Colours::white.withAlpha(pal.topEdge));
            g.fillRect(0.0f, 0.0f, w, 1.0f);
            g.setColour(juce::Colours::black.withAlpha(pal.bottomEdge));
            g.fillRect(0.0f, h - 1.5f, w, 1.5f);

            // Rack ears
            for(const float x : { 17.0f, w - 17.0f })
            {
                drawEarSlot(g, x, 13.0f);
                drawEarSlot(g, x, h - 13.0f);
            }

            // Case screws
            drawScrew(g,  71.0f, 19.0f, 5.0f, 0.4f);
            drawScrew(g,  71.0f, 81.0f, 5.0f, 1.1f);
            drawScrew(g, w - 74.0f, 19.0f, 5.0f, 0.9f);
            drawScrew(g, w - 74.0f, 81.0f, 5.0f, 0.2f);
            drawScrew(g, 589.0f, 87.0f, 3.2f, 0.7f);

            // Legends
            g.setColour(juce::Colour(pal.legend));
            g.setFont(sansFont(11.0f));
            g.drawText("Volume", 84, 79, 70, 14, juce::Justification::centred, false);

            g.setColour(juce::Colour(pal.keyLegend));
            g.setFont(sansFont(11.5f));
            g.drawText("Select",    146, 39, 60, 14, juce::Justification::centred, false);
            g.drawText("Bank Lock", 402, 39, 70, 14, juce::Justification::centred, false);
            g.drawText("Enter",     589, 39, 60, 14, juce::Justification::centred, false);

            g.setColour(juce::Colour(pal.powerLegend));
            g.drawText("Power", 906, 44, 60, 14, juce::Justification::centredRight, false);

            g.setColour(juce::Colour(pal.legend));
            g.setFont(sansFont(8.5f, false));
            g.drawText("Panel", kThemeSwitchX - 12, kThemeSwitchY - 13, kThemeSwitchW + 24, 11,
                       juce::Justification::centred, false);
            for(int i = 0; i < kLedCount; ++i)
            {
                const int cx = kLedFirstX + i * kLedStepX;
                g.drawFittedText(ledLabels[static_cast<size_t>(i)], cx - 17, 39, 34, 21,
                                 juce::Justification::centred, 2, 1.0f);
            }

            // Browser strip
            const juce::Rectangle<float> strip(0.0f, h, w, static_cast<float>(kStripHeight));
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0c0d0e), 0.0f, strip.getY(),
                                                   juce::Colour(0xff121315), 0.0f, strip.getBottom(), false));
            g.fillRect(strip);
            g.setColour(juce::Colours::black);
            g.fillRect(0.0f, h, w, 2.0f);
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRect(0.0f, h + 2.0f, w, 1.0f);

            g.setColour(juce::Colour(kLabelBlue));
            g.setFont(sansFont(9.5f));
            const int labelY = kStripControlY - 14;
            g.drawText("BANK",  kStripBankX,  labelY, 80, 12, juce::Justification::centredLeft, false);
            g.drawText("PATCH", kStripPatchX, labelY, 80, 12, juce::Justification::centredLeft, false);

            return image;
        }

        void drawTitle(juce::Graphics& g, const Theme theme, const juce::String& name)
        {
            const Palette& pal = paletteFor(theme);

            // the maker's symbol first, where the hardware has its maker's, then the name
            drawSymbol(g, juce::Colour(pal.titleOutline), { kSymbolX, 9.0f, 24.0f, 30.0f });

            const juce::Path path = logoPath(name, 27.0f, kNameX, 34.0f);
            g.setColour(juce::Colour(pal.titleOutline));
            g.strokePath(path, juce::PathStrokeType(1.0f));

            if(!pal.stripe)
                return;

            // The stripe runs over symbol and name and never shrinks below the Matrix one.
            g.setColour(juce::Colour(0xff1f6fd2));
            g.fillRect(kSymbolX, 3.0f, juce::jmax(306.0f, path.getBounds().getRight() - kSymbolX), 4.5f);
        }

        void drawLed(juce::Graphics& g, const int index, const bool lit)
        {
            const float cx = static_cast<float>(kLedFirstX + index * kLedStepX);
            const float cy = static_cast<float>(kLedY);
            constexpr float r = 2.6f;

            if(lit)
            {
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff3b30).withAlpha(0.55f), cx, cy,
                                                       juce::Colour(0x00ff3b30), cx + 9.0f, cy, true));
                g.fillEllipse(cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
            }

            g.setColour(juce::Colours::black);
            g.fillEllipse(cx - r - 0.8f, cy - r - 0.8f, (r + 0.8f) * 2, (r + 0.8f) * 2);
            g.setColour(lit ? juce::Colour(0xffff4a3d) : juce::Colour(0xff4a0e0e));
            g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
            if(lit)
            {
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.fillEllipse(cx - 1.2f, cy - 1.5f, 1.6f, 1.6f);
            }
        }
    }

    RackLookAndFeel::RackLookAndFeel()
    {
        const juce::Colour background(0xff0f1012);
        const juce::Colour outline(0xff36393e);
        const juce::Colour text(0xffe6e6e6);
        const juce::Colour blue(rackPanel::kLabelBlue);

        setColour(juce::ComboBox::backgroundColourId, background);
        setColour(juce::ComboBox::outlineColourId, outline);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, blue);
        setColour(juce::ComboBox::focusedOutlineColourId, blue);

        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff17181a));
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff1f5fb0));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff17181a));
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::ComboBox::buttonColourId, background);
    }

    PanelButton::PanelButton(const juce::String& legend) : juce::Button(legend), m_legend(legend)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void PanelButton::paintButton(juce::Graphics& g, const bool highlighted, const bool down)
    {
        auto r = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff020202));
        g.fillRoundedRectangle(r, 3.5f);

        r = r.reduced(1.5f);
        if(down)
            r = r.translated(0.0f, 0.8f);

        const juce::Colour top    = down ? juce::Colour(0xff161718) : juce::Colour(highlighted ? 0xff3a3c40 : 0xff303236);
        const juce::Colour bottom = down ? juce::Colour(0xff0a0a0b) : juce::Colour(0xff101112);
        g.setGradientFill(juce::ColourGradient(top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 2.5f);

        if(!down)
        {
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.fillRect(r.getX() + 2.5f, r.getY() + 0.6f, r.getWidth() - 5.0f, 0.9f);
        }

        if(m_legend.isNotEmpty())
        {
            // Filled as an outline, like the panel title, so it matches the cached legends.
            juce::Path legend = textPath(sansFont(13.5f), m_legend, 0.0f, 0.0f);
            const auto box = legend.getBounds();
            legend.applyTransform(juce::AffineTransform::translation(r.getCentreX() - box.getCentreX(),
                                                                     r.getCentreY() - box.getCentreY()));
            g.setColour(juce::Colour(kLegendWhite));
            g.fillPath(legend);
        }
    }

    PowerSwitch::PowerSwitch() : juce::Button("Power")
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void PowerSwitch::paintButton(juce::Graphics& g, bool, bool)
    {
        auto r = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff050505));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);

        r = r.reduced(4.0f);
        const bool on = getToggleState();
        const auto upper = r.withHeight(r.getHeight() * 0.5f);
        const auto lower = r.withTop(upper.getBottom());

        // The pressed half sits in shadow, the other one catches the light.
        const auto pressed = on ? upper : lower;
        const auto raised  = on ? lower : upper;

        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0a0a0b), 0.0f, pressed.getY(),
                                               juce::Colour(0xff17181a), 0.0f, pressed.getBottom(), false));
        g.fillRect(pressed);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c3e42), 0.0f, raised.getY(),
                                               juce::Colour(0xff1c1d1f), 0.0f, raised.getBottom(), false));
        g.fillRect(raised);

        g.setColour(juce::Colours::black);
        g.fillRect(r.getX(), upper.getBottom() - 0.5f, r.getWidth(), 1.0f);

        const float cx = r.getCentreX();
        g.setColour(on ? juce::Colour(rackPanel::kLabelRed) : juce::Colour(0xff8a8a8a));
        g.fillRect(cx - 0.8f, upper.getCentreY() - 3.5f, 1.6f, 7.0f);
        g.setColour(juce::Colour(0xff8a8a8a));
        g.drawEllipse(cx - 3.0f, lower.getCentreY() - 3.0f, 6.0f, 6.0f, 1.2f);
    }

    LegendButton::LegendButton(const juce::String& legend) : juce::Button(legend)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void LegendButton::setTheme(const rackPanel::Theme theme)
    {
        m_theme = theme;
        repaint();
    }

    void LegendButton::paintButton(juce::Graphics& g, const bool highlighted, const bool down)
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        const juce::Colour colour(paletteFor(m_theme).legend);

        g.setColour(colour.withAlpha(down ? 0.22f : highlighted ? 0.12f : 0.0f));
        g.fillRoundedRectangle(r, 2.5f);
        g.setColour(colour.withAlpha(highlighted ? 0.8f : 0.4f));
        g.drawRoundedRectangle(r, 2.5f, 0.8f);

        g.setColour(colour.withAlpha(highlighted ? 1.0f : 0.85f));
        g.setFont(sansFont(8.5f, false));
        g.drawText(getName(), getLocalBounds(), juce::Justification::centred, false);
    }

    ThemeSwitch::ThemeSwitch() : juce::Button("Panel")
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void ThemeSwitch::paintButton(juce::Graphics& g, bool, bool)
    {
        const auto r = getLocalBounds().toFloat();
        const bool white = getToggleState();

        // Recess, one half in each finish
        g.setColour(juce::Colour(0xff050505));
        g.fillRoundedRectangle(r, 2.5f);
        const auto slot = r.reduced(1.5f);
        g.setColour(juce::Colour(0xff1b1c1e));
        g.fillRect(slot.withWidth(slot.getWidth() * 0.5f));
        g.setColour(juce::Colour(0xffdddcd7));
        g.fillRect(slot.withLeft(slot.getCentreX()));
        g.setColour(juce::Colour(0xff808080).withAlpha(0.6f));
        g.drawRoundedRectangle(r.reduced(0.5f), 2.5f, 1.0f);

        // The lever covers the finish that is not in use and leaves the chosen one showing.
        const auto lever = (white ? slot.withWidth(slot.getWidth() * 0.5f) : slot.withLeft(slot.getCentreX())).reduced(0.5f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff6a6c70), 0.0f, lever.getY(),
                                               juce::Colour(0xff2c2d30), 0.0f, lever.getBottom(), false));
        g.fillRoundedRectangle(lever, 1.5f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        for(const float dx : { -2.0f, 0.0f, 2.0f })
            g.fillRect(lever.getCentreX() + dx - 0.4f, lever.getY() + 1.5f, 0.8f, lever.getHeight() - 3.0f);
    }

    void LibraryButton::paintButton(juce::Graphics& g, const bool highlighted, const bool down)
    {
        const bool on = getToggleState();
        getLookAndFeel().drawButtonBackground(g, *this, findColour(on ? buttonOnColourId : buttonColourId), highlighted, down);

        // three books upright and a fourth leaning on them, centred in the key
        const auto r = getLocalBounds().toFloat();
        const float s = r.getHeight() * 0.56f;            // the height of a book
        const float w = s * 0.15f, gap = s * 0.11f, lean = 0.42f;
        const float total = 3.0f * w + 3.0f * gap + w + s * std::sin(lean);
        const float x0 = r.getCentreX() - total * 0.5f, bottom = r.getCentreY() + s * 0.5f;
        juce::Path books;
        for(int i = 0; i < 3; ++i)
            books.addRoundedRectangle(x0 + static_cast<float>(i) * (w + gap), bottom - s, w, s, w * 0.25f);
        juce::Path leaning;
        const float lx = x0 + 3.0f * (w + gap);
        leaning.addRoundedRectangle(lx, bottom - s, w, s, w * 0.25f);
        leaning.applyTransform(juce::AffineTransform::rotation(lean, lx, bottom));
        books.addPath(leaning);

        g.setColour(findColour(on ? textColourOnId : textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
        g.fillPath(books);
    }

    VolumeKnob::VolumeKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setRange(0.0, 1.0);
        setDoubleClickReturnValue(true, 1.0);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void VolumeKnob::paint(juce::Graphics& g)
    {
        const auto b  = getLocalBounds().toFloat();
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float r  = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 2.0f;

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(cx - r - 1.0f, cy - r + 1.5f, (r + 1.0f) * 2, (r + 1.0f) * 2);

        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3f4145), cx - r, cy - r,
                                               juce::Colour(0xff09090a), cx + r, cy + r, false));
        g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

        const float cap = r * 0.74f;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff222326), cx, cy - cap,
                                               juce::Colour(0xff131415), cx, cy + cap, false));
        g.fillEllipse(cx - cap, cy - cap, cap * 2, cap * 2);

        const float proportion = static_cast<float>(valueToProportionOfLength(getValue()));
        const float angle = juce::degreesToRadians(-135.0f + 270.0f * proportion);
        const float sx = std::sin(angle), sy = -std::cos(angle);
        g.setColour(juce::Colour(kLegendWhite));
        g.drawLine(cx + sx * r * 0.30f, cy + sy * r * 0.30f,
                   cx + sx * r * 0.95f, cy + sy * r * 0.95f, 1.8f);
    }

    AboutPanel::AboutPanel(const juce::String& name, const juce::String& version)
        : m_name(name), m_version(version),
          m_link("github.com/reales/retromulator", juce::URL("https://github.com/reales/retromulator"))
    {
        m_link.setFont(sansFont(13.0f, false), false, juce::Justification::centredLeft);
        m_link.setColour(juce::HyperlinkButton::textColourId, juce::Colour(rackPanel::kLabelBlue));
        m_link.setTooltip({});
        addAndMakeVisible(m_link);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void AboutPanel::resized()
    {
        m_link.setBounds(446, 62, 240, 18);
    }

    void AboutPanel::paint(juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfa17181a), 0.0f, 0.0f,
                                               juce::Colour(0xfa0b0c0d), 0.0f, b.getBottom(), false));
        g.fillRect(b);
        // Symbol and name as on the panel
        drawSymbol(g, juce::Colour(0xffe4e4e4), { 34.0f, 18.0f, 56.0f, 70.0f });
        const juce::Path name = logoPath(m_name, 30.0f, 104.0f, 66.0f);
        g.setColour(juce::Colour(rackPanel::kLabelBlue));
        g.fillRect(104.0f, 22.0f, name.getBounds().getRight() - 104.0f, 4.5f);
        g.setColour(juce::Colour(0xffe4e4e4));
        g.strokePath(name, juce::PathStrokeType(1.0f));

        g.setColour(juce::Colour(kLegendWhite));
        g.setFont(sansFont(13.0f));
        g.drawText("Version " + m_version, 104, 80, 320, 18, juce::Justification::centredLeft, false);
        g.drawText("Created by GPR Music Project", 104, 100, 320, 18, juce::Justification::centredLeft, false);

        constexpr int x = 446, w = 560;
        g.setFont(sansFont(13.0f, false));
        g.drawText("Based on Retromulator by discoDSP, built on the Gearmulator emulation engines.",
                   x, 42, w, 18, juce::Justification::centredLeft, false);
        g.setColour(juce::Colour(0xffa8a8a8));
        g.setFont(sansFont(11.5f, false));
        g.drawFittedText("Free software under the GNU General Public License v3: it comes with no warranty, "
                         "and its source code is available to everyone who receives it.\n"
                         "Not affiliated with or endorsed by Oberheim. Matrix-1000 is a trademark of its owner.",
                         x, 84, w, 46, juce::Justification::topLeft, 3, 1.0f);

        g.setFont(sansFont(10.5f, false));
        g.drawText("Click to close", getWidth() - 140, getHeight() - 22, 124, 14, juce::Justification::centredRight, false);
    }

    void StartProgressPanel::setProgress(const Stage stage, const float fraction)
    {
        const float f = juce::jlimit(0.0f, 1.0f, fraction);
        if(stage == m_stage && juce::approximatelyEqual(f, m_fraction))
            return;
        m_stage = stage;
        m_fraction = f;
        repaint();
    }

    void StartProgressPanel::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::black.withAlpha(0.62f));

        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre(470.0f, 112.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c1d20), 0.0f, box.getY(),
                                               juce::Colour(0xff111214), 0.0f, box.getBottom(), false));
        g.fillRoundedRectangle(box, 6.0f);
        g.setColour(juce::Colour(0xff3a3d43));
        g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);

        const float x = box.getX() + 22.0f, w = box.getWidth() - 44.0f;
        g.setColour(juce::Colour(rackPanel::kLabelBlue));
        g.fillRect(x, box.getY() + 14.0f, 150.0f, 3.5f);

        g.setColour(juce::Colour(kLegendWhite));
        g.setFont(sansFont(13.0f));
        g.drawText("First start", juce::Rectangle<float>(x, box.getY() + 23.0f, w, 18.0f), juce::Justification::centredLeft, false);

        g.setFont(sansFont(11.5f, false));
        g.drawText(m_stage == Stage::Calibrating ? "The firmware is tuning its six voices..."
                                                 : "Reading the factory patches from the patch EPROM...",
                   juce::Rectangle<float>(x, box.getY() + 44.0f, w - 44.0f, 16.0f), juce::Justification::centredLeft, false);
        g.drawText(juce::String(juce::roundToInt(m_fraction * 100.0f)) + " %",
                   juce::Rectangle<float>(x, box.getY() + 44.0f, w, 16.0f), juce::Justification::centredRight, false);

        const juce::Rectangle<float> track(x, box.getY() + 66.0f, w, 9.0f);
        g.setColour(juce::Colour(0xff0a0b0c));
        g.fillRoundedRectangle(track, 4.5f);
        g.setColour(juce::Colour(0xff34373c));
        g.drawRoundedRectangle(track.reduced(0.5f), 4.5f, 1.0f);
        if(m_fraction > 0.0f)
        {
            g.setColour(juce::Colour(rackPanel::kLabelBlue));
            g.fillRoundedRectangle(track.withWidth(juce::jmax(9.0f, track.getWidth() * m_fraction)).reduced(1.0f), 3.5f);
        }

        g.setColour(juce::Colour(0xffa8a8a8));
        g.setFont(sansFont(10.5f, false));
        g.drawText("This is done once: the next start is immediate.",
                   juce::Rectangle<float>(x, box.getY() + 84.0f, w, 14.0f), juce::Justification::centredLeft, false);
    }

    SevenSegmentDisplay::SevenSegmentDisplay()
    {
        setInterceptsMouseClicks(false, false);
    }

    void SevenSegmentDisplay::setText(const juce::String& text)
    {
        const juce::String t = text.paddedRight(' ', 3).substring(0, 3);
        if(t == m_text)
            return;
        m_text = t;
        repaint();
    }

    void SevenSegmentDisplay::setDot(const bool lit)
    {
        if(lit == m_dot)
            return;
        m_dot = lit;
        repaint();
    }

    void SevenSegmentDisplay::paint(juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff030303));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c0607), 0.0f, bounds.getY(),
                                               juce::Colour(0xff0d0203), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds.reduced(2.0f), 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

        const auto area = bounds.reduced(10.0f, 8.0f);
        const float cell = area.getWidth() / 3.0f;
        const float w = cell * 0.60f;
        const float h = area.getHeight();
        const float t = h * 0.13f;
        constexpr float gap = 1.2f;

        for(int i = 0; i < 3; ++i)
        {
            const float x0 = area.getX() + cell * static_cast<float>(i) + (cell - w) * 0.35f;
            const float y0 = area.getY();
            const float cx = x0 + w * 0.5f;
            const float hLen = w - t - gap;
            const float vLen = h * 0.5f - t * 0.5f - gap;
            const float upperY = y0 + h * 0.25f + t * 0.25f;
            const float lowerY = y0 + h * 0.75f - t * 0.25f;

            const std::array<juce::Path, 7> segments
            {
                horizontalSegment(cx, y0 + t * 0.5f, hLen, t),
                verticalSegment(x0 + w - t * 0.5f, upperY, vLen, t),
                verticalSegment(x0 + w - t * 0.5f, lowerY, vLen, t),
                horizontalSegment(cx, y0 + h - t * 0.5f, hLen, t),
                verticalSegment(x0 + t * 0.5f, lowerY, vLen, t),
                verticalSegment(x0 + t * 0.5f, upperY, vLen, t),
                horizontalSegment(cx, y0 + h * 0.5f, hLen, t)
            };

            // Slant the digit around its baseline
            const auto slant = juce::AffineTransform::translation(0.0f, -(y0 + h))
                                   .sheared(-0.10f, 0.0f)
                                   .translated(0.0f, y0 + h);

            const int bits = segmentsFor(m_text[i]);
            for(size_t s = 0; s < segments.size(); ++s)
            {
                juce::Path p = segments[s];
                p.applyTransform(slant);

                if((bits & (1 << s)) != 0)
                {
                    g.setColour(juce::Colour(kSegmentLit).withAlpha(0.22f));
                    g.strokePath(p, juce::PathStrokeType(3.0f));
                    g.setColour(juce::Colour(kSegmentLit));
                }
                else
                {
                    g.setColour(juce::Colour(kSegmentOff));
                }
                g.fillPath(p);
            }

            const bool dot = (i == 0 && m_dot);
            const float dx = x0 + w + t * 0.85f;
            const float dy = y0 + h - t * 0.5f;
            if(dot)
            {
                g.setColour(juce::Colour(kSegmentLit).withAlpha(0.22f));
                g.fillEllipse(dx - t, dy - t, t * 2, t * 2);
            }
            g.setColour(juce::Colour(dot ? kSegmentLit : kSegmentOff));
            g.fillEllipse(dx - t * 0.5f, dy - t * 0.5f, t, t);
        }
    }
}
