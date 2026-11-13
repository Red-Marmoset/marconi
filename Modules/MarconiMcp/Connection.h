// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The one connection the AI drives: connecting without a person to answer questions, and following the picture

#pragma once

#include <MarconiCore/QuickConnect.h>
#include <MarconiCore/RemoteSession.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        struct ConnectRequest
        {
            std::string address;
            std::string username;
            std::string password;         // empty: none (the caller may fill it from MARCONI_PASSWORD)
            std::string trustFingerprint; // accept a certificate or SSH host key with this fingerprint
            PictureQuality quality = PictureQuality::Balanced;
            std::chrono::milliseconds timeout{20000};
        };

        struct ConnectOutcome
        {
            bool connected = false;
            std::string error; // in plain words, when not connected
            // Set when the connection stopped at an untrusted certificate or SSH host key: what to pass back as
            // trustFingerprint to accept it, and what it is.
            std::string untrustedFingerprint;
            std::string untrustedDescription;
            bool needsPassword = false; // the computer asked for credentials we didn't have, or turned them down
        };

        // What the tools need to know about the connection, read under its lock.
        struct ConnectionFacts
        {
            bool connected = false;
            std::string address;
            std::string desktopName;
            int width = 0;
            int height = 0;
            std::vector<Rfb::Screen> screens;
            bool encrypted = false;
            std::string encryptionName;
            std::string lastError; // why the connection ended, if it ended by itself
        };

        // One connection at a time. A later version can keep several of these, chosen by a name in each call.
        class Connection : private SessionListener
        {
        public:
            Connection() = default;
            ~Connection() override;
            Connection(const Connection&) = delete;
            Connection& operator=(const Connection&) = delete;

            // Ends any earlier connection, connects, and waits until the computer has described its desktop, the
            // connection fails, or the timeout passes.
            ConnectOutcome Connect(const ConnectRequest& request);
            void Disconnect();

            ConnectionFacts GetFacts() const;
            // The session while connected, else null. Valid until the next Connect or Disconnect.
            RemoteSession* GetSession() const;

            // Waits until the picture has been quiet for `quiet` (and at least one picture has arrived since
            // connecting), or `limit` passes. True if it settled.
            bool WaitUntilQuiet(std::chrono::milliseconds quiet, std::chrono::milliseconds limit);
            // Waits until a picture update arrives after this call, or `limit` passes. True if one arrived.
            bool WaitForUpdate(std::chrono::milliseconds limit);

            // The cursor the server last sent (empty when it draws its own into the picture).
            CursorShape GetCursor() const;
            // Where we last put the pointer, if we have; the cursor is drawn there in screenshots.
            bool GetPointer(int& x, int& y) const;
            void SetPointer(int x, int y);

        private:
            void OnConnected(const Rfb::ServerInit& serverInit) override;
            void OnFramebufferUpdated(const std::vector<Rfb::Rect>& rects) override;
            void OnDesktopResized(int width, int height) override;
            void OnScreenLayout(const std::vector<Rfb::Screen>& screens) override;
            void OnCursorShape(const CursorShape& cursor) override;
            void OnDisconnected(const DisconnectInfo& info) override;
            bool OnCredentialsRequired(Rfb::CredentialsKind kind, Rfb::Credentials& inOutCredentials) override;
            bool OnUntrustedCertificate(const Net::TlsCertificate& certificate) override;
            bool OnUntrustedSshHostKey(const Net::SshHostKeyQuestion& question) override;

            mutable std::mutex m_mutex;
            std::condition_variable m_changed;
            std::unique_ptr<RemoteSession> m_session;
            ConnectRequest m_request;
            ConnectOutcome m_outcome;    // filled in while connecting
            bool m_finished = false;     // connected or failed: Connect can stop waiting
            bool m_connected = false;
            std::string m_desktopName;
            std::string m_lastError;
            std::vector<Rfb::Screen> m_screens;
            int m_credentialsAsked = 0;
            uint64_t m_updates = 0;
            std::chrono::steady_clock::time_point m_lastUpdate;
            CursorShape m_cursor;
            bool m_pointerKnown = false;
            int m_pointerX = 0;
            int m_pointerY = 0;
        };

        // A fingerprint as typed or as we print it, compared without case, spaces, colons or a "SHA256:" prefix.
        bool SameFingerprint(const std::string& a, const std::string& b);
    } // namespace Mcp
} // namespace Marconi
