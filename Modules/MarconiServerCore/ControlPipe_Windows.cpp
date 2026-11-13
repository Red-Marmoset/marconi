// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Windows named pipe between the session helper and the tray icon: the server side that accepts tray icons and
// works out whether each is an administrator, and the client side the tray icon connects with.
//
// NOTE: NOT YET BUILT OR RUN ON WINDOWS (written in a Linux cloud environment with no Windows SDK). The protocol
// and state machine above it are tested on Linux (ControlSession); the pipe, its access rules and the administrator
// check need trying on a real machine: see the Phase 3 checklist in SERVER_PLAN.md.

#include "ControlPipe.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            namespace
            {
                // The same name as kPipeName in ControlProtocol.h, as wide text.
                constexpr const wchar_t* kPipeNameWide = L"\\\\.\\pipe\\MarconiServerControl";

                // SYSTEM and administrators: everything. Signed-in users (the interactive group): read and write, so
                // a tray icon in any user's session can connect. Remote clients are refused separately by
                // PIPE_REJECT_REMOTE_CLIENTS.
                constexpr const wchar_t* kPipeSddl = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)";

                // A pipe opened for overlapped I/O, so a read waiting in one thread never blocks a write from
                // another (a synchronous pipe handle serialises them). Owns the handle when `owns` is set.
                class OverlappedPipe : public Wire::Transport
                {
                public:
                    OverlappedPipe(HANDLE handle, bool owns) : m_handle(handle), m_owns(owns) {}
                    ~OverlappedPipe() override
                    {
                        if (m_owns && m_handle != INVALID_HANDLE_VALUE)
                        {
                            CloseHandle(m_handle);
                        }
                    }

                    ptrdiff_t Read(void* buffer, size_t length) override
                    {
                        DWORD want = static_cast<DWORD>(length < 65536 ? length : 65536);
                        DWORD done = 0;
                        if (!Transfer(false, buffer, want, done))
                        {
                            // The other end closing is a normal end of stream, anything else an error.
                            return GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_PIPE_NOT_CONNECTED ||
                                           GetLastError() == ERROR_OPERATION_ABORTED
                                       ? 0
                                       : -1;
                        }
                        return static_cast<ptrdiff_t>(done);
                    }

                    bool Write(const void* data, size_t length) override
                    {
                        const uint8_t* bytes = static_cast<const uint8_t*>(data);
                        while (length > 0)
                        {
                            DWORD want = static_cast<DWORD>(length < 65536 ? length : 65536);
                            DWORD done = 0;
                            if (!Transfer(true, const_cast<uint8_t*>(bytes), want, done) || done == 0)
                            {
                                return false;
                            }
                            bytes += done;
                            length -= done;
                        }
                        return true;
                    }

                    // Ends any read or write waiting on the pipe (they fail with ERROR_OPERATION_ABORTED).
                    void Cancel() { CancelIoEx(m_handle, nullptr); }

                private:
                    bool Transfer(bool write, void* buffer, DWORD length, DWORD& done)
                    {
                        OVERLAPPED overlapped = {};
                        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                        if (overlapped.hEvent == nullptr)
                        {
                            return false;
                        }
                        BOOL ok = write ? WriteFile(m_handle, buffer, length, nullptr, &overlapped)
                                        : ReadFile(m_handle, buffer, length, nullptr, &overlapped);
                        if (!ok && GetLastError() == ERROR_IO_PENDING)
                        {
                            ok = GetOverlappedResult(m_handle, &overlapped, &done, TRUE);
                        }
                        else if (ok)
                        {
                            ok = GetOverlappedResult(m_handle, &overlapped, &done, FALSE);
                        }
                        DWORD error = GetLastError();
                        CloseHandle(overlapped.hEvent);
                        SetLastError(error);
                        return ok != FALSE;
                    }

                    HANDLE m_handle;
                    bool m_owns;
                };

                // True if the process on the other end of the pipe runs with an elevated administrator token. A
                // user in the Administrators group who hasn't elevated has the group switched off in their token,
                // so this is false for them: changing the password asks for the elevation prompt, as the plan says.
                bool ClientIsAdministrator(HANDLE pipe)
                {
                    ULONG processId = 0;
                    if (!GetNamedPipeClientProcessId(pipe, &processId))
                    {
                        return false;
                    }
                    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
                    if (process == nullptr)
                    {
                        return false;
                    }
                    HANDLE token = nullptr;
                    bool opened = OpenProcessToken(process, TOKEN_QUERY | TOKEN_DUPLICATE, &token) != FALSE;
                    CloseHandle(process);
                    if (!opened)
                    {
                        return false;
                    }
                    // CheckTokenMembership wants an impersonation token, so make one from the process token.
                    HANDLE impersonation = nullptr;
                    bool duplicated = DuplicateToken(token, SecurityIdentification, &impersonation) != FALSE;
                    CloseHandle(token);
                    if (!duplicated)
                    {
                        return false;
                    }
                    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
                    PSID administrators = nullptr;
                    BOOL member = FALSE;
                    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators))
                    {
                        if (!CheckTokenMembership(impersonation, administrators, &member))
                        {
                            member = FALSE;
                        }
                        FreeSid(administrators);
                    }
                    CloseHandle(impersonation);
                    return member != FALSE;
                }
            } // namespace

            struct ControlPipeServer::Impl
            {
                struct Connection
                {
                    HANDLE pipe = INVALID_HANDLE_VALUE;
                    std::unique_ptr<OverlappedPipe> transport;
                    std::mutex sessionMutex;
                    ControlSession* session = nullptr; // set while the connection's thread runs it
                    std::thread thread;
                    std::atomic<bool> finished{false};
                };

                Impl(ControlHost& hostIn, const std::string& versionIn) : host(hostIn), serverVersion(versionIn) {}

                // Joins and closes the connections whose tray icon has gone. Call with `mutex` held.
                void ReapFinished()
                {
                    for (size_t i = 0; i < connections.size();)
                    {
                        Connection& connection = *connections[i];
                        if (connection.finished)
                        {
                            connection.thread.join();
                            connection.transport.reset();
                            CloseHandle(connection.pipe);
                            connections.erase(connections.begin() + static_cast<ptrdiff_t>(i));
                        }
                        else
                        {
                            ++i;
                        }
                    }
                }

                void ServeConnection(Connection* connection)
                {
                    PeerInfo peer;
                    peer.isAdministrator = ClientIsAdministrator(connection->pipe);
                    ControlSession session(*connection->transport, host, peer, serverVersion);
                    {
                        std::lock_guard<std::mutex> lock(connection->sessionMutex);
                        connection->session = &session;
                    }
                    session.Run();
                    {
                        std::lock_guard<std::mutex> lock(connection->sessionMutex);
                        connection->session = nullptr;
                    }
                    DisconnectNamedPipe(connection->pipe);
                    connection->finished = true;
                }

                void AcceptLoop()
                {
                    SECURITY_ATTRIBUTES attributes = {};
                    attributes.nLength = sizeof(attributes);
                    PSECURITY_DESCRIPTOR descriptor = nullptr;
                    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kPipeSddl, SDDL_REVISION_1,
                                                                              &descriptor, nullptr))
                    {
                        return;
                    }
                    attributes.lpSecurityDescriptor = descriptor;
                    HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

                    bool first = true;
                    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
                    {
                        // FILE_FLAG_FIRST_PIPE_INSTANCE on the first pipe stops another program creating a pipe of
                        // the same name ahead of us.
                        DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                         (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
                        HANDLE pipe = CreateNamedPipeW(kPipeNameWide, openMode,
                                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                                           PIPE_REJECT_REMOTE_CLIENTS,
                                                       PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &attributes);
                        if (pipe == INVALID_HANDLE_VALUE)
                        {
                            if (first)
                            {
                                break; // the name is taken by something else: serve nothing rather than share it
                            }
                            WaitForSingleObject(stopEvent, 1000);
                            continue;
                        }
                        first = false;

                        OVERLAPPED overlapped = {};
                        overlapped.hEvent = connectEvent;
                        ResetEvent(connectEvent);
                        bool connected = false;
                        if (ConnectNamedPipe(pipe, &overlapped))
                        {
                            connected = true;
                        }
                        else
                        {
                            DWORD error = GetLastError();
                            if (error == ERROR_PIPE_CONNECTED)
                            {
                                connected = true; // the tray icon connected between Create and Connect
                            }
                            else if (error == ERROR_IO_PENDING)
                            {
                                HANDLE waits[2] = {connectEvent, stopEvent};
                                DWORD woken = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                                if (woken == WAIT_OBJECT_0)
                                {
                                    connected = true;
                                }
                                else
                                {
                                    CancelIoEx(pipe, &overlapped);
                                }
                            }
                        }
                        if (!connected)
                        {
                            CloseHandle(pipe);
                            continue;
                        }

                        std::lock_guard<std::mutex> lock(mutex);
                        ReapFinished();
                        auto connection = std::make_unique<Connection>();
                        connection->pipe = pipe;
                        connection->transport = std::make_unique<OverlappedPipe>(pipe, false);
                        Connection* raw = connection.get();
                        connection->thread = std::thread([this, raw]() { ServeConnection(raw); });
                        connections.push_back(std::move(connection));
                    }
                    CloseHandle(connectEvent);
                    LocalFree(descriptor);
                }

                ControlHost& host;
                std::string serverVersion;
                HANDLE stopEvent = nullptr;
                std::thread acceptThread;
                std::mutex mutex; // guards `connections`
                std::vector<std::unique_ptr<Connection>> connections;
            };

            ControlPipeServer::ControlPipeServer(ControlHost& host, const std::string& serverVersion)
                : m_impl(new Impl(host, serverVersion))
            {
            }

            ControlPipeServer::~ControlPipeServer()
            {
                Stop();
            }

            bool ControlPipeServer::Start(std::string& error)
            {
                if (m_impl->stopEvent != nullptr)
                {
                    return true; // already started
                }
                m_impl->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (m_impl->stopEvent == nullptr)
                {
                    error = "could not create the control pipe's stop event";
                    return false;
                }
                m_impl->acceptThread = std::thread([this]() { m_impl->AcceptLoop(); });
                return true;
            }

            void ControlPipeServer::Stop()
            {
                if (m_impl->stopEvent == nullptr)
                {
                    return;
                }
                SetEvent(m_impl->stopEvent);
                if (m_impl->acceptThread.joinable())
                {
                    m_impl->acceptThread.join();
                }
                {
                    std::lock_guard<std::mutex> lock(m_impl->mutex);
                    for (auto& connection : m_impl->connections)
                    {
                        {
                            std::lock_guard<std::mutex> sessionLock(connection->sessionMutex);
                            if (connection->session != nullptr)
                            {
                                connection->session->Stop();
                            }
                        }
                        connection->transport->Cancel(); // wakes the session's blocked read
                    }
                    for (auto& connection : m_impl->connections)
                    {
                        connection->thread.join();
                        connection->transport.reset();
                        CloseHandle(connection->pipe);
                    }
                    m_impl->connections.clear();
                }
                CloseHandle(m_impl->stopEvent);
                m_impl->stopEvent = nullptr;
            }

            void ControlPipeServer::NotifyChanged()
            {
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                for (auto& connection : m_impl->connections)
                {
                    std::lock_guard<std::mutex> sessionLock(connection->sessionMutex);
                    if (connection->session != nullptr)
                    {
                        connection->session->PushStatus();
                    }
                }
            }

            std::unique_ptr<Wire::Transport> ConnectToControlPipe(std::string& error)
            {
                for (int attempt = 0; attempt < 2; ++attempt)
                {
                    // Identification level: the helper (SYSTEM) may learn who connected but may not act as them.
                    HANDLE pipe = CreateFileW(kPipeNameWide, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                              FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                                              nullptr);
                    if (pipe != INVALID_HANDLE_VALUE)
                    {
                        return std::make_unique<OverlappedPipe>(pipe, true);
                    }
                    if (GetLastError() == ERROR_PIPE_BUSY)
                    {
                        WaitNamedPipeW(kPipeNameWide, 2000); // every instance is serving someone: wait for a free one
                        continue;
                    }
                    break;
                }
                error = GetLastError() == ERROR_FILE_NOT_FOUND ? "the Marconi Server helper is not running"
                                                              : "could not open the Marconi Server control pipe";
                return nullptr;
            }
        } // namespace Control
    } // namespace Server
} // namespace Marconi
