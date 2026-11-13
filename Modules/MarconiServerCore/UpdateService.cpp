// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's update of this server

#include "UpdateService.h"

#include <MarconiCore/Wire/WireMessages.h>

#include <Log/Log.h>

#include <chrono>
#include <filesystem>
#include <system_error>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            namespace fs = std::filesystem;

            // One update at a time for the whole program, whichever viewer offers it.
            std::atomic<bool> s_updateUnderWay{false};

            // A name as it may go in the log (it comes from the viewer): control characters become spaces.
            std::string LogSafe(const std::string& text)
            {
                std::string safe = text.substr(0, 64);
                for (char& c : safe)
                {
                    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                    {
                        c = ' ';
                    }
                }
                return safe;
            }
        } // namespace

        std::vector<std::string> SilentInstallArguments()
        {
            // /S is NSIS's silent mode; /restart is Installer/MarconiServer.nsi's own: start Marconi Server again at
            // the end even though it had already closed itself before Setup looked for it.
            return {"/S", "/restart"};
        }

        UpdateService::UpdateService(const UpdateEnvironment* environment, const UpdateSettings& settings,
                                     const UpdateViewer& viewer, std::function<bool(const Wire::Bytes&)> send,
                                     std::function<void()> endSession)
            : m_environment(environment), m_settings(settings), m_viewer(viewer), m_send(std::move(send)),
              m_endSession(std::move(endSession)), m_guard(std::make_shared<Guard>())
        {
            m_guard->owner = this;
        }

        UpdateService::~UpdateService()
        {
            bool wasWaiting = false;
            {
                std::lock_guard<std::mutex> lock(m_guard->mutex); // waits out an answer being handled right now
                m_guard->alive = false;
                wasWaiting = m_guard->waiting;
                m_guard->waiting = false;
                m_guard->changed.notify_all();
            }
            if (m_timer.joinable())
            {
                m_timer.join();
            }
            if (wasWaiting && m_environment != nullptr && m_environment->approver != nullptr)
            {
                m_environment->approver->Withdraw(); // the viewer left: the question can close
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state == State::Receiving)
            {
                DiscardFile(); // half an installer is no use to anyone
            }
            ReleaseBusy();
        }

        void UpdateService::StopTimer()
        {
            {
                std::lock_guard<std::mutex> lock(m_guard->mutex);
                m_guard->changed.notify_all();
            }
            if (m_timer.joinable() && m_timer.get_id() != std::this_thread::get_id())
            {
                m_timer.join();
            }
        }

        void UpdateService::ReleaseBusy()
        {
            if (m_ownsBusy)
            {
                m_ownsBusy = false;
                s_updateUnderWay = false;
            }
        }

        std::string UpdateService::StagedPath() const
        {
            return (fs::path(m_environment->stagingFolder) / kStagedFileName).string();
        }

        uint64_t UpdateService::FreeSpace() const
        {
            if (m_environment == nullptr)
            {
                return UINT64_MAX;
            }
            if (m_environment->freeSpace)
            {
                return m_environment->freeSpace(m_environment->stagingFolder);
            }
            std::error_code code;
            fs::path folder(m_environment->stagingFolder);
            // The folder may not exist yet: ask about the nearest one that does.
            while (!folder.empty() && !fs::exists(folder, code))
            {
                folder = folder.parent_path();
            }
            fs::space_info info = fs::space(folder.empty() ? fs::path(".") : folder, code);
            return code ? UINT64_MAX : static_cast<uint64_t>(info.available);
        }

        void UpdateService::DiscardFile()
        {
            if (m_file.is_open())
            {
                m_file.close();
            }
            if (m_environment != nullptr && !m_environment->stagingFolder.empty())
            {
                std::error_code code;
                fs::remove(StagedPath(), code);
            }
        }

        void UpdateService::Answer(Wire::UpdateAnswer::Status status, const std::string& detail)
        {
            Wire::UpdateAnswer answer;
            answer.status = status;
            answer.serverVersion = m_settings.runningVersion;
            answer.detail = detail;
            m_send(answer.Encode());
        }

        void UpdateService::Fail(Wire::UpdateResult::Status status, const std::string& detail)
        {
            LOG_INFO("Server", "Update from '%s' refused (%u): %s", LogSafe(m_viewer.name).c_str(),
                     static_cast<unsigned>(status), detail.c_str());
            DiscardFile();
            m_state = State::Done;
            ReleaseBusy();
            Wire::UpdateResult result;
            result.status = status;
            result.detail = detail;
            m_send(result.Encode());
        }

        bool UpdateService::Handle(Wire::Type type, Wire::Reader& body)
        {
            if (type == Wire::Type::UpdateOffer)
            {
                HandleOffer(Wire::UpdateOffer::Decode(body));
                return true;
            }
            if (type == Wire::Type::UpdateChunk)
            {
                HandleChunk(Wire::UpdateChunk::Decode(body));
                return true;
            }
            return false;
        }

        void UpdateService::HandleOffer(const Wire::UpdateOffer& offer)
        {
            using Status = Wire::UpdateAnswer::Status;
            UpdateApprover* approver = nullptr;
            UpdateQuestion question;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                OfferContext context;
                context.runningVersion = m_settings.runningVersion;
                context.permission = m_settings.permission;
                context.allowUnsigned = m_settings.allowUnsigned;
                context.supported = m_environment != nullptr && m_environment->launcher != nullptr &&
                                    !m_environment->stagingFolder.empty();
                if (m_environment != nullptr)
                {
                    context.platform = m_environment->platform;
                    context.canAskPerson = m_environment->approver != nullptr;
                    context.runningSigned =
                        m_environment->inspector != nullptr &&
                        m_environment->inspector->InspectRunningServer().signature == SignatureState::Valid;
                }
                const bool inProgress = m_state == State::Asking || m_state == State::Receiving || m_started;
                context.busy = inProgress || s_updateUnderWay.load();
                if (!offer.IsCheckOnly() && context.supported)
                {
                    context.freeBytes = FreeSpace();
                }
                Status status = DecideOffer(offer, context);
                if (offer.IsCheckOnly() || (status != Status::SendIt && status != Status::AskingPerson))
                {
                    if (!offer.IsCheckOnly())
                    {
                        LOG_INFO("Server", "Update offer from '%s' (version %s) refused: answer %u",
                                 LogSafe(m_viewer.name).c_str(), LogSafe(offer.version).c_str(),
                                 static_cast<unsigned>(status));
                    }
                    Answer(status);
                    return;
                }
                bool expected = false;
                if (!s_updateUnderWay.compare_exchange_strong(expected, true))
                {
                    Answer(Status::Busy);
                    return;
                }
                m_ownsBusy = true;
                m_offer = offer;
                m_received = 0;
                LOG_INFO("Server", "Viewer '%s' (Marconi %s) offers Marconi Server %s (%llu bytes); %s",
                         LogSafe(m_viewer.name).c_str(), LogSafe(m_viewer.version).c_str(),
                         LogSafe(offer.version).c_str(), static_cast<unsigned long long>(offer.size),
                         status == Status::AskingPerson ? "asking the person at this computer" : "allowed by the settings");
                if (status == Status::SendIt)
                {
                    if (BeginReceiving())
                    {
                        Answer(Status::SendIt);
                    }
                    return;
                }
                m_state = State::Asking;
                m_holdInput = true; // the viewer can't click "allow" for the person here
                Answer(Status::AskingPerson);
                approver = m_environment->approver;
                question.viewerName = m_viewer.name;
                question.viewerVersion = m_viewer.version;
                question.address = m_viewer.address;
                question.currentVersion = m_settings.runningVersion;
                question.newVersion = offer.version;
            }

            // Outside the lock: an approver may answer straight away, on this thread.
            StopTimer();
            {
                std::lock_guard<std::mutex> lock(m_guard->mutex);
                m_guard->waiting = true;
            }
            std::weak_ptr<Guard> weak = m_guard;
            const int timeoutMs = m_environment->approvalTimeoutMs;
            m_timer = std::thread([weak, timeoutMs]() {
                std::shared_ptr<Guard> guard = weak.lock();
                if (!guard)
                {
                    return;
                }
                std::unique_lock<std::mutex> lock(guard->mutex);
                if (!guard->changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                             [&]() { return !guard->alive || !guard->waiting; }))
                {
                    guard->waiting = false; // nobody answered in time: that counts as no
                    UpdateService* owner = guard->owner;
                    lock.unlock();
                    // The owner can't go away meanwhile: its destructor joins this thread before it is gone.
                    owner->OnApproval(false);
                    if (owner->m_environment->approver != nullptr)
                    {
                        owner->m_environment->approver->Withdraw();
                    }
                }
            });
            approver->Ask(question, [weak](bool allowed) {
                std::shared_ptr<Guard> guard = weak.lock();
                if (!guard)
                {
                    return; // the viewer left long ago
                }
                std::lock_guard<std::mutex> lock(guard->mutex);
                if (!guard->alive || !guard->waiting)
                {
                    return; // too late: the time ran out or the viewer left
                }
                guard->waiting = false;
                guard->changed.notify_all();
                guard->owner->OnApproval(allowed);
            });
        }

        void UpdateService::OnApproval(bool allowed)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state != State::Asking)
            {
                return;
            }
            m_holdInput = false;
            if (!allowed)
            {
                LOG_INFO("Server", "The update from '%s' was turned down here (or not answered in time)",
                         LogSafe(m_viewer.name).c_str());
                m_state = State::Done;
                ReleaseBusy();
                Answer(Wire::UpdateAnswer::Status::Declined);
                return;
            }
            LOG_INFO("Server", "The update from '%s' was allowed here", LogSafe(m_viewer.name).c_str());
            if (BeginReceiving())
            {
                Answer(Wire::UpdateAnswer::Status::SendIt);
            }
        }

        bool UpdateService::BeginReceiving()
        {
            std::error_code code;
            fs::path folder(m_environment->stagingFolder);
            fs::create_directories(folder, code);
#if !defined(_WIN32)
            // Only this account may read or change what is waiting to be run (on Windows the data folder's
            // permissions, set by the installer, do this).
            fs::permissions(folder, fs::perms::owner_all, fs::perm_options::replace, code);
#endif
            fs::remove(StagedPath(), code); // whatever an earlier update left
            m_file.open(StagedPath(), std::ios::binary | std::ios::trunc);
            if (!m_file)
            {
                LOG_INFO("Server", "Could not create %s for the update", StagedPath().c_str());
                m_state = State::Done;
                ReleaseBusy();
                Answer(Wire::UpdateAnswer::Status::NoRoom, "the update could not be saved here");
                return false;
            }
            m_state = State::Receiving;
            m_received = 0;
            return true;
        }

        void UpdateService::HandleChunk(const Wire::UpdateChunk& chunk)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state != State::Receiving)
            {
                return; // nothing was agreed (or it already failed): ignored
            }
            if (chunk.offset != m_received || chunk.data.size() > m_offer.size - m_received)
            {
                Fail(Wire::UpdateResult::Status::BadUpload, "a piece arrived out of order or past the offered size");
                return;
            }
            if (chunk.data.empty() && m_received != m_offer.size)
            {
                return; // an empty piece changes nothing
            }
            m_file.write(reinterpret_cast<const char*>(chunk.data.data()), static_cast<std::streamsize>(chunk.data.size()));
            if (!m_file)
            {
                const bool full = FreeSpace() < chunk.data.size();
                Fail(full ? Wire::UpdateResult::Status::NoRoom : Wire::UpdateResult::Status::WriteFailed,
                     "writing the update failed");
                return;
            }
            m_received += chunk.data.size();
            if (m_received == m_offer.size)
            {
                FinishReceiving();
            }
        }

        void UpdateService::FinishReceiving()
        {
            m_file.close();
            if (m_file.fail())
            {
                Fail(Wire::UpdateResult::Status::WriteFailed, "closing the update file failed");
                return;
            }
            // The hash is worked out again from the disk, so what is checked is what will run.
            Wire::Bytes sha256;
            uint64_t size = 0;
            if (!HashFile(StagedPath(), sha256, size))
            {
                Fail(Wire::UpdateResult::Status::WriteFailed, "the saved update could not be read back");
                return;
            }
            InstallerFacts running;
            InstallerFacts candidate;
            if (m_environment->inspector != nullptr)
            {
                running = m_environment->inspector->InspectRunningServer();
                candidate = m_environment->inspector->Inspect(StagedPath());
            }
            std::string detail;
            Wire::UpdateResult::Status status =
                CheckArrivedInstaller(m_offer, size, sha256, running, candidate, m_settings.runningVersion,
                                      m_settings.allowUnsigned, detail);
            if (status != Wire::UpdateResult::Status::Installing)
            {
                Fail(status, detail);
                return;
            }
            std::string error;
            if (!m_environment->launcher->Launch(StagedPath(), SilentInstallArguments(), error))
            {
                Fail(Wire::UpdateResult::Status::LaunchFailed, error);
                return;
            }
            LOG_INFO("Server", "Started the installer for Marconi Server %s; stopping so it can replace this program",
                     LogSafe(m_offer.version).c_str());
            m_started = true;
            m_state = State::Done;
            Wire::UpdateResult result;
            result.status = Wire::UpdateResult::Status::Installing;
            m_send(result.Encode());
            Wire::Bye bye;
            bye.reason = "Marconi Server is updating; it will be back in a moment.";
            m_send(bye.Encode());
            if (m_endSession)
            {
                m_endSession();
            }
            if (m_environment->onInstallerStarted)
            {
                m_environment->onInstallerStarted();
            }
        }
    } // namespace Server
} // namespace Marconi
