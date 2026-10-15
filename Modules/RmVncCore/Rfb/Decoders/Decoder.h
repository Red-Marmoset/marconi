// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Interface for framebuffer update rectangle decoders

#pragma once

#include <RmVncCore/Rfb/Framebuffer.h>
#include <RmVncCore/Rfb/Messages.h>
#include <RmVncCore/Rfb/Stream.h>

#include <memory>

namespace RmVnc
{
    namespace Rfb
    {
        class Decoder
        {
        public:
            virtual ~Decoder() = default;

            virtual int32_t GetEncoding() const = 0;

            // Reads one rectangle's payload from `in` and draws it into `framebuffer`. The rectangle has already
            // been checked against the framebuffer bounds. Implementations must not hold the framebuffer mutex
            // while blocked on the network: read first, then lock and draw.
            virtual void Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer) = 0;

            // Called when a new connection starts, so stateful decoders (zlib streams) begin afresh.
            virtual void Reset() {}
        };

        std::unique_ptr<Decoder> CreateDecoder(int32_t encoding);
    } // namespace Rfb
} // namespace RmVnc
