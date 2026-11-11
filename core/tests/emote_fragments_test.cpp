#include "forearm/emote_fragments.h"
#include <cstdlib>
#include <iostream>
#include <string>
#define CHECK(x) do { if (!(x)) {std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1);} } while (0)
int main() {
    const std::string text = "Kappa Kappa Kappa";
    forearm::MessageFragmentCursor cursor(text, text.size());
    // First occurrence is plain text. Only the second has Twitch emote
    // metadata; a renderer must not globally replace the same literal token.
    const auto plain = cursor.consume("Kappa ");
    const auto emote = cursor.consume("Kappa");
    cursor.consume(" Kappa");
    CHECK(cursor.complete());
    CHECK(plain.byte_offset == 0 && plain.byte_length == 6);
    CHECK(emote.position_valid && emote.byte_offset == 6 && emote.byte_length == 5);
    const std::string unicode = "\xC3\xA9\xF0\x9F\x98\x80 Kappa \xF0\x9F\x92\x9C";
    forearm::MessageFragmentCursor utf8(unicode, unicode.size());
    utf8.consume("\xC3\xA9\xF0\x9F\x98\x80 ");
    const auto positioned = utf8.consume("Kappa");
    utf8.consume(" \xF0\x9F\x92\x9C");
    CHECK(utf8.complete() && positioned.byte_offset == 7 && positioned.byte_length == 5);
    forearm::MessageFragmentCursor repeated("KappaKappa", 10);
    const auto first = repeated.consume("Kappa"), second = repeated.consume("Kappa");
    CHECK(repeated.complete() && first.byte_offset == 0 && second.byte_offset == 5);
    forearm::MessageFragmentCursor mismatch(text, text.size());
    mismatch.consume("Kappa "); mismatch.consume("different"); mismatch.consume(" Kappa");
    CHECK(!mismatch.complete()); // Caller must discard every tentative span.
    forearm::MessageFragmentCursor missing(text, text.size()); missing.consume("Kappa");
    CHECK(!missing.complete());
    forearm::MessageFragmentCursor partial(unicode, unicode.size());
    const auto split = partial.consume(std::string_view(unicode.data(), 1));
    CHECK(!split.position_valid && !partial.complete()); // Cannot split a UTF-8 codepoint.
    const std::string bounded = std::string(2044, 'x') + "Kappa";
    forearm::MessageFragmentCursor clipped(bounded, 2048);
    clipped.consume(std::string_view(bounded.data(), 2044));
    const auto outside = clipped.consume("Kappa");
    CHECK(clipped.complete() && !outside.position_valid);
    const std::string fits = std::string(2043, 'x') + "Kappa";
    forearm::MessageFragmentCursor final(fits, 2048);
    final.consume(std::string_view(fits.data(), 2043));
    const auto exact = final.consume("Kappa");
    CHECK(final.complete() && exact.position_valid && exact.byte_offset == 2043 && exact.byte_length == 5);
    // More than eight occurrences are still walked to verify complete text;
    // metadata callers cap retained occurrences and leave the rest literal.
    std::string many;
    for (unsigned i=0;i<12;++i) many += "Kappa ";
    forearm::MessageFragmentCursor long_run(many, many.size());
    for (unsigned i=0;i<12;++i) {
        const auto range = long_run.consume("Kappa");
        CHECK(range.position_valid && range.byte_offset == i*6 && range.byte_length == 5);
        long_run.consume(" ");
    }
    CHECK(long_run.complete());
    std::cout << "Exact UTF-8 emote fragment occurrence metadata passed\n";
}
