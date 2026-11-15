#include "forearm/token_session.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) {std::cerr << "line " << __LINE__ << ": " << #x << '\n'; std::exit(1);} } while (0)
using namespace forearm;
using Blob = std::array<std::uint8_t, kTokenSessionBytes>;
TokenPair pair(const char* marker, std::int64_t expiry = 1800000000) {
    TokenPair value{}; std::strcpy(value.access, marker); std::strcpy(value.refresh, "SYNTHETIC-refresh");
    std::strcpy(value.client_id, "SyntheticClient123"); value.expires_epoch = expiry; return value;
}
Blob record(const TokenPair& value, std::uint64_t generation, TokenSessionPhase phase) {
    Blob bytes{}; CHECK(encode_token_session(value, generation, phase, bytes.data(), bytes.size())); return bytes;
}
TokenLoadState recover(const Blob* journal, const TokenPair* legacy, bool pending, TokenPair& output, TokenSessionInfo& info,
                       const char* client = "SyntheticClient123", bool marker_valid = true) {
    return recover_token_session(journal != nullptr, journal ? journal->data() : nullptr, journal ? journal->size() : 0,
        legacy != nullptr, legacy, legacy ? sizeof(*legacy) : 0, pending, marker_valid, client, output, info);
}
int main() {
    auto old = pair("SYNTHETIC-old-access"), replacement = pair("SYNTHETIC-replacement-access");
    TokenPair output{}; TokenSessionInfo info;
    CHECK(recover(nullptr, nullptr, false, output, info) == TokenLoadState::NoSession);
    CHECK(recover(nullptr, &old, false, output, info) == TokenLoadState::LegacyReady);
    CHECK(!std::strcmp(output.access, old.access) && !info.generation);
    CHECK(recover(nullptr, &old, true, output, info) == TokenLoadState::LegacyInterrupted && !*output.refresh);
    CHECK(recover(nullptr, &old, false, output, info, "OtherClient") == TokenLoadState::ClientMismatch);
    CHECK(recover(nullptr, &old, false, output, info, old.client_id, false) == TokenLoadState::Corrupt);
    CHECK(recover_token_session(false, nullptr, 0, true, &old, sizeof(old), false, true,
        old.client_id, output, info, TokenAuthorityState::Present) == TokenLoadState::MissingJournal);
    CHECK(!*output.refresh); // Includes an index NVS sanitized into NOT_FOUND.
    CHECK(recover_token_session(false, nullptr, 0, true, &old, sizeof(old), false, true,
        old.client_id, output, info, TokenAuthorityState::Invalid) == TokenLoadState::Corrupt);
    CHECK(recover_token_session(false, nullptr, 0, true, &old, sizeof(old), false, true,
        old.client_id, output, info, TokenAuthorityState::Unavailable) == TokenLoadState::StorageUnavailable);

    const auto ready = record(old, 1, TokenSessionPhase::Ready);
    const auto pending = record(old, 1, TokenSessionPhase::RefreshPending);
    const auto completed = record(replacement, 2, TokenSessionPhase::Ready);
    // Power fails before pending commit: old refresh was never sent and is safe.
    CHECK(recover(&ready, &old, false, output, info) == TokenLoadState::JournalReady && info.generation == 1);
    CHECK(!std::strcmp(output.access, old.access));
    // Pending is durable before HTTP. Power failure before/during/after the
    // one-time request has no confirmed successor: never replay/fall back.
    CHECK(recover(&pending, &old, false, output, info) == TokenLoadState::InterruptedRefresh);
    CHECK(info.generation == 1 && info.pending && !*output.access && !*output.refresh);
    // The successful successor needs no cleanup commit. Stale legacy pending
    // and even corrupt legacy-marker type cannot discard its valid new pair.
    CHECK(recover(&completed, &old, true, output, info) == TokenLoadState::JournalReady);
    CHECK(info.generation == 2 && !info.pending && !std::strcmp(output.access, replacement.access));
    CHECK(recover(&completed, &old, true, output, info, old.client_id, false) == TokenLoadState::JournalReady);
    CHECK(recover_token_session(true, completed.data(), completed.size(), true, &old, sizeof(old), true, false,
        old.client_id, output, info, TokenAuthorityState::Present) == TokenLoadState::JournalReady);
    CHECK(recover_token_session(true, completed.data(), completed.size(), true, &old, sizeof(old), false, true,
        old.client_id, output, info, TokenAuthorityState::Invalid) == TokenLoadState::Corrupt);
    CHECK(recover(&completed, &old, false, output, info, "OtherClient") == TokenLoadState::ClientMismatch && !*output.refresh);

    for (const auto offset : {0u, 4u, 5u, 8u, 16u, 24u, 89u, 2138u, 4187u, 4190u}) {
        auto corrupt = completed; corrupt[offset] ^= 1;
        CHECK(recover(&corrupt, &old, false, output, info) == TokenLoadState::Corrupt && !*output.refresh);
    }
    CHECK(decode_token_session(completed.data(), completed.size() - 1, old.client_id, output, info) == TokenLoadState::Corrupt);
    auto corrupt_state = completed; corrupt_state[5] = 99;
    session_put(corrupt_state.data() + kSessionChecksumOffset, session_crc(corrupt_state.data(), kSessionChecksumOffset), 4);
    CHECK(recover(&corrupt_state, &old, false, output, info) == TokenLoadState::Corrupt);
    auto expired = pair("SYNTHETIC-expired-access", 1700000000);
    const auto expired_record = record(expired, 3, TokenSessionPhase::Ready);
    CHECK(recover(&expired_record, nullptr, false, output, info) == TokenLoadState::JournalReady);
    CHECK(auth_validation_due(false, 0, 0, output.expires_epoch, 1800000000));
    CHECK(auth_validation_due(true, 1000, 1001, output.expires_epoch, 1800000000));
    CHECK(!auth_validation_due(true, 1000, 1001, 1800000400, 1800000000));
    CHECK(auth_validation_due(true, 1000, 3601000, 1800000400, 1800000000));

    const auto signed_out = record(TokenPair{}, 4, TokenSessionPhase::SignedOut);
    CHECK(recover(&signed_out, &old, true, output, info) == TokenLoadState::SignedOut && !*output.access);
    for (std::size_t i = 16; i < kSessionChecksumOffset; ++i) CHECK(signed_out[i] == 0);
    std::uint64_t next = 0; CHECK(next_token_generation(0, next) && next == 1);
    CHECK(next_token_generation(3, next) && next == 4);
    CHECK(!next_token_generation(UINT64_MAX, next));
    Blob invalid{}; CHECK(!encode_token_session(old, 0, TokenSessionPhase::Ready, invalid.data(), invalid.size()));
    auto bad = old; bad.access[0] = 0;
    CHECK(!encode_token_session(bad, 1, TokenSessionPhase::Ready, invalid.data(), invalid.size()));
    CHECK(can_stage_access_expiry_test(true, true, false, false, 1800000121, 1800000000));
    CHECK(!can_stage_access_expiry_test(true, true, false, false, 1800000120, 1800000000));
    CHECK(!can_stage_access_expiry_test(false, true, false, false, 1800001000, 1800000000));
    CHECK(!can_stage_access_expiry_test(true, false, false, false, 1800001000, 1800000000));
    CHECK(!can_stage_access_expiry_test(true, true, true, false, 1800001000, 1800000000));
    CHECK(!can_stage_access_expiry_test(true, true, false, true, 1800001000, 1800000000));
    auto diagnostic = old;
    std::memset(diagnostic.access, 0, sizeof(diagnostic.access)); std::strcpy(diagnostic.access, kDiagnosticInvalidAccess);
    const auto staged = record(diagnostic, 5, TokenSessionPhase::Ready);
    CHECK(recover(&staged, nullptr, false, output, info) == TokenLoadState::JournalReady);
    CHECK(!std::strcmp(output.refresh, old.refresh) && !std::strcmp(output.client_id, old.client_id));
    CHECK(!std::strcmp(output.access, kDiagnosticInvalidAccess));
    CHECK(auth_validation_due(false, 0, 1, output.expires_epoch, 1800000000));
    bad = old; std::memset(bad.refresh, 'x', sizeof(bad.refresh));
    CHECK(!encode_token_session(bad, 1, TokenSessionPhase::Ready, invalid.data(), invalid.size()));
    std::cout << "Atomic token journal power-cut recovery, legacy compatibility, expiry and client binding passed\n";
}
