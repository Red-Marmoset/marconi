// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The graphics pipeline's surfaces, cache and pictures, drawn into the framebuffer.

#include <MarconiCore/Rdp/GfxRenderer.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr size_t kMaxCacheEntries = 5462; // MS-RDPEGFX's largest cache (no SMALL_CACHE flag sent)
            constexpr int kMaxSurfaceSide = 16384;
        } // namespace

        GfxRenderer::GfxRenderer(Rfb::Framebuffer& framebuffer, std::function<void(int, int)> resize)
            : m_framebuffer(framebuffer), m_resize(std::move(resize))
        {
        }

        void GfxRenderer::Reset()
        {
            m_surfaces.clear();
            m_cache.clear();
        }

        bool GfxRenderer::Handle(const std::vector<GfxPdu>& pdus, Result& result, std::string& error)
        {
            for (const GfxPdu& pdu : pdus)
            {
                if (!HandleOne(pdu, result, error))
                {
                    return false;
                }
            }
            return true;
        }

        bool GfxRenderer::Clip(const Surface& surface, GfxRect& rect)
        {
            rect.left = std::max(rect.left, 0);
            rect.top = std::max(rect.top, 0);
            rect.right = std::min(rect.right, surface.width);
            rect.bottom = std::min(rect.bottom, surface.height);
            return rect.left < rect.right && rect.top < rect.bottom;
        }

        void GfxRenderer::Present(const Surface& surface, const GfxRect& area, Result& result)
        {
            if (!surface.mapped)
            {
                return;
            }
            Rfb::Rect dest{surface.outputX + area.left, surface.outputY + area.top, area.Width(), area.Height()};
            int sourceX = area.left;
            int sourceY = area.top;
            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                // Clip to the desktop, keeping the source in step.
                if (dest.x < 0)
                {
                    dest.width += dest.x;
                    sourceX -= dest.x;
                    dest.x = 0;
                }
                if (dest.y < 0)
                {
                    dest.height += dest.y;
                    sourceY -= dest.y;
                    dest.y = 0;
                }
                dest.width = std::min(dest.width, m_framebuffer.GetWidth() - dest.x);
                dest.height = std::min(dest.height, m_framebuffer.GetHeight() - dest.y);
                if (dest.IsEmpty())
                {
                    return;
                }
                m_framebuffer.PutPixels(dest,
                                        surface.pixels.data() + static_cast<size_t>(sourceY) * surface.width + sourceX,
                                        static_cast<size_t>(surface.width));
            }
            result.changed.push_back(dest);
        }

        void GfxRenderer::WarnOnce(uint16_t codecId)
        {
            if (m_warnedCodecs.insert(codecId).second)
            {
                LOG_WARNING("Rdp", "The server sent a picture in %s, which Marconi can't show yet",
                            GfxCodecName(codecId).c_str());
            }
        }

        bool GfxRenderer::WireToSurface(const GfxPdu& pdu, Result& result, std::string& error)
        {
            auto it = m_surfaces.find(pdu.surfaceId);
            if (it == m_surfaces.end())
            {
                LOG_WARNING("Rdp", "A picture for surface %u, which doesn't exist", pdu.surfaceId);
                return true;
            }
            Surface& surface = it->second;
            switch (static_cast<GfxCodec>(pdu.codecId))
            {
                case GfxCodec::Uncompressed:
                {
                    // 32-bit pixels, B, G, R and X or A, row after row.
                    GfxRect rect = pdu.rect;
                    int width = rect.Width();
                    int height = rect.Height();
                    if (width <= 0 || height <= 0 ||
                        pdu.data.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                    {
                        LOG_WARNING("Rdp", "An uncompressed picture whose size doesn't match its rectangle");
                        return true;
                    }
                    GfxRect clipped = rect;
                    if (!Clip(surface, clipped))
                    {
                        return true;
                    }
                    for (int y = clipped.top; y < clipped.bottom; ++y)
                    {
                        const uint8_t* src = pdu.data.data() + (static_cast<size_t>(y - rect.top) * width +
                                                                (clipped.left - rect.left)) * 4;
                        uint32_t* dst = surface.pixels.data() + static_cast<size_t>(y) * surface.width;
                        for (int x = clipped.left; x < clipped.right; ++x, src += 4)
                        {
                            dst[x] = (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[1]) << 8) |
                                     src[0];
                        }
                    }
                    Present(surface, clipped, result);
                    return true;
                }
                case GfxCodec::Progressive:
                {
                    std::vector<ProgressiveRect> updated;
                    if (!DecodeProgressive(pdu.data.data(), pdu.data.size(), surface.progressive,
                                           surface.pixels.data(), surface.width, surface.height, surface.width,
                                           updated, error))
                    {
                        error = "a RemoteFX Progressive picture didn't decode: " + error;
                        return false;
                    }
                    for (const ProgressiveRect& rect : updated)
                    {
                        Present(surface, GfxRect{rect.left, rect.top, rect.right, rect.bottom}, result);
                    }
                    return true;
                }
                default:
                    WarnOnce(pdu.codecId);
                    return true;
            }
        }

        bool GfxRenderer::HandleOne(const GfxPdu& pdu, Result& result, std::string& error)
        {
            switch (pdu.command)
            {
                case GfxCommand::CapsConfirm:
                    LOG_INFO("Rdp", "Graphics pipeline started (version 0x%08x, flags 0x%x)", pdu.capsVersion,
                             pdu.capsFlags);
                    return true;
                case GfxCommand::ResetGraphics:
                {
                    if (pdu.width == 0 || pdu.height == 0 || pdu.width > kMaxSurfaceSide ||
                        pdu.height > kMaxSurfaceSide)
                    {
                        error = "the server reset the graphics to an impossible desktop size";
                        return false;
                    }
                    LOG_INFO("Rdp", "Graphics reset: desktop %ux%u", pdu.width, pdu.height);
                    // As Windows' and FreeRDP's clients do: the surfaces stay, their codec state goes.
                    for (auto& entry : m_surfaces)
                    {
                        entry.second.progressive = ProgressiveSurface();
                    }
                    m_resize(static_cast<int>(pdu.width), static_cast<int>(pdu.height));
                    return true;
                }
                case GfxCommand::CreateSurface:
                {
                    if (pdu.width == 0 || pdu.height == 0 || pdu.width > kMaxSurfaceSide ||
                        pdu.height > kMaxSurfaceSide)
                    {
                        error = "the server created a surface of an impossible size";
                        return false;
                    }
                    Surface surface;
                    surface.width = static_cast<int>(pdu.width);
                    surface.height = static_cast<int>(pdu.height);
                    surface.pixels.assign(static_cast<size_t>(surface.width) * surface.height, 0);
                    m_surfaces[pdu.surfaceId] = std::move(surface);
                    return true;
                }
                case GfxCommand::DeleteSurface:
                    m_surfaces.erase(pdu.surfaceId);
                    return true;
                case GfxCommand::MapSurfaceToOutput:
                case GfxCommand::MapSurfaceToScaledOutput:
                {
                    auto it = m_surfaces.find(pdu.surfaceId);
                    if (it == m_surfaces.end())
                    {
                        return true;
                    }
                    Surface& surface = it->second;
                    if (pdu.command == GfxCommand::MapSurfaceToScaledOutput &&
                        (pdu.width != static_cast<uint32_t>(surface.width) ||
                         pdu.height != static_cast<uint32_t>(surface.height)))
                    {
                        LOG_WARNING("Rdp", "The server stretches a surface, which Marconi draws unstretched");
                    }
                    surface.mapped = true;
                    surface.outputX = static_cast<int>(pdu.outputX);
                    surface.outputY = static_cast<int>(pdu.outputY);
                    Present(surface, GfxRect{0, 0, surface.width, surface.height}, result);
                    return true;
                }
                case GfxCommand::WireToSurface1:
                case GfxCommand::WireToSurface2:
                    return WireToSurface(pdu, result, error);
                case GfxCommand::DeleteEncodingContext:
                {
                    auto it = m_surfaces.find(pdu.surfaceId);
                    if (it != m_surfaces.end())
                    {
                        it->second.progressive = ProgressiveSurface();
                    }
                    return true;
                }
                case GfxCommand::SolidFill:
                {
                    auto it = m_surfaces.find(pdu.surfaceId);
                    if (it == m_surfaces.end())
                    {
                        return true;
                    }
                    Surface& surface = it->second;
                    for (GfxRect rect : pdu.rects)
                    {
                        if (!Clip(surface, rect))
                        {
                            continue;
                        }
                        for (int y = rect.top; y < rect.bottom; ++y)
                        {
                            uint32_t* row = surface.pixels.data() + static_cast<size_t>(y) * surface.width;
                            std::fill(row + rect.left, row + rect.right, pdu.fillColor);
                        }
                        Present(surface, rect, result);
                    }
                    return true;
                }
                case GfxCommand::SurfaceToSurface:
                {
                    auto from = m_surfaces.find(pdu.sourceSurfaceId);
                    auto to = m_surfaces.find(pdu.surfaceId);
                    if (from == m_surfaces.end() || to == m_surfaces.end())
                    {
                        return true;
                    }
                    GfxRect source = pdu.rect;
                    if (!Clip(from->second, source))
                    {
                        return true;
                    }
                    // Copied out first: the source and destination may be the same surface and overlap.
                    int width = source.Width();
                    int height = source.Height();
                    std::vector<uint32_t> block(static_cast<size_t>(width) * height);
                    for (int y = 0; y < height; ++y)
                    {
                        const uint32_t* row = from->second.pixels.data() +
                                              static_cast<size_t>(source.top + y) * from->second.width + source.left;
                        std::copy(row, row + width, block.begin() + static_cast<size_t>(y) * width);
                    }
                    Surface& dest = to->second;
                    for (const GfxPoint& point : pdu.points)
                    {
                        GfxRect target{point.x, point.y, point.x + width, point.y + height};
                        GfxRect clipped = target;
                        if (!Clip(dest, clipped))
                        {
                            continue;
                        }
                        for (int y = clipped.top; y < clipped.bottom; ++y)
                        {
                            const uint32_t* src = block.data() + static_cast<size_t>(y - target.top) * width +
                                                  (clipped.left - target.left);
                            std::copy(src, src + clipped.Width(),
                                      dest.pixels.data() + static_cast<size_t>(y) * dest.width + clipped.left);
                        }
                        Present(dest, clipped, result);
                    }
                    return true;
                }
                case GfxCommand::SurfaceToCache:
                {
                    auto it = m_surfaces.find(pdu.surfaceId);
                    GfxRect source = pdu.rect;
                    if (it == m_surfaces.end() || !Clip(it->second, source))
                    {
                        return true;
                    }
                    if (m_cache.size() >= kMaxCacheEntries && m_cache.find(pdu.cacheSlot) == m_cache.end())
                    {
                        error = "the server overfilled the graphics cache";
                        return false;
                    }
                    CacheEntry entry;
                    entry.width = source.Width();
                    entry.height = source.Height();
                    entry.pixels.resize(static_cast<size_t>(entry.width) * entry.height);
                    for (int y = 0; y < entry.height; ++y)
                    {
                        const uint32_t* row = it->second.pixels.data() +
                                              static_cast<size_t>(source.top + y) * it->second.width + source.left;
                        std::copy(row, row + entry.width, entry.pixels.begin() + static_cast<size_t>(y) * entry.width);
                    }
                    m_cache[pdu.cacheSlot] = std::move(entry);
                    return true;
                }
                case GfxCommand::CacheToSurface:
                {
                    auto cached = m_cache.find(pdu.cacheSlot);
                    auto it = m_surfaces.find(pdu.surfaceId);
                    if (cached == m_cache.end() || it == m_surfaces.end())
                    {
                        return true;
                    }
                    const CacheEntry& entry = cached->second;
                    Surface& dest = it->second;
                    for (const GfxPoint& point : pdu.points)
                    {
                        GfxRect target{point.x, point.y, point.x + entry.width, point.y + entry.height};
                        GfxRect clipped = target;
                        if (!Clip(dest, clipped))
                        {
                            continue;
                        }
                        for (int y = clipped.top; y < clipped.bottom; ++y)
                        {
                            const uint32_t* src = entry.pixels.data() + static_cast<size_t>(y - target.top) * entry.width +
                                                  (clipped.left - target.left);
                            std::copy(src, src + clipped.Width(),
                                      dest.pixels.data() + static_cast<size_t>(y) * dest.width + clipped.left);
                        }
                        Present(dest, clipped, result);
                    }
                    return true;
                }
                case GfxCommand::EvictCacheEntry:
                    m_cache.erase(pdu.cacheSlot);
                    return true;
                case GfxCommand::StartFrame:
                    return true;
                case GfxCommand::EndFrame:
                    ++m_framesDecoded;
                    result.replies.push_back(BuildGfxFrameAcknowledge(pdu.frameId, m_framesDecoded));
                    return true;
                default:
                    return true; // cache import reply, window mapping (RAIL), and anything newer
            }
        }
    } // namespace Rdp
} // namespace Marconi
