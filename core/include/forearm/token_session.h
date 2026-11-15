#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace forearm {
constexpr std::size_t kSessionTokenBytes = 2049;
struct TokenPair {
    char access[kSessionTokenBytes]{};
    char refresh[kSessionTokenBytes]{};
    char client_id[65]{};
    std::int64_t expires_epoch = 0;
};
// These assertions bind only the old deployed ABI. New records have an
// explicit byte layout independent of compiler padding or host word size.
static_assert(offsetof(TokenPair, expires_epoch) == 4168, "Legacy token expiry ABI changed");
static_assert(sizeof(TokenPair) == 4176, "Legacy token record ABI changed");
constexpr std::size_t kLegacyTokenPairBytes = 4176;
constexpr std::size_t kTokenSessionBytes = 4191;
constexpr std::size_t kSessionClientOffset = 24, kSessionAccessOffset = 89, kSessionRefreshOffset = 2138;
constexpr std::size_t kSessionChecksumOffset = kTokenSessionBytes - 4;
constexpr char kDiagnosticInvalidAccess[] = "FOREARM_DIAGNOSTIC_INVALID_ACCESS_FOR_BOOT_TEST";
enum class TokenSessionPhase : std::uint8_t { Ready = 1, RefreshPending = 2, SignedOut = 3 };
enum class TokenLoadState : std::uint8_t {
    Unchecked, NoSession, LegacyReady, JournalReady, InterruptedRefresh,
    LegacyInterrupted, Corrupt, ClientMismatch, StorageUnavailable, SignedOut, MissingJournal
};
enum class TokenAuthorityState : std::uint8_t { Absent, Present, Invalid, Unavailable };
struct TokenSessionInfo { std::uint64_t generation = 0; bool pending = false; };
inline const char* token_load_text(TokenLoadState state) {
    switch (state) {
    case TokenLoadState::Unchecked: return "Saved login has not been checked";
    case TokenLoadState::NoSession: return "No saved Twitch login; tap Connect";
    case TokenLoadState::LegacyReady: return "Saved Twitch login loaded";
    case TokenLoadState::JournalReady: return "Saved Twitch login loaded";
    case TokenLoadState::InterruptedRefresh: return "Reconnect required after interrupted token refresh";
    case TokenLoadState::LegacyInterrupted: return "Reconnect required after an interrupted legacy refresh";
    case TokenLoadState::Corrupt: return "Reconnect required: saved login record is invalid";
    case TokenLoadState::ClientMismatch: return "Reconnect required: saved login belongs to another Twitch app";
    case TokenLoadState::StorageUnavailable: return "Reconnect required: credential storage is unavailable";
    case TokenLoadState::SignedOut: return "Signed out of Twitch; tap Connect";
    case TokenLoadState::MissingJournal: return "Reconnect required: migrated login journal is missing";
    }
    return "Reconnect required";
}
inline bool token_load_ready(TokenLoadState state) { return state == TokenLoadState::LegacyReady || state == TokenLoadState::JournalReady; }
inline std::uint32_t session_crc(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
inline void session_put(std::uint8_t* destination, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) destination[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
inline std::uint64_t session_get(const std::uint8_t* source, unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(source[i]) << (i * 8);
    return value;
}
inline bool session_string(const std::uint8_t* value, std::size_t bytes, bool client = false) {
    const auto* end = static_cast<const std::uint8_t*>(std::memchr(value, 0, bytes));
    if (!end || end == value || value[bytes - 1]) return false;
    for (const auto* p = value; p != end; ++p) {
        if (*p < 33 || *p > 126) return false;
        if (client && !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) return false;
    }
    return true;
}
inline bool session_credentials(const std::uint8_t* access, const std::uint8_t* refresh,
                                const std::uint8_t* client, std::uint64_t expiry) {
    // Expired access credentials remain recoverable: Twitch validation decides
    // whether to refresh. Expiry is a scheduling hint, never a login veto.
    return session_string(access, kSessionTokenBytes) && session_string(refresh, kSessionTokenBytes) &&
           session_string(client, 65, true) && expiry >= 946684800 && expiry <= 4102444800ULL;
}
inline bool encode_token_session(const TokenPair& pair, std::uint64_t generation, TokenSessionPhase phase,
                                 std::uint8_t* output, std::size_t size) {
    if (!output || size != kTokenSessionBytes || !generation ||
        (phase != TokenSessionPhase::Ready && phase != TokenSessionPhase::RefreshPending && phase != TokenSessionPhase::SignedOut)) return false;
    if (phase != TokenSessionPhase::SignedOut && !session_credentials(
        reinterpret_cast<const std::uint8_t*>(pair.access), reinterpret_cast<const std::uint8_t*>(pair.refresh),
        reinterpret_cast<const std::uint8_t*>(pair.client_id), pair.expires_epoch)) return false;
    std::memset(output, 0, size); std::memcpy(output, "FCTJ", 4); output[4] = 1; output[5] = static_cast<std::uint8_t>(phase);
    session_put(output + 8, generation, 8);
    if (phase != TokenSessionPhase::SignedOut) {
        session_put(output + 16, pair.expires_epoch, 8);
        std::memcpy(output + kSessionClientOffset, pair.client_id, sizeof(pair.client_id));
        std::memcpy(output + kSessionAccessOffset, pair.access, sizeof(pair.access));
        std::memcpy(output + kSessionRefreshOffset, pair.refresh, sizeof(pair.refresh));
    }
    session_put(output + kSessionChecksumOffset, session_crc(output, kSessionChecksumOffset), 4);
    return true;
}
inline TokenLoadState decode_token_session(const void* input, std::size_t bytes, std::string_view expected_client,
                                           TokenPair& output, TokenSessionInfo& info) {
    output = {}; info = {};
    if (!input || bytes != kTokenSessionBytes) return TokenLoadState::Corrupt;
    const auto* data = static_cast<const std::uint8_t*>(input);
    if (std::memcmp(data, "FCTJ", 4) || data[4] != 1 || data[6] || data[7] ||
        session_get(data + kSessionChecksumOffset, 4) != session_crc(data, kSessionChecksumOffset)) return TokenLoadState::Corrupt;
    const auto phase = static_cast<TokenSessionPhase>(data[5]);
    if (phase != TokenSessionPhase::Ready && phase != TokenSessionPhase::RefreshPending && phase != TokenSessionPhase::SignedOut) return TokenLoadState::Corrupt;
    info.generation = session_get(data + 8, 8);
    if (!info.generation) return TokenLoadState::Corrupt;
    info.pending = phase == TokenSessionPhase::RefreshPending;
    if (phase == TokenSessionPhase::SignedOut) {
        for (std::size_t i = 16; i < kSessionChecksumOffset; ++i) if (data[i]) return TokenLoadState::Corrupt;
        return TokenLoadState::SignedOut;
    }
    const auto expiry = session_get(data + 16, 8);
    if (!session_credentials(data + kSessionAccessOffset, data + kSessionRefreshOffset, data + kSessionClientOffset, expiry)) return TokenLoadState::Corrupt;
    if (expected_client != reinterpret_cast<const char*>(data + kSessionClientOffset)) return TokenLoadState::ClientMismatch;
    if (info.pending) return TokenLoadState::InterruptedRefresh;
    std::memcpy(output.access, data + kSessionAccessOffset, sizeof(output.access));
    std::memcpy(output.refresh, data + kSessionRefreshOffset, sizeof(output.refresh));
    std::memcpy(output.client_id, data + kSessionClientOffset, sizeof(output.client_id)); output.expires_epoch = expiry;
    return TokenLoadState::JournalReady;
}
inline TokenLoadState recover_token_session(bool journal_present, const void* journal, std::size_t journal_bytes,
                                            bool legacy_present, const void* legacy, std::size_t legacy_bytes,
                                            bool legacy_pending, bool legacy_marker_valid,
                                            std::string_view expected_client, TokenPair& output, TokenSessionInfo& info,
                                            TokenAuthorityState authority = TokenAuthorityState::Absent) {
    output = {}; info = {};
    if (authority == TokenAuthorityState::Unavailable) return TokenLoadState::StorageUnavailable;
    if (authority == TokenAuthorityState::Invalid) return TokenLoadState::Corrupt;
    // A new-format record is authoritative, including Pending/invalid/tombstone.
    // Falling back could replay a refresh token already consumed by its journal.
    if (journal_present) return decode_token_session(journal, journal_bytes, expected_client, output, info);
    if (authority == TokenAuthorityState::Present) return TokenLoadState::MissingJournal;
    if (!legacy_marker_valid) return TokenLoadState::Corrupt;
    if (legacy_pending) { info.pending = true; return TokenLoadState::LegacyInterrupted; }
    if (!legacy_present) return TokenLoadState::NoSession;
    if (!legacy || legacy_bytes != kLegacyTokenPairBytes) return TokenLoadState::Corrupt;
    const auto* data = static_cast<const std::uint8_t*>(legacy);
    const auto expiry = session_get(data + 4168, 8);
    if (!session_credentials(data, data + kSessionTokenBytes, data + kSessionTokenBytes * 2, expiry)) return TokenLoadState::Corrupt;
    if (expected_client != reinterpret_cast<const char*>(data + kSessionTokenBytes * 2)) return TokenLoadState::ClientMismatch;
    std::memcpy(output.access, data, sizeof(output.access)); std::memcpy(output.refresh, data + kSessionTokenBytes, sizeof(output.refresh));
    std::memcpy(output.client_id, data + kSessionTokenBytes * 2, sizeof(output.client_id)); output.expires_epoch = expiry;
    return TokenLoadState::LegacyReady;
}
inline bool next_token_generation(std::uint64_t current, std::uint64_t& next) {
    if (current == std::numeric_limits<std::uint64_t>::max()) return false;
    next = current + 1; return true;
}
inline bool auth_validation_due(bool validated, std::int64_t last_ms, std::int64_t now_ms,
                                std::int64_t expiry, std::int64_t now_epoch) {
    return !validated || now_ms - last_ms >= 3600000 || now_epoch + 90 >= expiry;
}
inline bool can_stage_access_expiry_test(bool read_only, bool authenticated, bool login_pending,
                                        bool mutation_pending, std::int64_t expiry, std::int64_t now_epoch) {
    return read_only && authenticated && !login_pending && !mutation_pending && expiry - now_epoch > 120;
}
} // namespace forearm
