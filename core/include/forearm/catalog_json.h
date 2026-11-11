#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace forearm {
enum class JsonEvent { Object, EndObject, Array, EndArray, Key, String, Number, Boolean, Null };
// Allocation-free streaming JSON validation/tokenization. Strings are decoded
// into a bounded scratch token; overflow is reported while ignored fields keep
// streaming. No DOM or untrusted response-sized allocation is needed.
class CatalogJson {
public:
    using Callback = void(*)(void*, JsonEvent, std::string_view, unsigned, bool);
    CatalogJson(Callback callback, void* context, std::size_t byte_limit = 4 * 1024 * 1024)
        : limit_(byte_limit), callback_(callback), context_(context) {}
    bool feed(const char* data, std::size_t size) {
        if (!okay_ || size > limit_ - bytes_) return okay_ = false;
        bytes_ += size;
        for (std::size_t i = 0; i < size && okay_; ++i) consume(static_cast<unsigned char>(data[i]));
        return okay_;
    }
    bool finish() {
        if (scalar_) finish_scalar();
        return okay_ && complete_ && !depth_ && !string_;
    }
private:
    enum Expect : std::uint8_t { FirstKey, Key, Colon, ObjectValue, ObjectComma, FirstValue, ArrayValue, ArrayComma };
    struct Frame { bool object; Expect expect; };
    Frame frames_[24]{};
    unsigned depth_ = 0;
    bool okay_ = true, started_ = false, complete_ = false, string_ = false, key_ = false, scalar_ = false;
    bool escaped_ = false, overflow_ = false;
    unsigned unicode_digits_ = 0, unicode_value_ = 0, high_surrogate_ = 0, surrogate_phase_ = 0;
    unsigned utf8_left_ = 0, utf8_value_ = 0, utf8_min_ = 0;
    char token_[257]{};
    std::size_t used_ = 0, bytes_ = 0, limit_;
    Callback callback_; void* context_;
    void emit(JsonEvent event) { if (callback_) callback_(context_, event, {token_, used_}, depth_, overflow_); }
    void reset_token() { used_ = 0; overflow_ = false; }
    void append(unsigned char c) { if (used_ < sizeof(token_) - 1) token_[used_++] = static_cast<char>(c); else overflow_ = true; }
    bool value() {
        if (!depth_) { if (started_) return okay_ = false; started_ = true; return true; }
        auto& f = frames_[depth_ - 1];
        if (f.object && f.expect == ObjectValue) f.expect = ObjectComma;
        else if (!f.object && (f.expect == FirstValue || f.expect == ArrayValue)) f.expect = ArrayComma;
        else return okay_ = false;
        return true;
    }
    void scalar_complete() { if (!depth_) complete_ = true; }
    static int hex(unsigned char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    void codepoint(unsigned cp) {
        if (cp < 0x80) append(cp);
        else if (cp < 0x800) { append(0xc0 | (cp >> 6)); append(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { append(0xe0 | (cp >> 12)); append(0x80 | ((cp >> 6) & 63)); append(0x80 | (cp & 63)); }
        else { append(0xf0 | (cp >> 18)); append(0x80 | ((cp >> 12) & 63)); append(0x80 | ((cp >> 6) & 63)); append(0x80 | (cp & 63)); }
    }
    void string_byte(unsigned char c) {
        if (unicode_digits_) {
            const auto digit = hex(c); if (digit < 0) { okay_ = false; return; }
            unicode_value_ = unicode_value_ * 16 + digit;
            if (--unicode_digits_) return;
            if (unicode_value_ >= 0xd800 && unicode_value_ <= 0xdbff && !high_surrogate_) {
                high_surrogate_ = unicode_value_; surrogate_phase_ = 1; return;
            }
            if (high_surrogate_) {
                if (unicode_value_ < 0xdc00 || unicode_value_ > 0xdfff) { okay_ = false; return; }
                codepoint(0x10000 + ((high_surrogate_ - 0xd800) << 10) + unicode_value_ - 0xdc00);
                high_surrogate_ = 0;
            } else if (unicode_value_ >= 0xdc00 && unicode_value_ <= 0xdfff) okay_ = false;
            else codepoint(unicode_value_);
            return;
        }
        if (surrogate_phase_) {
            if (surrogate_phase_ == 1 && c == '\\') { surrogate_phase_ = 2; return; }
            if (surrogate_phase_ == 2 && c == 'u') { surrogate_phase_ = 0; unicode_digits_ = 4; unicode_value_ = 0; return; }
            okay_ = false; return;
        }
        if (escaped_) {
            escaped_ = false;
            if (c == 'u') { unicode_digits_ = 4; unicode_value_ = 0; return; }
            switch (c) {
            case '"': case '\\': case '/': append(c); return;
            case 'b': append('\b'); return; case 'f': append('\f'); return;
            case 'n': append('\n'); return; case 'r': append('\r'); return; case 't': append('\t'); return;
            default: okay_ = false; return;
            }
        }
        if (utf8_left_) {
            if ((c & 0xc0) != 0x80) { okay_ = false; return; }
            utf8_value_ = (utf8_value_ << 6) | (c & 63); append(c);
            if (!--utf8_left_ && (utf8_value_ < utf8_min_ || utf8_value_ > 0x10ffff ||
                                (utf8_value_ >= 0xd800 && utf8_value_ <= 0xdfff))) okay_ = false;
            return;
        }
        if (c == '"') {
            string_ = false;
            if (key_) { frames_[depth_ - 1].expect = Colon; emit(JsonEvent::Key); }
            else { emit(JsonEvent::String); scalar_complete(); }
        } else if (c == '\\') escaped_ = true;
        else if (c < 32) okay_ = false;
        else if (c < 128) append(c);
        else if (c >= 0xc2 && c <= 0xdf) { utf8_left_ = 1; utf8_value_ = c & 31; utf8_min_ = 0x80; append(c); }
        else if (c >= 0xe0 && c <= 0xef) { utf8_left_ = 2; utf8_value_ = c & 15; utf8_min_ = 0x800; append(c); }
        else if (c >= 0xf0 && c <= 0xf4) { utf8_left_ = 3; utf8_value_ = c & 7; utf8_min_ = 0x10000; append(c); }
        else okay_ = false;
    }
    void finish_scalar() {
        scalar_ = false;
        const std::string_view token(token_, used_);
        if (overflow_) { okay_ = false; return; }
        if (token == "true" || token == "false") emit(JsonEvent::Boolean);
        else if (token == "null") emit(JsonEvent::Null);
        else {
            std::size_t i = 0;
            if (i < token.size() && token[i] == '-') ++i;
            if (i < token.size() && token[i] == '0') ++i;
            else {
                const auto start = i;
                while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
                if (start == i) { okay_ = false; return; }
            }
            if (i < token.size() && token[i] == '.') {
                const auto start = ++i;
                while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
                if (start == i) { okay_ = false; return; }
            }
            if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
                ++i;
                if (i < token.size() && (token[i] == '+' || token[i] == '-')) ++i;
                const auto start = i;
                while (i < token.size() && token[i] >= '0' && token[i] <= '9') ++i;
                if (start == i) { okay_ = false; return; }
            }
            if (i != token.size()) { okay_ = false; return; }
            emit(JsonEvent::Number);
        }
        scalar_complete();
    }
    void consume(unsigned char c) {
        if (string_) { string_byte(c); return; }
        if (scalar_) {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'E') { append(c); return; }
            finish_scalar(); if (!okay_) return;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return;
        if (c == '"') {
            key_ = depth_ && frames_[depth_ - 1].object &&
                (frames_[depth_ - 1].expect == FirstKey || frames_[depth_ - 1].expect == Key);
            if (!key_ && !value()) return;
            reset_token(); string_ = true; return;
        }
        if (c == '{' || c == '[') {
            if (!value() || depth_ == 24) { okay_ = false; return; }
            frames_[depth_++] = {c == '{', c == '{' ? FirstKey : FirstValue};
            reset_token(); emit(c == '{' ? JsonEvent::Object : JsonEvent::Array); return;
        }
        if (c == '}' || c == ']') {
            if (!depth_) { okay_ = false; return; }
            const auto f = frames_[depth_ - 1];
            if ((c == '}' && (!f.object || (f.expect != FirstKey && f.expect != ObjectComma))) ||
                (c == ']' && (f.object || (f.expect != FirstValue && f.expect != ArrayComma)))) { okay_ = false; return; }
            reset_token(); emit(c == '}' ? JsonEvent::EndObject : JsonEvent::EndArray); --depth_; scalar_complete(); return;
        }
        if (c == ':') {
            if (!depth_ || !frames_[depth_ - 1].object || frames_[depth_ - 1].expect != Colon) okay_ = false;
            else frames_[depth_ - 1].expect = ObjectValue;
            return;
        }
        if (c == ',') {
            if (!depth_) { okay_ = false; return; }
            auto& f = frames_[depth_ - 1];
            if (f.expect == ObjectComma) f.expect = Key;
            else if (f.expect == ArrayComma) f.expect = ArrayValue;
            else okay_ = false;
            return;
        }
        if (!value()) return;
        reset_token(); scalar_ = true; append(c);
    }
};
} // namespace forearm
