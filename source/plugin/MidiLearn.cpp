#include "MidiLearn.h"

#include "HeadlessProcessor.h"
#include "ParameterPool.h"
#include "matrixLib/patch.h"
#include "synthLib/midiTypes.h"

#include <algorithm>

namespace retromulator
{
    namespace
    {
        // How long a data entry MSB waits for its LSB before it is taken alone. The two can
        // fall into different audio blocks, which is a block's length apart by the time they
        // are here.
        constexpr int kPendingMs = 40;
        constexpr int kTimerMs = 10;
        constexpr const char* kSettingsKey = "midiProfile";
        constexpr const char* kProfileTag  = "MidiProfile";
        constexpr const char* kStateTag    = "MidiMap";
        // what an exported profile says it is
        constexpr const char* kJsonFormat  = "MTRX-1K MIDI profile";
        constexpr int kJsonVersion = 1;

        // Controllers that cannot be assigned: bank select, the data entry and the NRPN and
        // RPN numbers, the channel mode messages, and those the instrument plays with itself
        // (the levers and pedals, the volume, the sustain pedal).
        bool learnable(const int cc)
        {
            switch(cc)
            {
            case 0: case 1: case 2: case 4: case 6: case 7: case 32: case 38: case 64:
                return false;
            default:
                return cc >= 0 && cc < 120 && !(cc >= 96 && cc <= 101);
            }
        }

        // how long the controls have to rest before what they told is written to the profile
        constexpr juce::uint32 kRangeSaveMs = 1500;

        juce::File settingsFile()
        {
            return juce::File(juce::String(HeadlessProcessor::getDataFolder()) + "settings.xml");
        }

        juce::String readSetting()
        {
            const auto file = settingsFile();
            if(file.existsAsFile())
                if(const auto xml = juce::XmlDocument::parse(file))
                    return xml->getStringAttribute(kSettingsKey);
            return {};
        }

        void writeSetting(const juce::String& value)
        {
            const auto file = settingsFile();
            std::unique_ptr<juce::XmlElement> xml;
            if(file.existsAsFile())
                xml = juce::XmlDocument::parse(file);
            if(!xml)
                xml = std::make_unique<juce::XmlElement>("RetromulatorSettings");
            if(xml->getStringAttribute(kSettingsKey) == value)
                return;
            xml->setAttribute(kSettingsKey, value);
            xml->writeTo(file);
        }
    }

    MidiLearn::MidiLearn(HeadlessProcessor& proc) : m_proc(proc)
    {
        // a new instance starts with the profile that was chosen last
        const auto last = readSetting();
        if(last.isEmpty() || !load(last))
            load(standardName());
    }

    MidiLearn::~MidiLearn()
    {
        stopTimer();
        if(m_rangeDirty)
            writeLearned();
    }

    // ── profiles ──────────────────────────────────────────────────────────────

    bool MidiLearn::isBuiltIn(const juce::String& name)
    {
        return name.equalsIgnoreCase(noneName()) || name.equalsIgnoreCase(standardName());
    }

    juce::File MidiLearn::profileFolder()
    {
        return juce::File(juce::String(HeadlessProcessor::getDataFolder())).getChildFile("MIDI");
    }

    juce::File MidiLearn::profileFile(const juce::String& name)
    {
        return profileFolder().getChildFile(juce::File::createLegalFileName(name) + ".xml");
    }

    juce::StringArray MidiLearn::getProfileNames() const
    {
        juce::StringArray names;
        juce::Array<juce::File> files;
        profileFolder().findChildFiles(files, juce::File::findFiles, false, "*.xml");
        for(const auto& f : files)
        {
            const auto name = f.getFileNameWithoutExtension();
            if(!isBuiltIn(name))
                names.add(name);
        }
        names.sortNatural();
        names.insert(0, noneName());
        names.insert(1, standardName());
        return names;
    }

    bool MidiLearn::load(const juce::String& name)
    {
        // what the profile in use still has to be told goes to it first
        if(m_rangeDirty)
            writeLearned();
        m_pending = false;

        if(name.equalsIgnoreCase(noneName()))
        {
            m_mappings.clear();
            m_profile = noneName();
        }
        else if(name.equalsIgnoreCase(standardName()))
        {
            buildStandard();
            m_profile = standardName();
        }
        else
        {
            const auto file = profileFile(name);
            if(!file.existsAsFile())
                return false;
            const auto xml = juce::XmlDocument::parse(file);
            if(!xml || !xml->hasTagName(kProfileTag) || !fromXml(*xml))
                return false;
            m_profile = file.getFileNameWithoutExtension();
        }
        m_modified = false;
        m_armed = -1;
        m_lastLearned.clear();
        changed(false);
        return true;
    }

    bool MidiLearn::selectProfile(const juce::String& name)
    {
        if(!load(name))
            return false;
        writeSetting(m_profile);
        return true;
    }

    juce::String MidiLearn::saveProfile(const juce::String& name)
    {
        const auto wanted = name.trim();
        if(wanted.isEmpty())
            return "Enter a name for the profile.";
        if(isBuiltIn(wanted))
            return "That name is taken by a built-in profile.";
        const auto legal = juce::File::createLegalFileName(wanted);
        if(legal.isEmpty())
            return "That name cannot be used for a file.";

        const auto file = profileFile(legal);
        if(!file.getParentDirectory().isDirectory() && !file.getParentDirectory().createDirectory())
            return "The profile folder could not be created.";
        auto xml = toXml(kProfileTag);
        xml->setAttribute("name", legal);
        if(!xml->writeTo(file))
            return "The profile could not be written.";

        m_profile = legal;
        m_modified = false;
        m_rangeDirty = false;
        writeSetting(m_profile);
        changed(false);
        return {};
    }

    bool MidiLearn::deleteProfile(const juce::String& name)
    {
        if(isBuiltIn(name))
            return false;
        const auto file = profileFile(name);
        if(file.existsAsFile() && !file.deleteFile())
            return false;
        // the profile in use is gone: nothing is assigned until another one is chosen
        if(m_profile.equalsIgnoreCase(name))
            selectProfile(noneName());
        else
            changed(false);
        return true;
    }

    juce::String MidiLearn::exportProfile(const juce::File& file) const
    {
        juce::Array<juce::var> maps;
        for(const auto& m : m_mappings)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty("type", m.nrpn ? "nrpn" : "cc");
            e->setProperty("number", static_cast<int>(m.number));
            e->setProperty("max", static_cast<int>(m.max));
            if(m.nrpn)
                e->setProperty("fine", m.fine);
            if(m.target >= kBusTargetFirst)
            {
                const int i = m.target - kBusTargetFirst;
                e->setProperty("bus", i / 3 + 1);
                e->setProperty("field", i % 3 == BusSource ? "source" : (i % 3 == BusAmount ? "amount" : "destination"));
            }
            else
            {
                e->setProperty("param", m.target);
                // for whoever reads the file; the number is what counts
                e->setProperty("name", targetName(m.target));
            }
            maps.add(juce::var(e));
        }

        auto* root = new juce::DynamicObject();
        const juce::var json(root);
        root->setProperty("format", kJsonFormat);
        root->setProperty("version", kJsonVersion);
        root->setProperty("name", m_profile);
        root->setProperty("mappings", maps);

        if(!file.getParentDirectory().isDirectory() && !file.getParentDirectory().createDirectory())
            return "The folder of the file could not be created.";
        if(!file.replaceWithText(juce::JSON::toString(json) + "\n"))
            return "The file could not be written.";
        return {};
    }

    juce::String MidiLearn::importProfile(const juce::File& file)
    {
        if(!file.existsAsFile())
            return "That file is not there.";
        juce::var json;
        if(juce::JSON::parse(file.loadFileAsString(), json).failed() || !json.isObject())
            return "That file is not JSON.";
        if(json["format"].toString() != kJsonFormat)
            return "That file is not a MIDI profile of MTRX-1K.";
        if(static_cast<int>(json["version"]) > kJsonVersion)
            return "That profile was written by a newer version of MTRX-1K.";
        const auto* list = json["mappings"].getArray();
        if(!list)
            return "That file holds no assignments.";

        // the same in the form of a profile on file, to be read by what reads those
        juce::XmlElement xml(kProfileTag);
        for(const auto& item : *list)
        {
            if(!item.isObject())
                continue;
            auto* e = xml.createNewChildElement("Map");
            e->setAttribute("type", item["type"].toString());
            for(const char* key : {"number", "max", "param", "bus"})
                if(item.hasProperty(key))
                    e->setAttribute(key, static_cast<int>(item[key]));
            if(item.hasProperty("fine"))
                e->setAttribute("fine", static_cast<bool>(item["fine"]) ? 1 : 0);
            if(item.hasProperty("field"))
                e->setAttribute("field", item["field"].toString());
        }

        // the name the file carries, or the file's own; never that of a profile that is there
        auto name = juce::File::createLegalFileName(json["name"].toString().trim());
        if(name.isEmpty())
            name = juce::File::createLegalFileName(file.getFileNameWithoutExtension().trim());
        if(name.isEmpty())
            name = "Imported";
        const auto base = name;
        for(int n = 2; isBuiltIn(name) || profileFile(name).existsAsFile(); ++n)
            name = base + " (" + juce::String(n) + ")";

        // what the profile in use still has to be told goes to it first
        if(m_rangeDirty)
            writeLearned();
        m_pending = false;

        const auto before = m_mappings;
        fromXml(xml);
        if(m_mappings.empty())
        {
            m_mappings = before;
            return "That file holds no assignment that can be used.";
        }
        const auto error = saveProfile(name);
        if(error.isNotEmpty())
        {
            m_mappings = before;
            return error;
        }
        m_armed = -1;
        m_lastLearned.clear();
        return {};
    }

    void MidiLearn::buildStandard()
    {
        m_mappings.clear();
        const auto* pool = m_proc.getParameterPool();
        if(!pool)
            return;
        for(int cc = 0; cc < 128; ++cc)
        {
            const int native = pool->getBuiltInNative(cc);
            if(native < 0 || !learnable(cc))
                continue;
            Mapping m;
            m.number = static_cast<uint16_t>(cc);
            m.target = native;
            m_mappings.push_back(m);
        }
    }

    // ── assignments ───────────────────────────────────────────────────────────

    const MidiLearn::Mapping* MidiLearn::findForTarget(const int target) const
    {
        for(const auto& m : m_mappings)
            if(m.target == target)
                return &m;
        return nullptr;
    }

    MidiLearn::Mapping* MidiLearn::find(const bool nrpn, const int number)
    {
        for(auto& m : m_mappings)
            if(m.nrpn == nrpn && m.number == number)
                return &m;
        return nullptr;
    }

    void MidiLearn::removeMapping(const size_t index)
    {
        if(index >= m_mappings.size())
            return;
        m_mappings.erase(m_mappings.begin() + static_cast<std::ptrdiff_t>(index));
        changed(true);
    }

    void MidiLearn::removeTarget(const int target)
    {
        const auto before = m_mappings.size();
        m_mappings.erase(std::remove_if(m_mappings.begin(), m_mappings.end(),
                                        [target](const Mapping& m) { return m.target == target; }), m_mappings.end());
        if(m_mappings.size() != before)
        {
            m_lastLearned = targetName(target) + ":  cleared";
            changed(true);
        }
    }

    void MidiLearn::clearAll()
    {
        if(m_mappings.empty())
            return;
        m_mappings.clear();
        m_pending = false;
        m_armed = -1;
        m_lastLearned.clear();
        changed(true);
    }

    void MidiLearn::relearnRange(const size_t index)
    {
        if(index >= m_mappings.size())
            return;
        m_mappings[index].max = 0;
        changed(true);
    }

    juce::String MidiLearn::targetName(const int target)
    {
        if(target >= kBusTargetFirst)
        {
            static const char* const fields[3] = {"Source", "Amount", "Destination"};
            const int i = target - kBusTargetFirst;
            return "Matrix " + juce::String(i / 3 + 1) + " " + fields[i % 3];
        }
        if(const auto* p = matrixLib::patch::findParam(static_cast<uint8_t>(target)))
            return p->name;
        return "Parameter " + juce::String(target);
    }

    juce::String MidiLearn::controllerName(const Mapping& m, const bool brief)
    {
        if(m.nrpn)
            return (brief ? "N " : "NRPN ") + juce::String(static_cast<int>(m.number));
        return "CC " + juce::String(static_cast<int>(m.number));
    }

    void MidiLearn::assign(Mapping m)
    {
        // one control for one parameter: whatever either had before goes
        m_mappings.erase(std::remove_if(m_mappings.begin(), m_mappings.end(), [&m](const Mapping& o)
        {
            return o.target == m.target || (o.nrpn == m.nrpn && o.number == m.number);
        }), m_mappings.end());
        m_mappings.push_back(m);
        m_lastLearned = targetName(m.target) + " = " + controllerName(m);
        m_armed = -1;
        changed(true);
    }

    void MidiLearn::changed(const bool modifies)
    {
        if(modifies)
            m_modified = true;
        refreshWanted();
        ++m_revision;
    }

    // Something the control itself told: that its value has an LSB, or how far it goes. It
    // belongs to the profile, so a saved profile that is otherwise untouched is written again.
    void MidiLearn::learnedMore()
    {
        if(!m_modified && !isBuiltIn(m_profile))
        {
            // not at every step of a control that is being turned: once it rests
            m_rangeDirty = true;
            m_rangeChanged = juce::Time::getMillisecondCounter();
            if(!isTimerRunning())
                startTimer(kTimerMs);
        }
        else
        {
            m_modified = true;
        }
        ++m_revision;
    }

    void MidiLearn::writeLearned()
    {
        m_rangeDirty = false;
        if(m_modified || isBuiltIn(m_profile))
            return;
        auto xml = toXml(kProfileTag);
        xml->setAttribute("name", m_profile);
        if(!xml->writeTo(profileFile(m_profile)))
        {
            m_modified = true;
            ++m_revision;
        }
    }

    void MidiLearn::refreshWanted()
    {
        std::array<bool, 128> wanted{};
        bool nrpn = m_learning;
        for(const auto& m : m_mappings)
        {
            if(m.nrpn)
                nrpn = true;
            else
                wanted[m.number & 0x7f] = true;
        }
        // with NRPNs in use their numbers and the data entry are ours, and so are the RPN
        // numbers, which say that a data entry is not
        if(nrpn)
            for(const int cc : {6, 38, 98, 99, 100, 101})
                wanted[static_cast<size_t>(cc)] = true;
        for(size_t i = 0; i < wanted.size(); ++i)
            m_wanted[i].store(wanted[i], std::memory_order_relaxed);
    }

    // ── learning ──────────────────────────────────────────────────────────────

    void MidiLearn::setLearning(const bool on)
    {
        if(on == m_learning)
            return;
        m_learning = on;
        m_armed = -1;
        m_lastLearned.clear();
        m_refused.clear();
        changed(false);
    }

    void MidiLearn::arm(const int target)
    {
        if(!m_learning || target == m_armed)
            return;
        m_armed = target;
        m_lastLearned.clear();
        m_refused.clear();
        ++m_revision;
    }

    juce::String MidiLearn::getStatus() const
    {
        if(!m_learning)
            return {};
        if(m_armed >= 0)
            return m_refused.isNotEmpty() ? m_refused : targetName(m_armed) + ":  move a control";
        if(m_lastLearned.isNotEmpty())
            return m_lastLearned;
        return "LEARN:  click a parameter";
    }

    // ── MIDI ──────────────────────────────────────────────────────────────────

    bool MidiLearn::handleController(const synthLib::SMidiEvent& ev)
    {
        if(!ev.sysex.empty() || (ev.a & 0xf0) != synthLib::M_CONTROLCHANGE)
            return false;
        if(ev.source != synthLib::MidiEventSource::Host && ev.source != synthLib::MidiEventSource::Physical)
            return false;

        const int channel = ev.a & 0x0f;
        const int cc = ev.b & 0x7f;
        const int value = ev.c & 0x7f;
        auto& st = m_nrpn[static_cast<size_t>(channel)];

        // the monitor keeps what came, whatever becomes of it
        std::move(m_trail.begin() + 1, m_trail.end(), m_trail.begin());
        m_trail.back() = static_cast<uint16_t>((cc << 8) | value);
        ++m_seenCount;

        switch(cc)
        {
        case 99:
        case 98:
            // a value still waiting for its LSB belongs to the number before
            flushPending();
            (cc == 99 ? st.msb : st.lsb) = static_cast<uint8_t>(value);
            st.rpn = false;
            st.selected = !(st.msb == 127 && st.lsb == 127);
            return true;
        case 101:
        case 100:
            flushPending();
            st.rpn = true;
            return true;
        case 6:
            if(st.rpn || !st.selected)
            {
                m_seen = {Seen::NotOurs, true, 0, value, 0, -1};
                return false;
            }
            // the LSB keeps what it had: see NrpnState
            st.dataMsb = static_cast<uint8_t>(value);
            onNrpn(channel, false);
            return true;
        case 38:
            if(st.rpn || !st.selected)
            {
                m_seen = {Seen::NotOurs, true, 0, value, 0, -1};
                return false;
            }
            st.dataLsb = static_cast<uint8_t>(value);
            onNrpn(channel, true);
            return true;
        case 96:
        case 97:
            return false;
        default:
            return onController(cc, value);
        }
    }

    bool MidiLearn::onController(const int cc, const int value)
    {
        if(m_learning && m_armed >= 0)
        {
            if(learnable(cc))
            {
                Mapping m;
                m.number = static_cast<uint16_t>(cc);
                m.target = m_armed;
                assign(m);
            }
            else
            {
                // a lever, a pedal or the volume: say why nothing is assigned
                const auto refused = "CC " + juce::String(cc) + " is the instrument's own";
                if(refused != m_refused)
                {
                    m_refused = refused;
                    ++m_revision;
                }
            }
        }
        if(auto* m = find(false, cc))
        {
            emit(*m, value);
            return true;
        }
        m_seen = {Seen::Unassigned, false, cc, value, 0, -1};
        return false;
    }

    void MidiLearn::onNrpn(const int channel, const bool lsbMessage)
    {
        const auto& st = m_nrpn[static_cast<size_t>(channel)];
        const int number = (st.msb << 7) | st.lsb;

        bool fresh = false;
        if(m_learning && m_armed >= 0)
        {
            Mapping m;
            m.nrpn = true;
            m.fine = lsbMessage;
            // a value in two parts can run to anything, 60 as well as 16383: its range is
            // learned; one that comes as an MSB alone is taken for 0 to 127 until it says otherwise
            m.max = static_cast<uint16_t>(lsbMessage ? 0 : 127);
            m.number = static_cast<uint16_t>(number);
            m.target = m_armed;
            assign(m);
            fresh = true;
        }

        auto* m = find(true, number);
        if(!m)
        {
            m_seen = {Seen::Unassigned, true, number, lsbMessage ? (st.dataMsb << 7) | st.dataLsb : st.dataMsb, 0, -1};
            return;
        }

        // the first LSB shows that this control sends its value in two parts
        if(lsbMessage && !m->fine)
        {
            m->fine = true;
            m->max = 0;
            learnedMore();
        }

        if(lsbMessage)
        {
            m_pending = false;
            emit(*m, (st.dataMsb << 7) | st.dataLsb);
        }
        else if(m->fine || fresh)
        {
            // Taken at once, the MSB alone would be a value the control never meant: wait
            // for the LSB, and only if none comes take the MSB by itself. The same for an
            // assignment made this moment, of which it is not known yet whether an LSB follows.
            m_pending = true;
            m_pendingChannel = channel;
            m_pendingNumber = number;
            m_pendingSince = juce::Time::getMillisecondCounter();
            if(!isTimerRunning())
                startTimer(kTimerMs);
            m_seen = {Seen::Waiting, true, number, st.dataMsb, m->max, m->target};
        }
        else
        {
            emit(*m, st.dataMsb);
        }
    }

    void MidiLearn::flushPending()
    {
        if(!m_pending)
            return;
        m_pending = false;
        const auto& st = m_nrpn[static_cast<size_t>(m_pendingChannel)];
        auto* m = find(true, m_pendingNumber);
        if(!m)
            return;
        if(!m->fine)
        {
            emit(*m, st.dataMsb);
            return;
        }
        // The second part never came, and the one at hand is of the value before: with a
        // new MSB it makes a number the control never sent, 255 on the way from 127 to 128.
        // It is taken for what it is worth, but not as news of how far the control goes.
        if(m->max == 0)
            return;
        const int value = std::min((st.dataMsb << 7) | st.dataLsb, static_cast<int>(m->max));
        m_seen = {Seen::Applied, true, m->number, value, m->max, m->target};
        apply(*m, value);
    }

    void MidiLearn::timerCallback()
    {
        const auto now = juce::Time::getMillisecondCounter();
        if(m_pending && now - m_pendingSince >= static_cast<juce::uint32>(kPendingMs))
            flushPending();
        // what the controls told about themselves goes into the profile once they rest
        if(m_rangeDirty && now - m_rangeChanged >= kRangeSaveMs)
            writeLearned();
        if(!m_pending && !m_rangeDirty)
            stopTimer();
    }

    void MidiLearn::emit(Mapping& m, const int value)
    {
        // The range of a control is what it sends: the largest value seen so far is its top.
        // One full turn of the control teaches it.
        if(value > m.max)
        {
            m.max = static_cast<uint16_t>(std::min(value, static_cast<int>(kMaxLimit)));
            learnedMore();
        }
        m_seen = {Seen::Applied, m.nrpn, m.number, value, m.max, m.target};
        apply(m, value);
    }

    juce::String MidiLearn::getMonitorText() const
    {
        if(m_seenCount == 0)
            return "nothing yet from an assigned control";

        // the control changes as they came, newest last
        juce::String text;
        const size_t n = std::min<size_t>(m_seenCount, kTrail);
        for(size_t i = kTrail - n; i < kTrail; ++i)
            text << static_cast<int>(m_trail[i] >> 8) << ":" << static_cast<int>(m_trail[i] & 0x7f) << "  ";

        const auto name = (m_seen.nrpn ? "NRPN " : "CC ") + juce::String(m_seen.number);
        switch(m_seen.kind)
        {
        case Seen::Applied:
            text << "=  " << name << " at " << m_seen.value << " of 0 - " << m_seen.max << "  >  " << targetName(m_seen.target);
            break;
        case Seen::Waiting:
            text << "=  " << name << ", first part " << m_seen.value << ": waiting for the second";
            break;
        case Seen::Unassigned:
            text << "=  " << name << " at " << m_seen.value << ": not assigned";
            break;
        case Seen::NotOurs:
            text << "=  data entry that belongs to no NRPN";
            break;
        case Seen::Nothing:
            break;
        }
        return text + "   (" + juce::String(m_seenCount) + ")";
    }

    void MidiLearn::apply(const Mapping& m, const int value) const
    {
        auto* pool = m_proc.getParameterPool();
        if(!pool || pool->getCore() != SynthType::Matrix)
            return;
        if(m.target >= kBusTargetFirst)
        {
            const int i = m.target - kBusTargetFirst;
            pool->setBusFromController(i / 3, i % 3, value, m.max);
        }
        else
        {
            pool->setFromController(m.target, value, m.max);
        }
    }

    // ── files and session ─────────────────────────────────────────────────────

    std::unique_ptr<juce::XmlElement> MidiLearn::toXml(const char* tag) const
    {
        auto xml = std::make_unique<juce::XmlElement>(tag);
        xml->setAttribute("version", 1);
        for(const auto& m : m_mappings)
        {
            auto* e = xml->createNewChildElement("Map");
            e->setAttribute("type", m.nrpn ? "nrpn" : "cc");
            e->setAttribute("number", static_cast<int>(m.number));
            e->setAttribute("max", static_cast<int>(m.max));
            if(m.nrpn)
                e->setAttribute("fine", m.fine ? 1 : 0);
            if(m.target >= kBusTargetFirst)
            {
                const int i = m.target - kBusTargetFirst;
                e->setAttribute("bus", i / 3 + 1);
                e->setAttribute("field", i % 3 == BusSource ? "source" : (i % 3 == BusAmount ? "amount" : "destination"));
            }
            else
            {
                e->setAttribute("param", m.target);
                // for whoever reads the file; the number is what counts
                e->setAttribute("name", targetName(m.target));
            }
        }
        return xml;
    }

    bool MidiLearn::fromXml(const juce::XmlElement& xml)
    {
        std::vector<Mapping> maps;
        for(const auto* e : xml.getChildWithTagNameIterator("Map"))
        {
            Mapping m;
            m.nrpn = e->getStringAttribute("type") == "nrpn";
            const int number = e->getIntAttribute("number", -1);
            if(number < 0 || number > (m.nrpn ? static_cast<int>(kMaxLimit) : 127))
                continue;
            m.number = static_cast<uint16_t>(number);
            m.fine = m.nrpn && e->getIntAttribute("fine", 0) != 0;
            // 0 = still to be learned from the control
            m.max = static_cast<uint16_t>(juce::jlimit(0, m.nrpn ? static_cast<int>(kMaxLimit) : 127, e->getIntAttribute("max", 127)));

            if(e->hasAttribute("bus"))
            {
                const int bus = e->getIntAttribute("bus", 0) - 1;
                const auto field = e->getStringAttribute("field");
                const int f = field == "source" ? BusSource : (field == "amount" ? BusAmount : (field == "destination" ? BusDestination : -1));
                if(bus < 0 || bus >= ParameterPool::MatrixBusCount || f < 0)
                    continue;
                m.target = kBusTargetFirst + bus * 3 + f;
            }
            else
            {
                m.target = e->getIntAttribute("param", -1);
                if(m.target < 0 || !matrixLib::patch::findParam(static_cast<uint8_t>(m.target)))
                    continue;
            }

            // a file put together by hand may assign something twice: the first stands
            const bool taken = std::any_of(maps.begin(), maps.end(), [&m](const Mapping& o)
            {
                return o.target == m.target || (o.nrpn == m.nrpn && o.number == m.number);
            });
            if(!taken)
                maps.push_back(m);
        }
        m_mappings = std::move(maps);
        return true;
    }

    juce::String MidiLearn::toStateString() const
    {
        auto xml = toXml(kStateTag);
        xml->setAttribute("profile", m_profile);
        xml->setAttribute("modified", m_modified ? 1 : 0);
        return xml->toString(juce::XmlElement::TextFormat().singleLine());
    }

    void MidiLearn::fromStateString(const juce::String& state)
    {
        if(m_rangeDirty)
            writeLearned();
        m_pending = false;
        const auto xml = state.isNotEmpty() ? juce::XmlDocument::parse(state) : nullptr;
        if(!xml || !xml->hasTagName(kStateTag) || !fromXml(*xml))
        {
            load(standardName());
            return;
        }
        m_profile = xml->getStringAttribute("profile", noneName());
        m_modified = xml->getIntAttribute("modified", 0) != 0;
        m_armed = -1;
        m_lastLearned.clear();
        changed(false);
    }
}
