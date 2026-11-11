#include "forearm/device_config.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#define CHECK(x) do { if (!(x)) {std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1);} } while (0)
int main() {
    forearm::DeviceConfiguration stored{};
    std::strcpy(stored.ssid, "fixture-network"); std::strcpy(stored.password, "fixture-password");
    std::strcpy(stored.client_id, "PublicClient123"); std::strcpy(stored.channel, "Fixture_Channel");
    stored.read_only = 1;
    forearm::DeviceConfiguration decoded{};
    // A deployed196-byte blob has no flag: decode it as the legacy profile,
    // preserving all four strings exactly and initializing the appended byte.
    CHECK(forearm::decode_device_configuration(&stored, 196, decoded));
    CHECK(decoded.read_only == 0);
    CHECK(std::memcmp(&stored, &decoded, 196) == 0);
    CHECK(forearm::decode_device_configuration(&stored, sizeof(stored), decoded));
    CHECK(decoded.read_only == 1 && std::memcmp(&stored, &decoded, sizeof(stored)) == 0);
    for (const std::size_t size : {0u, 195u, 198u, 1024u}) {
        decoded = stored;
        CHECK(!forearm::decode_device_configuration(&stored, size, decoded));
        CHECK(decoded.ssid[0] == 0 && decoded.password[0] == 0 && decoded.read_only == 0);
    }
    CHECK(!forearm::decode_device_configuration(nullptr, 196, decoded));
    for (const auto flag : {2u, 255u}) {
        stored.read_only = flag;
        CHECK(!forearm::decode_device_configuration(&stored, sizeof(stored), decoded));
        CHECK(decoded.password[0] == 0);
    }
    stored.read_only = 1;
    const auto good = stored;
    for (const std::size_t last_byte : {32u, 97u, 162u, 195u}) {
        auto bad = good;
        reinterpret_cast<unsigned char*>(&bad)[last_byte] = 'x';
        CHECK(!forearm::decode_device_configuration(&bad, sizeof(bad), decoded));
    }
    auto bad = good; std::strcpy(bad.client_id, "client&other=value");
    CHECK(!forearm::decode_device_configuration(&bad, sizeof(bad), decoded));
    bad = good; std::strcpy(bad.channel, "foreign/channel");
    CHECK(!forearm::decode_device_configuration(&bad, sizeof(bad), decoded));
    bad = good; std::strcpy(bad.channel, "channel_name_longer_than_25");
    CHECK(!forearm::decode_device_configuration(&bad, sizeof(bad), decoded));
    // Wi-Fi-only provisioning with no Client ID/channel is still compatible.
    stored.client_id[0] = stored.channel[0] = 0;
    CHECK(forearm::decode_device_configuration(&stored, sizeof(stored), decoded));
    CHECK(std::string(decoded.password) == "fixture-password" && decoded.read_only == 1);
    std::cout << "NVS configuration legacy/profile compatibility passed\n";
}
