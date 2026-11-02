// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The RDP graphics pipeline's messages (MS-RDPEGFX 2.2.2): what the server sends on the
// "Microsoft::Windows::RDS::Graphics" dynamic channel (surfaces, frames, encoded pictures, fills, copies and the
// cache), and the client's capability advertisement and frame acknowledgements.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        constexpr const char* kGfxChannelName = "Microsoft::Windows::RDS::Graphics";

        enum class GfxCommand : uint16_t
        {
            WireToSurface1 = 0x0001,
            WireToSurface2 = 0x0002,
            DeleteEncodingContext = 0x0003,
            SolidFill = 0x0004,
            SurfaceToSurface = 0x0005,
            SurfaceToCache = 0x0006,
            CacheToSurface = 0x0007,
            EvictCacheEntry = 0x0008,
            CreateSurface = 0x0009,
            DeleteSurface = 0x000A,
            StartFrame = 0x000B,
            EndFrame = 0x000C,
            FrameAcknowledge = 0x000D,
            ResetGraphics = 0x000E,
            MapSurfaceToOutput = 0x000F,
            CacheImportOffer = 0x0010,
            CacheImportReply = 0x0011,
            CapsAdvertise = 0x0012,
            CapsConfirm = 0x0013,
            MapSurfaceToWindow = 0x0015,
            QoeFrameAcknowledge = 0x0016,
            MapSurfaceToScaledOutput = 0x0017,
            MapSurfaceToScaledWindow = 0x0018,
        };

        // The codecs a Wire To Surface PDU names (codecId).
        enum class GfxCodec : uint16_t
        {
            Uncompressed = 0x0000,
            RemoteFx = 0x0003,
            ClearCodec = 0x0008,
            Progressive = 0x0009,
            Planar = 0x000A,
            Avc420 = 0x000B,
            Alpha = 0x000C,
            ProgressiveV2 = 0x000D,
            Avc444 = 0x000E,
            Avc444v2 = 0x000F,
        };

        // Capability set versions and flags (MS-RDPEGFX 2.2.3).
        constexpr uint32_t kGfxCapVersion8 = 0x00080004;
        constexpr uint32_t kGfxCapVersion81 = 0x00080105;
        constexpr uint32_t kGfxCapVersion10 = 0x000A0002;
        constexpr uint32_t kGfxCapVersion107 = 0x000A0701;
        constexpr uint32_t kGfxCapsAvcDisabled = 0x00000020;
        constexpr uint32_t kGfxCapsScaledMapDisable = 0x00000080;

        // A rectangle as the pipeline sends it: left/top inclusive, right/bottom exclusive.
        struct GfxRect
        {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            int Width() const { return right - left; }
            int Height() const { return bottom - top; }
        };

        struct GfxPoint
        {
            int x = 0;
            int y = 0;
        };

        // One server message. Only the fields its command uses are filled in.
        struct GfxPdu
        {
            GfxCommand command = GfxCommand::StartFrame;
            uint16_t surfaceId = 0;        // most surface commands; the destination for SurfaceToSurface
            uint16_t sourceSurfaceId = 0;  // SurfaceToSurface
            uint16_t codecId = 0;          // WireToSurface
            uint32_t codecContextId = 0;   // WireToSurface2, DeleteEncodingContext
            uint8_t pixelFormat = 0;       // WireToSurface, CreateSurface
            GfxRect rect;                  // WireToSurface1's destination; the source of SurfaceToSurface/Cache
            std::vector<GfxRect> rects;    // SolidFill
            std::vector<GfxPoint> points;  // SurfaceToSurface, CacheToSurface
            uint32_t fillColor = 0;        // SolidFill, as 0x00RRGGBB
            uint16_t cacheSlot = 0;        // SurfaceToCache, CacheToSurface, EvictCacheEntry
            uint32_t width = 0;            // CreateSurface, ResetGraphics
            uint32_t height = 0;
            uint32_t frameId = 0;          // StartFrame, EndFrame
            uint32_t outputX = 0;          // MapSurfaceToOutput (and the scaled form)
            uint32_t outputY = 0;
            uint32_t capsVersion = 0;      // CapsConfirm
            uint32_t capsFlags = 0;
            Bytes data;                    // WireToSurface: the encoded picture
        };

        // Splits a decompressed graphics message into its PDUs and parses each one this client acts on (others
        // come back with only `command` set). False with `error` if malformed.
        bool ParseGfxPdus(const Bytes& message, std::vector<GfxPdu>& out, std::string& error);

        // The client's Caps Advertise: the versions Marconi supports, H.264 turned off.
        Bytes BuildGfxCapsAdvertise();
        // A Frame Acknowledge for `frameId`, with the running count of frames decoded.
        Bytes BuildGfxFrameAcknowledge(uint32_t frameId, uint32_t totalFramesDecoded);

        // A name for a codec id, for messages.
        std::string GfxCodecName(uint16_t codecId);
    } // namespace Rdp
} // namespace Marconi
