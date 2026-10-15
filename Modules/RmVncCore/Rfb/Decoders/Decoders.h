// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Concrete decoder classes (exposed for tests; normal code goes through CreateDecoder)

#pragma once

#include <RmVncCore/Rfb/Decoders/Decoder.h>
#include <RmVncCore/Rfb/Decoders/ZlibStream.h>

#include <algorithm>
#include <array>
#include <vector>

namespace RmVnc
{
    namespace Rfb
    {
        // Scratch image a decoder fills before copying it into the framebuffer in one locked step.
        struct PixelBuffer
        {
            int width = 0;
            int height = 0;
            std::vector<uint32_t> pixels;

            void Resize(int w, int h)
            {
                width = w;
                height = h;
                pixels.resize(static_cast<size_t>(w) * h);
            }
            uint32_t* Row(int y) { return pixels.data() + static_cast<size_t>(y) * width; }
            void Fill(int x, int y, int w, int h, uint32_t colour)
            {
                for (int row = y; row < y + h; ++row)
                {
                    std::fill_n(Row(row) + x, w, colour);
                }
            }
            void CopyTo(Framebuffer& framebuffer, int x, int y)
            {
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                framebuffer.PutPixels({x, y, width, height}, pixels.data(), width);
            }
        };

        class RawDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::Raw; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;

        private:
            std::vector<uint32_t> m_pixels;
        };

        class CopyRectDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::CopyRect; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;
        };

        class RreDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::Rre; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;

        private:
            PixelBuffer m_buffer;
        };

        class HextileDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::Hextile; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;

        private:
            PixelBuffer m_buffer;
        };

        class ZrleDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::Zrle; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;
            void Reset() override { m_zlib.Reset(); }

        private:
            ZlibStream m_zlib;
            PixelBuffer m_buffer;
        };

        class TightDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::Tight; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;
            void Reset() override;

        private:
            void DecodeBasic(InStream& in, const Rect& rect, uint8_t control);
            void DecodeJpeg(InStream& in, const Rect& rect);
            Bytes ReadData(InStream& in, int stream, size_t size);

            std::array<ZlibStream, 4> m_zlib;
            PixelBuffer m_buffer;
        };

        // Tight's "compact length": 1-3 bytes, 7 bits each, least significant first.
        uint32_t ReadCompactLength(InStream& in);
    } // namespace Rfb
} // namespace RmVnc
