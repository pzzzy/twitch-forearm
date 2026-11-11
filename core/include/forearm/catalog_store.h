#pragma once
#include "forearm/seventv_catalog.h"
#include <algorithm>

namespace forearm {
// Globals and channel aliases occupy distinct records. A channel transaction
// can therefore be rolled back to its starting count without altering globals.
inline bool catalog_add(CatalogEmote* items, unsigned& count, unsigned capacity, const CatalogEmote& value) {
    for (unsigned i = 0; i < count; ++i) {
        if (items[i].global == value.global && !std::strcmp(items[i].name, value.name)) {
            items[i] = value; return true;
        }
    }
    if (count >= capacity) return false;
    items[count++] = value; return true;
}
inline const CatalogEmote* catalog_find(const CatalogEmote* items, unsigned count,
                                      std::string_view name, bool channel_matches) {
    auto* found = std::lower_bound(items, items + count, name,
        [](const CatalogEmote& item, std::string_view key) { return std::string_view(item.name) < key; });
    const CatalogEmote* global = nullptr;
    for (; found != items + count && name == found->name; ++found) {
        if (!found->global && channel_matches) return found;
        if (found->global) global = found;
    }
    return global;
}
} // namespace forearm
