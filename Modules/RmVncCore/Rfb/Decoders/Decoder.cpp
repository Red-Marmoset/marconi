// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Decoder factory

#include <RmVncCore/Rfb/Decoders/Decoders.h>

namespace RmVnc
{
    namespace Rfb
    {
        std::unique_ptr<Decoder> CreateDecoder(int32_t encoding)
        {
            switch (encoding)
            {
            case Encoding::Raw:
                return std::make_unique<RawDecoder>();
            case Encoding::CopyRect:
                return std::make_unique<CopyRectDecoder>();
            case Encoding::Rre:
                return std::make_unique<RreDecoder>();
            case Encoding::Hextile:
                return std::make_unique<HextileDecoder>();
            case Encoding::Zrle:
                return std::make_unique<ZrleDecoder>();
            case Encoding::Tight:
                return std::make_unique<TightDecoder>();
            default:
                return nullptr;
            }
        }
    } // namespace Rfb
} // namespace RmVnc
