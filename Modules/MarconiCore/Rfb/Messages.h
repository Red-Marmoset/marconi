// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB protocol constants (RFC 6143 + community extensions) and the pixel format / rectangle types

#pragma once

#include <MarconiCore/Rfb/Stream.h>

#include <algorithm>
#include <cstdint>

namespace Marconi
{
    namespace Rfb
    {
        namespace SecurityType
        {
            constexpr uint8_t Invalid = 0;
            constexpr uint8_t None = 1;
            constexpr uint8_t VncAuth = 2;
            constexpr uint8_t Tight = 16;
            constexpr uint8_t VeNCrypt = 19;
            constexpr uint8_t AppleRemoteDesktop = 30;
        } // namespace SecurityType

        // VeNCrypt (security type 19) subtypes. TLS* encrypt with anonymous TLS (the server has no certificate),
        // X509* with a server certificate. Then None needs no login, Vnc the VNC password and Plain a user name and
        // password. Plain on its own is unencrypted and is never chosen.
        namespace VeNCrypt
        {
            constexpr uint32_t Plain = 256;
            constexpr uint32_t TlsNone = 257;
            constexpr uint32_t TlsVnc = 258;
            constexpr uint32_t TlsPlain = 259;
            constexpr uint32_t X509None = 260;
            constexpr uint32_t X509Vnc = 261;
            constexpr uint32_t X509Plain = 262;
        } // namespace VeNCrypt

        namespace ClientMessage
        {
            constexpr uint8_t SetPixelFormat = 0;
            constexpr uint8_t SetEncodings = 2;
            constexpr uint8_t FramebufferUpdateRequest = 3;
            constexpr uint8_t KeyEvent = 4;
            constexpr uint8_t PointerEvent = 5;
            constexpr uint8_t ClientCutText = 6;
            constexpr uint8_t SetDesktopSize = 251;
        } // namespace ClientMessage

        namespace ServerMessage
        {
            constexpr uint8_t FramebufferUpdate = 0;
            constexpr uint8_t SetColourMapEntries = 1;
            constexpr uint8_t Bell = 2;
            constexpr uint8_t ServerCutText = 3;
        } // namespace ServerMessage

        namespace Encoding
        {
            constexpr int32_t Raw = 0;
            constexpr int32_t CopyRect = 1;
            constexpr int32_t Rre = 2;
            constexpr int32_t Hextile = 5;
            constexpr int32_t Tight = 7;
            constexpr int32_t Zrle = 16;

            // Pseudo-encodings
            constexpr int32_t CompressLevel0 = -256; // ... -247 for level 9
            constexpr int32_t QualityLevel0 = -32;   // ... -23 for level 9
            constexpr int32_t Cursor = -239;
            constexpr int32_t DesktopSize = -223;
            constexpr int32_t LastRect = -224;
            constexpr int32_t ExtendedDesktopSize = -308;
            constexpr int32_t ExtendedClipboard = -1063131698;
        } // namespace Encoding

        const char* EncodingName(int32_t encoding);

        // One of the server's displays, from ExtendedDesktopSize: where it sits in the desktop.
        struct Screen
        {
            uint32_t id = 0;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            uint32_t flags = 0;
        };

        struct Rect
        {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;

            bool IsEmpty() const { return width <= 0 || height <= 0; }
            int Right() const { return x + width; }
            int Bottom() const { return y + height; }

            Rect Union(const Rect& other) const
            {
                if (IsEmpty())
                {
                    return other;
                }
                if (other.IsEmpty())
                {
                    return *this;
                }
                int left = std::min(x, other.x);
                int top = std::min(y, other.y);
                return {left, top, std::max(Right(), other.Right()) - left, std::max(Bottom(), other.Bottom()) - top};
            }
        };

        struct PixelFormat
        {
            uint8_t bitsPerPixel = 32;
            uint8_t depth = 24;
            bool bigEndian = false;
            bool trueColour = true;
            uint16_t redMax = 255;
            uint16_t greenMax = 255;
            uint16_t blueMax = 255;
            uint8_t redShift = 16;
            uint8_t greenShift = 8;
            uint8_t blueShift = 0;

            // The format Marconi always asks for: 32bpp little-endian 0x00RRGGBB, so decoders can store pixels
            // straight into the Framebuffer.
            static PixelFormat Client() { return PixelFormat(); }

            static PixelFormat Read(InStream& in);
            void Write(OutBuffer& out) const;
        };
    } // namespace Rfb
} // namespace Marconi
