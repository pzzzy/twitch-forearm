#include "forearm/device_login.h"
#include <cstdlib>
#include <iostream>
#include <string>
#define CHECK(x) do { if (!(x)) {std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1);} } while (0)
int main() {
    char url[512];
    const std::string base = "https://www.twitch.tv/activate";
    auto valid = [&](const std::string& input, const std::string& expected) {
        CHECK(forearm::normalize_public_device_url(input, url, sizeof(url)));
        CHECK(std::string(url) == expected);
    };
    valid(base + "?device-code=EXAMPLE1", base + "?device-code=EXAMPLE1&public=true");
    valid(base + "?public=true&device-code=EXAMPLE1", base + "?public=true&device-code=EXAMPLE1");
    valid(base + "?device-code=EXAMPLE1&public=true", base + "?device-code=EXAMPLE1&public=true");
    valid(base, base + "?public=true");
    valid(base + "?", base + "?public=true");
    valid(base + "?device-code=EXAMPLE1&", base + "?device-code=EXAMPLE1&public=true");
    valid(base + "?device-code=A%2BB&extra=a+b", base + "?device-code=A%2BB&extra=a+b&public=true");
    valid(base + "?%70ublic=%74rue&device-code=A", base + "?%70ublic=%74rue&device-code=A");
    for (const auto& input : {
        "", "http://www.twitch.tv/activate", "https://twitch.tv/activate", "https://id.twitch.tv/activate",
        "https://www.twitch.tv.evil.test/activate", "https://www.twitch.tv@evil.test/activate",
        "https://www.twitch.tv:443/activate", "https://www.twitch.tv/activate/extra", "https://www.twitch.tv/activate/",
        "https://www.twitch.tv/%61ctivate", "//www.twitch.tv/activate", "javascript:alert(1)",
        "https://www.twitch.tv/activate#public=true", "https://www.twitch.tv/activate?public=false",
        "https://www.twitch.tv/activate?public=0", "https://www.twitch.tv/activate?public=",
        "https://www.twitch.tv/activate?public", "https://www.twitch.tv/activate?public=true&public=true",
        "https://www.twitch.tv/activate?public=true&public=false", "https://www.twitch.tv/activate?public=true&%70ublic=false",
        "https://www.twitch.tv/activate?Public=false", "https://www.twitch.tv/activate?public=TRUE",
        "https://www.twitch.tv/activate?device-code=x\n", "https://www.twitch.tv/activate?device-code=x%0a",
        "https://www.twitch.tv/activate?device-code=%", "https://www.twitch.tv/activate?device-code=%GG",
        "https://www.twitch.tv/activate?device-code=%00", "https://www.twitch.tv/activate?device-code=x\\y"
    }) {
        url[0] = 'x';
        CHECK(!forearm::normalize_public_device_url(input, url, sizeof(url)));
        CHECK(url[0] == 0);
    }
    char small[32];
    CHECK(!forearm::normalize_public_device_url(base, small, sizeof(small)));
    CHECK(small[0] == 0);
    CHECK(!forearm::normalize_public_device_url(base + "?code=" + std::string(490, 'a'), url, sizeof(url)));
    CHECK(!forearm::normalize_public_device_url(base, nullptr, 0));
    std::cout << "Public device login URL validation passed\n";
}
