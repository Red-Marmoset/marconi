// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Phase 3 logic that needs no Windows: the service supervisor against a fake session list and launcher, the tray
// icon's control-pipe messages and state machine over an in-memory pipe, and lock on disconnect in the session

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiCore/Wire/WireMessages.h>
#include <MarconiServerCore/ControlProtocol.h>
#include <MarconiServerCore/ControlSession.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/ListenerControlHost.h>
#include <MarconiServerCore/ServiceSupervisor.h>
#include <MarconiServerCore/ServiceConfig.h>
#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;
    using namespace Marconi::Server;

    namespace
    {
        // -- Supervisor fakes ----------------------------------------------------------------------------------

        class FakeSessions : public ConsoleSessionSource
        {
        public:
            uint32_t GetActiveConsoleSessionId() override { return session; }
            uint32_t session = kNoConsoleSession;
        };

        class FakeLauncher : public HelperLauncher
        {
        public:
            bool Launch(uint32_t sessionId, HelperHandle& handle, std::string& error) override
            {
                if (failLaunch)
                {
                    error = "access denied";
                    return false;
                }
                handle = ++nextHandle;
                running[handle] = sessionId;
                launched.push_back(sessionId);
                return true;
            }
            bool IsRunning(HelperHandle handle) override { return running.count(handle) != 0; }
            void Terminate(HelperHandle handle) override
            {
                running.erase(handle);
                ++terminated;
            }
            // The helper process dies by itself.
            void Crash()
            {
                running.clear();
            }

            HelperHandle nextHandle = 0;
            std::map<HelperHandle, uint32_t> running;
            std::vector<uint32_t> launched; // the session of each launch, in order
            int terminated = 0;
            bool failLaunch = false;
        };

        // -- Control pipe fakes --------------------------------------------------------------------------------

        class FakeHost : public Control::ControlHost
        {
        public:
            Control::ServerStatus GetStatus() override
            {
                std::lock_guard<std::mutex> lock(mutex);
                return status;
            }
            Control::ResultCode DisconnectViewer(uint32_t viewerId, std::string& text) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                disconnected.push_back(viewerId);
                for (size_t i = 0; i < status.viewers.size(); ++i)
                {
                    if (viewerId == 0 || status.viewers[i].id == viewerId)
                    {
                        status.viewers.erase(status.viewers.begin() + static_cast<ptrdiff_t>(i));
                        return Control::ResultCode::Ok;
                    }
                }
                text = "Nobody is connected.";
                return Control::ResultCode::NoSuchViewer;
            }
            Control::ResultCode SetPasswordHash(const std::string& hash, std::string& text) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (failPassword)
                {
                    text = "disk full";
                    return Control::ResultCode::Failed;
                }
                passwords.push_back(hash);
                status.passwordSet = !hash.empty();
                return Control::ResultCode::Ok;
            }

            std::mutex mutex;
            Control::ServerStatus status;
            std::vector<uint32_t> disconnected;
            std::vector<std::string> passwords;
            bool failPassword = false;
        };

        // One control connection: a session thread on one end, and the pipes for a client on the other.
        struct ControlRig
        {
            explicit ControlRig(bool administrator = false)
                : serverTransport(clientToServer, serverToClient), clientTransport(serverToClient, clientToServer)
            {
                host.status.serverVersion = "0.9.9.1";
                host.status.port = 1895;
                host.status.consoleSession = 1;
                host.status.desktop = "Default";
                Control::ViewerInfo viewer;
                viewer.id = 7;
                viewer.name = "Sem's laptop";
                viewer.connectedSeconds = 42;
                host.status.viewers.push_back(viewer);
                Control::PeerInfo peer;
                peer.isAdministrator = administrator;
                session = std::make_unique<Control::ControlSession>(serverTransport, host, peer, "0.9.9.1");
                thread = std::thread([this]() { session->Run(); });
            }
            ~ControlRig()
            {
                clientToServer.Close();
                serverToClient.Close();
                thread.join();
            }

            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport;
            PipeTransport clientTransport;
            FakeHost host;
            std::unique_ptr<Control::ControlSession> session;
            std::thread thread;
        };

        // -- Session helpers -----------------------------------------------------------------------------------

        // Logs a viewer in over the in-memory transport and returns once the first frame is flowing.
        struct ViewerRig
        {
            explicit ViewerRig(bool lockOnDisconnect)
                : serverTransport(clientToServer, serverToClient),
                  clientTransport(serverToClient, clientToServer),
                  screen(4, 3)
            {
                Server::Settings settings;
                settings.allowNoPassword = true; // these tests log in without a password
                settings.lockOnDisconnect = lockOnDisconnect;
                session = std::make_unique<Server::Session>(serverTransport, screen, input, settings);
                session->SetFrameIntervalMs(1);
                thread = std::thread([this]() { session->Run(); });
            }

            void Send(const Wire::Bytes& framed) { clientTransport.Write(framed.data(), framed.size()); }

            void LogIn()
            {
                Wire::MessageReader reader(clientTransport);
                Wire::Hello hello;
                hello.clientName = "test viewer";
                Send(hello.Encode());
                Wire::Bytes bytes = reader.Next(); // Welcome
                Send(Wire::Login{""}.Encode());
                bytes = reader.Next(); // LoginResult
                Wire::Reader body(bytes);
                CHECK(Wire::ReadType(body) == Wire::Type::LoginResult);
                // The frame loop is running from here; keep draining so it never blocks on a full pipe.
                drain = std::thread([this]() {
                    uint8_t buffer[4096];
                    while (serverToClient.Read(buffer, sizeof(buffer)) > 0)
                    {
                    }
                });
            }

            // The viewer's end of the connection goes away, as when its window closes.
            void Leave()
            {
                clientToServer.Close();
            }

            void Finish()
            {
                clientToServer.Close();
                thread.join();
                serverToClient.Close();
                if (drain.joinable())
                {
                    drain.join();
                }
            }

            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport;
            PipeTransport clientTransport;
            Server::TestPatternScreen screen;
            RecordingInput input;
            std::unique_ptr<Server::Session> session;
            std::thread thread;
            std::thread drain;
        };

        bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs = 3000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (!predicate())
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return true;
        }
    } // namespace

    void RunServiceTests()
    {
        // -- The supervisor ------------------------------------------------------------------------------------

        RunTest("supervisor: starts the helper in the console session and leaves it alone", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            ServiceSupervisor supervisor(sessions, launcher);
            sessions.session = 1;
            supervisor.Tick(0);
            CHECK(supervisor.IsHelperRunning());
            CHECK(supervisor.GetHelperSession() == 1);
            CHECK(launcher.launched.size() == 1 && launcher.launched[0] == 1);
            for (int64_t t = 1000; t < 10000; t += 1000)
            {
                supervisor.Tick(t);
            }
            CHECK(launcher.launched.size() == 1);
            CHECK(launcher.terminated == 0);
        });

        RunTest("supervisor: waits for a console session, and ends the helper when there is none", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            ServiceSupervisor supervisor(sessions, launcher);
            supervisor.Tick(0);
            supervisor.Tick(1000);
            CHECK(!supervisor.IsHelperRunning());
            CHECK(launcher.launched.empty());
            sessions.session = 2;
            supervisor.Tick(2000);
            CHECK(supervisor.IsHelperRunning() && supervisor.GetHelperSession() == 2);
            sessions.session = kNoConsoleSession; // a fast user switch in progress
            supervisor.Tick(3000);
            CHECK(!supervisor.IsHelperRunning());
            CHECK(launcher.running.empty());
            sessions.session = 3;
            supervisor.Tick(3100);
            CHECK(supervisor.GetHelperSession() == 3); // started at once, no wait
        });

        RunTest("supervisor: a console session change moves the helper straight away", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            ServiceSupervisor supervisor(sessions, launcher);
            sessions.session = 1;
            supervisor.Tick(0);
            sessions.session = 4;
            supervisor.Tick(10);
            CHECK(launcher.terminated == 1);
            CHECK(launcher.launched.size() == 2 && launcher.launched[1] == 4);
            CHECK(launcher.running.size() == 1);
            CHECK(supervisor.GetHelperSession() == 4);
        });

        RunTest("supervisor: a helper that exits is restarted after a wait that grows, then resets", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            SupervisorPolicy policy;
            policy.firstRetryMs = 1000;
            policy.maxRetryMs = 4000;
            policy.stableAfterMs = 60000;
            ServiceSupervisor supervisor(sessions, launcher, policy);
            sessions.session = 1;
            supervisor.Tick(0);
            CHECK(launcher.launched.size() == 1);

            launcher.Crash();
            supervisor.Tick(100); // noticed; wait 1000 ms
            CHECK(!supervisor.IsHelperRunning());
            supervisor.Tick(1000);
            CHECK(launcher.launched.size() == 1); // not yet (due at 1100)
            supervisor.Tick(1100);
            CHECK(launcher.launched.size() == 2);

            launcher.Crash();
            supervisor.Tick(1200); // second quick failure: wait 2000 ms
            supervisor.Tick(3100);
            CHECK(launcher.launched.size() == 2);
            supervisor.Tick(3200);
            CHECK(launcher.launched.size() == 3);

            launcher.Crash();
            supervisor.Tick(3300); // third: 4000 ms (the ceiling)
            supervisor.Tick(7200);
            CHECK(launcher.launched.size() == 3);
            supervisor.Tick(7300);
            CHECK(launcher.launched.size() == 4);

            launcher.Crash();
            supervisor.Tick(7400); // fourth: still the ceiling
            supervisor.Tick(11300);
            CHECK(launcher.launched.size() == 4);
            supervisor.Tick(11400);
            CHECK(launcher.launched.size() == 5);

            // Up for over a minute, then it dies: the wait starts over at the first step.
            supervisor.Tick(80000);
            launcher.Crash();
            supervisor.Tick(80100);
            CHECK(supervisor.GetFailureCount() == 1);
            supervisor.Tick(81100);
            CHECK(launcher.launched.size() == 6);
        });

        RunTest("supervisor: a launch that fails is retried with a wait and the reason is kept", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            ServiceSupervisor supervisor(sessions, launcher);
            sessions.session = 1;
            launcher.failLaunch = true;
            supervisor.Tick(0);
            CHECK(!supervisor.IsHelperRunning());
            CHECK(supervisor.GetLastError() == "access denied");
            CHECK(supervisor.GetFailureCount() == 1);
            launcher.failLaunch = false;
            supervisor.Tick(500);
            CHECK(!supervisor.IsHelperRunning()); // still waiting
            supervisor.Tick(1000);
            CHECK(supervisor.IsHelperRunning());
            CHECK(supervisor.GetLastError().empty());
        });

        RunTest("supervisor: a new session is tried at once even while waiting out failures", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            ServiceSupervisor supervisor(sessions, launcher);
            sessions.session = 1;
            launcher.failLaunch = true;
            supervisor.Tick(0);
            launcher.failLaunch = false;
            sessions.session = 2;
            supervisor.Tick(10); // well inside the wait for session 1
            CHECK(supervisor.GetHelperSession() == 2);
        });

        RunTest("supervisor: Stop ends the helper and nothing starts after", []() {
            FakeSessions sessions;
            FakeLauncher launcher;
            ServiceSupervisor supervisor(sessions, launcher);
            sessions.session = 1;
            supervisor.Tick(0);
            supervisor.Stop();
            CHECK(launcher.running.empty());
            supervisor.Tick(5000);
            CHECK(launcher.launched.size() == 1);
        });

        // -- The control pipe's messages -----------------------------------------------------------------------

        RunTest("control messages encode and decode back", []() {
            Control::ServerStatus status;
            status.serverVersion = "1.2.3.4";
            status.port = 1895;
            status.passwordSet = true;
            status.lockOnDisconnect = true;
            status.consoleSession = 3;
            status.desktop = "Winlogon";
            Control::ViewerInfo a;
            a.id = 1;
            a.name = "Laptop";
            a.connectedSeconds = 90;
            Control::ViewerInfo b;
            b.id = 2;
            b.name = std::string("na\xC3\xAFve phone");
            b.connectedSeconds = 0;
            status.viewers = {a, b};

            auto unwrap = [](const Wire::Bytes& framed, Control::Type expected, Wire::Bytes& storage) {
                // Strip the varint length by decoding it by hand: the framing is covered in the wire tests.
                Wire::Reader length(framed);
                uint64_t n = length.Varint();
                storage.assign(framed.end() - static_cast<ptrdiff_t>(n), framed.end());
                Wire::Reader body(storage);
                CHECK(Control::ReadType(body) == expected);
            };

            Wire::Bytes storage;
            Control::Status statusMessage;
            statusMessage.status = status;
            unwrap(statusMessage.Encode(), Control::Type::Status, storage);
            {
                Wire::Reader body(storage);
                Control::ReadType(body);
                Control::Status decoded = Control::Status::Decode(body);
                CHECK(decoded.status == status);
                CHECK(body.AtEnd());
            }

            Control::Hello hello;
            hello.clientName = "tray icon (Sem)";
            unwrap(hello.Encode(), Control::Type::Hello, storage);
            {
                Wire::Reader body(storage);
                Control::ReadType(body);
                Control::Hello decoded = Control::Hello::Decode(body);
                CHECK(decoded.version == Control::kVersion && decoded.clientName == "tray icon (Sem)");
            }

            Control::Welcome welcome;
            welcome.serverVersion = "9.9";
            unwrap(welcome.Encode(), Control::Type::Welcome, storage);
            {
                Wire::Reader body(storage);
                Control::ReadType(body);
                CHECK(Control::Welcome::Decode(body).serverVersion == "9.9");
            }

            Control::DisconnectViewer disconnect;
            disconnect.viewerId = 77;
            unwrap(disconnect.Encode(), Control::Type::DisconnectViewer, storage);
            {
                Wire::Reader body(storage);
                Control::ReadType(body);
                CHECK(Control::DisconnectViewer::Decode(body).viewerId == 77);
            }

            Control::SetPassword password;
            password.passwordHash = "pbkdf2$abc";
            unwrap(password.Encode(), Control::Type::SetPassword, storage);
            {
                Wire::Reader body(storage);
                Control::ReadType(body);
                CHECK(Control::SetPassword::Decode(body).passwordHash == "pbkdf2$abc");
            }

            Control::Result result;
            result.request = Control::Type::SetPassword;
            result.code = Control::ResultCode::NotAdministrator;
            result.text = "needs an administrator";
            unwrap(result.Encode(), Control::Type::Result, storage);
            {
                Wire::Reader body(storage);
                Control::ReadType(body);
                Control::Result decoded = Control::Result::Decode(body);
                CHECK(decoded.request == Control::Type::SetPassword);
                CHECK(decoded.code == Control::ResultCode::NotAdministrator);
                CHECK(decoded.text == "needs an administrator");
            }

            unwrap(Control::GetStatus().Encode(), Control::Type::GetStatus, storage);
            CHECK(storage.size() == 1);
        });

        RunTest("control messages: random bytes are decoded or refused, never crash", []() {
            uint32_t state = 0x1234ABCDu;
            auto next = [&state]() {
                state = state * 1664525u + 1013904223u;
                return state >> 8;
            };
            for (int i = 0; i < 3000; ++i)
            {
                Wire::Bytes bytes(next() % 48);
                for (uint8_t& b : bytes)
                {
                    b = static_cast<uint8_t>(next());
                }
                try
                {
                    Wire::Reader a(bytes);
                    Control::Status::Decode(a);
                }
                catch (const Wire::WireError&)
                {
                }
                try
                {
                    Wire::Reader b(bytes);
                    Control::Result::Decode(b);
                    Wire::Reader c(bytes);
                    Control::Hello::Decode(c);
                    Wire::Reader d(bytes);
                    Control::SetPassword::Decode(d);
                }
                catch (const Wire::WireError&)
                {
                }
            }
            CHECK(true);
        });

        // -- The control pipe's state machine ------------------------------------------------------------------

        RunTest("control: Hello, Welcome and the first status arrive without asking", []() {
            ControlRig rig;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            CHECK(client.GetServerVersion() == "0.9.9.1");
            const Control::ServerStatus& status = client.GetLatestStatus();
            CHECK(status.port == 1895 && status.desktop == "Default" && status.consoleSession == 1);
            CHECK(status.viewers.size() == 1 && status.viewers[0].name == "Sem's laptop");
        });

        RunTest("control: the status follows the server's state", []() {
            ControlRig rig;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            {
                std::lock_guard<std::mutex> lock(rig.host.mutex);
                rig.host.status.viewers.clear();
                rig.host.status.desktop = "Winlogon";
            }
            Control::ServerStatus status;
            CHECK(client.RefreshStatus(status, error));
            CHECK(status.viewers.empty() && status.desktop == "Winlogon");
        });

        RunTest("control: a pushed status is kept while waiting for an answer", []() {
            ControlRig rig;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            {
                std::lock_guard<std::mutex> lock(rig.host.mutex);
                rig.host.status.desktop = "Winlogon";
            }
            CHECK(rig.session->PushStatus());
            std::string text;
            CHECK(client.DisconnectViewer(0, text) == Control::ResultCode::Ok);
            // The pushed status arrived ahead of the Result and is now the latest.
            CHECK(client.GetLatestStatus().desktop == "Winlogon");
        });

        RunTest("control: Disconnect viewer ends the named viewer, or reports there is none", []() {
            ControlRig rig;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            std::string text;
            CHECK(client.DisconnectViewer(99, text) == Control::ResultCode::NoSuchViewer);
            CHECK(!text.empty());
            CHECK(client.DisconnectViewer(7, text) == Control::ResultCode::Ok);
            std::lock_guard<std::mutex> lock(rig.host.mutex);
            CHECK(rig.host.disconnected.size() == 2 && rig.host.disconnected[1] == 7);
        });

        RunTest("control: Set password is refused for a non-administrator and never reaches the server", []() {
            ControlRig rig(false);
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            std::string text;
            CHECK(client.SetPasswordHash("pbkdf2$new", text) == Control::ResultCode::NotAdministrator);
            CHECK(!text.empty());
            {
                std::lock_guard<std::mutex> lock(rig.host.mutex);
                CHECK(rig.host.passwords.empty());
            }
            // The connection survives, so the tray icon can still read the status.
            Control::ServerStatus status;
            CHECK(client.RefreshStatus(status, error));
        });

        RunTest("control: an administrator's Set password is stored and shows in the status", []() {
            ControlRig rig(true);
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            std::string text;
            CHECK(client.SetPasswordHash("pbkdf2$new", text) == Control::ResultCode::Ok);
            Control::ServerStatus status;
            CHECK(client.RefreshStatus(status, error));
            CHECK(status.passwordSet);
            std::lock_guard<std::mutex> lock(rig.host.mutex);
            CHECK(rig.host.passwords.size() == 1 && rig.host.passwords[0] == "pbkdf2$new");
        });

        RunTest("control: a failure storing the password comes back with its reason", []() {
            ControlRig rig(true);
            rig.host.failPassword = true;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            std::string text;
            CHECK(client.SetPasswordHash("x", text) == Control::ResultCode::Failed);
            CHECK(text == "disk full");
        });

        RunTest("control: a request before Hello is refused and ends the connection", []() {
            ControlRig rig;
            Wire::Bytes request = Control::GetStatus().Encode();
            rig.clientTransport.Write(request.data(), request.size());
            Wire::MessageReader reader(rig.clientTransport);
            Wire::Bytes bytes = reader.Next();
            Wire::Reader body(bytes);
            CHECK(Control::ReadType(body) == Control::Type::Result);
            CHECK(Control::Result::Decode(body).code == Control::ResultCode::BadRequest);
            CHECK(!rig.session->IsReady());
            // The session has stopped reading: the thread finishes without the pipe being closed.
            rig.thread.join();
            rig.thread = std::thread([]() {}); // the rig's destructor joins this
        });

        RunTest("control: a Hello from another version is refused with a reason", []() {
            ControlRig rig;
            Control::Hello hello;
            hello.version = 99;
            Wire::Bytes request = hello.Encode();
            rig.clientTransport.Write(request.data(), request.size());
            Wire::MessageReader reader(rig.clientTransport);
            Wire::Bytes bytes = reader.Next();
            Wire::Reader body(bytes);
            CHECK(Control::ReadType(body) == Control::Type::Result);
            Control::Result result = Control::Result::Decode(body);
            CHECK(result.code == Control::ResultCode::BadRequest && !result.text.empty());
            rig.thread.join();
            rig.thread = std::thread([]() {});
        });

        RunTest("control: a message of an unknown type is skipped and the pipe keeps working", []() {
            ControlRig rig;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            Wire::Writer body;
            body.U8(200).String("from a newer tray icon");
            Wire::Bytes unknown = Wire::Frame(body.Data());
            rig.clientTransport.Write(unknown.data(), unknown.size());
            Control::ServerStatus status;
            CHECK(client.RefreshStatus(status, error));
        });

        RunTest("control: a malformed message ends the connection", []() {
            ControlRig rig;
            Control::ControlClient client(rig.clientTransport);
            std::string error;
            CHECK(client.Connect("tray icon", error));
            Wire::Writer body;
            body.U8(static_cast<uint8_t>(Control::Type::DisconnectViewer)).U8(1); // an id needs four bytes
            Wire::Bytes bad = Wire::Frame(body.Data());
            rig.clientTransport.Write(bad.data(), bad.size());
            rig.thread.join(); // Run returned by itself
            rig.thread = std::thread([]() {});
            CHECK(!rig.session->IsReady());
            Control::ServerStatus status;
            // Closing the server's end makes the client see the pipe end instead of hanging.
            rig.serverToClient.Close();
            CHECK(!client.RefreshStatus(status, error));
        });

        // -- Lock on disconnect and the Lock message -----------------------------------------------------------

        RunTest("lock: a Lock message from a logged-in viewer locks the computer once", []() {
            ViewerRig rig(false);
            rig.LogIn();
            rig.Send(Wire::Lock().Encode());
            rig.Send(Wire::Lock().Encode()); // a second one for the same viewer changes nothing
            CHECK(WaitUntil([&]() { return rig.input.locks.load() >= 1; }));
            rig.Send(Wire::Bye().Encode());
            rig.Finish();
            CHECK(rig.input.locks.load() == 1);
        });

        RunTest("lock: with the setting on, a viewer that leaves locks the computer", []() {
            ViewerRig rig(true);
            rig.LogIn();
            rig.Leave(); // the connection drops with no Lock and no Bye
            rig.thread.join();
            CHECK(rig.input.locks.load() == 1);
            rig.serverToClient.Close();
            rig.drain.join();
        });

        RunTest("lock: with the setting on, a Bye also locks, and a Lock before it doesn't lock twice", []() {
            ViewerRig rig(true);
            rig.LogIn();
            rig.Send(Wire::Lock().Encode());
            rig.Send(Wire::Bye().Encode());
            rig.Finish();
            CHECK(rig.input.locks.load() == 1);
        });

        RunTest("lock: with the setting off, leaving does not lock", []() {
            ViewerRig rig(false);
            rig.LogIn();
            rig.Send(Wire::Bye().Encode());
            rig.Finish();
            CHECK(rig.input.locks.load() == 0);
        });

        RunTest("lock: a viewer that never logged in does not lock the computer", []() {
            ViewerRig rig(true);
            Wire::MessageReader reader(rig.clientTransport);
            Wire::Hello hello;
            rig.Send(hello.Encode());
            reader.Next(); // Welcome
            rig.clientToServer.Close(); // leaves before the Login
            rig.thread.join();
            CHECK(rig.input.locks.load() == 0);
        });

        RunTest("lock: the server ending the viewer itself does not lock", []() {
            ViewerRig rig(true);
            rig.LogIn();
            rig.session->Stop();
            // Stop only ends the frame loop; the viewer's read is woken by the connection closing.
            rig.clientToServer.Close();
            rig.thread.join();
            CHECK(rig.input.locks.load() == 0);
            rig.serverToClient.Close();
            rig.drain.join();
        });

        RunTest("lock: the Lock message encodes to a type byte and decodes back", []() {
            Wire::Bytes framed = Wire::Lock().Encode();
            Wire::Reader length(framed);
            CHECK(length.Varint() == 1);
            CHECK(framed.size() == 2 && framed[1] == static_cast<uint8_t>(Wire::Type::Lock));
            Wire::Reader empty(nullptr, 0);
            Wire::Lock::Decode(empty);
        });

        // -- Over a real socket: the Wire client's lock request, and the tray icon's view of the listener ----------

        RunTest("lock: the Wire client's lock request reaches the server before it disconnects", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            settings.port = 0;
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.ServeOne(5000); });

            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.clientName = "lock viewer";
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));

            CHECK(client.SendLock());
            CHECK(WaitUntil([&]() { return input.locks.load() == 1; }));
            client.Disconnect();
            listener.Stop();
            serverThread.join();
            CHECK(input.locks.load() == 1);
        });

        RunTest("tray status: the listener reports the connected viewer and can disconnect it", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            settings.port = 0;
            settings.serverVersion = "1.0.0.0";
            settings.lockOnDisconnect = true;
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.ServeOne(5000); });

            std::string changedTo;
            Server::ListenerControlHost host(
                listener, settings, 5,
                [&](const std::string& hash, std::string&) {
                    changedTo = hash;
                    return Control::ResultCode::Ok;
                },
                []() { return std::string("Default"); });
            Control::ServerStatus status = host.GetStatus();
            CHECK(status.viewers.empty() && !status.passwordSet && status.lockOnDisconnect);
            CHECK(status.port == listener.GetPort() && status.consoleSession == 5 && status.desktop == "Default");

            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.clientName = "status viewer";
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(WaitUntil([&]() { return host.GetStatus().viewers.size() == 1; }));
            status = host.GetStatus();
            CHECK(status.viewers[0].name == "status viewer" && status.viewers[0].id != 0);

            std::string text;
            CHECK(host.DisconnectViewer(status.viewers[0].id + 100, text) == Control::ResultCode::NoSuchViewer);
            CHECK(host.DisconnectViewer(status.viewers[0].id, text) == Control::ResultCode::Ok);
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            serverThread.join();
            CHECK(host.GetStatus().viewers.empty());
            // Disconnected from the server's side: that is not "the viewer left", so no lock.
            CHECK(input.locks.load() == 0);

            CHECK(host.SetPasswordHash("hash", text) == Control::ResultCode::Ok);
            CHECK(changedTo == "hash" && host.GetStatus().passwordSet);
            listener.Stop();
        });

        // -- The settings file the service and helper share ----------------------------------------------------

        RunTest("service settings: a missing file leaves the defaults, a saved file loads back", []() {
            const char* tmp = std::getenv("TMPDIR");
            std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/marconi-svc-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            settings.port = 4321;
            std::string error;
            CHECK(Server::LoadServiceConfig(dir, settings, error)); // no folder yet
            CHECK(settings.port == 4321 && !settings.lockOnDisconnect);

            settings.serverName = "Office PC";
            settings.passwordHash = "pbkdf2$1$aa$bb";
            settings.lockOnDisconnect = true;
            CHECK(Server::SaveServiceConfig(dir, settings, error));

            Server::Settings loaded;
            CHECK(Server::LoadServiceConfig(dir, loaded, error));
            CHECK(loaded.port == 4321 && loaded.serverName == "Office PC");
            CHECK(loaded.passwordHash == "pbkdf2$1$aa$bb" && loaded.lockOnDisconnect);
            std::filesystem::remove_all(dir);
        });

        RunTest("service settings: comments and unknown keys are skipped, bad values are refused", []() {
            const char* tmp = std::getenv("TMPDIR");
            std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/marconi-svc2-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            std::filesystem::create_directories(dir);
            auto write = [&](const char* text) {
                std::ofstream file(Server::GetConfigPath(dir), std::ios::trunc);
                file << text;
            };
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            std::string error;
            write("# a comment\n\nfuture-key = whatever\nport = 1900\n");
            CHECK(Server::LoadServiceConfig(dir, settings, error) && settings.port == 1900);
            write("port=notanumber\n");
            CHECK(!Server::LoadServiceConfig(dir, settings, error) && !error.empty());
            write("port=70000\n");
            CHECK(!Server::LoadServiceConfig(dir, settings, error));
            write("lock-on-disconnect=maybe\n");
            CHECK(!Server::LoadServiceConfig(dir, settings, error));
            write("no equals sign\n");
            CHECK(!Server::LoadServiceConfig(dir, settings, error));
            std::filesystem::remove_all(dir);
        });
    }
} // namespace MarconiServerTest
