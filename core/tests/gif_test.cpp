#include "forearm/gif_frames.h"
#include <cassert>
#include <memory>
#include <vector>
int main() {
    const uint8_t bytes[] = {
        71,  73,  70,  56,  57,  97,  16,  0,   16,  0,   129, 0,   0,   255, 0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   33,  255, 11,  78,  69,  84,  83,  67,  65,  80,  69,  50,  46,  48,  3,   1,   0,   0,   0,
        33,  249, 4,   0,   10,  0,   0,   0,   44,  0,   0,   0,   0,   16,  0,   16,  0,   0,   8,   29,  0,   1,
        8,   28,  72,  176, 160, 193, 131, 8,   19,  42,  92,  200, 176, 161, 195, 135, 16,  35,  74,  156, 72,  177,
        162, 197, 129, 1,   1,   0,   33,  249, 4,   1,   20,  0,   1,   0,   44,  0,   0,   0,   0,   16,  0,   16,
        0,   129, 0,   128, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   8,   29,  0,   1,   8,   28,  72,  176,
        160, 193, 131, 8,   19,  42,  92,  200, 176, 161, 195, 135, 16,  35,  74,  156, 72,  177, 162, 197, 129, 1,
        1,   0,   33,  249, 4,   1,   30,  0,   1,   0,   44,  0,   0,   0,   0,   16,  0,   16,  0,   129, 0,   0,
        255, 0,   0,   0,   0,   0,   0,   0,   0,   0,   8,   29,  0,   1,   8,   28,  72,  176, 160, 193, 131, 8,
        19,  42,  92,  200, 176, 161, 195, 135, 16,  35,  74,  156, 72,  177, 162, 197, 129, 1,   1,   0,   59};
    auto work = std::make_unique<forearm::GifWorkspace>();
    unsigned count = 0, total = 0;
    assert(forearm::gif_frames(bytes, sizeof(bytes), *work,
                               [&](const uint8_t *rgba, unsigned w, unsigned h, unsigned delay, unsigned frame) {
                                   assert(w == 16 && h == 16 && frame == count);
                                   const uint8_t expected[][3] = {{255, 0, 0}, {0, 128, 0}, {0, 0, 255}};
                                   for (unsigned i = 0; i < w * h; ++i) {
                                       for (unsigned c = 0; c < 3; ++c)
                                           assert(rgba[i * 4 + c] == expected[count][c]);
                                       assert(rgba[i * 4 + 3] == 255);
                                   }
                                   total += delay;
                                   ++count;
                                   return true;
                               }));
    assert(count == 3 && total == 600);
    for (size_t n = 0; n < sizeof(bytes); ++n)
        assert(!forearm::gif_frames(bytes, n, *work,
                                    [](const uint8_t *, unsigned, unsigned, unsigned, unsigned) { return true; }));
    for (unsigned n = 0; n < 10000; ++n) {
        std::vector<uint8_t> altered(bytes, bytes + sizeof(bytes));
        altered[(n * 3571u) % altered.size()] ^= uint8_t(n * 17u);
        forearm::gif_frames(altered.data(), altered.size(), *work,
                            [](const uint8_t *, unsigned, unsigned, unsigned, unsigned) { return true; });
    }

    // Original synthetic partial-frame fixture, independently composited with Pillow.
    // Exercises transparency and disposal 1/2/3, not just complete opaque frames.
    const uint8_t disposal[] = {
        71,  73,  70,  56,  57,  97,  32,  0,   32,  0,   129, 0,   0,   0,   0,   0,   255, 0,   0,   0,   0,   0,
        0,   0,   0,   33,  255, 11,  78,  69,  84,  83,  67,  65,  80,  69,  50,  46,  48,  3,   1,   0,   0,   0,
        33,  249, 4,   5,   3,   0,   0,   0,   44,  0,   0,   0,   0,   32,  0,   32,  0,   0,   8,   84,  0,   3,
        8,   28,  72,  48,  0,   128, 131, 8,   19,  42,  92,  88,  176, 225, 194, 135, 16,  27,  22,  132, 72,  49,
        161, 68,  130, 21,  51,  94,  28,  152, 177, 226, 70,  129, 29,  41,  126, 52,  24,  242, 225, 200, 146, 38,
        63,  162, 100, 168, 114, 165, 197, 150, 46,  99,  202, 156, 73,  179, 166, 205, 155, 56,  115, 234, 220, 201,
        179, 167, 207, 159, 64,  131, 10,  29,  74,  180, 168, 209, 163, 15,  3,   2,   0,   33,  249, 4,   9,   7,
        0,   0,   0,   44,  0,   0,   0,   0,   15,  0,   14,  0,   129, 0,   0,   0,   0,   128, 0,   0,   0,   0,
        0,   0,   0,   8,   49,  0,   1,   8,   28,  72,  176, 160, 193, 131, 8,   19,  18,  12,  192, 176, 161, 195,
        0,   11,  31,  62,  140, 40,  177, 33,  197, 138, 16,  7,   98,  180, 168, 113, 99,  70,  129, 30,  63,  2,
        8,   121, 177, 98,  73,  137, 39,  31,  6,   4,   0,   33,  249, 4,   13,  10,  0,   0,   0,   44,  0,   0,
        0,   0,   32,  0,   32,  0,   129, 0,   0,   0,   0,   0,   255, 0,   0,   0,   0,   0,   0,   8,   79,  0,
        1,   8,   28,  72,  176, 160, 193, 131, 8,   19,  42,  92,  200, 176, 161, 195, 135, 16,  35,  74,  156, 168,
        48,  128, 197, 139, 24,  3,   80,  4,   144, 177, 227, 198, 142, 25,  63,  130, 188, 40,  114, 164, 70,  138,
        38,  45,  150, 28,  185, 18,  100, 75,  143, 40,  83,  190, 12,  185, 177, 166, 205, 155, 56,  115, 234, 220,
        201, 179, 167, 207, 159, 64,  131, 10,  29,  58,  52,  32,  0,   33,  249, 4,   5,   20,  0,   0,   0,   44,
        8,   0,   6,   0,   15,  0,   14,  0,   129, 0,   0,   0,   255, 255, 0,   0,   0,   0,   0,   0,   0,   8,
        49,  0,   1,   8,   28,  72,  176, 160, 193, 131, 8,   19,  18,  12,  192, 176, 161, 195, 0,   11,  31,  62,
        140, 40,  177, 33,  197, 138, 16,  7,   98,  180, 168, 113, 99,  70,  129, 30,  63,  2,   8,   121, 177, 98,
        73,  137, 39,  31,  6,   4,   0,   59};
    const uint32_t expected_hashes[] = {4129991955u, 2471353354u, 2255944107u, 3275532290u};
    unsigned disposal_frames = 0;
    assert(forearm::gif_frames(disposal, sizeof(disposal), *work,
                               [&](const uint8_t *rgba, unsigned w, unsigned h, unsigned, unsigned frame) {
                                   assert(frame < sizeof(expected_hashes) / sizeof(expected_hashes[0]));
                                   uint32_t hash = 2166136261u;
                                   for (unsigned i = 0; i < w * h; ++i)
                                       for (unsigned c = 0; c < 4; ++c)
                                           hash = (hash ^ (rgba[i * 4 + 3] ? rgba[i * 4 + c] : 0)) * 16777619u;
                                   assert(hash == expected_hashes[frame]);
                                   ++disposal_frames;
                                   return true;
                               }));
    assert(disposal_frames == sizeof(expected_hashes) / sizeof(expected_hashes[0]));
}
