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
#include <utility>
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
            // connecting), or `limit` passes. True if it settled. Quiet counts from the last update or the last
            // input (NoteInput), whichever is later: straight after a click the screen hasn't changed yet, and a
            // picture taken then would show the moment before it.
            bool WaitUntilQuiet(std::chrono::milliseconds quiet, std::chrono::milliseconds limit);
            // Until the server has sent a picture since connecting or the last resize, asks for the whole desktop
            // again every 700 ms, up to `limit`. Some servers answer the first request with only the desktop's
            // size and then send only changes, so a still screen would never arrive (the viewer does the same).
            // True once there is a picture.
            bool EnsurePicture(std::chrono::milliseconds limit);

            // Asks for the whole desktop and waits up to `limit` for the answer, so the picture is current even if
            // the server has missed changes (some only send the first picture and size notices after it). True if
            // the answer came.
            bool RefreshPicture(std::chrono::milliseconds limit);

            // Waits until a picture update arrives after this call, or `limit` passes. True if one arrived. With
            // `area` (desktop pixels), only updates that touch it count.
            bool WaitForUpdate(std::chrono::milliseconds limit, const Rfb::Rect* area = nullptr);

            // The server's clipboard as UTF-8 text, as last sent (`received` false if it never has). With the
            // extended clipboard, asks for it first and waits up to `limit` for the answer.
            std::string GetServerClipboard(std::chrono::milliseconds limit, bool& received);

            // The cursor the server last sent (empty when it draws its own into the picture).
            CursorShape GetCursor() const;
            // Where we last put the pointer, if we have; the cursor is drawn there in screenshots.
            bool GetPointer(int& x, int& y) const;
            void SetPointer(int x, int y);
            // Records that input was just sent (see WaitUntilQuiet).
            void NoteInput();

        private:
            void OnConnected(const Rfb::ServerInit& serverInit) override;
            void OnFramebufferUpdated(const std::vector<Rfb::Rect>& rects) override;
            void OnDesktopResized(int width, int height) override;
            void OnScreenLayout(const std::vector<Rfb::Screen>& screens) override;
            void OnCursorShape(const CursorShape& cursor) override;
            void OnServerCutText(const std::string& latin1Text) override;
            void OnServerClipboard(const Rfb::ClipboardContent& content) override;
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
            bool m_needPicture = true; // no pixels since connecting or the last resize
            // The areas the most recent updates changed, each with its update's number (m_updates after it).
            std::vector<std::pair<uint64_t, Rfb::Rect>> m_recentRects;
            std::string m_clipboard;
            bool m_clipboardReceived = false;
            uint64_t m_clipboardUpdates = 0;
            std::chrono::steady_clock::time_point m_lastUpdate;
            std::chrono::steady_clock::time_point m_lastInput;
            CursorShape m_cursor;
            bool m_pointerKnown = false;
            int m_pointerX = 0;
            int m_pointerY = 0;
        };

        // A fingerprint as typed or as we print it, compared without case, spaces, colons or a "SHA256:" prefix.
        bool SameFingerprint(const std::string& a, const std::string& b);
    } // namespace Mcp
} // namespace Marconi
