// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ClearCodec (MS-RDPEGFX 2.2.4.1), which Windows uses over the graphics pipeline for text and other sharp-edged
// content: a run-length "residual" layer, "bands" of vertical bars from two caches, subcodec rectangles
// (uncompressed, NSCodec or palette run-length), and a glyph cache. The caches and a sequence number are shared by
// the whole graphics channel, so one decoder serves every surface.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        class ClearDecoder
        {
        public:
            ClearDecoder();

            // Decodes one ClearCodec bitmap for the `width` by `height` area at `x`,`y` of a surface (`pixels`,
            // 0x00RRGGBB, `surfaceWidth` by `surfaceHeight`, rows `surfaceWidth` apart). Parts outside the surface
            // are clipped where the format allows it. False with `error` if malformed (the caches may then hold
            // part of it, as with any decoder that shares them).
            bool Decode(const uint8_t* data, size_t size, uint32_t* pixels, int surfaceWidth, int surfaceHeight,
                        int x, int y, int width, int height, std::string& error);

            // What the server's Reset Graphics resets: the sequence number only (the caches outlive it).
            void ResetSequence() { m_sequence = 0; }

        private:
            struct Bar
            {
                std::vector<uint32_t> pixels; // at least `count` long
                uint32_t count = 0;
                void Fit(); // grows `pixels` (with black) to `count`
            };
            struct Glyph
            {
                std::vector<uint32_t> pixels;
                uint32_t count = 0;
            };

            struct Target
            {
                uint32_t* pixels;
                int surfaceWidth;
                int surfaceHeight;
                int x;
                int y;
                int width;
                int height;
            };

            bool Residual(const uint8_t* data, size_t size, const Target& target, std::string& error);
            bool Bands(const uint8_t* data, size_t size, const Target& target, std::string& error);
            bool Subcodecs(const uint8_t* data, size_t size, const Target& target, std::string& error);

            uint8_t m_sequence = 0;
            std::vector<Bar> m_bars;      // 32768 full-height vertical bars
            std::vector<Bar> m_shortBars; // 16384 partial bars
            uint32_t m_barCursor = 0;
            uint32_t m_shortBarCursor = 0;
            std::vector<Glyph> m_glyphs;  // 4000
        };
    } // namespace Rdp
} // namespace Marconi
