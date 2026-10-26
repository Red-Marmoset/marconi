// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Remote Desktop (RDP) connection the viewer drives, as a RemoteSession like the VNC and Marconi clients: a
// worker thread connects (Rdp::Connection), then reads the server's bitmap and pointer updates into the framebuffer
// and sends keyboard and mouse input. Milestones 1-5 of Documentation/RDP.md, wired together.

#pragma once

#include <MarconiCore/Net/SendQueue.h>
#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Rdp/Output.h>
#include <MarconiCore/RemoteSession.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        class Connection;
        struct PointerUpdate;

        struct ClientOptions
        {
            std::string host;
            uint16_t port = 3389;
            // The account on the other computer. The user name may carry a domain as "DOMAIN\\user". When the
            // password is empty the listener is asked (CredentialsKind::Account) before connecting.
            Rfb::Credentials credentials;
            std::string clientName = "Marconi"; // this computer's name, as the server shows it
            int connectTimeoutMs = 10000;
            // The desktop size to ask the server for; the server may answer with another, which is what the
            // framebuffer then takes.
            uint16_t desktopWidth = 1024;
            uint16_t desktopHeight = 768;
            // A server certificate with this SHA-256 fingerprint is trusted without asking (the user accepted it
            // before). Any other certificate this computer doesn't trust goes to the listener's
            // OnUntrustedCertificate.
            std::vector<uint8_t> trustedCertificateSha256;
            // Makes the encrypted stream. Empty means Net::CreateTlsStream; tests substitute their own.
            std::function<std::unique_ptr<Net::TlsStream>(Net::Socket&, const Net::TlsOptions&)> createTlsStream;
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
            std::vector<Rfb::Screen> GetScreens() const override { return {}; }
            bool SupportsDesktopResize() const override { return false; }
            void RequestDesktopSize(int width, int height) override;
            void SetQuality(int compressLevel, int qualityLevel) override;
            std::chrono::steady_clock::duration GetTimeSinceInput() const override;
            TrafficStats TakeTrafficStats() override;

        private:
            static constexpr size_t kPointerCacheSize = 25; // what the Pointer capability set advertises

            void Run(ClientOptions options);
            // Connection thread: the live session, once the connection sequence is done. Returns the disconnect
            // info to report.
            void SessionLoop(Connection& connection, DisconnectInfo& info);
            void HandleMcsPdu(Connection& connection, const Bytes& mcsPdu, DisconnectInfo& info, bool& finished);
            void HandleFastPath(const Bytes& payload);
            void ApplyBitmapUpdate(const std::vector<BitmapRect>& rects);
            void ApplyPointerUpdate(const PointerUpdate& update);
            void ResizeDesktop(int width, int height);
            // The send thread: one or more input events in one Input PDU; dropped while not connected.
            void SendInputEvents(const std::vector<Bytes>& events);
            void SendPointerNow(int x, int y, uint8_t buttonMask); // the send thread: SendPointerEvent's work
            bool SendPdu(const Bytes& pdu);
            void MarkInput();

            SessionListener& m_listener;
            Rfb::Framebuffer m_framebuffer;

            std::thread m_thread;
            std::atomic<bool> m_stopping{false};
            std::atomic<bool> m_connected{false};
            std::atomic<bool> m_encrypted{false};
            mutable std::mutex m_infoMutex;
            std::string m_encryptionName; // guarded by m_infoMutex

            // The connection, made by the worker thread for its run; Disconnect() shuts it down through this, and
            // the input senders write to it (a copy of the pointer keeps it alive while they do).
            std::mutex m_connectionMutex;
            std::shared_ptr<Connection> m_connection; // guarded by m_connectionMutex
            Net::SendQueue m_sendQueue;          // sends the input and requests, so the UI never waits for them
            std::mutex m_sendMutex;              // serialises whole PDUs from the send and worker threads
            uint32_t m_shareId = 0;              // guarded by m_sendMutex
            uint16_t m_userChannelId = 0;        // guarded by m_sendMutex: the source of the PDUs we send
            uint8_t m_buttons = 0;               // the last pointer button mask sent; guarded by m_sendMutex
            int m_desktopWidth = 0;              // guarded by m_sendMutex (RequestFullUpdate needs it)
            int m_desktopHeight = 0;

            // Connection thread only.
            std::vector<uint32_t> m_palette; // for 8-bit bitmaps; empty until the server sends one
            std::array<CursorShape, kPointerCacheSize> m_pointerCache;
            std::string m_lastErrorInfo; // the server's reason for ending the connection, if it sent one
            int m_decodeWarnings = 0;    // how many bitmap decode failures have been logged

            std::atomic<std::chrono::steady_clock::rep> m_lastInputTime{0};
            std::atomic<int> m_statUpdates{0};
            std::atomic<int> m_statRectangles{0};
            std::atomic<int> m_statRequests{0};
            std::atomic<int> m_statPointerEvents{0};
            std::atomic<int> m_statKeyEvents{0};
        };
    } // namespace Rdp
} // namespace Marconi
