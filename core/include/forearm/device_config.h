#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace forearm {
// The first196 bytes are the deployed legacy NVS layout. The explicit byte
// flag avoids reading an untrusted persisted representation as a C++ bool.
struct DeviceConfiguration {
    char ssid[33]{};
    char password[65]{};
    char client_id[65]{};
    char channel[33]{};
    std::uint8_t read_only = 0;
};
constexpr std::size_t kLegacyConfigurationBytes = 196;
static_assert(std::is_trivially_copyable<DeviceConfiguration>::value, "NVS configuration must be copyable");
static_assert(offsetof(DeviceConfiguration, read_only) == kLegacyConfigurationBytes, "Preserve deployed configuration layout");
static_assert(sizeof(DeviceConfiguration) == 197, "Unexpected persisted configuration padding");

inline bool decode_device_configuration(const void* bytes, std::size_t size, DeviceConfiguration& output) {
    output = {};
    if (!bytes || (size != kLegacyConfigurationBytes && size != sizeof(DeviceConfiguration))) return false;
    DeviceConfiguration candidate{};
    std::memcpy(&candidate, bytes, size);
    if (candidate.ssid[32] || candidate.password[64] || candidate.client_id[64] || candidate.channel[32] || candidate.read_only > 1)
        return false;
    const auto alnum = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    for (const auto* p = candidate.client_id; *p; ++p) if (!alnum(*p)) return false;
    if (std::strlen(candidate.channel) > 25) return false;
    for (const auto* p = candidate.channel; *p; ++p) if (!alnum(*p) && *p != '_') return false;
    output = candidate;
    return true;
}
} // namespace forearm
