// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The client side of the Marconi protocol: connects to a Marconi server and drives it on a worker thread, as a
// RemoteSession the viewer uses just like the VNC client

#pragma once

#include <MarconiCore/Net/SendQueue.h>
#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/RemoteSession.h>
#include <MarconiCore/Wire/WireChannel.h>
#include <MarconiCore/Wire/WireMessages.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        struct ClientOptions
        {
            std::string host;
            uint16_t port = kDefaultPort;
            std::string password;   // sent at login (inside TLS once encryption is added)
            std::string clientName; // shown in the server's log and its "someone connected" notice
            int connectTimeoutMs = 10000;
            uint64_t capabilities = Capability::MultipleDisplays; // what this client offers; grows with each phase
            // A server certificate with this SHA-256 fingerprint is trusted without asking (the user accepted it
            // before). Any other goes to the listener's OnUntrustedCertificate.
            std::vector<uint8_t> trustedCertificateSha256;
        };

        // Runs the protocol on a background thread. The listener's callbacks arrive on that thread; a UI listener
        // marshals them to the UI thread, exactly as it does for the VNC client. The framebuffer, input and the
        // rest of RemoteSession are safe to use from any thread.
        class Client : public RemoteSession
        {
        public:
            explicit Client(SessionListener& listener);
            ~Client() override;

            Client(const Client&) = delete;
            Client& operator=(const Client&) = delete;

            // Starts connecting on a background thread. Any previous connection is closed first.
            void Connect(const ClientOptions& options);

            // RemoteSession.
            void Disconnect() override;
            bool IsConnected() const override { return m_connected.load(); }
            bool IsEncrypted() const override { return m_encrypted.load(); }
            std::string GetEncryptionName() const override;
            Rfb::Framebuffer& GetFramebuffer() override { return m_framebuffer; }
            void SendPointerEvent(int x, int y, uint8_t buttonMask) override;
            void SendKeyEvent(uint32_t keysym, bool down) override;
            void SendClientCutText(const std::string& latin1Text) override;
            bool HasExtendedClipboard() const override { return false; }
            bool ServerAcceptsClipboardImages() const override { return false; }
            bool OfferClipboard(const Rfb::ClipboardContent& content) override;
            bool RequestServerClipboard() override { return false; }
            void RequestFullUpdate() override;
            std::vector<Rfb::Screen> GetScreens() const override;
            bool SupportsDesktopResize() const override { return m_supportsResize.load(); }
            void RequestDesktopSize(int width, int height) override;
            void SetQuality(int compressLevel, int qualityLevel) override;
            std::chrono::steady_clock::duration GetTimeSinceInput() const override;
            TrafficStats TakeTrafficStats() override;

        private:
            void Run(ClientOptions options);
            bool Handshake(MessageReader& reader, ClientOptions& options, DisconnectInfo& info);
            void HandleStill(const Still& still);
            void HandleMove(const Move& move);
            void ApplyDisplays(const std::vector<Display>& displays, bool initial);
            bool Send(const Bytes& framed); // one message, serialised; false if not connected or the write failed
            // Queues one message for the send thread, so the caller (the UI) never waits for the network. Dropped
            // while not connected. See SendQueue::Post for `replaceKey`.
            void Post(const Bytes& framed, uint32_t replaceKey = 0);
            void MarkInput();

            SessionListener& m_listener;
            Rfb::Framebuffer m_framebuffer;

            std::thread m_thread;
            Net::Socket m_socket;
            std::mutex m_writeMutex;       // serialises whole messages from the send and worker threads
            std::unique_ptr<Net::TlsStream> m_tls;   // the encrypted stream once the handshake succeeded
            std::unique_ptr<Transport> m_transport;  // over the TLS stream; guarded by m_writeMutex for Send
            std::atomic<bool> m_connected{false};
            std::atomic<bool> m_encrypted{false};
            Net::SendQueue m_sendQueue; // sends the input and requests, while connected
            mutable std::mutex m_nameMutex;
            std::string m_encryptionName; // guarded by m_nameMutex
            std::atomic<bool> m_stopping{false};
            std::atomic<bool> m_supportsResize{false};

            std::vector<Rfb::Rect> m_frameRects; // changed rects collected between FrameBegin and FrameEnd
            mutable std::mutex m_infoMutex;
            std::vector<Rfb::Screen> m_screens; // guarded by m_infoMutex

            std::atomic<std::chrono::steady_clock::rep> m_lastInputTime{0};
            std::atomic<int> m_statUpdates{0};
            std::atomic<int> m_statRectangles{0};
            std::atomic<int> m_statPointerEvents{0};
            std::atomic<int> m_statKeyEvents{0};
        };
    } // namespace Wire
} // namespace Marconi
