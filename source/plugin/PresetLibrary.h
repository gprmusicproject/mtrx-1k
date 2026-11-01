#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

// What MTRX-1K knows about a patch besides its sound: its category and whether it is a
// favourite. The factory patches and those of the bank GPR Music Project Signature have a
// category of their own; what the player sets, a favourite or another category, is kept in
// presets.xml in the data folder, which every instance in every host shares.
//
// A patch is known by its bank (the name of the bank file), its place in it and its name: a
// setting made for a place that now holds a patch of another name no longer applies.

namespace retromulator::presets
{
    enum class Category : uint8_t { None, Pad, Lead, Bass, Brass, Strings, Poly, Keys, FX };
    constexpr int CategoryCount = 9;
    const char* categoryName(Category _category);

    // What the panel of the presets, the PATCH selector and the < > keys let through
    struct Filter
    {
        Category category = Category::None;   // None: every category
        bool favourites = false;
        bool active() const { return category != Category::None || favourites; }
        bool matches(Category _category, bool _favourite) const
        {
            return (category == Category::None || _category == category) && (!favourites || _favourite);
        }
    };

    class Library
    {
    public:
        static Library& instance();

        Category category(const std::string& _bank, int _slot, const std::string& _name);
        bool isFavourite(const std::string& _bank, int _slot, const std::string& _name);
        void setCategory(const std::string& _bank, int _slot, const std::string& _name, Category _category);
        void setFavourite(const std::string& _bank, int _slot, const std::string& _name, bool _favourite);

        // A patch saved from another: the new one takes its category, and stays a favourite
        // when it was saved in its own place.
        void carry(const std::string& _fromBank, int _fromSlot, const std::string& _fromName,
                   const std::string& _toBank, int _toSlot, const std::string& _toName);

        // the category a patch has when the player set none
        static Category factoryCategory(const std::string& _bank, int _slot, const std::string& _name);

        // grows with every change, so that a view knows when to look again
        uint32_t getRevision() const { return m_revision; }

    private:
        struct Entry
        {
            std::string name;
            Category category = Category::None;
            bool hasCategory = false;
            bool favourite = false;
        };

        Library() = default;
        const Entry* find(const std::string& _bank, int _slot, const std::string& _name);
        Entry& place(const std::string& _bank, int _slot, const std::string& _name);
        void reloadIfChanged();
        void save();

        std::recursive_mutex m_mutex;
        std::map<std::string, Entry> m_entries;   // bank, a tab and the place
        int64_t m_fileTime = -1;
        uint32_t m_revision = 1;
    };
}
