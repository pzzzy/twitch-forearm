#pragma once
#include "forearm/catalog_json.h"
#include "forearm/media.h"
#include <cstring>

namespace forearm {
struct CatalogEmote { char name[65]{}; char id[37]{}; bool global = false; };
// Extracts only v3 global .emotes[] or user .emote_set.emotes[]. Every byte of
// the surrounding JSON is validated; only tiny selected scalar fields survive.
class SevenTvCatalogReader {
public:
    using Add = void(*)(void*, const CatalogEmote&);
    SevenTvCatalogReader(Add add, void* context, bool global) : json_(event, this), add_(add), context_(context), global_(global) {}
    bool feed(const char* data, std::size_t bytes) { return json_.feed(data, bytes); }
    bool finish() { return json_.finish() && root_object_ && !invalid_shape_ && (found_array_ || (!global_ && null_channel_)); }
    bool has_channel_set() const { return !global_ && found_array_ && !null_channel_ && !invalid_shape_; }
private:
    enum Key { Other, Emotes, EmoteSet, Name, Id, Data, Animated, Host, Url };
    CatalogJson json_;
    Add add_; void* context_; bool global_;
    Key keys_[25]{};
    unsigned array_depth_ = 0, entry_depth_ = 0, fields_ = 0;
    bool root_object_ = false, found_array_ = false, invalid_ = false, animated_ = false;
    bool null_channel_ = false, saw_channel_key_ = false, invalid_shape_ = false;
    char name_[65]{}, id_[33]{}, data_id_[33]{}, host_[128]{};
    static Key key(std::string_view value) {
        if (value == "emotes") return Emotes;
        if (value == "emote_set") return EmoteSet;
        if (value == "name") return Name;
        if (value == "id") return Id;
        if (value == "data") return Data;
        if (value == "animated") return Animated;
        if (value == "host") return Host;
        if (value == "url") return Url;
        return Other;
    }
    template<std::size_t N> void save(char (&output)[N], std::string_view value, unsigned mask, bool overflow) {
        if (overflow || value.empty() || value.size() >= N || value.find('\0') != std::string_view::npos || (fields_ & mask)) { invalid_ = true; return; }
        fields_ |= mask; std::memcpy(output, value.data(), value.size()); output[value.size()] = 0;
    }
    static void event(void* context, JsonEvent event, std::string_view text, unsigned depth, bool overflow) {
        static_cast<SevenTvCatalogReader*>(context)->consume(event, text, depth, overflow);
    }
    void consume(JsonEvent event, std::string_view text, unsigned depth, bool overflow) {
        if (event == JsonEvent::Key) {
            keys_[depth] = overflow ? Other : key(text);
            if (!global_ && depth == 1 && keys_[depth] == EmoteSet) {
                if (saw_channel_key_) invalid_shape_ = true;
                saw_channel_key_ = true;
            }
            return;
        }
        if (!global_ && event == JsonEvent::Null && depth == 1 && keys_[1] == EmoteSet) null_channel_ = true;
        if (event == JsonEvent::Object) {
            keys_[depth] = Other;
            if (depth == 1) root_object_ = true;
            if (array_depth_ && depth == array_depth_ + 1) {
                entry_depth_ = depth; fields_ = 0; invalid_ = animated_ = false;
                name_[0] = id_[0] = data_id_[0] = host_[0] = 0;
            }
        }
        if (event == JsonEvent::Array &&
            ((global_ && depth == 2 && keys_[1] == Emotes) ||
             (!global_ && depth == 3 && keys_[1] == EmoteSet && keys_[2] == Emotes))) {
            array_depth_ = depth; found_array_ = true;
        }
        if (entry_depth_ && event == JsonEvent::String) {
            if (depth == entry_depth_ && keys_[depth] == Name) save(name_, text, 1, overflow);
            else if (depth == entry_depth_ && keys_[depth] == Id) save(id_, text, 2, overflow);
            else if (depth == entry_depth_ + 1 && keys_[entry_depth_] == Data && keys_[depth] == Id) save(data_id_, text, 4, overflow);
            else if (depth == entry_depth_ + 2 && keys_[entry_depth_] == Data && keys_[entry_depth_ + 1] == Host && keys_[depth] == Url) save(host_, text, 8, overflow);
        }
        if (entry_depth_ && event == JsonEvent::Boolean && depth == entry_depth_ + 1 &&
            keys_[entry_depth_] == Data && keys_[depth] == Animated) {
            if (fields_ & 16) invalid_ = true;
            fields_ |= 16; animated_ = text == "true";
        }
        if (event == JsonEvent::EndObject && depth == entry_depth_) {
            bool name_ok = *name_;
            for (const auto c : std::string_view(name_)) if (static_cast<unsigned char>(c) <= 32 || c == 127) name_ok = false;
            char expected[128]; std::snprintf(expected, sizeof(expected), "//cdn.7tv.app/emote/%s", id_);
            const auto host = std::string_view(host_);
            const bool host_ok = host == expected || (host.substr(0, 6) == "https:" && host.substr(6) == expected);
            if (!invalid_ && fields_ == 31 && name_ok && seventv_id(id_) && !std::strcmp(id_, data_id_) && host_ok) {
                CatalogEmote result{}; std::strcpy(result.name, name_);
                std::snprintf(result.id, sizeof(result.id), "7tv:%.26s%s", id_, animated_ ? ":a" : ""); result.global = global_;
                if (add_) add_(context_, result);
            }
            entry_depth_ = 0;
        }
        if (event == JsonEvent::EndArray && depth == array_depth_) array_depth_ = 0;
    }
};
} // namespace forearm
