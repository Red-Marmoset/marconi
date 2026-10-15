// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Concrete decoder classes (exposed for tests; normal code goes through CreateDecoder)

#pragma once

#include <RmVncCore/Rfb/Decoders/Decoder.h>

#include <algorithm>
#include <vector>

namespace RmVnc
{
    namespace Rfb
    {
        class RawDecoder : public Decoder
        {
        public:
            int32_t GetEncoding() const override { return Encoding::Raw; }
            void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) override;

        private:
            std::vector<uint32_t> m_pixels;
        };
    } // namespace Rfb
} // namespace RmVnc
