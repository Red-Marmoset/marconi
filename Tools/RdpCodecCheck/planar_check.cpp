// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Cross-check Marconi's planar decoder against FreeRDP 3's: pictures from FreeRDP's encoder (RGB, run-length and
// raw, with and without alpha, both row orders), and synthetic streams in every header combination (YCoCg at each
// colour loss level, half-size chroma, alpha, run-length planes from arbitrary control bytes) decoded by both.
// Not built by BuildTool. From the Marconi root, with freerdp3-dev installed (Linux):
//   g++ -std=c++20 -O1 -IModules -I/usr/include/freerdp3 -I/usr/include/winpr3
//       Tools/RdpCodecCheck/planar_check.cpp Modules/MarconiCore/Rdp/Planar.cpp -lfreerdp3 -lwinpr3 -o planar_check
#include <MarconiCore/Rdp/Planar.h>
extern "C" {
#include <freerdp/codec/planar.h>
#include <freerdp/codec/color.h>
#include <freerdp/primitives.h>
}
#include <cstdio>
#include <random>
#include <string>
#include <vector>
static std::mt19937 rng(4321);
static int compare(const char* what, const std::vector<uint32_t>& mine, const std::vector<uint32_t>& theirs)
{
    for (size_t i = 0; i < mine.size(); ++i)
    {
        if ((mine[i] & 0xFFFFFF) != (theirs[i] & 0xFFFFFF))
        {
            printf("  %s: pixel %zu differs (mine %06x freerdp %06x)\n", what, i, mine[i] & 0xFFFFFF, theirs[i] & 0xFFFFFF);
            return 1;
        }
    }
    return 0;
}
// A valid run-length plane: random control bytes that fill each row exactly.
static void RlePlane(std::vector<uint8_t>& out, int w, int h)
{
    for (int y = 0; y < h; ++y)
    {
        int x = 0;
        while (x < w)
        {
            int left = w - x;
            int kind = rng() % 3;
            if (kind == 0 && left >= 16)
            {
                int extra = std::min(15, left - 16 - (int)(rng() % 3));
                if (extra < 0) extra = 0;
                bool longer = left >= 32 + extra && rng() % 2;
                out.push_back(static_cast<uint8_t>((extra << 4) | (longer ? 2 : 1)));
                x += extra + (longer ? 32 : 16);
            }
            else
            {
                int raw = std::min(left, 1 + (int)(rng() % 15));
                int run = std::min(left - raw, (int)(rng() % 16));
                if (run == 1 || run == 2) run = 0;
                out.push_back(static_cast<uint8_t>((raw << 4) | run));
                for (int i = 0; i < raw; ++i) out.push_back(static_cast<uint8_t>(rng()));
                x += raw + run;
            }
        }
    }
}
int main()
{
    primitives_set_hints(PRIMITIVES_PURE_SOFT);
    int failures = 0, cases = 0;
    // A) FreeRDP's encoder.
    for (int t = 0; t < 24; ++t)
    {
        int w = 1 + rng() % 130, h = 1 + rng() % 70;
        bool topdown = t % 2;
        DWORD flags = (t % 4 < 2 ? PLANAR_FORMAT_HEADER_RLE : 0) | (t % 8 < 4 ? PLANAR_FORMAT_HEADER_NA : 0);
        std::vector<uint32_t> src(w * h);
        for (int i = 0; i < w * h; ++i)
        {
            int x = i % w, y = i / w;
            src[i] = (t % 3 == 0) ? (uint32_t)rng() : (((x * 3) & 255) << 16 | ((y * 5) & 255) << 8 | ((x / 7 + y / 5) % 2 ? 200 : 20)) | 0xFF000000;
        }
        BITMAP_PLANAR_CONTEXT* enc = freerdp_bitmap_planar_context_new(flags, w, h);
        freerdp_planar_topdown_image(enc, topdown);
        UINT32 size = 0;
        BYTE* data = freerdp_bitmap_compress_planar(enc, (BYTE*)src.data(), PIXEL_FORMAT_BGRA32, w, h, w * 4, nullptr, &size);
        if (!data) { printf("  encoder refused %dx%d flags %x\n", w, h, (unsigned)flags); continue; }
        std::vector<uint8_t> msg(data, data + size);
        free(data);
        freerdp_bitmap_planar_context_free(enc);
        BITMAP_PLANAR_CONTEXT* dec = freerdp_bitmap_planar_context_new(0, w, h);
        std::vector<uint32_t> theirs(w * h, 0), mine;
        bool ok1 = freerdp_bitmap_decompress_planar(dec, msg.data(), msg.size(), w, h, (BYTE*)theirs.data(), PIXEL_FORMAT_BGRX32, w * 4, 0, 0, w, h, !topdown);
        freerdp_bitmap_planar_context_free(dec);
        std::string err;
        bool ok2 = Marconi::Rdp::DecodePlanar(msg.data(), msg.size(), w, h, !topdown, mine, err);
        ++cases;
        if (!ok1 || !ok2) { printf("  encoded %d: freerdp %d mine %d %s\n", t, ok1, ok2, err.c_str()); failures++; continue; }
        char label[64]; snprintf(label, 64, "encoded %d (%dx%d flags %x)", t, w, h, (unsigned)flags);
        failures += compare(label, mine, theirs);
        // and it is the source, exactly (planar RGB is lossless)
        failures += compare(label, mine, src);
    }
    // B) synthetic streams, every header combination.
    for (int t = 0; t < 400; ++t)
    {
        int w = 1 + rng() % 90, h = 1 + rng() % 40;
        int cll = rng() % 8;
        bool cs = cll > 0 && rng() % 2;
        bool rle = rng() % 2;
        bool alpha = rng() % 2;
        bool bottomUp = rng() % 2;
        uint8_t header = (uint8_t)(cll | (cs ? 0x08 : 0) | (rle ? 0x10 : 0) | (alpha ? 0 : 0x20));
        std::vector<uint8_t> msg{header};
        int cw = cs ? (w + 1) / 2 : w, ch = cs ? (h + 1) / 2 : h;
        auto plane = [&](int pw, int ph) {
            if (rle) RlePlane(msg, pw, ph);
            else for (int i = 0; i < pw * ph; ++i) msg.push_back((uint8_t)rng());
        };
        if (alpha) plane(w, h);
        plane(w, h); plane(cw, ch); plane(cw, ch);
        if (!rle) msg.push_back(0); // the pad byte
        BITMAP_PLANAR_CONTEXT* dec = freerdp_bitmap_planar_context_new(0, w, h);
        std::vector<uint32_t> theirs(w * h, 0), mine;
        bool ok1 = freerdp_bitmap_decompress_planar(dec, msg.data(), msg.size(), w, h, (BYTE*)theirs.data(), PIXEL_FORMAT_BGRX32, w * 4, 0, 0, w, h, bottomUp);
        freerdp_bitmap_planar_context_free(dec);
        std::string err;
        bool ok2 = Marconi::Rdp::DecodePlanar(msg.data(), msg.size(), w, h, bottomUp, mine, err);
        ++cases;
        if (ok1 != ok2) { printf("  synthetic %d (header %02x %dx%d): freerdp %d mine %d %s\n", t, header, w, h, ok1, ok2, err.c_str()); failures++; continue; }
        if (!ok1) continue;
        char label[80]; snprintf(label, 80, "synthetic %d (header %02x, %dx%d, %s)", t, header, w, h, bottomUp ? "bottom up" : "top down");
        failures += compare(label, mine, theirs);
    }
    printf("planar cross-check: %d cases, %d failures\n", cases, failures);
    return failures;
}
