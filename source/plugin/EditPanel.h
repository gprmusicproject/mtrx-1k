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

        // Follows values changed from outside: the host, a controller, a program change.
        void refresh();

        // The loaded bank or program changed through the panel (save, revert).
        std::function<void()> onProgramChanged;

        void paint(juce::Graphics& g) override;
        void resized() override;

        struct Context;
        class Control;

    private:
        class SaveOverlay;

        struct Section
        {
            juce::Rectangle<int> bounds;
            juce::String title;
        };

        void addSections();
        void showSave();
        void revert();
        void init();

        HeadlessProcessor& m_proc;
        std::unique_ptr<Context> m_context;

        std::vector<Section> m_sections;
        std::vector<std::unique_ptr<Control>> m_controls;
        std::unique_ptr<juce::Button> m_saveBtn, m_revertBtn, m_initBtn;
        std::unique_ptr<SaveOverlay> m_saveOverlay;

        juce::String m_patchTitle;
        // The sound is the initialised patch, not the program the title names. Kept by the
        // processor, so it comes back with a session; this is what the header last drew.
        bool m_initialised = false;
        bool m_edited = false;
        SynthType m_boundCore = SynthType::None;
    };
}
