#pragma once

#include "HeadlessProcessor.h"
#include "RackPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

// Patch editor of the GPL editor: every parameter of a Matrix-1000 patch and its modulation
// matrix, laid out as sections under the rack unit and drawn in the finish of its panel.
// The controls are views of the host parameter slots, so what they change is automatable,
// follows incoming controllers and is saved with the session.

namespace retromulator
{
    class EditPanel final : public juce::Component
    {
    public:
        static constexpr int kHeight = 562;

        explicit EditPanel(HeadlessProcessor& proc);
        ~EditPanel() override;

        void setTheme(rackPanel::Theme theme);

        // Number and name of the loaded program, shown in the header.
        void setPatchTitle(const juce::String& title);

        // Three pages in the same place: the patch, the effects that follow the instrument, and
        // the presets to choose from. The header is the same for all: the effects are saved with
        // the patch.
        enum class Page { Patch, Effects, Presets };
        void setPage(Page page);
        Page getPage() const { return m_page; }

        // Follows values changed from outside: the host, a controller, a program change.
        void refresh();

        // The loaded bank or program changed through the panel (save, revert).
        std::function<void()> onProgramChanged;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void visibilityChanged() override;

        struct Context;
        class Control;

    private:
        class SaveOverlay;
        class LearnLayer;
        class MidiOverlay;
        class PresetList;

        struct Section
        {
            juce::Rectangle<int> bounds;
            juce::String title;
        };

        void addSections();
        void addEffects();
        void paintEffects(juce::Graphics& g);
        void addPresets();
        void paintPresets(juce::Graphics& g);
        // the controls of the page that is shown
        const std::vector<std::unique_ptr<Control>>& pageControls() const { return m_page == Page::Patch ? m_controls : m_page == Page::Effects ? m_fxControls : m_presetControls; }
        void showSave();
        void revert();
        void init();
        // Learning of controller assignments on or off; the controls are covered while it is on.
        void setLearning(bool on);

        HeadlessProcessor& m_proc;
        std::unique_ptr<Context> m_context;

        std::vector<Section> m_sections;
        std::vector<std::unique_ptr<Control>> m_controls;
        std::vector<std::unique_ptr<Control>> m_fxControls;
        std::vector<std::unique_ptr<Control>> m_presetControls;
        std::unique_ptr<PresetList> m_presetList;
        std::unique_ptr<juce::TextEditor> m_presetSearch;
        void styleSearch();
        Page m_page = Page::Patch;
        std::unique_ptr<juce::Button> m_saveBtn, m_revertBtn, m_initBtn;
        std::unique_ptr<SaveOverlay> m_saveOverlay;

        // MIDI learn: its two keys, the layer over the controls and the page of the profiles
        std::unique_ptr<juce::Button> m_learnBtn, m_midiBtn;
        std::unique_ptr<LearnLayer> m_learnLayer;
        std::unique_ptr<MidiOverlay> m_midiOverlay;
        uint32_t m_learnRevision = 0;     // what of the MIDI learn the panel last drew
        uint32_t m_monitorCount = 0;
        int m_fxLit = -1;                // the chain and racks that play, as the effects page last drew them
        juce::String m_learnStatus, m_profileText;

        juce::String m_patchTitle;
        // The sound is the initialised patch, not the program the title names. Kept by the
        // processor, so it comes back with a session; this is what the header last drew.
        bool m_initialised = false;
        bool m_edited = false;
        SynthType m_boundCore = SynthType::None;
    };
}
