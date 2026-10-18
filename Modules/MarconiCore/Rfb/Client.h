// Copyright (c) 2026 Red Marmoset Studios Ltd.
// VNC client connection: owns the socket and a worker thread that runs the RFB protocol

#pragma once

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/RemoteSession.h>
#include <MarconiCore/Rfb/ClipboardChannel.h>
#include <MarconiCore/Rfb/Decoders/Decoder.h>
#include <MarconiCore/Rfb/Framebuffer.h>
#include <MarconiCore/Rfb/Handshake.h>
#include <MarconiCore/Rfb/TightFileTransfer.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Rfb
    {
        struct ConnectionOptions
        {
            std::string host;
            uint16_t port = 5900;
            bool shared = true;
            Credentials credentials; // used first; the listener is asked if they're missing or rejected
            // How long to wait for the connection, and then for each of the server's answers during the handshake.
            int connectTimeoutMs = 10000;
            // A server certificate with this SHA-256 fingerprint is trusted without asking (the user accepted it
            // before). Any other certificate this computer doesn't trust goes to the listener's
            // OnUntrustedCertificate.
            std::vector<uint8_t> trustedCertificateSha256;
            // Makes the encrypted stream when a server asks for encryption. Empty means Net::CreateTlsStream; tests
            // substitute their own.
            std::function<std::unique_ptr<Net::TlsStream>(Net::Socket&, const Net::TlsOptions&)> createTlsStream;
            bool createTlsStreamAnonymous = true; // with createTlsStream: whether it makes anonymous connections too
            std::vector<int32_t> encodings; // preferred order; empty = Marconi's default order
            int compressLevel = 2;           // Tight/ZRLE zlib level hint 0-9, or -1 to let the server choose
            int qualityLevel = 8;            // Tight JPEG quality 0-9, or -1 for lossless only (no JPEG)
            // Diagnostics: send `encodings` exactly as given (pseudo-encodings included, nothing added).
            bool exactEncodings = false;
            // Repeat SetPixelFormat after the server resizes the desktop.
            bool resendPixelFormatOnResize = true;
            // Use TightVNC's own login when a server offers it: it logs in the same way, and adds file transfer.
            bool allowTight = true;
            // Ask for encryption when the server offers it and this build can encrypt. Cleared for a retry after
            // DisconnectInfo::encryptionUnusable.
            bool allowEncryption = true;
            // Offer the Extended Clipboard, which carries UTF-8 text and images with servers that support it.
            bool extendedClipboard = true;
        };

        // CursorShape, DisconnectInfo, TrafficStats and ClientListener live in <MarconiCore/RemoteSession.h>: they are
        // shared with every other kind of connection.

        class Client : public RemoteSession, private ByteSink
        {
        public:
            explicit Client(ClientListener& listener);
            ~Client() override;

            Client(const Client&) = delete;
            Client& operator=(const Client&) = delete;

            // Starts connecting on a background thread. Any previous connection is closed first.
            void Connect(const ConnectionOptions& options);
            // Closes the connection and waits for the connection thread to finish. If called from a listener
            // callback (i.e. on the connection thread) it can't wait, so the thread is detached and finishes by
            // itself: the Client must then outlive it, and Connect() must not be called until OnDisconnected.
            void Disconnect() override;
            bool IsConnected() const override { return m_connected.load(); }

            // Whether the connection is encrypted, and with what (e.g. "TLS 1.3"). Meaningful once connected. Any
            // thread.
            bool IsEncrypted() const override { return m_encrypted.load(); }

            // TightVNC file transfer on this connection (the server offered it during the Tight login).
            bool HasFileTransfer() const { return m_fileTransfer.load(); }
            // Any thread. False if not connected.
            bool SendFileTransferRequest(const Bytes& request);
            // Called on the connection thread with each file transfer reply.
            void SetFileTransferHandler(std::function<void(const FileTransfer::Reply&)> handler);
            std::string GetEncryptionName() const override;

            Framebuffer& GetFramebuffer() override { return m_framebuffer; }

            // Input. Safe to call from any thread; silently ignored while not connected.
            void SendPointerEvent(int x, int y, uint8_t buttonMask) override;
            void SendKeyEvent(uint32_t keysym, bool down) override;
            void SendClientCutText(const std::string& latin1Text) override;

            // The Extended Clipboard, once a server has announced it (any thread). OfferClipboard tells the server
            // what we have (it fetches it when someone pastes); false means the extension isn't active or the
            // server takes none of the content's formats, so text should go through SendClientCutText instead.
            // RequestServerClipboard asks for the server's clipboard, delivered through OnServerClipboard.
            bool HasExtendedClipboard() const override { return m_clipboardChannel.IsActive(); }
            bool ServerAcceptsClipboardImages() const override { return m_clipboardChannel.AcceptsImages(); }
            bool OfferClipboard(const ClipboardContent& content) override { return m_clipboardChannel.Offer(content); }
            bool RequestServerClipboard() override { return m_clipboardChannel.RequestFromServer(); }
            void RequestFullUpdate() override;

            // The server's displays, once it has described them (ExtendedDesktopSize); empty otherwise. Any thread.
            std::vector<Screen> GetScreens() const override;
            // Whether the server can be asked to change its screen size (it sent ExtendedDesktopSize). Any thread.
            bool SupportsDesktopResize() const override { return m_supportsResize.load(); }
            // Asks the server to make its desktop one display of this size. Ignored unless SupportsDesktopResize();
            // the answer comes as OnDesktopResized and OnScreenLayout, or OnDesktopResizeRefused. Any thread.
            void RequestDesktopSize(int width, int height) override;
            static Bytes BuildSetDesktopSize(int width, int height, const std::vector<Screen>& screens);

            // Changes the compression and JPEG levels (see ConnectionOptions) on a live connection by sending the
            // encodings again. Any thread; ignored until connected.
            void SetQuality(int compressLevel, int qualityLevel) override;

            // macOS Screen Sharing (RFB 3.889), which has its own quirks and keyboard. Meaningful once connected.
            bool IsAppleServer() const { return m_appleServer.load(); }

            // How long since the last key or pointer event was sent (or attempted). Any thread.
            std::chrono::steady_clock::duration GetTimeSinceInput() const override;

            // Returns the counts so far and starts again from zero. Any thread.
            TrafficStats TakeTrafficStats() override;

            // The encodings advertised by default, most preferred first (only those with decoders).
            static std::vector<int32_t> GetDefaultEncodings();

        private:
            // Reads from the encrypted stream once there is one, else straight from the socket.
            class ConnectionSource : public ByteSource
            {
            public:
                explicit ConnectionSource(Client& client) : m_client(client) {}
                ptrdiff_t Read(void* buffer, size_t length) override
                {
                    return m_client.ReadFromConnection(buffer, length);
                }

            private:
                Client& m_client;
            };

            void Send(const Bytes& data) override;
            void SendIfConnected(const Bytes& data);
            bool SendRaw(const Bytes& data); // caller holds m_sendMutex
            ptrdiff_t ReadFromConnection(void* buffer, size_t length);
            void StartTls(bool anonymous, const ConnectionOptions& options, const InStream& in);
            void Run(ConnectionOptions options);
            void SendPixelFormat();
            void SendSetupMessages(const ConnectionOptions& options);
            void SendEncodings(const std::vector<int32_t>& encodings);
            static Bytes BuildSetEncodings(const std::vector<int32_t>& encodings);
            // The real encodings we decode, plus the level hints and pseudo-encodings, in the order to advertise.
            std::vector<int32_t> ComposeEncodings(int compressLevel, int qualityLevel) const;
            void SendUpdateRequest(bool incremental);
            void MessageLoop(InStream& in);
            void HandleFramebufferUpdate(InStream& in);
            void HandleServerCutText(InStream& in);
            void HandleCursor(InStream& in, const Rect& rect);
            void HandleExtendedDesktopSize(InStream& in, const Rect& rect);
            void ResizeFramebuffer(int width, int height);
            bool IsAppleMislabelledRect(const Rect& rect);
            bool IsWholeDesktop(const Rect& rect);
            bool DecodeAppleWholeDesktop(Decoder& decoder, InStream& in, const Rect& rect);
            Rect DecodeAppleMislabelledRect(Decoder& decoder, InStream& in, const Rect& rect);

            ClientListener& m_listener;
            Framebuffer m_framebuffer;
            ClipboardChannel m_clipboardChannel;
            bool m_extendedClipboard = true; // connection thread only: whether we offered the extension
            std::map<int32_t, std::unique_ptr<Decoder>> m_decoders;
            static constexpr int kDetailedUpdates = 3;
            int m_detailedUpdates = 0; // connection thread only: how many more updates to log in detail
            std::atomic<bool> m_appleServer{false}; // macOS Screen Sharing (RFB 3.889)
            bool m_resendPixelFormatOnResize = true; // connection thread only
            std::atomic<int> m_statUpdates{0};
            std::atomic<int> m_statRectangles{0};
            std::atomic<int> m_statRequests{0};
            std::atomic<int> m_statPointerEvents{0};
            std::atomic<int> m_statKeyEvents{0};
            std::atomic<std::chrono::steady_clock::rep> m_lastInputTime{0}; // steady_clock ticks

            std::thread m_thread;
            std::mutex m_socketMutex; // guards m_socket's handle against Shutdown() racing with assignment
            std::mutex m_sendMutex;   // serialises whole messages from the UI and connection threads; guards m_tls
            Net::Socket m_socket;
            std::unique_ptr<Net::TlsStream> m_tls; // the encrypted stream once the handshake switched to one
            std::atomic<bool> m_connected{false};
        std::atomic<bool> m_fileTransfer{false};
        std::mutex m_fileTransferMutex;
        std::function<void(const FileTransfer::Reply&)> m_fileTransferHandler;
            std::atomic<bool> m_stopping{false};
            std::atomic<bool> m_encrypted{false};
            std::atomic<bool> m_supportsResize{false};
            mutable std::mutex m_infoMutex;
            std::string m_encryptionName; // guarded by m_infoMutex
            std::vector<Screen> m_screens; // guarded by m_infoMutex
            std::vector<int32_t> m_baseEncodings; // guarded by m_infoMutex: the encodings with decoders, in order
            bool m_wantExtendedClipboard = true;  // guarded by m_infoMutex
        };
    } // namespace Rfb
} // namespace Marconi
