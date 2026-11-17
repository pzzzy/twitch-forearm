#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace forearm {
// Original bounded GIF decoder. Caller supplies workspace outside task stacks.
struct GifWorkspace {
    uint8_t canvas[128 * 128 * 4]{}, previous[128 * 128 * 4]{}, indices[128 * 128]{}, suffix[4096]{}, stack[4096]{};
    uint16_t prefix[4096]{};
};
struct GifReader {
    const uint8_t *data;
    size_t size, pos = 0;
    bool ok = true;
    uint8_t byte() {
        if (pos >= size) {
            ok = false;
            return 0;
        }
        return data[pos++];
    }
    unsigned word() {
        unsigned a = byte();
        return a | (unsigned(byte()) << 8);
    }
    bool skip(size_t n) {
        if (n > size - pos) {
            ok = false;
            return false;
        }
        pos += n;
        return true;
    }
};
template <class Emit> bool gif_frames(const uint8_t *data, size_t size, GifWorkspace &w, Emit emit) {
    if (!data || size < 14 || (memcmp(data, "GIF89a", 6) && memcmp(data, "GIF87a", 6)))
        return false;
    GifReader r{data, size, 6};
    unsigned width = r.word(), height = r.word();
    if (!width || !height || width > 128 || height > 128)
        return false;
    unsigned packed = r.byte();
    unsigned background = r.byte();
    r.byte();
    uint8_t global[768]{}, local[768]{};
    unsigned global_n = 0;
    auto palette = [&](uint8_t *p, unsigned n) {
        if (!r.ok || n * 3 > r.size - r.pos)
            return false;
        memcpy(p, r.data + r.pos, n * 3);
        return r.skip(n * 3);
    };
    if (packed & 128) {
        global_n = 2u << (packed & 7);
        if (!palette(global, global_n))
            return false;
    }
    memset(w.canvas, 0, width * height * 4);
    unsigned disposal = 0, delay = 10, transparent = 256, frames = 0;
    size_t work = 0;
    while (r.ok) {
        unsigned tag = r.byte();
        if (tag == 0x3b)
            return frames > 0;
        if (tag == 0x21) {
            unsigned type = r.byte();
            if (type == 0xf9) {
                if (r.byte() != 4)
                    return false;
                unsigned flags = r.byte();
                delay = r.word();
                unsigned t = r.byte();
                if (r.byte() != 0)
                    return false;
                disposal = (flags >> 2) & 7;
                transparent = (flags & 1) ? t : 256;
                if (disposal > 3)
                    return false;
            } else {
                for (unsigned n = r.byte(); n && r.ok; n = r.byte())
                    if (!r.skip(n))
                        return false;
            }
            continue;
        }
        if (tag != 0x2c || ++frames > 512)
            return false;
        unsigned x = r.word(), y = r.word(), fw = r.word(), fh = r.word(), flags = r.byte();
        if (!fw || !fh || x + fw > width || y + fh > height)
            return false;
        auto *colors = global;
        unsigned count = global_n;
        if (flags & 128) {
            count = 2u << (flags & 7);
            if (!palette(local, count))
                return false;
            colors = local;
        }
        if (!count)
            return false;
        unsigned minimum = r.byte();
        if (minimum < 2 || minimum > 8)
            return false;
        unsigned block = 0, bits = 0, nbits = 0;
        bool terminated = false;
        auto code = [&](unsigned n) -> int {
            while (nbits < n) {
                if (!block) {
                    block = r.byte();
                    if (!r.ok || !block) {
                        terminated = true;
                        return -1;
                    }
                }
                unsigned b = r.byte();
                --block;
                if (!r.ok)
                    return -1;
                bits |= b << nbits;
                nbits += 8;
            }
            unsigned c = bits & ((1u << n) - 1);
            bits >>= n;
            nbits -= n;
            return int(c);
        };
        unsigned clear = 1u << minimum, end = clear + 1, next = end + 1, code_size = minimum + 1;
        int old = -1;
        uint8_t first = 0;
        size_t out = 0;
        bool ended = false;
        for (;;) {
            if (++work > 8000000)
                return false;
            int c = code(code_size);
            if (c < 0)
                break;
            if (unsigned(c) == clear) {
                next = end + 1;
                code_size = minimum + 1;
                old = -1;
                continue;
            }
            if (unsigned(c) == end) {
                ended = true;
                break;
            }
            unsigned k = unsigned(c), sp = 0;
            if (k >= next) {
                if (k != next || old < 0)
                    return false;
                w.stack[sp++] = first;
                k = unsigned(old);
            }
            while (k >= clear) {
                if (k >= next || sp >= 4095 || ++work > 8000000)
                    return false;
                w.stack[sp++] = w.suffix[k];
                k = w.prefix[k];
            }
            first = uint8_t(k);
            w.stack[sp++] = first;
            while (sp) {
                if (out >= fw * fh)
                    return false;
                w.indices[out++] = w.stack[--sp];
            }
            if (old >= 0 && next < 4096) {
                w.prefix[next] = uint16_t(old);
                w.suffix[next] = first;
                ++next;
                if (next == (1u << code_size) && code_size < 12)
                    ++code_size;
            }
            old = c;
        }
        if (!ended || !r.ok || out != fw * fh)
            return false;
        if (!terminated) {
            if (!r.skip(block))
                return false;
            for (unsigned n = r.byte(); n && r.ok; n = r.byte())
                if (!r.skip(n))
                    return false;
        }
        if (frames == 1 && global_n > background)
            for (unsigned i = 0; i < width * height; ++i) {
                memcpy(w.canvas + i * 4, global + background * 3, 3);
                w.canvas[i * 4 + 3] = transparent == 256 ? 255 : 0;
            }
        if (disposal == 3)
            memcpy(w.previous, w.canvas, width * height * 4);
        unsigned input_row = 0;
        auto row = [&](unsigned target) {
            for (unsigned col = 0; col < fw; ++col) {
                unsigned index = w.indices[input_row * fw + col];
                if (index != transparent) {
                    if (index >= count)
                        return false;
                    auto *pixel = w.canvas + ((y + target) * width + x + col) * 4;
                    memcpy(pixel, colors + index * 3, 3);
                    pixel[3] = 255;
                }
            }
            ++input_row;
            return true;
        };
        if (flags & 64) {
            const unsigned starts[] = {0, 4, 2, 1}, steps[] = {8, 8, 4, 2};
            for (unsigned p = 0; p < 4; ++p)
                for (unsigned j = starts[p]; j < fh; j += steps[p])
                    if (!row(j))
                        return false;
        } else
            for (unsigned j = 0; j < fh; ++j)
                if (!row(j))
                    return false;
        if (!emit(w.canvas, width, height, delay ? std::clamp(delay * 10u, 10u, 10000u) : 100u, frames - 1))
            return false;
        if (disposal == 2)
            for (unsigned j = y; j < y + fh; ++j)
                for (unsigned col = x; col < x + fw; ++col) {
                    auto *p = w.canvas + (j * width + col) * 4;
                    if (global_n > background)
                        memcpy(p, global + background * 3, 3);
                    else
                        memset(p, 0, 3);
                    p[3] = transparent == 256 ? 255 : 0;
                }
        if (disposal == 3)
            memcpy(w.canvas, w.previous, width * height * 4);
        disposal = 0;
        delay = 10;
        transparent = 256;
    }
    return false;
}
} // namespace forearm
