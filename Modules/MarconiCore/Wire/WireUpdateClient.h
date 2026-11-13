// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer's half of updating a Marconi Server: offers the installer, sends it in pieces once the server agrees,
// and reports each step. Wire::Client owns one and hands it the update messages it receives.

#pragma once

#include <MarconiCore/Wire/WireUpdateMessages.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Marconi
{
    namespace Wire
    {
        class UpdateUploader
        {
        public:
            enum class Stage
            {
                Idle,
                Checking,         // a check-only offer is out
                CheckAnswered,    // the server answered it (see Progress::answer)
                Offering,         // the real offer is out
                WaitingForPerson, // the server is asking the person at its screen
                Sending,          // the pieces are going out
                ServerChecking,   // everything was sent; the server is checking it
                Installing,       // the server started the installer and is stopping
                Refused,          // the server said no (Progress::answer says why)
                Failed,           // the file was refused after it arrived (Progress::result), or sending failed
            };

            struct Progress
            {
                Stage stage = Stage::Idle;
                UpdateAnswer::Status answer = UpdateAnswer::Status::SendIt;
                UpdateResult::Status result = UpdateResult::Status::Installing;
                uint64_t sent = 0;
                uint64_t total = 0;
                std::string version;       // the installer's version (what was offered or checked)
                std::string serverVersion; // what the server said it runs
                std::string error;         // a failure on this side (the file couldn't be read, the link failed)
            };

            // Called with each change, on the connection's thread or the sending thread; never with a lock held.
            using Listener = std::function<void(const Progress&)>;

            // `send` writes one framed message to the server (false if it couldn't).
            explicit UpdateUploader(std::function<bool(const Bytes&)> send);
            ~UpdateUploader();

            UpdateUploader(const UpdateUploader&) = delete;
            UpdateUploader& operator=(const UpdateUploader&) = delete;

            void SetListener(Listener listener);

            // Asks what the server would say to version `version` (nothing is sent after it). False if it couldn't
            // be sent, or something is already under way.
            bool Check(const std::string& version);
            // Reads the installer, works out its SHA-256 and offers it as `version`. False (with `error`, plain
            // words) if the file can't be read, is too big, or something is already under way.
            bool Start(const std::string& installerPath, const std::string& version, std::string& error);

            // The connection's thread: an update message from the server. True if it was one (consumed).
            bool OnMessage(Type type, Reader& body);

            // The connection ended (or a new one starts): stops sending and forgets everything except whether the
            // server said it is installing (IsInstalling stays true until the next Start or Check).
            void Reset();

            Progress GetProgress() const;
            bool IsInstalling() const { return m_installing.load(); }
            // Forgets the "installing" mark once the viewer has reconnected and compared versions.
            void ClearInstalling() { m_installing = false; }

        private:
            void SetStage(Stage stage);                   // records and reports
            void Report();
            void SendPieces();                            // the sending thread
            void StopSending();

            std::function<bool(const Bytes&)> m_send;
            mutable std::mutex m_mutex;
            Listener m_listener; // guarded by m_mutex (copied out before calling)
            Progress m_progress; // guarded by m_mutex
            Bytes m_file;        // the installer being sent; guarded by m_mutex while being replaced
            std::thread m_sender;
            std::atomic<bool> m_cancel{false};
            std::atomic<bool> m_installing{false};
        };
    } // namespace Wire
} // namespace Marconi
