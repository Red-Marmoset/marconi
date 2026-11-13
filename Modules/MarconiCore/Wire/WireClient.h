// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The client side of the Marconi protocol: connects to a Marconi server and drives it on a worker thread, as a
// RemoteSession the viewer uses just like the VNC client

#pragma once

#include <MarconiCore/Files/MarconiFiles.h>
#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/RemoteSession.h>
#include <MarconiCore/Video/VideoReceiver.h>
#include <MarconiCore/Wire/WireChannel.h>
#include <MarconiCore/Wire/WireMessages.h>

#include <atomic>
#include <chrono>
#include <memory>
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
            // With no password above, ask the listener's OnCredentialsRequired for one before the login; declining
            // ends the connection with no error. Off, an empty password is sent as it is (tests, --allow-no-password).
            bool askForPassword = false;
            std::string clientName; // shown in the server's log and its "someone connected" notice
            int connectTimeoutMs = 10000;
            bool offerFiles = true; // also offer file transfer (the Files capability)
            // What this client offers; grows with each phase.
            uint64_t capabilities =
                Capability::MultipleDisplays | Capability::ResizeDisplay | Capability::Clipboard;
            // A server certificate with this SHA-256 fingerprint is trusted without asking (the user accepted it
            // before). Any other goes to the listener's OnUntrustedCertificate.
            std::vector<uint8_t> trustedCertificateSha256;
            // Video (Phase 4). The viewer offers it only when it has a decoder: the platform's, or `videoDecoderFactory`
            // (a test's, or a build's own) together with the codecs that factory can do in `videoCodecs`.
            bool offerVideo = false; // opt-in until the Windows and Mac decoders have been run (--video)
            Video::VideoPreferences video; // what the viewer wants of the stream (Hybrid, no limits by default)
            Video::VideoDecoderFactory videoDecoderFactory;
            std::vector<VideoCodec> videoCodecs;
            bool videoStats = false; // log frames a second, bitrate and decode time every few seconds (--stats)
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
            bool HasExtendedClipboard() const override { return m_clipboardGranted.load(); }
            bool ServerAcceptsClipboardImages() const override { return m_clipboardGranted.load(); }
            bool OfferClipboard(const Rfb::ClipboardContent& content) override;
            bool RequestServerClipboard() override;
            void RequestFullUpdate() override;
            std::vector<Rfb::Screen> GetScreens() const override;
            bool SupportsDesktopResize() const override { return m_supportsResize.load(); }
            void RequestDesktopSize(int width, int height) override;
            void SelectDisplay(uint32_t displayId) override;
            void SetQuality(int compressLevel, int qualityLevel) override;
            std::chrono::steady_clock::duration GetTimeSinceInput() const override;
            TrafficStats TakeTrafficStats() override;
            bool SendLock() override;

            // Video: true once the server granted it. Changes what the viewer wants of the stream (picture or still
            // only, the most bitrate and frame rate, quality); takes effect at once and does nothing without video.
            bool SupportsVideo() const { return m_videoGranted.load(); }
            bool IsVideoStreaming() const { return m_videoStreaming.load(); }
            void SetVideoPreferences(const Video::VideoPreferences& preferences);

            // File transfer with the server, once connected: true if the server agreed to it. The pointer stays
            // valid as long as this client does; its calls fail while there is no connection.
            bool SupportsFiles() const { return m_supportsFiles.load(); }
            Files::MarconiFiles* GetFiles() { return &m_files; }

        private:
            void Run(ClientOptions options);
            bool Handshake(MessageReader& reader, ClientOptions& options, DisconnectInfo& info);
            void HandleStill(const Still& still);
            void HandleMove(const Move& move);
            void HandleVideoFrame(const VideoFrame& frame);
            void ApplyDisplays(const std::vector<Display>& displays, bool initial);
            void HandleClipboardOffer(const ClipboardOffer& offer);
            void HandleClipboardRequest(const ClipboardRequest& request);
            void HandleClipboardData(ClipboardData&& data);
            bool Send(const Bytes& framed); // one message, serialised; false if not connected or the write failed
            void MarkInput();

            SessionListener& m_listener;
            Rfb::Framebuffer m_framebuffer;

            std::thread m_thread;
            Net::Socket m_socket;
            std::mutex m_writeMutex;       // serialises whole messages from the UI and worker threads
            std::unique_ptr<Net::TlsStream> m_tls;   // the encrypted stream once the handshake succeeded
            std::unique_ptr<Transport> m_transport;  // over the TLS stream; guarded by m_writeMutex for Send
            std::atomic<bool> m_connected{false};
            std::atomic<bool> m_encrypted{false};
            mutable std::mutex m_nameMutex;
            std::string m_encryptionName; // guarded by m_nameMutex
            std::atomic<bool> m_stopping{false};
            std::atomic<bool> m_supportsResize{false};
            std::atomic<bool> m_supportsFiles{false};
            Files::MarconiFiles m_files;

            // The video stream's viewer side: made at the start of a connection that offers video, used on the worker
            // thread (the UI thread only changes its preferences, through the shared pointer taken under the lock).
            std::shared_ptr<Video::VideoReceiver> GetVideo() const;
            mutable std::mutex m_videoMutex;
            std::shared_ptr<Video::VideoReceiver> m_video; // guarded by m_videoMutex
            std::atomic<bool> m_videoGranted{false};
            std::atomic<bool> m_videoStreaming{false};

            // Clipboard sharing, once the server granted it. `m_offered` is what we last told the server we have (it
            // asks for it when someone pastes); the await flags and `m_incoming` collect what we asked the server for,
            // which is delivered to the listener once every asked-for format has arrived.
            std::atomic<bool> m_clipboardGranted{false};
            std::mutex m_clipboardMutex;
            Bytes m_offeredText;
            Bytes m_offeredPng;
            int m_awaitText = 0;  // requests sent for the server's text that it hasn't answered yet
            int m_awaitImage = 0; // the same for its image (rapid copies leave several outstanding)
            Rfb::ClipboardContent m_incoming;
            std::atomic<uint32_t> m_selectedDisplay{0}; // the display being streamed, 0 for the whole desktop

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
