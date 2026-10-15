// Copyright (c) 2026 Red Marmoset Studios Ltd.
// VNC client connection: owns the socket and a worker thread that runs the RFB protocol

#pragma once

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Rfb/Decoders/Decoder.h>
#include <MarconiCore/Rfb/Framebuffer.h>
#include <MarconiCore/Rfb/Handshake.h>

#include <atomic>
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
            int connectTimeoutMs = 10000;
            std::vector<int32_t> encodings; // preferred order; empty = Marconi's default order
            int compressLevel = 2;           // Tight/ZRLE zlib level hint 0-9, or -1 to let the server choose
            int qualityLevel = 8;            // Tight JPEG quality 0-9, or -1 for lossless only (no JPEG)
        };

        // Cursor image for local rendering: ARGB pixels (alpha 0 or 255), hotspot in pixels. Empty = hidden.
        struct CursorShape
        {
            int width = 0;
            int height = 0;
            int hotspotX = 0;
            int hotspotY = 0;
            std::vector<uint32_t> argb;
        };

        struct DisconnectInfo
        {
            std::string error;                 // empty for a clean, locally requested or cancelled disconnect
            bool authenticationFailed = false; // the server rejected the credentials; retrying may help
            bool wasConnected = false;         // got as far as ServerInit
        };

        // Callbacks arrive on the connection thread. UI implementations should marshal to the UI thread.
        class ClientListener
        {
        public:
            virtual ~ClientListener() = default;

            virtual void OnConnected(const ServerInit& /*serverInit*/) {}
            // A complete FramebufferUpdate has been applied; `rects` are the areas that changed.
            virtual void OnFramebufferUpdated(const std::vector<Rect>& /*rects*/) {}
            virtual void OnDesktopResized(int /*width*/, int /*height*/) {}
            // The server sent its cursor shape: draw it locally (the framebuffer no longer contains it).
            virtual void OnCursorShape(const CursorShape& /*cursor*/) {}
            virtual void OnServerCutText(const std::string& /*latin1Text*/) {}
            virtual void OnBell() {}
            // Exactly once per Connect().
            virtual void OnDisconnected(const DisconnectInfo& /*info*/) {}
            // Blocking request for credentials. Return false to cancel the connection.
            virtual bool OnCredentialsRequired(bool /*needUsername*/, Credentials& /*inOutCredentials*/)
            {
                return false;
            }
        };

        // Counts since the last TakeTrafficStats().
        struct TrafficStats
        {
            int updates = 0;
            int rectangles = 0;
            int requests = 0;
            int pointerEvents = 0;
            int keyEvents = 0;
        };

        class Client : private ByteSink
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
            void Disconnect();
            bool IsConnected() const { return m_connected.load(); }

            Framebuffer& GetFramebuffer() { return m_framebuffer; }

            // Input. Safe to call from any thread; silently ignored while not connected.
            void SendPointerEvent(int x, int y, uint8_t buttonMask);
            void SendKeyEvent(uint32_t keysym, bool down);
            void SendClientCutText(const std::string& latin1Text);
            void RequestFullUpdate();

            // Returns the counts so far and starts again from zero. Any thread.
            TrafficStats TakeTrafficStats();

            // The encodings advertised by default, most preferred first (only those with decoders).
            static std::vector<int32_t> GetDefaultEncodings();

        private:
            void Send(const Bytes& data) override;
            void SendIfConnected(const Bytes& data);
            void Run(ConnectionOptions options);
            void SendSetupMessages(const ConnectionOptions& options);
            void SendUpdateRequest(bool incremental);
            void MessageLoop(InStream& in);
            void HandleFramebufferUpdate(InStream& in);
            void HandleServerCutText(InStream& in);
            void HandleCursor(InStream& in, const Rect& rect);
            void HandleExtendedDesktopSize(InStream& in, const Rect& rect);
            void ResizeFramebuffer(int width, int height);

            ClientListener& m_listener;
            Framebuffer m_framebuffer;
            std::map<int32_t, std::unique_ptr<Decoder>> m_decoders;
            int m_updatesReceived = 0; // connection thread only
            std::atomic<int> m_statUpdates{0};
            std::atomic<int> m_statRectangles{0};
            std::atomic<int> m_statRequests{0};
            std::atomic<int> m_statPointerEvents{0};
            std::atomic<int> m_statKeyEvents{0};

            std::thread m_thread;
            std::mutex m_socketMutex; // guards m_socket's handle against Shutdown() racing with assignment
            std::mutex m_sendMutex;   // serialises whole messages from the UI and connection threads
            Net::Socket m_socket;
            std::atomic<bool> m_connected{false};
            std::atomic<bool> m_stopping{false};
        };
    } // namespace Rfb
} // namespace Marconi
