// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The connection acceptor

#include "Listener.h"

#include "TlsServerTransport.h"

#include <Crypto/TlsServer.h>
#include <MarconiCore/Wire/WireChannel.h>

#include <Log/Log.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        Listener::Listener(ScreenSource& screen, InputSink& input, const Settings& settings)
            : m_screen(screen), m_input(input), m_settings(settings), m_auth(settings.passwordHash)
        {
            m_idleMinutes = settings.idleMinutes;
        }

        bool Listener::SetAllowedAddresses(const std::vector<std::string>& entries, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_allowMutex);
            return m_allow.Set(entries, error);
        }

        void Listener::SetIdleMinutes(int minutes)
        {
            m_idleMinutes = minutes < 0 ? 0 : minutes;
            std::lock_guard<std::mutex> lock(m_currentMutex);
            if (m_current != nullptr)
            {
                m_current->SetIdleMinutes(m_idleMinutes);
            }
        }

        namespace
        {
            std::string ToHex(const std::vector<uint8_t>& bytes)
            {
                static const char* digits = "0123456789abcdef";
                std::string out;
                out.reserve(bytes.size() * 2);
                for (uint8_t b : bytes)
                {
                    out.push_back(digits[b >> 4]);
                    out.push_back(digits[b & 0x0F]);
                }
                return out;
            }
        } // namespace

        bool Listener::EnsureIdentity(std::string& error)
        {
            if (m_settings.certificatePem.empty() || m_settings.privateKeyPem.empty())
            {
                Crypto::TlsIdentity identity;
                std::string name = m_settings.serverName.empty() ? "Marconi Server" : m_settings.serverName;
                if (!Crypto::MakeSelfSignedIdentity(name, 3650, identity, error))
                {
                    return false;
                }
                m_settings.certificatePem = identity.certificatePem;
                m_settings.privateKeyPem = identity.privateKeyPem;
            }
            std::vector<uint8_t> sha256;
            if (!Crypto::CertificateFingerprint(m_settings.certificatePem, sha256, error))
            {
                return false;
            }
            m_fingerprint = ToHex(sha256);
            return true;
        }

        bool Listener::Start(std::string& error, const std::string& bindAddress)
        {
            if (!EnsureIdentity(error))
            {
                return false;
            }
            // A list that can't be read stops the server starting: carrying on would let in everyone, which is not
            // what the person who wrote the list meant.
            if (!SetAllowedAddresses(m_settings.allowedAddresses, error))
            {
                error = "The list of who may connect has an entry that can't be read. " + error;
                return false;
            }
            if (!m_listenSocket.Listen(bindAddress, m_settings.port, error))
            {
                return false;
            }
            // Which video encoders this computer has is worked out once, here, not for each connection: asking
            // can be slow, and a connection that hasn't logged in shouldn't be able to make the server ask.
            if (!m_videoFactorySet && m_settings.videoEnabled)
            {
                m_videoFactory = [](Wire::VideoCodec codec) { return CreatePlatformVideoEncoder(codec); };
                m_videoCodecs = PlatformEncodableCodecs();
                m_videoFactorySet = true;
            }
            m_port = m_listenSocket.GetLocalPort();
            LOG_INFO("Server", "Listening on %s:%u, certificate %s", bindAddress.c_str(),
                     static_cast<unsigned>(m_port), m_fingerprint.c_str());
            return true;
        }

        namespace
        {
            // The most connections handled at once, and the most from one address. A connection counts from the
            // moment it is accepted, so these bound what people without the password can make the server hold.
            constexpr int kMaxConnections = 16;
            constexpr int kMaxConnectionsPerAddress = 4;
        } // namespace

        bool Listener::TakeConnectionSlot(const std::string& address)
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            if (m_connectionCount >= kMaxConnections || m_connectionsByAddress[address] >= kMaxConnectionsPerAddress)
            {
                return false;
            }
            ++m_connectionCount;
            ++m_connectionsByAddress[address];
            return true;
        }

        void Listener::ReleaseConnectionSlot(const std::string& address)
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            --m_connectionCount;
            auto found = m_connectionsByAddress.find(address);
            if (found != m_connectionsByAddress.end() && --found->second <= 0)
            {
                m_connectionsByAddress.erase(found);
            }
        }

        std::vector<Wire::Display> Listener::DisplaysForWelcome()
        {
            // The screen source is only asked from one thread at a time. While a viewer is being served its frame
            // loop is that thread, so its last list is used; otherwise the lock keeps a session from starting
            // (and two connections from asking at once) while the screen is asked here.
            std::lock_guard<std::mutex> lock(m_currentMutex);
            if (m_current != nullptr)
            {
                return m_current->GetDisplays();
            }
            return m_screen.GetDisplays();
        }

        bool Listener::ServeOne(int acceptTimeoutMs)
        {
            if (m_stop)
            {
                return false;
            }
            Net::Socket client;
            std::string error;
            // 0 means wait for ever; Accept treats <=0 as its own kind of wait, so pass a large value instead.
            if (!m_listenSocket.Accept(client, acceptTimeoutMs > 0 ? acceptTimeoutMs : 24 * 60 * 60 * 1000, error))
            {
                return false; // timed out, was shut down, or failed; the caller (or Run) decides what next
            }
            const std::string address = client.GetPeerAddress();
            if (!TakeConnectionSlot(address))
            {
                client.Close();
                return true;
            }
            ServeConnection(client);
            ReleaseConnectionSlot(address);
            return true;
        }

        void Listener::ServeConnection(Net::Socket& client)
        {
            // Who may connect is decided first, before any byte is sent: a refused address gets nothing but a closed
            // connection, so it learns nothing about this server.
            bool allowed = false;
            {
                std::lock_guard<std::mutex> lock(m_allowMutex);
                allowed = m_allow.Allows(client.GetPeerAddress());
            }
            if (!allowed)
            {
                LOG_INFO("Server", "Refused a connection from %s: it is not on the list of who may connect",
                         client.GetPeerAddress().empty() ? "an unknown address" : client.GetPeerAddress().c_str());
                client.Close();
                return;
            }

            // Known to Stop() from here, so a connection that hasn't logged in is ended with the rest.
            {
                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                m_connectionSockets.push_back(&client);
            }
            struct Forget
            {
                Listener& listener;
                Net::Socket& socket;
                ~Forget()
                {
                    std::lock_guard<std::mutex> lock(listener.m_connectionsMutex);
                    auto& sockets = listener.m_connectionSockets;
                    sockets.erase(std::remove(sockets.begin(), sockets.end(), &socket), sockets.end());
                }
            } forget{*this, client};
            if (m_stop)
            {
                client.Close();
                return;
            }

            // Until the login is done, every wait for this connection has a limit: one that connects and says
            // nothing is dropped instead of being waited on for ever.
            client.SetReceiveTimeout(m_loginTimeoutMs);

            // The four-byte marker goes out in the clear, so a probe can tell a Marconi server from anything else on
            // the port, then the connection is upgraded to TLS before any protocol message.
            if (!client.SendAll(Wire::kMagic, sizeof(Wire::kMagic)))
            {
                client.Close();
                return;
            }
            Crypto::TlsServerOptions tlsOptions;
            tlsOptions.identity.certificatePem = m_settings.certificatePem;
            tlsOptions.identity.privateKeyPem = m_settings.privateKeyPem;
            tlsOptions.receive = [&client](void* buffer, size_t length) { return client.Receive(buffer, length); };
            tlsOptions.sendAll = [&client](const void* data, size_t length) { return client.SendAll(data, length); };
            std::unique_ptr<Crypto::TlsServer> tls = Crypto::TlsServer::Create(std::move(tlsOptions));
            std::string tlsError;
            if (!tls->Handshake(tlsError))
            {
                LOG_INFO("Server", "A viewer's TLS handshake failed: %s", tlsError.c_str());
                client.Close();
                return;
            }

            TlsServerTransport transport(std::move(tls));
            Session session(transport, m_screen, m_input, m_settings, &m_auth);
            session.SetFrameIntervalMs(m_frameIntervalMs);
            session.SetClipboard(m_clipboard);
            session.SetGamepads(m_gamepadBackend);
            if (m_videoFactorySet)
            {
                session.SetVideoEncoderFactory(m_videoFactory, m_videoCodecs);
            }
            session.SetNotifier(m_notifier, client.GetPeerAddress());
            session.SetUpdateEnvironment(m_updateEnvironment);
            if (m_idleClock)
            {
                session.SetIdleClock(m_idleClock);
            }
            session.SetEndHandler([&client]() { client.Shutdown(); });
            session.SetDisplaysProvider([this]() { return DisplaysForWelcome(); });
            // Called with the password accepted. One viewer is served at a time, so this waits for the one before
            // to leave, then makes this session the current one.
            bool admitted = false;
            session.SetAdmission([this, &session, &client, &admitted]() {
                std::unique_lock<std::mutex> lock(m_currentMutex);
                m_currentFree.wait(lock, [this]() { return m_current == nullptr || m_stop.load(); });
                if (m_stop)
                {
                    return false;
                }
                m_current = &session;
                session.SetIdleMinutes(m_idleMinutes); // under the lock, so a change in between isn't missed
                m_currentSocket = &client;
                m_currentId = ++m_viewerCounter;
                m_currentSince = std::chrono::steady_clock::now();
                admitted = true;
                client.SetReceiveTimeout(0); // logged in: from here the idle limit is what ends a quiet viewer
                return true;
            });
            session.Run();
            if (admitted)
            {
                // Taking the lock also waits out a GetViewer or DisconnectViewer that is using the session.
                {
                    std::lock_guard<std::mutex> lock(m_currentMutex);
                    m_current = nullptr;
                    m_currentSocket = nullptr;
                }
                m_currentFree.notify_all();
            }
            client.Close();
        }

        void Listener::Run()
        {
            // Each connection gets its own thread, so the wait for one viewer's handshake or password never holds
            // up another. A failed Accept while stopping ends the loop; otherwise it waits a moment so a transient
            // error can't spin the CPU, then tries again.
            struct Worker
            {
                std::thread thread;
                std::shared_ptr<std::atomic<bool>> done;
            };
            std::vector<Worker> workers;
            auto reap = [&workers](bool all) {
                for (auto it = workers.begin(); it != workers.end();)
                {
                    if (all || it->done->load())
                    {
                        it->thread.join();
                        it = workers.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            };
            while (!m_stop)
            {
                reap(false);
                auto client = std::make_shared<Net::Socket>();
                std::string error;
                if (!m_listenSocket.Accept(*client, 1000, error))
                {
                    if (!m_stop && error.find("Timed out") == std::string::npos)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    continue;
                }
                const std::string address = client->GetPeerAddress();
                if (!TakeConnectionSlot(address))
                {
                    // Too many connections at once, or too many from this address: not taken on.
                    LOG_INFO("Server", "Turned away a connection from %s: too many at once",
                             address.empty() ? "an unknown address" : address.c_str());
                    client->Close();
                    continue;
                }
                Worker worker;
                worker.done = std::make_shared<std::atomic<bool>>(false);
                std::shared_ptr<std::atomic<bool>> done = worker.done;
                worker.thread = std::thread([this, client, address, done]() {
                    ServeConnection(*client);
                    ReleaseConnectionSlot(address);
                    *done = true;
                });
                workers.push_back(std::move(worker));
            }
            reap(true); // Stop() shut every connection down, so these end promptly
        }

        void Listener::Stop()
        {
            m_stop = true;
            {
                std::lock_guard<std::mutex> lock(m_currentMutex);
                if (m_current != nullptr)
                {
                    m_current->Stop();
                }
            }
            m_currentFree.notify_all(); // a viewer waiting its turn gives up
            {
                // Wakes every connection's blocked read, logged in or not, so their threads can finish.
                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                for (Net::Socket* socket : m_connectionSockets)
                {
                    socket->Shutdown();
                }
            }
            m_listenSocket.Shutdown(); // wake a blocked Accept
        }
        bool Listener::GetViewer(ViewerSnapshot& viewer) const
        {
            std::lock_guard<std::mutex> lock(m_currentMutex);
            if (m_current == nullptr || !m_current->IsLoggedIn())
            {
                return false; // nobody, or someone who hasn't passed the login yet
            }
            viewer.id = m_currentId;
            viewer.name = m_current->GetViewerName();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                           m_currentSince);
            viewer.connectedSeconds = seconds.count() > 0 ? static_cast<uint32_t>(seconds.count()) : 0;
            return true;
        }

        bool Listener::DisconnectViewer(uint32_t viewerId)
        {
            std::lock_guard<std::mutex> lock(m_currentMutex);
            if (m_current == nullptr || (viewerId != 0 && viewerId != m_currentId))
            {
                return false;
            }
            m_current->Stop();
            m_currentSocket->Shutdown(); // wakes the session's blocked read so Run can return
            return true;
        }
    } // namespace Server
} // namespace Marconi
