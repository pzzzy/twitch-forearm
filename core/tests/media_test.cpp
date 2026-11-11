#include "forearm/media_enrich.h"
#include "forearm/seventv_catalog.h"
#include "forearm/catalog_store.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) {std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1);} } while (0)
using namespace forearm;
const char* lookup(std::string_view name) {
    if (name == "SYNTHETIC") return "7tv:0123456789abcdef01234567";
    if (name == ":)") return "7tv:0123456789abcdef01234567:a";
    return nullptr;
}
void add(void* context, const CatalogEmote& emote) { static_cast<std::vector<CatalogEmote>*>(context)->push_back(emote); }
bool catalog(std::string_view input, bool global, std::vector<CatalogEmote>& output, unsigned chunk = 1) {
    SevenTvCatalogReader parser(add, &output, global);
    for (std::size_t i = 0; i < input.size(); i += chunk)
        if (!parser.feed(input.data() + i, std::min<std::size_t>(chunk, input.size() - i))) return false;
    return parser.finish();
}
int main() {
    char url[256];
    CHECK(media_url("25", url, sizeof(url)) && std::string_view(url).find("static-cdn.jtvnw.net/emoticons/v2/25/static/dark/1.0") != std::string_view::npos);
    CHECK(media_url("7tv:0123456789abcdef01234567:a", url, sizeof(url)) && std::string_view(url).find("1x_static.png") != std::string_view::npos);
    CHECK(media_url("7tv:01FCY771D800007PQ2DF3GDTN6", url, sizeof(url)) && std::string_view(url).find("/1x.png") != std::string_view::npos);
    CHECK(media_url("emoji:1f600", url, sizeof(url)) && std::string_view(url).find("@388f518b756404152f128a3539f5e7f9a6f1ebd6/") != std::string_view::npos);
    for (const auto* bad : {"https://bad.invalid/a", "7tv:../secret", "7tv:0123456789abcdef01234567?x=1", "emoji:../../file", "emoji:1f600?x", "emoji:1f600--200d", "emoji:ABC", "7tv:01FCY771D800007PQ2DF3GDTN6:evil"}) CHECK(!media_url(bad, url, sizeof(url)));
    CHECK(!media_url("emoji:1f600", url, 16));
    unsigned w = 0, h = 0; media_dimensions(72, 72, w, h); CHECK(w == 48 && h == 48);
    media_dimensions(128, 128, w, h); CHECK(w == 48 && h == 48);
    media_dimensions(128, 32, w, h); CHECK(w == 128 && h == 32);
    media_dimensions(25, 28, w, h); CHECK(w == 25 && h == 28);
    media_dimensions(1, 128, w, h); CHECK(w == 1 && h == 48);

    const std::string object = R"({"id":"0123456789abcdef01234567","name":"SYNTHETIC","data":{"id":"0123456789abcdef01234567","animated":true,"host":{"url":"//cdn.7tv.app/emote/0123456789abcdef01234567","files":[]}}})";
    for (unsigned chunk : {1, 2, 7, 1024}) {
        std::vector<CatalogEmote> entries;
        CHECK(catalog("{\"emotes\":[" + object + "]}", true, entries, chunk));
        CHECK(entries.size() == 1 && std::string_view(entries[0].id) == "7tv:0123456789abcdef01234567:a" && entries[0].global);
        entries.clear(); CHECK(catalog("{\"emote_set\":{\"emotes\":[" + object + "]}}", false, entries, chunk));
        CHECK(entries.size() == 1 && !entries[0].global);
    }
    std::vector<CatalogEmote> entries;
    auto hostile = object; hostile.replace(hostile.find("//cdn.7tv.app"), 13, "//bad.invalid");
    CHECK(catalog("{\"emotes\":[" + hostile + "]}", true, entries) && entries.empty());
    for (const auto* invalid : {"{\"emotes\":[}", "{\"emotes\":[],}", "{\"emotes\":[true,]}", "{\"emotes\":[]}garbage", "{\"x\":01,\"emotes\":[]}", "{\"x\":\"\\uD800\",\"emotes\":[]}", "{\"x\":\"\\q\",\"emotes\":[]}"}) {
        entries.clear(); CHECK(!catalog(invalid, true, entries));
    }
    entries.clear(); CHECK(catalog("{\"ignored\":\"" + std::string(2048, 'x') + "\",\"emotes\":[]}", true, entries));
    entries.clear(); CHECK(catalog("{\"emote_set\":null}", false, entries) && entries.empty());
    for (const auto* bad_shape : {"{}", "{\"nested\":{\"emote_set\":null}}", "{\"emote_set\":{\"emotes\":null}}", "{\"emote_set\":null,\"emote_set\":{\"emotes\":[]}}"}) {
        entries.clear(); CHECK(!catalog(bad_shape, false, entries));
    }
    SevenTvCatalogReader no_set(nullptr, nullptr, false);
    CHECK(no_set.feed("{\"emote_set\":null}", 18) && no_set.finish() && !no_set.has_channel_set());
    CatalogEmote aliases[8]{}; unsigned alias_count = 0;
    CatalogEmote global{}; std::strcpy(global.name, "SYNTHETIC"); std::strcpy(global.id, "7tv:0123456789abcdef01234567"); global.global = true;
    CHECK(catalog_add(aliases, alias_count, 8, global));
    const auto original_global = aliases[0]; const auto global_count = alias_count;
    auto channel_alias = global; channel_alias.global = false; std::strcpy(channel_alias.id, "7tv:0123456789abcdef01234567:a");
    CHECK(catalog_add(aliases, alias_count, 8, channel_alias));
    CHECK(alias_count == 2 && !std::memcmp(&original_global, &aliases[0], sizeof(global)));
    alias_count = global_count; // Failed response rollback happens before sorting/publication.
    CHECK(catalog_find(aliases, alias_count, "SYNTHETIC", true)->global);
    CHECK(catalog_add(aliases, alias_count, 8, channel_alias));
    CHECK(!catalog_find(aliases, alias_count, "SYNTHETIC", true)->global);
    CHECK(catalog_find(aliases, alias_count, "SYNTHETIC", false)->global);
    std::swap(aliases[0], aliases[1]); // Equal-name ordering cannot change selection.
    CHECK(!catalog_find(aliases, alias_count, "SYNTHETIC", true)->global);
    CHECK(catalog_find(aliases, alias_count, "SYNTHETIC", false)->global);
    CatalogJson bounded(nullptr, nullptr, 4); CHECK(!bounded.feed("12345", 5));
    CatalogJson depth(nullptr, nullptr); const std::string deep(25, '['); CHECK(!depth.feed(deep.data(), deep.size()));

    const std::vector<std::pair<std::string, std::string>> emoji = {
        {u8"😀", "emoji:1f600"}, {u8"❤️", "emoji:2764"}, {u8"👍🏽", "emoji:1f44d-1f3fd"},
        {u8"👩‍💻", "emoji:1f469-200d-1f4bb"}, {u8"👨‍👩‍👧‍👦", "emoji:1f468-200d-1f469-200d-1f467-200d-1f466"},
        {u8"🇬🇧", "emoji:1f1ec-1f1e7"}, {u8"1️⃣", "emoji:31-20e3"},
        {u8"👩‍❤‍👩", "emoji:1f469-200d-2764-fe0f-200d-1f469"},
    };
    for (const auto& test : emoji) {
        MediaOccurrence result; std::size_t consumed = 0;
        CHECK(emoji_at(test.first, 0, result, consumed)); CHECK(result.id == test.second);
        CHECK(consumed == test.first.size() && result.byte_length == consumed && test.first == result.text);
    }
    for (const auto& literal : {std::string(u8"❤︎"), std::string(u8"😀‍a"), std::string(u8"😀‍"), std::string(u8"👍🏽🏽"), std::string("\xf0\x9f\x98", 3), std::string("\xc0\xaf", 2)}) {
        MediaOccurrence result; std::size_t consumed = 0; CHECK(!emoji_at(literal, 0, result, consumed));
    }
    std::string too_long = u8"😀"; for (unsigned i = 0; i < 20; ++i) too_long += u8"‍😀";
    MediaOccurrence result; std::size_t consumed = 0;
    CHECK(!emoji_at(too_long, 0, result, consumed) && consumed == too_long.size());
    const std::string unknown_chain = u8"\U0001FAFF‍😀";
    CHECK(!emoji_at(unknown_chain, 0, result, consumed) && consumed == unknown_chain.size());
    MediaOccurrence unknown_output[8]{};
    CHECK(collect_media(unknown_chain, unknown_output, 0, 8, lookup) == 0);
    MediaOccurrence occurrences[8]{};
    const std::string message = u8"é SYNTHETIC! xSYNTHETIC SYNTHETICx :) 😀";
    unsigned count = collect_media(message, occurrences, 0, 8, lookup);
    CHECK(count == 3 && occurrences[0].byte_offset == 3 && std::string_view(occurrences[0].text) == "SYNTHETIC");
    CHECK(std::string_view(occurrences[1].text) == ":)" && std::string_view(occurrences[2].id) == "emoji:1f600");
    occurrences[0] = {}; std::strcpy(occurrences[0].id, "25"); std::strcpy(occurrences[0].text, "SYNTHETIC");
    occurrences[0].byte_offset = 3; occurrences[0].byte_length = 9; occurrences[0].position_valid = true;
    count = collect_media(message, occurrences, 1, 8, lookup);
    CHECK(count == 3 && std::string_view(occurrences[0].id) == "25");
    const std::string many = u8"😀 😀 😀 😀 😀 😀 😀 😀 😀 😀";
    CHECK(collect_media(many, occurrences, 0, 8, lookup) == 8);
    std::cout << "Bounded provider JSON, exact emoji clusters, URLs, sizing and native-priority matching passed\n";
}
