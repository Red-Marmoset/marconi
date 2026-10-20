// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The client side of RDP: connects to a Remote Desktop server and drives it on a worker thread, as a RemoteSession
// the viewer uses just like the VNC and Marconi clients. It ties the connection sequence (Rdp/Connect) to the live
// loop: decoding the server's bitmap updates into the framebuffer and sending keyboard and mouse input. RDP
// milestones 4-5 in Documentation/RDP.md.

#pragma once

#include <MarconiCore/RemoteSession.h>

#include <atomic>
#include <chrono>
#include <cstdint>
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

        struct ClientOptions
        {
            std::string host;
            uint16_t port = 3389;
            std::string user;     // the Windows account to log in as
            std::string password; // its password
            std::string domain;   // its domain, or empty for a local account
            std::string clientName = "Marconi";
            int connectTimeoutMs = 15000;
            uint16_t width = 1280;  // the desktop size to ask the server for
            uint16_t height = 800;
            // A server certificate with this SHA-256 fingerprint is trusted without asking (the user accepted it
            // before). Any other goes to the listener's OnUntrustedCertificate.
            std::vector<uint8_t> trustedCertificateSha256;
        };

        // Runs RDP on a background thread. The listener's callbacks arrive on that thread; a UI listener marshals
        // them to the UI thread, exactly as it does for the other clients. The framebuffer, input and the rest of
        // RemoteSession are safe to use from any thread.
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
            void Run(ClientOptions options);
            void ApplyRect(const struct BitmapRect& rect); // decodes one rectangle into the framebuffer
            void SendInput(const std::vector<std::vector<uint8_t>>& events);
            void MarkInput();

            SessionListener& m_listener;
            Rfb::Framebuffer m_framebuffer;

            std::thread m_thread;
            std::mutex m_connectionMutex;            // guards m_connection against the input/worker threads
            std::unique_ptr<Connection> m_connection; // the live connection, guarded by m_connectionMutex
            std::atomic<bool> m_connected{false};
            std::atomic<bool> m_encrypted{false};
            std::atomic<bool> m_stopping{false};
            mutable std::mutex m_nameMutex;
            std::string m_encryptionName; // guarded by m_nameMutex

            std::vector<Rfb::Rect> m_frameRects; // changed rects collected within one output PDU (worker thread)
            uint8_t m_buttonState = 0;           // the VNC button mask last sent (for RDP's per-button down/up)

            std::atomic<std::chrono::steady_clock::rep> m_lastInputTime{0};
            std::atomic<int> m_statUpdates{0};
            std::atomic<int> m_statRectangles{0};
            std::atomic<int> m_statPointerEvents{0};
            std::atomic<int> m_statKeyEvents{0};
        };
    } // namespace Rdp
} // namespace Marconi
