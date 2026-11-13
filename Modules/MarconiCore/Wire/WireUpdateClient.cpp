// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer's half of updating a Marconi Server

#include <MarconiCore/Wire/WireUpdateClient.h>

#include <Crypto/Crypto.h>
#include <Log/Log.h>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace Marconi
{
    namespace Wire
    {
        UpdateUploader::UpdateUploader(std::function<bool(const Bytes&)> send) : m_send(std::move(send)) {}

        UpdateUploader::~UpdateUploader()
        {
            StopSending();
        }

        void UpdateUploader::SetListener(Listener listener)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_listener = std::move(listener);
        }

        UpdateUploader::Progress UpdateUploader::GetProgress() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_progress;
        }

        void UpdateUploader::Report()
        {
            Listener listener;
            Progress progress;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                listener = m_listener;
                progress = m_progress;
            }
            if (listener)
            {
                listener(progress);
            }
        }

        void UpdateUploader::SetStage(Stage stage)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_progress.stage = stage;
            }
            Report();
        }

        void UpdateUploader::StopSending()
        {
            m_cancel = true;
            if (m_sender.joinable())
            {
                if (m_sender.get_id() == std::this_thread::get_id())
                {
                    m_sender.detach(); // a listener on the sending thread reset us: it ends by itself
                }
                else
                {
                    m_sender.join();
                }
            }
            m_cancel = false;
        }

        namespace
        {
            bool IsBusy(UpdateUploader::Stage stage)
            {
                using Stage = UpdateUploader::Stage;
                return stage == Stage::Checking || stage == Stage::Offering || stage == Stage::WaitingForPerson ||
                       stage == Stage::Sending || stage == Stage::ServerChecking;
            }
        } // namespace

        bool UpdateUploader::Check(const std::string& version)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (IsBusy(m_progress.stage))
                {
                    return false;
                }
                m_progress = Progress();
                m_progress.stage = Stage::Checking;
                m_progress.version = version;
            }
            m_installing = false;
            UpdateOffer offer;
            offer.flags = UpdateOffer::kCheckOnly;
            offer.version = version;
            if (!m_send(offer.Encode()))
            {
                SetStage(Stage::Idle);
                return false;
            }
            Report();
            return true;
        }

        bool UpdateUploader::Start(const std::string& installerPath, const std::string& version, std::string& error)
        {
            if (IsBusy(GetProgress().stage))
            {
                error = "An update is already under way.";
                return false;
            }
            StopSending();
            Bytes file;
            {
                std::ifstream in(installerPath, std::ios::binary);
                if (!in)
                {
                    error = "Marconi couldn't open the Marconi Server installer (" + installerPath + ").";
                    return false;
                }
                in.seekg(0, std::ios::end);
                std::streamoff size = in.tellg();
                if (size <= 0 || static_cast<uint64_t>(size) > kMaxUpdateBytes)
                {
                    error = "The Marconi Server installer is empty or too large to send.";
                    return false;
                }
                in.seekg(0, std::ios::beg);
                file.resize(static_cast<size_t>(size));
                if (!in.read(reinterpret_cast<char*>(file.data()), size))
                {
                    error = "Marconi couldn't read the Marconi Server installer (" + installerPath + ").";
                    return false;
                }
            }
            UpdateOffer offer;
            offer.version = version;
            offer.size = file.size();
            offer.sha256.resize(32);
            Crypto::Sha256(file.data(), file.size(), offer.sha256.data());
            if (m_offerHook)
            {
                m_offerHook(offer);
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_file = std::move(file);
                m_progress = Progress();
                m_progress.stage = Stage::Offering;
                m_progress.version = version;
                m_progress.total = offer.size;
            }
            m_installing = false;
            if (!m_send(offer.Encode()))
            {
                error = "Marconi couldn't send the update offer: the connection isn't there.";
                SetStage(Stage::Idle);
                return false;
            }
            Report();
            return true;
        }

        void UpdateUploader::SendPieces()
        {
            uint64_t total = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                total = m_file.size();
            }
            uint64_t offset = 0;
            while (offset < total && !m_cancel)
            {
                UpdateChunk chunk;
                chunk.offset = offset;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_progress.stage != Stage::Sending)
                    {
                        return; // the server already gave its result (a refusal part-way through)
                    }
                    size_t count = static_cast<size_t>(std::min<uint64_t>(kUpdateChunkBytes, total - offset));
                    chunk.data.assign(m_file.begin() + static_cast<ptrdiff_t>(offset),
                                      m_file.begin() + static_cast<ptrdiff_t>(offset + count));
                }
                if (!m_send(chunk.Encode()))
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_progress.stage = Stage::Failed;
                    m_progress.error = "The connection was lost while sending the update.";
                    break;
                }
                offset += chunk.data.size();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_progress.sent = offset;
                    if (offset == total && m_progress.stage == Stage::Sending)
                    {
                        m_progress.stage = Stage::ServerChecking;
                    }
                }
                Report();
            }
            if (!m_cancel && GetProgress().stage == Stage::Failed)
            {
                Report();
            }
        }

        bool UpdateUploader::OnMessage(Type type, Reader& body)
        {
            if (type == Type::UpdateAnswer)
            {
                UpdateAnswer answer = UpdateAnswer::Decode(body);
                bool startSending = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_progress.answer = answer.status;
                    m_progress.serverVersion = answer.serverVersion;
                    if (m_progress.stage == Stage::Checking)
                    {
                        m_progress.stage = Stage::CheckAnswered;
                    }
                    else if (m_progress.stage == Stage::Offering || m_progress.stage == Stage::WaitingForPerson)
                    {
                        if (answer.status == UpdateAnswer::Status::SendIt)
                        {
                            m_progress.stage = Stage::Sending;
                            startSending = true;
                        }
                        else if (answer.status == UpdateAnswer::Status::AskingPerson)
                        {
                            m_progress.stage = Stage::WaitingForPerson;
                        }
                        else
                        {
                            m_progress.stage = Stage::Refused;
                            m_file.clear();
                        }
                    }
                    else
                    {
                        return true; // an answer nobody asked for: ignored
                    }
                }
                LOG_INFO("Wire", "Server update answer %u (server %s): %s", static_cast<unsigned>(answer.status),
                         answer.serverVersion.c_str(), answer.detail.c_str());
                if (startSending)
                {
                    if (m_sender.joinable())
                    {
                        m_sender.join(); // a previous run's thread, already finished
                    }
                    m_sender = std::thread([this]() { SendPieces(); });
                }
                Report();
                return true;
            }
            if (type == Type::UpdateResult)
            {
                UpdateResult result = UpdateResult::Decode(body);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_progress.stage != Stage::Sending && m_progress.stage != Stage::ServerChecking)
                    {
                        return true; // not expected now: ignored
                    }
                    m_progress.result = result.status;
                    m_progress.stage =
                        result.status == UpdateResult::Status::Installing ? Stage::Installing : Stage::Failed;
                    m_file.clear();
                }
                m_installing = result.status == UpdateResult::Status::Installing;
                LOG_INFO("Wire", "Server update result %u: %s", static_cast<unsigned>(result.status),
                         result.detail.c_str());
                Report();
                return true;
            }
            return false;
        }

        void UpdateUploader::Reset()
        {
            StopSending();
            bool report = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_file.clear();
                if (IsBusy(m_progress.stage))
                {
                    m_progress.stage = Stage::Failed;
                    m_progress.error = "The connection ended before the update was finished.";
                    report = true;
                }
            }
            if (report)
            {
                Report();
            }
        }
    } // namespace Wire
} // namespace Marconi
