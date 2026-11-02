// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Cross-check Marconi's ClearCodec (and NSCodec) decoders against FreeRDP 3's: the MS-RDPEGFX 4.1.1.1 samples
// (made by Windows), NSCodec pictures from FreeRDP's encoder, and synthetic sessions of many messages through one
// decoder (residual runs, bands with every kind of bar and cache hit, subcodecs of all three kinds, glyphs stored
// and reused, cache resets, pictures clipped by the surface edge), comparing the whole surface after each.
// Not built by BuildTool. From the Marconi root, with freerdp3-dev installed (Linux):
//   g++ -std=c++20 -O1 -IModules -I/usr/include/freerdp3 -I/usr/include/winpr3
//       Tools/RdpCodecCheck/clear_check.cpp Modules/MarconiCore/Rdp/Clear.cpp Modules/MarconiCore/Rdp/Nsc.cpp
//       -lfreerdp3 -lwinpr3 -o clear_check
#include <MarconiCore/Rdp/Clear.h>
#include <MarconiCore/Rdp/Nsc.h>
extern "C" {
#include <freerdp/codec/clear.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/color.h>
#include <freerdp/primitives.h>
#include <winpr/stream.h>
}
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>
static std::mt19937 rng(777);
struct W
{
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back(static_cast<uint8_t>(v)); }
    void u16(int v) { u8(v & 255); u8((v >> 8) & 255); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void bgr(uint32_t c) { u8(c & 255); u8((c >> 8) & 255); u8((c >> 16) & 255); }
    void add(const std::vector<uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); }
};
static uint32_t Colour() { static const uint32_t few[] = {0xFFFFFF, 0x000000, 0x3A90DB, 0xB66666}; return rng() % 3 ? few[rng() % 4] : (rng() & 0xFFFFFF); }
static void Run(W& w, uint32_t run, int firstByteSlot) // writes a run length in the 1/2/4-byte form
{
    (void)firstByteSlot;
    if (run < 0xFF) w.u8(run);
    else if (run < 0xFFFF) { w.u8(0xFF); w.u16(run); }
    else { w.u8(0xFF); w.u16(0xFFFF); w.u32(run); }
}
static std::vector<uint8_t> NscPicture(int width, int height)
{
    std::vector<uint32_t> src(width * height);
    for (int i = 0; i < width * height; ++i) src[i] = (rng() % 4 ? ((i % width) * 9 & 255) << 16 | ((i / width) * 13 & 255) << 8 | 0x40 : rng()) | 0xFF000000;
    NSC_CONTEXT* nsc = nsc_context_new();
    nsc_context_set_parameters(nsc, NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRA32);
    nsc_context_set_parameters(nsc, NSC_ALLOW_SUBSAMPLING, rng() % 2);
    nsc_context_set_parameters(nsc, NSC_COLOR_LOSS_LEVEL, 1 + rng() % 7);
    nsc_context_reset(nsc, width, height);
    wStream* s = Stream_New(nullptr, 1024 + width * height * 8);
    nsc_compose_message(nsc, s, (BYTE*)src.data(), width, height, width * 4);
    std::vector<uint8_t> out(Stream_Buffer(s), Stream_Buffer(s) + Stream_GetPosition(s));
    Stream_Free(s, TRUE);
    nsc_context_free(nsc);
    return out;
}
// One random ClearCodec message for a w x h area.
static std::vector<uint8_t> Message(uint8_t seq, int w, int h, int& glyphsStored)
{
    W m;
    uint8_t flags = 0;
    int glyphMode = rng() % 6; // 0: store, 1: hit, else none
    if (glyphMode == 0 && w * h <= 1024) flags |= 0x01;
    if (glyphMode == 1 && glyphsStored > 0) flags |= 0x03;
    if (rng() % 12 == 0) flags |= 0x04;
    m.u8(flags);
    m.u8(seq);
    if (flags & 0x01) m.u16((flags & 0x02) ? rng() % glyphsStored : glyphsStored++ % 4000);
    if ((flags & 0x03) == 0x03 && rng() % 2) return m.b; // a bare glyph hit
    W residual, bands, subcodecs;
    if (rng() % 3 && w * h > 0)
    {
        uint32_t left = w * h;
        while (left)
        {
            uint32_t run = std::min<uint32_t>(left, rng() % 3 ? 1 + rng() % 40 : 1 + rng() % 70000);
            residual.bgr(Colour());
            Run(residual, run, 0);
            left -= run;
        }
    }
    for (int band = rng() % 3; band > 0 && h > 0 && w > 0; --band)
    {
        int yStart = rng() % h, yEnd = std::min(h - 1, yStart + (int)(rng() % 52));
        int xStart = rng() % w, xEnd = std::min(w - 1, xStart + (int)(rng() % 20));
        bands.u16(xStart); bands.u16(xEnd); bands.u16(yStart); bands.u16(yEnd); bands.bgr(Colour());
        for (int i = xStart; i <= xEnd; ++i)
        {
            int kind = rng() % 4;
            if (kind == 0)
            {
                int yOn = rng() % 20, len = rng() % 30;
                if (yOn + len > 63) len = 63 - yOn;
                bands.u16(yOn | ((yOn + len) << 8));
                for (int k = 0; k < len; ++k) bands.bgr(Colour());
            }
            else if (kind == 1) { bands.u16(0x4000 | (rng() % 40)); bands.u8(rng() % 30); }
            else bands.u16(0x8000 | (rng() % 60));
        }
    }
    for (int sub = rng() % 3; sub > 0 && w > 0 && h > 0; --sub)
    {
        int sw = 1 + rng() % w, sh = 1 + rng() % h, sx = rng() % (w - sw + 1), sy = rng() % (h - sh + 1);
        int codec = rng() % 3;
        W body;
        if (codec == 0) for (int k = 0; k < sw * sh; ++k) body.bgr(Colour());
        else if (codec == 1) body.add(NscPicture(sw, sh));
        else
        {
            int count = 1 + rng() % 10;
            body.u8(count);
            for (int k = 0; k < count; ++k) body.bgr(Colour());
            int bits = 1; while (bits < 8 && (1 << bits) < count) ++bits;
            uint32_t left = sw * sh;
            while (left)
            {
                int stop = rng() % count, depth = rng() % (stop + 1);
                if ((uint32_t)depth + 1 > left) depth = left - 1;
                if (depth > stop) depth = stop;
                uint32_t run = std::min<uint32_t>(left - depth - 1, rng() % 50);
                body.u8(stop | (depth << bits));
                Run(body, run, 1);
                left -= run + depth + 1;
            }
        }
        subcodecs.u16(sx); subcodecs.u16(sy); subcodecs.u16(sw); subcodecs.u16(sh); subcodecs.u32(body.b.size()); subcodecs.u8(codec);
        subcodecs.add(body.b);
    }
    m.u32(residual.b.size()); m.u32(bands.b.size()); m.u32(subcodecs.b.size());
    m.add(residual.b); m.add(bands.b); m.add(subcodecs.b);
    return m.b;
}
static int Compare(const char* what, const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if ((a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF)) { printf("  %s: pixel %zu differs (mine %06x freerdp %06x)\n", what, i, a[i] & 0xFFFFFF, b[i] & 0xFFFFFF); return 1; }
    return 0;
}
int main()
{
    primitives_set_hints(PRIMITIVES_PURE_SOFT);
    int failures = 0, messages = 0, refused = 0;
    std::map<std::string, int> reasons;
    // A) the specification's samples (examples 2 to 4; example 1 needs a glyph cached first).
    struct Sample { const char* data; size_t size; int w, h; };
    static const char e2[] = "\x00\x0d\x00\x00\x00\x00\x00\x00\x00\x00\x82\x00\x00\x00\x00\x00\x00\x00\x4e\x00\x11\x00\x75\x00\x00\x00\x02\x0e\xff\xff\xff\x00\x00\x00\xdb\xff\xff\x00\x3a\x90\xff\xb6\x66\x66\xb6\xff\xb6\x66\x00\x90\xdb\xff\x00\x00\x3a\xdb\x90\x3a\x3a\x90\xdb\x66\x00\x00\xff\xff\xb6\x64\x64\x64\x11\x04\x11\x4c\x11\x4c\x11\x4c\x11\x4c\x11\x4c\x00\x47\x13\x00\x01\x01\x04\x00\x01\x00\x00\x47\x16\x00\x11\x02\x00\x47\x29\x00\x11\x01\x00\x49\x0a\x00\x01\x00\x04\x00\x01\x00\x00\x4a\x0a\x00\x09\x00\x01\x00\x00\x47\x05\x00\x01\x01\x1c\x00\x01\x00\x11\x4c\x11\x4c\x11\x4c\x00\x47\x0d\x4d\x00\x4d";
    static const char e3[] = "\x00\xdf\x0e\x00\x00\x00\x8b\x00\x00\x00\x00\x00\x00\x00\xfe\xfe\xfe\xff\x80\x05\xff\xff\xff\x40\xfe\xfe\xfe\x40\x00\x00\x3f\x00\x03\x00\x0b\x00\xfe\xfe\xfe\xc5\xd0\xc6\xd0\xc7\xd0\x68\xd4\x69\xd4\x6a\xd4\x6b\xd4\x6c\xd4\x6d\xd4\x1a\xd4\x1a\xd4\xa6\xd0\x6e\xd4\x6f\xd4\x70\xd4\x71\xd4\x72\xd4\x73\xd4\x74\xd4\x21\xd4\x22\xd4\x23\xd4\x24\xd4\x25\xd4\xd9\xd0\xda\xd0\xdb\xd0\xc5\xd0\xc5\xd0\xdc\xd0\xc2\xd0\x21\xd4\x22\xd4\x23\xd4\x24\xd4\x25\xd4\xc9\xd0\xca\xd0\x5a\xd4\x2b\xd1\x28\xd1\x2c\xd1\x75\xd4\x27\xd4\x28\xd4\x29\xd4\x2a\xd4\x1a\xd4\x1a\xd4\x1a\xd4\xb7\xd0\xb8\xd0\xb9\xd0\xba\xd0\xbb\xd0\xbc\xd0\xbd\xd0\xbe\xd0\xbf\xd0\xc0\xd0\xc1\xd0\xc2\xd0\xc3\xd0\xc4\xd0";
    static const char e4[] = "\x01\x0b\x78\x00\x00\x00\x00\x00\x46\x00\x00\x00\x00\x00\x00\x00\x00\x00\x06\x00\x00\x00\x0e\x00\x00\x00\x00\x00\x0f\xff\xff\xff\xff\xff\xff\xff\xff\xff\xb6\xff\xff\xff\xff\xff\xff\xff\xff\xff\xb6\x66\xff\xff\xff\xff\xff\xff\xff\xb6\x66\xdb\x90\x3a\xff\xff\xb6\xff\xff\xff\xff\xff\xff\xff\xff\xff\x46\x91\x47\x91\x48\x91\x49\x91\x4a\x91\x1b\x91";
    Sample samples[] = {{e2, sizeof(e2) - 1, 78, 17}, {e3, sizeof(e3) - 1, 64, 24}, {e4, sizeof(e4) - 1, 7, 15}};
    for (int i = 0; i < 3; ++i)
    {
        const Sample& s = samples[i];
        std::vector<uint32_t> mine(s.w * s.h, 0), theirs(s.w * s.h, 0);
        CLEAR_CONTEXT* clear = clear_context_new(FALSE);
        int rc = clear_decompress(clear, (const BYTE*)s.data, s.size, s.w, s.h, (BYTE*)theirs.data(), PIXEL_FORMAT_BGRX32, s.w * 4, 0, 0, s.w, s.h, nullptr);
        clear_context_free(clear);
        Marconi::Rdp::ClearDecoder decoder;
        std::string err;
        bool ok = decoder.Decode((const uint8_t*)s.data, s.size, mine.data(), s.w, s.h, 0, 0, s.w, s.h, err);
        char label[64]; snprintf(label, 64, "specification example %d", i + 2);
        if ((rc == 0) != ok || !ok) { printf("  %s: freerdp %d mine %d %s\n", label, rc, ok, err.c_str()); failures++; continue; }
        failures += Compare(label, mine, theirs);
        int nonBlack = 0; for (uint32_t p : mine) nonBlack += (p & 0xFFFFFF) != 0;
        printf("%s: %dx%d, %d of %d pixels drawn\n", label, s.w, s.h, nonBlack, s.w * s.h);
    }
    // B) NSCodec on its own, from FreeRDP's encoder.
    for (int t = 0; t < 60; ++t)
    {
        int w = 1 + rng() % 70, h = 1 + rng() % 40;
        std::vector<uint8_t> msg = NscPicture(w, h);
        NSC_CONTEXT* nsc = nsc_context_new();
        nsc_context_set_parameters(nsc, NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRX32);
        std::vector<uint32_t> theirs(w * h, 0), mine;
        bool ok1 = nsc_process_message(nsc, 32, w, h, msg.data(), msg.size(), (BYTE*)theirs.data(), PIXEL_FORMAT_BGRX32, w * 4, 0, 0, w, h, FREERDP_FLIP_NONE);
        nsc_context_free(nsc);
        std::string err;
        bool ok2 = Marconi::Rdp::DecodeNsc(msg.data(), msg.size(), w, h, mine, err);
        if (ok1 != ok2 || !ok1) { printf("  nsc %d (%dx%d): freerdp %d mine %d %s\n", t, w, h, ok1, ok2, err.c_str()); failures++; continue; }
        char label[64]; snprintf(label, 64, "nsc %d (%dx%d)", t, w, h);
        failures += Compare(label, mine, theirs);
    }
    // C) synthetic sessions.
    for (int session = 0; session < 40; ++session)
    {
        int sw = 40 + rng() % 200, sh = 30 + rng() % 120;
        std::vector<uint32_t> mine(sw * sh, 0), theirs(sw * sh, 0);
        CLEAR_CONTEXT* clear = clear_context_new(FALSE);
        Marconi::Rdp::ClearDecoder decoder;
        int glyphs = 0;
        uint8_t seq = rng() % 256;
        for (int k = 0; k < 60; ++k)
        {
            int w = 1 + rng() % 70, h = 1 + rng() % 60;
            // Mostly inside the surface; now and then hanging over its right and bottom edges.
            bool overhang = rng() % 6 == 0;
            int x = overhang ? rng() % sw : rng() % std::max(1, sw - w), y = overhang ? rng() % sh : rng() % std::max(1, sh - h);
            int glyphsBefore = glyphs;
            std::vector<uint8_t> msg = Message(seq, w, h, glyphs);
            bool scrambled = rng() % 30 == 0;
            if (scrambled) msg[1] ^= 0x55; // out of sequence: both must refuse
            int rc = clear_decompress(clear, msg.data(), msg.size(), w, h, (BYTE*)theirs.data(), PIXEL_FORMAT_BGRX32, sw * 4, x, y, sw, sh, nullptr);
            std::string err;
            bool ok = decoder.Decode(msg.data(), msg.size(), mine.data(), sw, sh, x, y, w, h, err);
            ++messages;
            if ((rc == 0) != ok) { printf("  session %d message %d: freerdp %d, mine %s\n", session, k, rc, ok ? "ok" : err.c_str()); failures++; break; }
            if (!ok) { ++refused; glyphs = glyphsBefore; reasons[err]++; }
            if (!scrambled) seq = static_cast<uint8_t>(seq + 1); // a refused message still used its number
            char label[64]; snprintf(label, 64, "session %d message %d", session, k);
            if (Compare(label, mine, theirs)) { failures++; break; }
        }
        clear_context_free(clear);
    }
    for (auto& r : reasons) printf("  refused %d times: %s\n", r.second, r.first.c_str());
    printf("clear cross-check: %d synthetic messages (%d refused by both), %d failures\n", messages, refused, failures);
    return failures;
}
