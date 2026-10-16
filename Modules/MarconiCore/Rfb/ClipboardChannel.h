// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Runs the Extended Clipboard exchange with a server: capabilities, offering what we have, fetching what it has

#pragma once

#include <MarconiCore/Rfb/Stream.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Rfb
    {
        // What a clipboard holds, in the formats Marconi carries. Either may be empty.
        struct ClipboardContent
        {
            std::string utf8Text;
            Bytes dib; // a Windows device-independent bitmap: BITMAPINFOHEADER followed by the pixels

            bool IsEmpty() const { return utf8Text.empty() && dib.empty(); }
        };

        // Used from the connection thread (server messages) and any other thread (offers and requests); every
        // outgoing message goes through `send`, every clipboard the server delivers through `onContent`, both
        // called with no lock held.
        class ClipboardChannel
        {
        public:
            static constexpr uint32_t kMaxTextBytes = 4 * 1024 * 1024;
            static constexpr uint32_t kMaxDibBytes = 64 * 1024 * 1024;

            using SendFunction = std::function<void(const Bytes& message)>;
            using ContentHandler = std::function<void(const ClipboardContent& content)>;

            ClipboardChannel(SendFunction send, ContentHandler onContent);

            // A new connection: nothing is known about the server until it sends its capabilities.
            void Reset();

            // The server announced the extension (and accepts text), so clipboard text travels as UTF-8.
            bool IsActive() const;
            // The server accepts images too.
            bool AcceptsImages() const;

            // Connection thread: the body (flags onwards) of a ServerCutText with a negative length.
            void HandleServerMessage(const Bytes& body);

            // Tells the server what we have; it asks for it when someone pastes. False if the extension isn't
            // active or the server accepts none of the content's formats, so the caller falls back to Latin-1 text.
            bool Offer(const ClipboardContent& content);

            // Asks the server for its clipboard; the answer arrives through `onContent`. False if not active.
            bool RequestFromServer();

        private:
            uint32_t FormatsOf(const ClipboardContent& content) const; // those the server accepts
            void SendProvide(uint32_t formats);                        // caller holds m_mutex

            mutable std::mutex m_mutex;
            SendFunction m_send;
            ContentHandler m_onContent;
            bool m_active = false;
            uint32_t m_serverFormats = 0; // formats the server accepts, among those Marconi carries
            ClipboardContent m_pending;   // what we last offered, for when the server asks
        };
    } // namespace Rfb
} // namespace Marconi
