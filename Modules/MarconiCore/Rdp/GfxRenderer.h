// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Carries out the graphics pipeline's commands (MS-RDPEGFX 3.3.5): keeps the server's surfaces and bitmap cache,
// decodes pictures into the surfaces, and copies whatever lands on a surface mapped to the desktop into the
// framebuffer. No networking: Rdp::Client feeds it parsed messages and sends back the replies it produces.

#pragma once

#include <MarconiCore/Rdp/Clear.h>
#include <MarconiCore/Rdp/Gfx.h>
#include <MarconiCore/Rdp/Progressive.h>
#include <MarconiCore/Rfb/Framebuffer.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        class GfxRenderer
        {
        public:
            // `resize` is called when the server resets the graphics to a new desktop size, before anything is
            // drawn at that size; it must resize `framebuffer`.
            GfxRenderer(Rfb::Framebuffer& framebuffer, std::function<void(int width, int height)> resize);

            // What handling some messages produced.
            struct Result
            {
                std::vector<Rfb::Rect> changed; // desktop areas redrawn, for the listener
                std::vector<Bytes> replies;     // messages for the server (frame acknowledgements)
            };

            // Carries out the PDUs of one graphics message. False with `error` only for something that makes the
            // rest of the session's pictures unreliable; a picture in a codec Marconi doesn't decode is skipped and
            // logged once instead.
            bool Handle(const std::vector<GfxPdu>& pdus, Result& result, std::string& error);

            // Forgets every surface and cache entry (the channel closed or the server reset the graphics).
            void Reset();

            uint32_t FramesDecoded() const { return m_framesDecoded; }

        private:
            struct Surface
            {
                int width = 0;
                int height = 0;
                std::vector<uint32_t> pixels; // 0x00RRGGBB, `width` per row
                bool mapped = false;
                int outputX = 0;
                int outputY = 0;
                ProgressiveSurface progressive;
            };
            struct CacheEntry
            {
                int width = 0;
                int height = 0;
                std::vector<uint32_t> pixels;
            };

            bool HandleOne(const GfxPdu& pdu, Result& result, std::string& error);
            bool WireToSurface(const GfxPdu& pdu, Result& result, std::string& error);
            // Clips `rect` to the surface; false if nothing is left.
            static bool Clip(const Surface& surface, GfxRect& rect);
            // Copies `pixels` (`stride` per row, the size of `rect`) onto the surface at `rect`, clipped, and shows it.
            void Blit(Surface& surface, const GfxRect& rect, const uint32_t* pixels, int stride, Result& result);
            // Copies a changed area of a surface to the framebuffer if the surface is on the desktop.
            void Present(const Surface& surface, const GfxRect& area, Result& result);
            void WarnOnce(uint16_t codecId);
            // Logs a picture that didn't decode, the first few times.
            void WarnSkipped(const char* codec, const std::string& why);

            Rfb::Framebuffer& m_framebuffer;
            std::function<void(int, int)> m_resize;
            std::map<uint16_t, Surface> m_surfaces;
            std::map<uint16_t, CacheEntry> m_cache;
            std::set<uint16_t> m_warnedCodecs;
            std::unique_ptr<ClearDecoder> m_clear; // made on first use; its caches belong to the whole channel
            uint32_t m_framesDecoded = 0;
            int m_skipWarnings = 0;
        };
    } // namespace Rdp
} // namespace Marconi
