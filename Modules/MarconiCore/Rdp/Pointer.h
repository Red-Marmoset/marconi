// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP pointer updates (MS-RDPBCGR 2.2.9.1.1.4 slow path, 2.2.9.1.2.1.5-11 fast path): the server's pointer shape,
// decoded to the ARGB CursorShape the viewer draws, plus the hidden/default/cached/position messages. RDP milestone 4.

#pragma once

#include <MarconiCore/RemoteSession.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        enum class PointerUpdateKind
        {
            Hidden,   // the server hid its pointer (SYSPTR_NULL)
            Default,  // the server wants the ordinary arrow (SYSPTR_DEFAULT)
            Position, // the pointer moved to `x`,`y` (the server moved it, not the user)
            New,      // a new shape, in `shape`, stored at `cacheIndex`
            Cached,   // use the shape stored at `cacheIndex` earlier
            Other,    // a kind this build doesn't handle (e.g. large pointers)
        };

        struct PointerUpdate
        {
            PointerUpdateKind kind = PointerUpdateKind::Other;
            uint16_t cacheIndex = 0;
            int x = 0;
            int y = 0;
            CursorShape shape;
        };

        // Parses a slow-path Pointer Update PDU's body (TS_POINTER_PDU: messageType, pad, then the data).
        bool ParsePointerPdu(const Bytes& body, PointerUpdate& out, std::string& error);

        // Parses a fast-path pointer update: `code` is the updateCode (FASTPATH_UPDATETYPE_PTR_NULL and the rest),
        // `data` its payload. False for a code that isn't a pointer update, or malformed data.
        bool ParseFastPathPointer(uint8_t code, const Bytes& data, PointerUpdate& out, std::string& error);

        // Decodes a TS_COLORPOINTERATTRIBUTE whose XOR mask is `xorBpp` bits deep (24 for the Color Pointer Update;
        // 1, 8, 16, 24 or 32 for the New Pointer Update) into `out.shape`, with `out.cacheIndex`. An 8-bit XOR mask
        // uses `palette` (256 entries of 0x00RRGGBB) when given, a grey ramp otherwise.
        bool DecodePointerAttribute(const Bytes& data, int xorBpp, const uint32_t* palette, PointerUpdate& out,
                                    std::string& error);

        // The ordinary arrow, for when the server asks for the default pointer.
        CursorShape DefaultArrowCursor();
    } // namespace Rdp
} // namespace Marconi
