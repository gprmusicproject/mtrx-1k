#include "PresetLibrary.h"

#include "HeadlessProcessor.h"

#include <juce_core/juce_core.h>

#include <unordered_map>

namespace retromulator::presets
{
    namespace
    {
        struct Entry
        {
            uint32_t name;       // FNV-1a of the patch name
            Category category;
        };
        #include "PresetCategories.inc"

        const char* const g_names[CategoryCount] = {"", "Pad", "Lead", "Bass", "Brass", "Strings", "Poly", "Keys", "FX"};
        constexpr const char* kSignatureBank = "GPR Music Project Signature";
        constexpr const char* kRomBankStart = "!ROM Bank ";   // as the processor names the ROM banks
        constexpr const char* kRamBank = "Matrix 1000 RAM";   // and the RAM bank, which until 1.0.0 alpha 4
        constexpr const char* kRamBankBefore = "Oberheim Matrix-1000";   // was named after the instrument

        uint32_t fingerprint(const std::string& _name)
        {
            uint32_t h = 0x811c9dc5u;
            for(const unsigned char c : _name)
                h = (h ^ c) * 0x01000193u;
            return h;
        }

        // the factory category of a patch name, wherever it is: none when patches of that
        // name are of different kinds
        Category byName(const std::string& _name)
        {
            static const std::unordered_map<uint32_t, Category> names = []
            {
                std::unordered_map<uint32_t, Category> m;
                std::unordered_map<uint32_t, bool> mixed;
                for(const auto& e : kFactory)
                {
                    const auto [it, added] = m.emplace(e.name, e.category);
                    if(!added && it->second != e.category)
                        mixed[e.name] = true;
                }
                for(const auto& [name, _] : mixed)
                    m[name] = Category::None;
                return m;
            }();
            const auto it = names.find(fingerprint(_name));
            return it == names.end() ? Category::None : it->second;
        }

        juce::File libraryFile() { return juce::File(juce::String(HeadlessProcessor::getDataFolder()) + "presets.xml"); }
        std::string keyOf(const std::string& _bank, const int _slot) { return _bank + "\t" + std::to_string(_slot); }
    }

    const char* categoryName(const Category _category)
    {
        const auto i = static_cast<int>(_category);
        return i >= 0 && i < CategoryCount ? g_names[i] : "";
    }

    Library& Library::instance()
    {
        static Library library;
        return library;
    }

    Category Library::factoryCategory(const std::string& _bank, const int _slot, const std::string& _name)
    {
        if(_slot < 0)
            return Category::None;
        const auto hash = fingerprint(_name);
        // a ROM bank holds the factory patches by number, when they are the ones of the table
        const std::string rom = kRomBankStart;
        if(_bank.size() == rom.size() + 1 && _bank.compare(0, rom.size(), rom) == 0)
        {
            const int bank = _bank.back() - '0';
            const int index = (bank - 2) * 100 + _slot;
            if(bank >= 2 && bank <= 9 && _slot < 100 && kFactory[index].name == hash)
                return kFactory[index].category;
        }
        if(_bank == kSignatureBank && _slot < static_cast<int>(std::size(kSignature)) && kSignature[_slot].name == hash)
            return kSignature[_slot].category;
        // the RAM bank with the contents it usually has
        if(_bank == kRamBank && _slot < static_cast<int>(std::size(kRam)) && kRam[_slot].name == hash)
            return kRam[_slot].category;
        // a factory patch somewhere else
        return byName(_name);
    }

    const Library::Entry* Library::find(const std::string& _bank, const int _slot, const std::string& _name)
    {
        reloadIfChanged();
        const auto it = m_entries.find(keyOf(_bank, _slot));
        return it != m_entries.end() && it->second.name == _name ? &it->second : nullptr;
    }

    Library::Entry& Library::place(const std::string& _bank, const int _slot, const std::string& _name)
    {
        reloadIfChanged();
        auto& e = m_entries[keyOf(_bank, _slot)];
        // the place holds another patch now: what was set for the old one goes
        if(e.name != _name)
            e = Entry{_name};
        return e;
    }

    Category Library::category(const std::string& _bank, const int _slot, const std::string& _name)
    {
        std::lock_guard lock(m_mutex);
        if(const auto* e = find(_bank, _slot, _name); e && e->hasCategory)
            return e->category;
        return factoryCategory(_bank, _slot, _name);
    }

    bool Library::isFavourite(const std::string& _bank, const int _slot, const std::string& _name)
    {
        std::lock_guard lock(m_mutex);
        const auto* e = find(_bank, _slot, _name);
        return e && e->favourite;
    }

    void Library::setCategory(const std::string& _bank, const int _slot, const std::string& _name, const Category _category)
    {
        std::lock_guard lock(m_mutex);
        auto& e = place(_bank, _slot, _name);
        // the factory category is no setting of the player's
        e.hasCategory = _category != factoryCategory(_bank, _slot, _name);
        e.category = _category;
        save();
    }

    void Library::setFavourite(const std::string& _bank, const int _slot, const std::string& _name, const bool _favourite)
    {
        std::lock_guard lock(m_mutex);
        place(_bank, _slot, _name).favourite = _favourite;
        save();
    }

    void Library::carry(const std::string& _fromBank, const int _fromSlot, const std::string& _fromName,
                        const std::string& _toBank, const int _toSlot, const std::string& _toName)
    {
        std::lock_guard lock(m_mutex);
        const auto category = this->category(_fromBank, _fromSlot, _fromName);
        const bool favourite = _fromBank == _toBank && _fromSlot == _toSlot && isFavourite(_fromBank, _fromSlot, _fromName);
        auto& e = place(_toBank, _toSlot, _toName);
        e = Entry{_toName};
        e.hasCategory = category != factoryCategory(_toBank, _toSlot, _toName);
        e.category = category;
        e.favourite = favourite;
        save();
    }

    void Library::reloadIfChanged()
    {
        const auto file = libraryFile();
        const int64_t time = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : 0;
        if(time == m_fileTime)
            return;
        m_fileTime = time;
        m_entries.clear();
        ++m_revision;
        const auto xml = juce::XmlDocument::parse(file);
        if(!xml || !xml->hasTagName("MtrxPresets"))
            return;
        for(const auto* p : xml->getChildWithTagNameIterator("Patch"))
        {
            Entry e{p->getStringAttribute("name").toStdString()};
            const auto category = p->getStringAttribute("category");
            for(int c = 1; c < CategoryCount; ++c)
                if(category == g_names[c])
                {
                    e.category = static_cast<Category>(c);
                    e.hasCategory = true;
                }
            if(category == "none")
                e.hasCategory = true;
            e.favourite = p->getBoolAttribute("favourite");
            // what was set in the RAM bank under its old name holds under the new one
            auto bank = p->getStringAttribute("bank").toStdString();
            if(bank == kRamBankBefore)
                bank = kRamBank;
            if(e.hasCategory || e.favourite)
                m_entries[keyOf(bank, p->getIntAttribute("slot"))] = e;
        }
    }

    void Library::save()
    {
        ++m_revision;
        juce::XmlElement xml("MtrxPresets");
        xml.setAttribute("version", 1);
        for(const auto& [key, e] : m_entries)
        {
            if(!e.hasCategory && !e.favourite)
                continue;
            const auto tab = key.rfind('\t');
            auto* p = xml.createNewChildElement("Patch");
            p->setAttribute("bank", juce::String(key.substr(0, tab)));
            p->setAttribute("slot", std::stoi(key.substr(tab + 1)));
            p->setAttribute("name", juce::String(e.name));
            if(e.hasCategory)
                p->setAttribute("category", e.category == Category::None ? juce::String("none") : juce::String(g_names[static_cast<int>(e.category)]));
            if(e.favourite)
                p->setAttribute("favourite", true);
        }
        const auto file = libraryFile();
        if(xml.writeTo(file))
            m_fileTime = file.getLastModificationTime().toMilliseconds();
    }
}
