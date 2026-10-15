// Copyright (c) 2026 Red Marmoset Studios Ltd.
// VNC client connection: owns the socket and a worker thread that runs the RFB protocol

#pragma once

#include <RmVncCore/Net/Socket.h>
#include <RmVncCore/Rfb/Decoders/Decoder.h>
#include <RmVncCore/Rfb/Framebuffer.h>
#include <RmVncCore/Rfb/Handshake.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace RmVnc
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
            std::vector<int32_t> encodings; // preferred order; empty = rmvnc's default order
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
            virtual void OnServerCutText(const std::string& /*latin1Text*/) {}
            virtual void OnBell() {}
            // Exactly once per Connect(). `error` is empty when the disconnect was requested locally.
            virtual void OnDisconnected(const std::string& /*error*/) {}
            // Blocking request for credentials. Return false to cancel the connection.
            virtual bool OnCredentialsRequired(bool /*needUsername*/, Credentials& /*inOutCredentials*/)
            {
                return false;
            }
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
            // Closes the connection and waits for the connection thread to finish.
            void Disconnect();
            bool IsConnected() const { return m_connected.load(); }

            Framebuffer& GetFramebuffer() { return m_framebuffer; }

            // Input. Safe to call from any thread; silently ignored while not connected.
            void SendPointerEvent(int x, int y, uint8_t buttonMask);
            void SendKeyEvent(uint32_t keysym, bool down);
            void SendClientCutText(const std::string& latin1Text);
            void RequestFullUpdate();

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

            ClientListener& m_listener;
            Framebuffer m_framebuffer;
            std::map<int32_t, std::unique_ptr<Decoder>> m_decoders;

            std::thread m_thread;
            std::mutex m_socketMutex; // guards m_socket's handle against Shutdown() racing with assignment
            std::mutex m_sendMutex;   // serialises whole messages from the UI and connection threads
            Net::Socket m_socket;
            std::atomic<bool> m_connected{false};
            std::atomic<bool> m_stopping{false};
        };
    } // namespace Rfb
} // namespace RmVnc
