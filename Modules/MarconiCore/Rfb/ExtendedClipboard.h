// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Extended Clipboard pseudo-encoding: clipboard messages that carry UTF-8 text and other formats, compressed

#pragma once

#include <MarconiCore/Rfb/Stream.h>

#include <cstdint>
#include <map>
#include <string>

namespace Marconi
{
    namespace Rfb
    {
        // With this pseudo-encoding, a ServerCutText or ClientCutText with a negative length carries an extended
        // message instead of Latin-1 text: U32 flags, then a body that depends on the action bit set in the flags.
        // Formats are the low 16 bits of the flags, actions the top 8.
        namespace ExtendedClipboard
        {
            constexpr int32_t kPseudoEncoding = -1063131698;

            namespace Format
            {
                constexpr uint32_t Text = 1;  // UTF-8 with "\r\n" line endings, NUL terminated
                constexpr uint32_t Rtf = 2;
                constexpr uint32_t Html = 4;
                constexpr uint32_t Dib = 8; // a Windows device-independent bitmap (BITMAPINFOHEADER + pixels)
                constexpr uint32_t Files = 16;
                constexpr uint32_t Mask = 0xFFFF;
            } // namespace Format

            namespace Action
            {
                constexpr uint32_t Caps = 1u << 24;    // body: a U32 maximum size per format bit set, in order
                constexpr uint32_t Request = 1u << 25; // please provide these formats
                constexpr uint32_t Peek = 1u << 26;    // please notify what you have
                constexpr uint32_t Notify = 1u << 27;  // these formats are available
                constexpr uint32_t Provide = 1u << 28; // body: zlib-compressed (U32 size + data) per format, in order
                constexpr uint32_t Mask = 0xFF000000;
            } // namespace Action

            struct Message
            {
                uint32_t flags = 0;
                std::map<uint32_t, uint32_t> maxSizes; // Caps: format -> the most bytes the sender accepts
                std::map<uint32_t, Bytes> data;        // Provide: format -> its bytes, as on the wire

                uint32_t GetFormats() const { return flags & Format::Mask; }
                bool Has(uint32_t flag) const { return (flags & flag) != 0; }
            };

            // Parses an extended message's body (the flags onwards). `maxProvideBytes` caps what a Provide may
            // decompress to. Throws RfbError on malformed data.
            Message Parse(const Bytes& body, size_t maxProvideBytes);

            // Message bodies (the flags onwards), for WrapClientMessage.
            Bytes BuildCaps(const std::map<uint32_t, uint32_t>& maxSizes, uint32_t actions);
            Bytes BuildNotify(uint32_t formats);
            Bytes BuildRequest(uint32_t formats);
            Bytes BuildPeek();
            Bytes BuildProvide(const std::map<uint32_t, Bytes>& data);

            // The whole ClientCutText message carrying `body`.
            Bytes WrapClientMessage(const Bytes& body);

            // UTF-8 text as the Text format carries it ("\r\n" line endings, NUL terminated), and back.
            Bytes TextToWire(const std::string& utf8);
            std::string TextFromWire(const Bytes& data);
        } // namespace ExtendedClipboard
    } // namespace Rfb
} // namespace Marconi
