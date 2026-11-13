// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The one connection the AI drives: connecting without a person to answer questions, and following the picture

#include <MarconiMcp/Connection.h>

#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            bool IsHexDigit(char c)
            {
                return std::isxdigit(static_cast<unsigned char>(c)) != 0;
            }

            char LowerCase(char c)
            {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            // Without spaces, colons or a "SHA256" prefix; hex in lower case (base64 keeps its case).
            std::string Normalised(const std::string& text)
            {
                std::string out;
                for (char c : text)
                {
                    if (c != ' ' && c != '\t' && c != ':')
                    {
                        out += c;
                    }
                }
                if (out.size() >= 6 && (out.compare(0, 6, "SHA256") == 0 || out.compare(0, 6, "sha256") == 0))
                {
                    out.erase(0, 6);
                }
                if (!out.empty() && std::all_of(out.begin(), out.end(), IsHexDigit))
                {
                    std::transform(out.begin(), out.end(), out.begin(), LowerCase);
                }
                return out;
            }
        } // namespace

        bool SameFingerprint(const std::string& a, const std::string& b)
        {
            // Certificate fingerprints are hex (any case); SSH ones are base64, where case matters.
            const std::string na = Normalised(a);
            return !na.empty() && na == Normalised(b);
        }

        Connection::~Connection()
        {
            Disconnect();
        }

        ConnectOutcome Connection::Connect(const ConnectRequest& request)
        {
            Disconnect();

            QuickConnectOptions options;
            options.address = request.address;
            options.username = request.username;
            options.password = request.password;
            options.quality = request.quality;
            options.trustedCertificateSha256 = request.trustedCertificate;
            options.sshTunnel = request.sshTunnel;
            options.clientName = "Marconi for AI tools";
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_request = request;
                m_outcome = ConnectOutcome();
                m_finished = false;
                m_connected = false;
                m_desktopName.clear();
                m_lastError.clear();
                m_screens.clear();
                m_credentialsAsked = 0;
                m_updates = 0;
                m_needPicture = true;
                m_recentRects.clear();
                m_clipboard.clear();
                m_clipboardReceived = false;
                m_clipboardUpdates = 0;
                m_cursor = CursorShape();
                m_pointerKnown = false;
            }

            std::string error;
            std::unique_ptr<RemoteSession> session = QuickConnect(options, *this, error);
            if (!session)
            {
                ConnectOutcome outcome;
                outcome.error = error.empty() ? "That address can't be used." : error;
                return outcome;
            }

            std::unique_lock<std::mutex> lock(m_mutex);
            m_session = std::move(session);
            const bool finished = m_changed.wait_for(lock, request.timeout, [this]() { return m_finished; });
            ConnectOutcome outcome = m_outcome;
            if (!finished)
            {
                outcome.error = "The computer didn't answer in time.";
            }
            outcome.connected = m_connected;
            lock.unlock();

            if (!outcome.connected)
            {
                Disconnect();
            }
            return outcome;
        }

        void Connection::Disconnect()
        {
            // The connection thread takes m_mutex in its callbacks, so it must not be held while waiting for it.
            std::unique_ptr<RemoteSession> session;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                session = std::move(m_session);
                m_connected = false;
            }
            if (session)
            {
                session->Disconnect();
            }
        }

        ConnectionFacts Connection::GetFacts() const
        {
            ConnectionFacts facts;
            RemoteSession* session = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                facts.connected = m_connected && m_session;
                facts.address = m_request.address;
                facts.desktopName = m_desktopName;
                facts.screens = m_screens;
                facts.lastError = m_lastError;
                session = facts.connected ? m_session.get() : nullptr;
            }
            // Not under m_mutex: the connection thread may hold the picture's lock when it calls us. The session
            // only goes away in Connect or Disconnect, which run on the same thread as this.
            if (session)
            {
                facts.encrypted = session->IsEncrypted();
                facts.encryptionName = session->GetEncryptionName();
                Rfb::Framebuffer& framebuffer = session->GetFramebuffer();
                std::lock_guard<std::mutex> pictureLock(framebuffer.GetMutex());
                facts.width = framebuffer.GetWidth();
                facts.height = framebuffer.GetHeight();
            }
            return facts;
        }

        RemoteSession* Connection::GetSession() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_connected ? m_session.get() : nullptr;
        }

        bool Connection::EnsurePicture(std::chrono::milliseconds limit)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            std::unique_lock<std::mutex> lock(m_mutex);
            int requests = 0;
            while (m_connected && m_needPicture)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                {
                    break;
                }
                RemoteSession* session = m_session.get();
                lock.unlock();
                if (requests > 0 && session)
                {
                    LOG_INFO("Mcp", "No picture yet; asking for the whole desktop again");
                    session->RequestFullUpdate();
                }
                ++requests;
                lock.lock();
                m_changed.wait_until(lock, std::min(deadline, now + std::chrono::milliseconds(700)),
                                     [this]() { return !m_connected || !m_needPicture; });
            }
            return m_connected && !m_needPicture;
        }

        bool Connection::RefreshPicture(std::chrono::milliseconds limit)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            RemoteSession* session = m_connected ? m_session.get() : nullptr;
            if (!session)
            {
                return false;
            }
            const uint64_t before = m_updates;
            lock.unlock();
            session->RequestFullUpdate();
            lock.lock();
            return m_changed.wait_for(lock, limit, [this, before]() { return m_updates != before || !m_connected; }) &&
                   m_connected;
        }

        bool Connection::WaitUntilQuiet(std::chrono::milliseconds quiet, std::chrono::milliseconds limit)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            std::unique_lock<std::mutex> lock(m_mutex);
            while (true)
            {
                const auto now = std::chrono::steady_clock::now();
                if (!m_connected)
                {
                    return false;
                }
                const auto lastActivity = std::max(m_lastUpdate, m_lastInput);
                if (m_updates > 0 && now - lastActivity >= quiet)
                {
                    return true;
                }
                if (now >= deadline)
                {
                    return false;
                }
                // Wake when the quiet period would end, or at the deadline, whichever is first.
                auto wakeAt = deadline;
                if (m_updates > 0)
                {
                    wakeAt = std::min(wakeAt, lastActivity + quiet);
                }
                m_changed.wait_until(lock, wakeAt);
            }
        }

        namespace
        {
            bool Overlap(const Rfb::Rect& a, const Rfb::Rect& b)
            {
                return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
            }
        } // namespace

        bool Connection::WaitForUpdate(std::chrono::milliseconds limit, const Rfb::Rect* area)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const uint64_t before = m_updates;
            const auto changed = [this, before, area]() {
                if (!m_connected)
                {
                    return true;
                }
                if (m_updates == before)
                {
                    return false;
                }
                if (area == nullptr)
                {
                    return true;
                }
                return std::any_of(m_recentRects.begin(), m_recentRects.end(),
                                   [before, area](const std::pair<uint64_t, Rfb::Rect>& entry) {
                                       return entry.first > before && Overlap(entry.second, *area);
                                   });
            };
            return m_changed.wait_for(lock, limit, changed) && m_connected;
        }

        std::string Connection::GetServerClipboard(std::chrono::milliseconds limit, bool& received)
        {
            RemoteSession* session = GetSession();
            if (session && session->HasExtendedClipboard())
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                const uint64_t before = m_clipboardUpdates;
                lock.unlock();
                if (session->RequestServerClipboard())
                {
                    lock.lock();
                    m_changed.wait_for(lock, limit,
                                       [this, before]() { return m_clipboardUpdates != before || !m_connected; });
                    lock.unlock();
                }
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            received = m_clipboardReceived;
            return m_clipboard;
        }

        CursorShape Connection::GetCursor() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_cursor;
        }

        bool Connection::GetPointer(int& x, int& y) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            x = m_pointerX;
            y = m_pointerY;
            return m_pointerKnown;
        }

        void Connection::SetPointer(int x, int y)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pointerKnown = true;
            m_pointerX = x;
            m_pointerY = y;
        }

        void Connection::NoteInput()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastInput = std::chrono::steady_clock::now();
        }

        void Connection::OnConnected(const Rfb::ServerInit& serverInit)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connected = true;
            m_finished = true;
            m_desktopName = serverInit.name;
            m_changed.notify_all();
        }

        void Connection::OnFramebufferUpdated(const std::vector<Rfb::Rect>& rects)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_updates;
            m_needPicture = false;
            m_lastUpdate = std::chrono::steady_clock::now();
            for (const Rfb::Rect& rect : rects)
            {
                m_recentRects.emplace_back(m_updates, rect);
            }
            // Enough for a waiter to see every update since it started; older ones are of no use.
            const size_t kKeep = 512;
            if (m_recentRects.size() > kKeep)
            {
                m_recentRects.erase(m_recentRects.begin(), m_recentRects.end() - kKeep);
            }
            m_changed.notify_all();
        }

        void Connection::OnDesktopResized(int /*width*/, int /*height*/)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_needPicture = true;
            m_lastUpdate = std::chrono::steady_clock::now();
            m_changed.notify_all();
        }

        void Connection::OnScreenLayout(const std::vector<Rfb::Screen>& screens)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_screens = screens;
        }

        void Connection::OnCursorShape(const CursorShape& cursor)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cursor = cursor;
        }

        void Connection::OnServerCutText(const std::string& latin1Text)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_clipboard = Latin1ToUtf8(latin1Text);
            m_clipboardReceived = true;
            ++m_clipboardUpdates;
            m_changed.notify_all();
        }

        void Connection::OnServerClipboard(const Rfb::ClipboardContent& content)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_clipboard = content.utf8Text;
            m_clipboardReceived = true;
            ++m_clipboardUpdates;
            m_changed.notify_all();
        }

        void Connection::OnDisconnected(const DisconnectInfo& info)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_finished && m_outcome.error.empty())
            {
                m_outcome.error = info.error.empty() ? "The connection was closed." : info.error;
            }
            if (info.authenticationFailed)
            {
                m_outcome.needsPassword = true;
            }
            if (m_connected)
            {
                m_lastError = info.error.empty() ? "The connection was closed." : info.error;
            }
            m_connected = false;
            m_finished = true;
            m_changed.notify_all();
        }

        bool Connection::OnCredentialsRequired(Rfb::CredentialsKind kind, Rfb::Credentials& inOutCredentials)
        {
            // Nobody to ask: answer once with what we were given, and never again (a second question means the
            // computer turned the first answer down).
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_credentialsAsked;
            const bool needsUser = kind == Rfb::CredentialsKind::Account || kind == Rfb::CredentialsKind::SshAccount;
            const bool haveUser = !m_request.username.empty() || !inOutCredentials.username.empty();
            if (m_credentialsAsked > 1 || m_request.password.empty() || (needsUser && !haveUser))
            {
                m_outcome.needsPassword = true;
                m_outcome.error = needsUser && !haveUser
                                      ? "This computer needs a user name and password."
                                      : (m_request.password.empty() ? "This computer needs a password."
                                                                    : "The computer turned the password down.");
                return false;
            }
            if (!m_request.username.empty())
            {
                inOutCredentials.username = m_request.username;
            }
            inOutCredentials.password = m_request.password;
            return true;
        }

        bool Connection::OnUntrustedCertificate(const Net::TlsCertificate& certificate)
        {
            const std::string fingerprint = BytesToHex(certificate.sha256);
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_request.trustFingerprint.empty() && SameFingerprint(m_request.trustFingerprint, fingerprint))
            {
                return true;
            }
            m_outcome.untrustedFingerprint = fingerprint;
            m_outcome.untrustedDescription = "The computer's certificate isn't trusted (" + certificate.problem +
                                             "). Issued to " + certificate.subject + ", valid until " +
                                             certificate.validUntil + ".";
            m_outcome.error = m_outcome.untrustedDescription;
            return false;
        }

        bool Connection::OnUntrustedSshHostKey(const Net::SshHostKeyQuestion& question)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if ((!m_request.trustFingerprint.empty() &&
                 SameFingerprint(m_request.trustFingerprint, question.fingerprint)) ||
                (!m_request.trustedSshHostKey.empty() &&
                 SameFingerprint(m_request.trustedSshHostKey, question.fingerprint)))
            {
                return true;
            }
            m_outcome.untrustedFingerprint = question.fingerprint;
            m_outcome.untrustedDescription =
                question.previous.empty()
                    ? "The SSH server " + question.host + " isn't known yet (" + question.algorithm + " key)."
                    : "The SSH server " + question.host + "'s key has changed since it was last trusted (" +
                          question.previous + ").";
            m_outcome.error = m_outcome.untrustedDescription;
            return false;
        }
    } // namespace Mcp
} // namespace Marconi
