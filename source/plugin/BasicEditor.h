#pragma once

#include "HeadlessProcessor.h"
#include "RackPanel.h"
#include "EditPanel.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace retromulator
{
    class BasicEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
    {
    public:
        explicit BasicEditor(HeadlessProcessor& p);
        ~BasicEditor() override { stopTimer(); setLookAndFeel(nullptr); }

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        HeadlessProcessor& m_proc;

        // Declared before the components that use it so it outlives them.
        RackLookAndFeel m_lookAndFeel;

        // Everything is laid out at the design size on this sheet, which is scaled as a whole
        // to the size of the window: the layout stays the same at any size, and all but the
        // background of the rack unit is drawn as paths and text, sharp at any scale.
        class Canvas final : public juce::Component
        {
        public:
            std::function<void(juce::Graphics&)> onPaint;
            void paint(juce::Graphics& g) override { if(onPaint) onPaint(g); }
        };
        Canvas m_canvas;
        juce::ComponentBoundsConstrainer m_constrainer;
        double m_scale = 1.0;
        double m_savedScale = 1.0;
        int    m_scaleSaveTicks = 0;      // timer ticks until a new size is kept
        int    m_backgroundScale = 2;     // pixels of the background picture to a unit of the layout
        static constexpr double kMinScale = 0.6;
        static constexpr double kMaxScale = 2.0;
        // height of the layout: the rack unit, and the patch editor when it is open
        int  logicalHeight() const;
        void updateConstrainer();
        void paintPanel(juce::Graphics& g);
        void rebuildBackground();
        double fittingScale(int logicalH) const;

        juce::TextButton m_prevBtn  { "<" };
        juce::TextButton m_nextBtn  { ">" };
        juce::ComboBox m_progCombo;
        juce::ComboBox m_bankCombo;
        juce::Label    m_statusLabel;

        // ── Rack panel ───────────────────────────────────────────────────────
        // What the display shows and what the keypad and +/- act on.
        enum class PanelMode { Patch, Bank };

        juce::Image         m_background;
        VolumeKnob          m_volume;
        PanelButton         m_selectBtn;
        PanelButton         m_bankLockBtn;
        PanelButton         m_enterBtn;
        PanelButton         m_plusBtn  { "+" };
        PanelButton         m_minusBtn { juce::CharPointer_UTF8("\xe2\x88\x92") };
        std::array<std::unique_ptr<PanelButton>, 10> m_digitBtns;
        SevenSegmentDisplay m_display;
        PowerSwitch         m_power;
        ThemeSwitch         m_themeSwitch;
        LegendButton        m_aboutBtn { "About" };
        // Opens the user manual.
        LegendButton        m_manualBtn { "Manual" };
        AboutPanel          m_about;
        // Shown while the core starts for the first time.
        StartProgressPanel  m_startProgress;
        // Open the patch editor under the panel, or the effects or the presets in its place.
        juce::TextButton    m_editBtn { "Edit" };
        juce::TextButton    m_fxBtn { "FX" };
        LibraryButton       m_listBtn { "Presets" };
        // Marks the loaded patch as a favourite, or not.
        juce::TextButton    m_starBtn;
        EditPanel           m_editPanel;
        rackPanel::Theme    m_theme = rackPanel::Theme::Black;

        PanelMode    m_mode = PanelMode::Patch;
        // While locked, +/- stop at the ends of the loaded bank instead of moving to the next one.
        bool         m_bankLock = false;
        juce::String m_entry;            // digits typed so far
        int          m_entryTicks = 0;   // timer ticks until a half-typed number is dropped
        juce::String m_flash;            // short message that overrides the display
        int          m_flashTicks = 0;
        int          m_statusTicks = 0;
        // Core the power switch brings back.
        SynthType    m_powerOffType = SynthType::None;
        // A core was asked for and its firmware was not found; it has been unloaded since.
        bool         m_firmwareMissingLatch = false;
        std::array<bool, rackPanel::kLedCount> m_leds{};

        static constexpr int kImportId         = 9999;
        static constexpr int kExportPresetId   = 9998;
        static constexpr int kExportBankId     = 9997;
        static constexpr int kConvertToVirusB  = 9994;
        static constexpr int kConvertToVirusA  = 9993;
        // 88emu boards occupy 9100+, one per model: the bank combo picks the board
        // because the SC family has no bank files to list.
        static constexpr int kEmu88BoardFirst  = 9100;
        static constexpr int kMatrixLfo1Sync   = 9860;
        static constexpr int kMatrixLfo2Sync   = 9861;
        static constexpr int kMatrixLfoDivFirst = 9820;

        juce::String m_lastSysexPath;
        uint32_t     m_lastBankGeneration = 0;
        juce::String m_lastBankFolder;
        SynthType    m_lastSynthType = SynthType::None;
        juce::String m_currentBankFolder;

        std::shared_ptr<juce::FileChooser> m_fileChooser;

        static bool isFolderMode(SynthType type, const juce::File& synthFolder);

        void updateStatus();
        void refreshBankCombo();
        void onLoadSysex();
        void onExportPreset();
        void onExportBank();
        void onConvertVirusBank(char targetVersion);
        void showMatrixLfoSyncMenu(int lfo);
        void updateMatrixLfoSyncItems();
        void navigatePatch(int delta);
        // < > with a filter on the presets: the next patch it lets through, in this bank or
        // the next ones.
        void navigateFiltered(int delta);
        // A program of the loaded bank as the PATCH selector lists it: number, name, and a star
        // before a favourite.
        juce::String programItemText(int index, const juce::String& name) const;
        // what the PATCH selector was last filled from
        uint32_t m_presetRevision = 0;
        presets::Filter m_shownFilter;
        int m_shownProgram = -1;
        void navigateBankFolder(int delta);
        // Boots a core in the background, or switches the unit off with SynthType::None.
        void startCore(SynthType type);
        void onCoreStarted(SynthType type);
        void onFirmwareMissing(SynthType type);
        void openManual();
        void setTheme(rackPanel::Theme theme);
        // the panel under the unit, open or closed, with the patch or the effects
        void setPanelOpen(bool open, EditPanel::Page page);
        // the page the processor keeps open
        EditPanel::Page keptPage() const;

        void refreshPanel();
        void onPanelDigit(int digit);
        void onPanelStep(int delta);
        void commitPanelEntry();
        void flashPanel(const juce::String& text);
        void selectPatchNumber(int number);
        // Number shown for a program of the loaded bank: 001 upward as in the patch list,
        // or the hardware's 000-999 on the Matrix, where the hundreds digit is the bank.
        int  patchDisplayNumber(int index) const;
        int  matrixBankBase() const;
        // Bank combo entry for a Matrix bank digit, or 0: 2-9 are the ROM bank files,
        // 0 and 1 the first bank that is not one of them.
        int  matrixBankItemId(int bank) const;
        // Digits a patch number takes on the keypad.
        int  patchEntryLength() const;
        // Bank combo entries that load something, leaving out its menu commands.
        std::vector<int> bankItemIds() const;
        int  currentBankNumber() const;
        void selectBankNumber(int number);
        void stepBank(int delta);
        void onPowerSwitch();

        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BasicEditor)
    };
}
