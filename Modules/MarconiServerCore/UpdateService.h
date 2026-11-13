// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's update of this server: answers its offer, asks the person here if the settings say so, saves the
// installer in the server's own folder, checks it and starts it. The session hands it the update messages.

#pragma once

#include "UpdateHost.h"
#include "UpdatePolicy.h"

#include <MarconiCore/Wire/WireUpdateMessages.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        // The settings an update is decided by (copied from Settings when the session starts).
        struct UpdateSettings
        {
            std::string runningVersion;
            UpdatePermission permission = UpdatePermission::Ask;
            bool allowUnsigned = false;
        };

        // Who is asking, for the question and the log.
        struct UpdateViewer
        {
            std::string name;
            std::string version;
            std::string address;
        };

        class UpdateService
        {
        public:
            // `environment` may be null (this server can't update itself: every offer is answered so). `send` writes
            // one framed message to the viewer; `endSession` ends the viewer's session once the installer started.
            UpdateService(const UpdateEnvironment* environment, const UpdateSettings& settings,
                          const UpdateViewer& viewer, std::function<bool(const Wire::Bytes&)> send,
                          std::function<void()> endSession);
            ~UpdateService(); // forgets a pending question and removes a half-received file

            UpdateService(const UpdateService&) = delete;
            UpdateService& operator=(const UpdateService&) = delete;

            // The session's input thread: an update message. True if it was one (consumed). Throws WireError on a
            // malformed one, like every decoder.
            bool Handle(Wire::Type type, Wire::Reader& body);

            // While the person at this computer is being asked, the viewer's mouse, keyboard and controllers are
            // not applied, so the viewer can't answer the question itself.
            bool IsHoldingInput() const { return m_holdInput.load(); }

            // True from the moment the installer was started (the server is about to stop).
            bool HasStartedInstaller() const { return m_started.load(); }

            // The staged file's name inside the staging folder.
            static constexpr const char* kStagedFileName = "MarconiServer-update-setup.exe";

        private:
            enum class State
            {
                Idle,
                Asking,    // waiting for the person here
                Receiving, // taking UpdateChunks
                Launching, // every check passed; the installer is being started (Windows may be asking for permission)
                Done,      // finished, one way or the other (a new offer may start again unless the installer ran)
            };

            void HandleOffer(const Wire::UpdateOffer& offer);
            void HandleChunk(const Wire::UpdateChunk& chunk);
            void OnApproval(bool allowed);   // from the approver or the timer, through the guard below
            bool BeginReceiving();           // opens the staged file; false after reporting why
            void FinishReceiving();          // the whole file is here: check it, then start it on m_launchThread
            void RunLauncher();              // m_launchThread: starts the installer, then tells the viewer
            void Fail(Wire::UpdateResult::Status status, const std::string& detail);
            void Answer(Wire::UpdateAnswer::Status status, const std::string& detail = std::string());
            void ReleaseBusy();
            void DiscardFile();
            std::string StagedPath() const;
            uint64_t FreeSpace() const;
            void StopTimer();

            // Keeps late answers away once this service is gone: callbacks hold the guard, not the service.
            struct Guard
            {
                std::mutex mutex;
                bool alive = true;
                bool waiting = false; // a question is open
                std::condition_variable changed;
                UpdateService* owner = nullptr;
            };

            const UpdateEnvironment* m_environment;
            UpdateSettings m_settings;
            UpdateViewer m_viewer;
            std::function<bool(const Wire::Bytes&)> m_send;
            std::function<void()> m_endSession;

            std::mutex m_mutex; // guards the state below (the input thread and the approver's thread both use it)
            State m_state = State::Idle;
            Wire::UpdateOffer m_offer;
            std::ofstream m_file;
            uint64_t m_received = 0;
            bool m_ownsBusy = false;
            std::atomic<bool> m_holdInput{false};
            std::atomic<bool> m_started{false};

            std::shared_ptr<Guard> m_guard;
            std::thread m_timer; // declines the question when nobody answers in time
            // Starting the installer can wait minutes for Windows' permission prompt, so it has its own thread and
            // the viewer's messages keep being read meanwhile.
            std::thread m_launchThread;
        };
    } // namespace Server
} // namespace Marconi
