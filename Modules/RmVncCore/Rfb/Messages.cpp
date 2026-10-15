// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB protocol constants (RFC 6143 + community extensions) and the pixel format / rectangle types

#include <RmVncCore/Rfb/Messages.h>

namespace RmVnc
{
    namespace Rfb
    {
        const char* EncodingName(int32_t encoding)
        {
            switch (encoding)
            {
            case Encoding::Raw:
                return "Raw";
            case Encoding::CopyRect:
                return "CopyRect";
            case Encoding::Rre:
                return "RRE";
            case Encoding::Hextile:
                return "Hextile";
            case Encoding::Tight:
                return "Tight";
            case Encoding::Zrle:
                return "ZRLE";
            case Encoding::Cursor:
                return "Cursor";
            case Encoding::DesktopSize:
                return "DesktopSize";
            case Encoding::LastRect:
                return "LastRect";
            case Encoding::ExtendedDesktopSize:
                return "ExtendedDesktopSize";
            default:
                return "Unknown";
            }
        }

        PixelFormat PixelFormat::Read(InStream& in)
        {
            PixelFormat format;
            format.bitsPerPixel = in.U8();
            format.depth = in.U8();
            format.bigEndian = in.U8() != 0;
            format.trueColour = in.U8() != 0;
            format.redMax = in.U16();
            format.greenMax = in.U16();
            format.blueMax = in.U16();
            format.redShift = in.U8();
            format.greenShift = in.U8();
            format.blueShift = in.U8();
            in.Skip(3);
            return format;
        }

        void PixelFormat::Write(OutBuffer& out) const
        {
            out.U8(bitsPerPixel)
                .U8(depth)
                .U8(bigEndian ? 1 : 0)
                .U8(trueColour ? 1 : 0)
                .U16(redMax)
                .U16(greenMax)
                .U16(blueMax)
                .U8(redShift)
                .U8(greenShift)
                .U8(blueShift)
                .Padding(3);
        }
    } // namespace Rfb
} // namespace RmVnc
