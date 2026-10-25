#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace synthLib { struct SMidiEvent; }

// MIDI learn for the patch parameters of the Matrix core: which control of the player's
// keyboard moves which parameter. An assignment is made by picking a parameter and moving
// the control; whether that control sends control changes or NRPNs is taken from what
// arrives. A set of assignments is a profile, kept as a file under a name of the player's
// choice. The assignments in use travel with the session.

namespace retromulator
{
    class HeadlessProcessor;

    class MidiLearn final : private juce::Timer
    {
    public:
        struct Mapping
        {
            bool     nrpn = false;
            bool     fine = false;     // NRPN whose value comes as data entry MSB and LSB
            uint16_t number = 0;       // controller 0-127 or NRPN 0-16383
            // Largest value the control sends, learned from the largest it has sent: a control
            // can stop anywhere, at 60 as well as at 16383. 0 = nothing seen yet.
            uint16_t max = 127;
            int      target = -1;
        };

        // A target is the number of a patch parameter (0-99), or one field of a bus of the
        // modulation matrix: kBusTargetFirst + bus * 3 + field.
        static constexpr int kBusTargetFirst = 256;
        // The stereo spread of the voices, a setting of the instance and not a patch
        // parameter: a number between the parameters and the buses.
        static constexpr int kSpreadTarget = 200;
        // the detune of the voices against each other, the firmware's controller 94, a setting
        // likewise; not the detune of DCO 2, which is a patch parameter
        static constexpr int kDetuneTarget = 201;
        enum BusField { BusSource = 0, BusAmount = 1, BusDestination = 2 };
        static constexpr uint16_t kMaxLimit = 16383;

        explicit MidiLearn(HeadlessProcessor& proc);
        ~MidiLearn() override;

        // ── profiles ──
        // No assignment at all, and the map the plugin always had, one CC per parameter.
        static juce::String noneName()     { return "None"; }
        static juce::String standardName() { return "Standard"; }
        static bool isBuiltIn(const juce::String& name);

        // "None", "Standard", then the saved profiles in alphabetical order.
        juce::StringArray getProfileNames() const;
        const juce::String& getProfileName() const { return m_profile; }
        // The assignments in use differ from the profile they were taken from.
        bool isModified() const { return m_modified; }

        // Takes the assignments of a profile. Changes that were not saved are dropped.
        bool selectProfile(const juce::String& name);
        // Writes the assignments in use under a name; an existing profile of that name is
        // replaced. Returns nothing when it worked, otherwise what went wrong.
        juce::String saveProfile(const juce::String& name);
        bool deleteProfile(const juce::String& name);

        // ── exchange ──
        // Writes the assignments in use to a JSON file, under the name of their profile,
        // to be kept or passed on. Returns nothing when it worked, otherwise what went wrong.
        juce::String exportProfile(const juce::File& file) const;
        // Reads a file written by exportProfile and keeps it as a saved profile, which
        // becomes the one in use. It takes the name the file carries; where a profile of
        // that name is there already a number is added to it, so that nothing is replaced.
        // Returns nothing when it worked, otherwise what went wrong.
        juce::String importProfile(const juce::File& file);

        // ── assignments ──
        const std::vector<Mapping>& getMappings() const { return m_mappings; }
        const Mapping* findForTarget(int target) const;
        void removeMapping(size_t index);
        void removeTarget(int target);
        // Removes every assignment at once. The profile on file is not touched.
        void clearAll();
        // Forgets how far a control goes: it is learned again from what the control sends.
        void relearnRange(size_t index);

        static juce::String targetName(int target);
        // "CC 74", "NRPN 1234"
        static juce::String controllerName(const Mapping& m, bool brief = false);

        // ── learning ──
        void setLearning(bool on);
        bool isLearning() const { return m_learning; }
        // The parameter the next control that moves is assigned to, or -1 for none.
        void arm(int target);
        int  getArmed() const { return m_armed; }
        // What the learning is waiting for or has just done, for the header.
        juce::String getStatus() const;

        // Goes up whenever something the editor shows has changed.
        uint32_t getRevision() const { return m_revision; }

        // ── monitor ──
        // The last control changes that reached the assignments, as they came, and what was
        // made of them: which control, which value, of what range, for which parameter.
        juce::String getMonitorText() const;
        // Goes up with every control change that came in.
        uint32_t getMonitorCount() const { return m_seenCount; }

        // ── MIDI ──
        // Audio thread. True for a controller the assignments take: it is kept from the
        // firmware and handed to handleController alone.
        bool wantsController(int cc) const noexcept
        {
            return m_wanted[static_cast<size_t>(cc & 0x7f)].load(std::memory_order_relaxed);
        }
        // Message thread. Every control change that came in, in the order it came.
        bool handleController(const synthLib::SMidiEvent& ev);

        // ── session ──
        juce::String toStateString() const;
        // An empty or broken string leaves the standard map, which is what a session saved
        // before profiles existed had.
        void fromStateString(const juce::String& state);

    private:
        // What a channel last said. A host that hands controllers over as parameter changes
        // may leave out one whose value has not changed: the number of NRPN 0 (two zeros) on
        // a fresh instance, the MSB of a value that stays under 128, the LSB when only the
        // MSB moves. So everything starts at zero and counts as said, and a part that does
        // not come keeps the value it had.
        struct NrpnState
        {
            uint8_t msb = 0, lsb = 0;
            uint8_t dataMsb = 0, dataLsb = 0;
            bool selected = true;    // false after the null number (127, 127)
            bool rpn = false;        // the number given last was an RPN: its data is not ours
        };

        // What the last control change came to.
        struct Seen
        {
            enum Kind { Nothing, Applied, Waiting, Unassigned, NotOurs };
            Kind kind = Nothing;
            bool nrpn = false;
            int  number = 0;
            int  value = 0;
            int  max = 0;
            int  target = -1;
        };

        void timerCallback() override;

        bool onController(int cc, int value);
        void onNrpn(int channel, bool lsbMessage);
        void flushPending();
        void assign(Mapping m);
        void emit(Mapping& m, int value);
        void apply(const Mapping& m, int value) const;
        Mapping* find(bool nrpn, int number);

        bool load(const juce::String& name);
        void buildStandard();
        void changed(bool modifies);
        void learnedMore();
        void writeLearned();
        void refreshWanted();

        std::unique_ptr<juce::XmlElement> toXml(const char* tag) const;
        bool fromXml(const juce::XmlElement& xml);
        static juce::File profileFolder();
        static juce::File profileFile(const juce::String& name);

        HeadlessProcessor& m_proc;

        std::vector<Mapping> m_mappings;
        juce::String m_profile;
        bool m_modified = false;

        bool m_learning = false;
        int  m_armed = -1;
        juce::String m_lastLearned;
        juce::String m_refused;       // why the control that moved was not assigned
        uint32_t m_revision = 1;

        std::array<NrpnState, 16> m_nrpn{};
        // a data entry MSB of a fine NRPN waits here for its LSB
        bool m_pending = false;
        int  m_pendingChannel = 0;
        int  m_pendingNumber = 0;
        juce::uint32 m_pendingSince = 0;
        // a range was learned that the saved profile does not hold yet
        bool m_rangeDirty = false;
        juce::uint32 m_rangeChanged = 0;

        // the monitor: the last control changes, controller in the high byte, newest last
        static constexpr size_t kTrail = 6;
        std::array<uint16_t, kTrail> m_trail{};
        uint32_t m_seenCount = 0;
        Seen m_seen;

        std::array<std::atomic<bool>, 128> m_wanted{};
    };
}
