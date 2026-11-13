// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray icon's mode decisions (own server or status-only) against a fake helper and a fake clock, the same logic
// over the real control messages on an in-memory pipe, and end to end on Linux: a real Listener with a
// ListenerControlHost on a Unix socket, with the tray logic talking to it.

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/ControlProtocol.h>
#include <MarconiServerCore/ControlSession.h>
#include <MarconiServerCore/ControlSocket.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/ListenerControlHost.h>
#include <MarconiServerCore/TestPatternScreen.h>
#include <MarconiServerCore/TrayMode.h>
#include <MarconiServerCore/TrayTransportLink.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace MarconiServerTest
{
    using namespace Marconi;
    using namespace Marconi::Server;
    using namespace Marconi::Server::Tray;

    namespace
    {
        bool WaitFor(const std::function<bool()>& predicate, int timeoutMs = 3000)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (!predicate())
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return true;
        }

        bool HasLabel(const std::vector<MenuItem>& items, const std::string& label)
        {
            for (const MenuItem& item : items)
            {
                if (item.label == label)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasLabelStartingWith(const std::vector<MenuItem>& items, const std::string& prefix)
        {
            for (const MenuItem& item : items)
            {
                if (item.label.compare(0, prefix.size(), prefix) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // A helper that can be switched on and off, whose links fail on demand.
        struct FakeHelper
        {
            bool present = false;
            ConnectOutcome failure = ConnectOutcome::Connected; // Connected = no failure, else what Connect returns
            int failNextStatuses = 0;                            // open links fail this many GetStatus calls
            Control::ServerStatus status;
            Control::ResultCode passwordCode = Control::ResultCode::Ok;
            std::string passwordText;
            std::vector<std::string> passwords;
            std::vector<uint32_t> disconnects;
            int connects = 0;
            int statusCalls = 0;
        };

        class FakeLink : public Link
        {
        public:
            explicit FakeLink(FakeHelper& helper) : m_helper(helper) {}
            bool GetStatus(Control::ServerStatus& status, std::string& error) override
            {
                ++m_helper.statusCalls;
                if (m_helper.failNextStatuses > 0)
                {
                    --m_helper.failNextStatuses;
                    error = "broken";
                    return false;
                }
                if (!m_helper.present)
                {
                    error = "gone";
                    return false;
                }
                status = m_helper.status;
                return true;
            }
            Control::ResultCode DisconnectViewer(uint32_t viewerId, std::string&) override
            {
                m_helper.disconnects.push_back(viewerId);
                m_helper.status.viewers.clear();
                return Control::ResultCode::Ok;
            }
            Control::ResultCode SetPasswordHash(const std::string& hash, std::string& text) override
            {
                text = m_helper.passwordText;
                if (m_helper.passwordCode == Control::ResultCode::Ok)
                {
                    m_helper.passwords.push_back(hash);
                    m_helper.status.passwordSet = !hash.empty();
                }
                return m_helper.passwordCode;
            }

        private:
            FakeHelper& m_helper;
        };

        class FakeConnector : public Connector
        {
        public:
            explicit FakeConnector(FakeHelper& helper) : m_helper(helper) {}
            ConnectResult Connect() override
            {
                ++m_helper.connects;
                ConnectResult result;
                if (!m_helper.present)
                {
                    result.outcome = ConnectOutcome::Absent;
                    return result;
                }
                if (m_helper.failure != ConnectOutcome::Connected)
                {
                    result.outcome = m_helper.failure;
                    result.text = "fake failure";
                    return result;
                }
                result.outcome = ConnectOutcome::Connected;
                result.link = std::make_unique<FakeLink>(m_helper);
                return result;
            }

        private:
            FakeHelper& m_helper;
        };

        // The machine with a fake helper, a clock moved by hand, and a log of what it told its owner.
        struct TrayRig
        {
            TrayRig() : connector(helper), mode(connector, [this]() { return now; })
            {
                helper.status.serverVersion = "1.0.0.0";
                helper.status.port = 1895;
                helper.status.passwordSet = true;
                helper.status.fingerprint = "0123456789abcdef0123456789abcdef";
                mode.SetModeChangedHandler([this](Mode from, Mode to) { changes.push_back({from, to}); });
                mode.SetStatusChangedHandler([this]() { ++statusChanges; });
            }
            void Advance(int64_t ms)
            {
                now += ms;
                mode.Tick();
            }
            // One poll interval later.
            void Poll() { Advance(3000); }

            FakeHelper helper;
            FakeConnector connector;
            int64_t now = 100000;
            TrayMode mode;
            std::vector<std::pair<Mode, Mode>> changes;
            int statusChanges = 0;
        };

        void AddViewer(Control::ServerStatus& status, uint32_t id, const std::string& name, uint32_t seconds)
        {
            Control::ViewerInfo viewer;
            viewer.id = id;
            viewer.name = name;
            viewer.connectedSeconds = seconds;
            status.viewers.push_back(viewer);
        }

        // -- A real ControlSession behind a TransportConnector, in memory -------------------------------------

        struct InMemoryHost : public Control::ControlHost
        {
            Control::ServerStatus GetStatus() override { return status; }
            Control::ResultCode DisconnectViewer(uint32_t viewerId, std::string& text) override
            {
                disconnected.push_back(viewerId);
                status.viewers.clear();
                text.clear();
                return Control::ResultCode::Ok;
            }
            Control::ResultCode SetPasswordHash(const std::string& hash, std::string&) override
            {
                passwords.push_back(hash);
                status.passwordSet = !hash.empty();
                return Control::ResultCode::Ok;
            }
            Control::ServerStatus status;
            std::vector<uint32_t> disconnected;
            std::vector<std::string> passwords;
        };

        // Hands out one in-memory connection at a time to a ControlSession on its own thread.
        struct InMemoryServer
        {
            struct Connection
            {
                Connection(Control::ControlHost& host, bool admin)
                    : serverSide(clientToServer, serverToClient), clientSide(serverToClient, clientToServer)
                {
                    Control::PeerInfo peer;
                    peer.isAdministrator = admin;
                    session = std::make_unique<Control::ControlSession>(serverSide, host, peer, "1.0.0.0");
                    thread = std::thread([this]() { session->Run(); });
                }
                ~Connection()
                {
                    clientToServer.Close();
                    serverToClient.Close();
                    thread.join();
                }
                Pipe clientToServer;
                Pipe serverToClient;
                PipeTransport serverSide;
                PipeTransport clientSide;
                std::unique_ptr<Control::ControlSession> session;
                std::thread thread;
            };

            // The transport the tray icon gets must outlive nothing but its own use; the connection lives here.
            class ClientEnd : public Wire::Transport
            {
            public:
                explicit ClientEnd(std::shared_ptr<Connection> connection) : m_connection(std::move(connection)) {}
                ptrdiff_t Read(void* buffer, size_t length) override { return m_connection->clientSide.Read(buffer, length); }
                bool Write(const void* data, size_t length) override { return m_connection->clientSide.Write(data, length); }

            private:
                std::shared_ptr<Connection> m_connection;
            };

            TransportFactory Factory()
            {
                return [this](std::string& error) -> std::unique_ptr<Wire::Transport> {
                    if (!up)
                    {
                        error = "nothing there";
                        return nullptr;
                    }
                    return std::make_unique<ClientEnd>(std::make_shared<Connection>(host, administrator));
                };
            }

            InMemoryHost host;
            bool up = true;
            bool administrator = false;
        };

        // A fake helper that answers a Hello with either a Welcome of another version or a refusal, like the real
        // one does for a Hello of another version.
        struct OtherVersionServer
        {
            explicit OtherVersionServer(bool refuse) : m_refuse(refuse) {}

            struct Connection
            {
                Connection(bool refuse)
                    : serverSide(clientToServer, serverToClient), clientSide(serverToClient, clientToServer)
                {
                    thread = std::thread([this, refuse]() {
                        try
                        {
                            Wire::MessageReader reader(serverSide);
                            reader.Next(); // the Hello
                            if (refuse)
                            {
                                Control::Result result;
                                result.request = Control::Type::Hello;
                                result.code = Control::ResultCode::BadRequest;
                                result.text = "different versions";
                                Wire::Bytes bytes = result.Encode();
                                serverSide.Write(bytes.data(), bytes.size());
                            }
                            else
                            {
                                Control::Welcome welcome;
                                welcome.version = Control::kVersion + 1;
                                Wire::Bytes bytes = welcome.Encode();
                                serverSide.Write(bytes.data(), bytes.size());
                            }
                            reader.Next(); // wait for the client to go away
                        }
                        catch (const Wire::WireError&)
                        {
                        }
                    });
                }
                ~Connection()
                {
                    clientToServer.Close();
                    serverToClient.Close();
                    thread.join();
                }
                Pipe clientToServer;
                Pipe serverToClient;
                PipeTransport serverSide;
                PipeTransport clientSide;
                std::thread thread;
            };

            class ClientEnd : public Wire::Transport
            {
            public:
                explicit ClientEnd(std::shared_ptr<Connection> connection) : m_connection(std::move(connection)) {}
                ptrdiff_t Read(void* buffer, size_t length) override { return m_connection->clientSide.Read(buffer, length); }
                bool Write(const void* data, size_t length) override { return m_connection->clientSide.Write(data, length); }

            private:
                std::shared_ptr<Connection> m_connection;
            };

            TransportFactory Factory()
            {
                return [this](std::string&) -> std::unique_ptr<Wire::Transport> {
                    return std::make_unique<ClientEnd>(std::make_shared<Connection>(m_refuse));
                };
            }

        private:
            bool m_refuse;
        };
    } // namespace

    void RunTrayTests()
    {
        // -- Which mode: the control pipe decides --------------------------------------------------------------

        RunTest("tray: with no helper on the pipe it runs its own server, straight away", []() {
            TrayRig rig;
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::OwnServer);
            CHECK(rig.changes.size() == 1 && rig.changes[0].first == Mode::Unknown &&
                  rig.changes[0].second == Mode::OwnServer);
        });

        RunTest("tray: with a helper on the pipe it is status-only and starts no server of its own", []() {
            TrayRig rig;
            rig.helper.present = true;
            AddViewer(rig.helper.status, 7, "Sem's laptop", 90);
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
            CHECK(rig.changes.size() == 1 && rig.changes[0].second == Mode::StatusOnly);
            const Snapshot& snapshot = rig.mode.GetSnapshot();
            CHECK(snapshot.status.port == 1895 && snapshot.status.passwordSet);
            CHECK(snapshot.status.viewers.size() == 1 && snapshot.status.viewers[0].name == "Sem's laptop");
            CHECK(snapshot.status.fingerprint == "0123456789abcdef0123456789abcdef");
        });

        RunTest("tray: it polls once per interval, not on every tick", []() {
            TrayRig rig;
            rig.mode.Tick();
            CHECK(rig.helper.connects == 1);
            rig.Advance(1000);
            rig.Advance(1000);
            CHECK(rig.helper.connects == 1);
            rig.Advance(1000);
            CHECK(rig.helper.connects == 2);
        });

        RunTest("tray: a helper that appears later takes over from the tray's own server", []() {
            TrayRig rig;
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::OwnServer);
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::OwnServer && rig.changes.size() == 1); // no repeat notice
            rig.helper.present = true;
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
            CHECK(rig.changes.size() == 2 && rig.changes[1].first == Mode::OwnServer &&
                  rig.changes[1].second == Mode::StatusOnly);
        });

        RunTest("tray: a helper that goes away for good hands back to its own server after three failed polls", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
            rig.helper.present = false;
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly && rig.mode.GetSnapshot().failedPolls == 1);
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly && rig.mode.GetSnapshot().failedPolls == 2);
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::OwnServer);
            CHECK(rig.changes.back().first == Mode::StatusOnly && rig.changes.back().second == Mode::OwnServer);
            CHECK(rig.mode.GetSnapshot().status.port == 0); // none of the helper's state is left to show
        });

        RunTest("tray: it can go back and forth when the service stops and starts again", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.mode.Tick();
            rig.helper.present = false;
            rig.Poll();
            rig.Poll();
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::OwnServer);
            rig.helper.present = true;
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
            CHECK(rig.changes.size() == 3); // Unknown > Status, Status > Own, Own > Status
        });

        RunTest("tray: one briefly failing poll does not flap the mode", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.mode.Tick();
            // The open link fails once and the reconnect works: nothing visible happens.
            rig.helper.failNextStatuses = 1;
            const int changesBefore = static_cast<int>(rig.changes.size());
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
            CHECK(rig.mode.GetSnapshot().failedPolls == 0);
            CHECK(static_cast<int>(rig.changes.size()) == changesBefore);
            CHECK(rig.helper.connects == 2); // it did reconnect
        });

        RunTest("tray: two failed polls then a good one stay status-only and the menu says so only meanwhile", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.mode.Tick();
            CHECK(!HasLabelStartingWith(StatusOnlyMenu(rig.mode.GetSnapshot()), "Lost contact"));
            rig.helper.present = false;
            rig.Poll();
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
            CHECK(HasLabelStartingWith(StatusOnlyMenu(rig.mode.GetSnapshot()), "Lost contact"));
            rig.helper.present = true;
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly && rig.mode.GetSnapshot().failedPolls == 0);
            CHECK(!HasLabelStartingWith(StatusOnlyMenu(rig.mode.GetSnapshot()), "Lost contact"));
            CHECK(rig.changes.size() == 1); // only the first Unknown > StatusOnly
        });

        RunTest("tray: a helper of another protocol version is never fought over the port", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.helper.failure = ConnectOutcome::IncompatibleVersion;
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::ServiceUnreadable);
            CHECK(rig.changes.size() == 1 && rig.changes[0].second == Mode::ServiceUnreadable);
            CHECK(!rig.mode.GetSnapshot().notice.empty());
            rig.Poll();
            rig.Poll();
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::ServiceUnreadable); // still no own server of its own
            std::vector<MenuItem> menu = StatusOnlyMenu(rig.mode.GetSnapshot());
            CHECK(HasLabelStartingWith(menu, "Marconi Server is running on this computer, but it is a different version"));
            for (const MenuItem& item : menu)
            {
                CHECK(item.kind != ItemKind::SetPassword && item.kind != ItemKind::DisconnectViewer);
            }
            // A matching helper later gets it going.
            rig.helper.failure = ConnectOutcome::Connected;
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
        });

        RunTest("tray: an unreadable helper at start is not fought over the port either, until it is really gone", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.helper.failure = ConnectOutcome::Failed;
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::ServiceUnreadable);
            rig.helper.present = false;
            rig.Poll();
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::ServiceUnreadable);
            rig.Poll();
            CHECK(rig.mode.GetMode() == Mode::OwnServer);
        });

        // -- The menu -------------------------------------------------------------------------------------------

        RunTest("tray menu: status-only shows who is connected, the password, the fingerprint and the actions", []() {
            TrayRig rig;
            rig.helper.present = true;
            AddViewer(rig.helper.status, 7, "Sem's laptop", 150);
            rig.mode.Tick();
            std::vector<MenuItem> menu = StatusOnlyMenu(rig.mode.GetSnapshot());
            CHECK(HasLabel(menu, "Marconi Server is running on this computer"));
            CHECK(HasLabel(menu, "Connected: Sem's laptop (2 minutes)"));
            CHECK(HasLabel(menu, "Port 1895"));
            CHECK(HasLabel(menu, "Password required"));
            CHECK(HasLabel(menu, "Fingerprint 0123456789abcdef..."));
            CHECK(HasLabel(menu, "Copy full fingerprint"));
            CHECK(HasLabel(menu, "Disconnect viewer"));
            CHECK(HasLabel(menu, "Change password..."));
            CHECK(menu.back().kind == ItemKind::Quit);
        });

        RunTest("tray menu: nobody connected and no password", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.helper.status.passwordSet = false;
            rig.helper.status.fingerprint.clear();
            rig.mode.Tick();
            std::vector<MenuItem> menu = StatusOnlyMenu(rig.mode.GetSnapshot());
            CHECK(HasLabel(menu, "Nobody is connected"));
            CHECK(HasLabel(menu, "No password set: viewers can't connect"));
            CHECK(HasLabel(menu, "Set password..."));
            CHECK(!HasLabel(menu, "Disconnect viewer") && !HasLabel(menu, "Copy full fingerprint"));
        });

        RunTest("tray menu: several viewers each get a Disconnect item naming them", []() {
            TrayRig rig;
            rig.helper.present = true;
            AddViewer(rig.helper.status, 7, "Sem", 5);
            AddViewer(rig.helper.status, 8, "Ana", 5);
            rig.mode.Tick();
            std::vector<MenuItem> menu = StatusOnlyMenu(rig.mode.GetSnapshot());
            CHECK(HasLabel(menu, "Disconnect Sem") && HasLabel(menu, "Disconnect Ana"));
            CHECK(!HasLabel(menu, "Disconnect viewer"));
        });

        RunTest("tray menu: the menu follows the helper's state as it changes", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.mode.Tick();
            CHECK(HasLabel(StatusOnlyMenu(rig.mode.GetSnapshot()), "Nobody is connected"));
            const int before = rig.statusChanges;
            AddViewer(rig.helper.status, 9, "Late", 1);
            rig.Poll();
            CHECK(rig.statusChanges > before);
            CHECK(HasLabel(StatusOnlyMenu(rig.mode.GetSnapshot()), "Connected: Late (less than a minute)"));
            CHECK(HasLabel(StatusOnlyMenu(rig.mode.GetSnapshot()), "Disconnect viewer"));
        });

        // -- Actions --------------------------------------------------------------------------------------------

        RunTest("tray: Disconnect viewer goes to the helper and the menu updates at once", []() {
            TrayRig rig;
            rig.helper.present = true;
            AddViewer(rig.helper.status, 7, "Sem", 5);
            rig.mode.Tick();
            ActionResult result = rig.mode.DisconnectViewer(7);
            CHECK(result.ok);
            CHECK(rig.helper.disconnects.size() == 1 && rig.helper.disconnects[0] == 7);
            CHECK(rig.mode.GetSnapshot().status.viewers.empty());
        });

        RunTest("tray: Set password refused for a non-administrator says an administrator is needed", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.helper.passwordCode = Control::ResultCode::NotAdministrator;
            rig.helper.passwordText = "Changing the password needs administrator permission.";
            rig.mode.Tick();
            ActionResult result = rig.mode.SetPasswordHash("hash");
            CHECK(!result.ok);
            CHECK(result.message.find("administrator") != std::string::npos);
            CHECK(result.message.find("Run as administrator") != std::string::npos);
            CHECK(result.message.find("not changed") != std::string::npos);
            CHECK(rig.helper.passwords.empty());
            CHECK(rig.mode.GetMode() == Mode::StatusOnly);
        });

        RunTest("tray: Set password failing on the helper's side shows its reason", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.helper.passwordCode = Control::ResultCode::Failed;
            rig.helper.passwordText = "Could not save the password: disk full";
            rig.mode.Tick();
            ActionResult result = rig.mode.SetPasswordHash("hash");
            CHECK(!result.ok && result.message == "The password was not changed: Could not save the password: disk full");
        });

        RunTest("tray: Set password succeeding is stored and the menu says a password is required", []() {
            TrayRig rig;
            rig.helper.present = true;
            rig.helper.status.passwordSet = false;
            rig.mode.Tick();
            CHECK(HasLabel(StatusOnlyMenu(rig.mode.GetSnapshot()), "Set password..."));
            ActionResult result = rig.mode.SetPasswordHash("pbkdf2$new");
            CHECK(result.ok);
            CHECK(rig.helper.passwords.size() == 1 && rig.helper.passwords[0] == "pbkdf2$new");
            CHECK(HasLabel(StatusOnlyMenu(rig.mode.GetSnapshot()), "Change password..."));
        });

        RunTest("tray: the actions say the helper can't be reached when it is not in status-only mode", []() {
            TrayRig rig;
            rig.mode.Tick();
            CHECK(rig.mode.GetMode() == Mode::OwnServer);
            ActionResult password = rig.mode.SetPasswordHash("hash");
            CHECK(!password.ok && password.message.find("isn't reachable") != std::string::npos);
            ActionResult disconnect = rig.mode.DisconnectViewer(1);
            CHECK(!disconnect.ok && !disconnect.message.empty());
        });

        RunTest("tray: the words for the pipe's results", []() {
            CHECK(DescribePasswordResult(Control::ResultCode::Ok, "").ok);
            CHECK(DescribePasswordResult(Control::ResultCode::BadRequest, "x").message.find("did not accept") !=
                  std::string::npos);
            CHECK(DescribePasswordResult(Control::ResultCode::Failed, "").message.find("could not save") !=
                  std::string::npos);
            CHECK(DescribeDisconnectResult(Control::ResultCode::NoSuchViewer, "").message ==
                  "That viewer has already gone.");
            CHECK(!DescribeDisconnectResult(Control::ResultCode::Failed, "").ok);
        });

        // -- The real control messages, in memory ---------------------------------------------------------------

        RunTest("tray over the control messages: status-only, Disconnect viewer, and the administrator refusal", []() {
            InMemoryServer server;
            server.host.status.serverVersion = "1.0.0.0";
            server.host.status.port = 1895;
            server.host.status.fingerprint = "abcdef0123456789abcdef";
            AddViewer(server.host.status, 3, "Sem", 10);
            TransportConnector connector(server.Factory(), "tray icon (test)");
            int64_t now = 0;
            TrayMode mode(connector, [&]() { return now; });
            mode.Tick();
            CHECK(mode.GetMode() == Mode::StatusOnly);
            CHECK(mode.GetSnapshot().status.fingerprint == "abcdef0123456789abcdef");
            CHECK(mode.GetSnapshot().status.viewers.size() == 1);

            ActionResult password = mode.SetPasswordHash("pbkdf2$x"); // the peer is not an administrator
            CHECK(!password.ok && password.message.find("administrator") != std::string::npos);
            CHECK(server.host.passwords.empty());
            CHECK(mode.GetMode() == Mode::StatusOnly);

            CHECK(mode.DisconnectViewer(3).ok);
            CHECK(server.host.disconnected.size() == 1 && server.host.disconnected[0] == 3);
            CHECK(mode.GetSnapshot().status.viewers.empty());
        });

        RunTest("tray over the control messages: an administrator's Set password is applied", []() {
            InMemoryServer server;
            server.administrator = true;
            TransportConnector connector(server.Factory(), "tray icon (test)");
            int64_t now = 0;
            TrayMode mode(connector, [&]() { return now; });
            mode.Tick();
            CHECK(mode.GetMode() == Mode::StatusOnly && !mode.GetSnapshot().status.passwordSet);
            CHECK(mode.SetPasswordHash("pbkdf2$x").ok);
            CHECK(server.host.passwords.size() == 1);
            CHECK(mode.GetSnapshot().status.passwordSet);
        });

        RunTest("tray: a helper of another protocol version (a different Welcome) falls back safely", []() {
            OtherVersionServer server(false);
            TransportConnector connector(server.Factory(), "tray icon (test)");
            int64_t now = 0;
            TrayMode mode(connector, [&]() { return now; });
            mode.Tick();
            CHECK(mode.GetMode() == Mode::ServiceUnreadable);
            CHECK(mode.GetSnapshot().notice.find("different version") != std::string::npos);
        });

        RunTest("tray: a helper that refuses our Hello over its version falls back safely", []() {
            OtherVersionServer server(true);
            TransportConnector connector(server.Factory(), "tray icon (test)");
            int64_t now = 0;
            TrayMode mode(connector, [&]() { return now; });
            mode.Tick();
            CHECK(mode.GetMode() == Mode::ServiceUnreadable);
        });

        RunTest("tray: the control status carries the fingerprint, and an older status without one still reads", []() {
            Control::Status status;
            status.status.port = 9;
            status.status.fingerprint = "feedface";
            Wire::Bytes framed = status.Encode();
            // Strip the length prefix (one byte here) to get type + body.
            Wire::Reader body(framed.data() + 1, framed.size() - 1);
            Control::ReadType(body);
            CHECK(Control::Status::Decode(body).status.fingerprint == "feedface");

            // An older helper's Status stops after the viewers: build one by cutting the fingerprint (a length byte 8
            // and eight characters) off the end and fixing the frame length.
            Wire::Bytes older(framed.begin() + 1, framed.end() - 9);
            Wire::Reader oldBody(older.data(), older.size());
            Control::ReadType(oldBody);
            Control::Status decoded = Control::Status::Decode(oldBody);
            CHECK(decoded.status.port == 9 && decoded.status.fingerprint.empty());
        });

#ifndef _WIN32
        // -- End to end on Linux: a real Listener and ListenerControlHost on a Unix socket ----------------------

        RunTest("tray end to end: a real listener's status, Disconnect viewer, and Set password over a Unix socket", []() {
            const char* tmp = std::getenv("TMPDIR");
            const std::string socketPath =
                std::string(tmp ? tmp : "/tmp") + "/marconi-tray-test-" + std::to_string(::getpid()) + ".sock";

            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // the test viewer logs in without a password
            settings.port = 0;
            settings.serverVersion = "1.0.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.Run(); });

            std::string changedTo;
            ListenerControlHost host(listener, settings, 0, [&](const std::string& hash, std::string&) {
                changedTo = hash;
                listener.SetPasswordHash(hash);
                return Control::ResultCode::Ok;
            });

            // Nobody is an administrator on this socket first.
            auto server = std::make_unique<Control::ControlSocketServer>(host, "1.0.0.0", Control::SocketAdmins::Nobody);
            CHECK(server->Start(socketPath, error));

            TransportConnector connector(
                [socketPath](std::string& e) { return Control::ConnectToControlSocket(socketPath, e); },
                "tray icon (test)");
            int64_t now = 0;
            TrayMode mode(connector, [&]() { return now; });
            std::vector<Mode> seen;
            mode.SetModeChangedHandler([&](Mode, Mode to) { seen.push_back(to); });
            mode.Tick();
            CHECK(mode.GetMode() == Mode::StatusOnly);
            CHECK(mode.GetSnapshot().status.port == listener.GetPort());
            CHECK(mode.GetSnapshot().status.fingerprint == listener.GetCertificateFingerprint());
            CHECK(!mode.GetSnapshot().status.fingerprint.empty());
            CHECK(!mode.GetSnapshot().status.passwordSet);
            CHECK(mode.GetSnapshot().status.viewers.empty());

            // A viewer connects: the next poll shows it.
            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.clientName = "tray test viewer";
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(WaitFor([&]() {
                now += 3000;
                mode.Tick();
                return mode.GetSnapshot().status.viewers.size() == 1;
            }));
            CHECK(mode.GetSnapshot().status.viewers[0].name == "tray test viewer");
            CHECK(HasLabel(StatusOnlyMenu(mode.GetSnapshot()), "Disconnect viewer"));

            // Set password: refused here, nothing changes, and the words say why.
            ActionResult refused = mode.SetPasswordHash("pbkdf2$new");
            CHECK(!refused.ok && refused.message.find("administrator") != std::string::npos);
            CHECK(changedTo.empty() && !mode.GetSnapshot().status.passwordSet);

            // Disconnect viewer ends the real viewer's connection.
            CHECK(mode.DisconnectViewer(mode.GetSnapshot().status.viewers[0].id).ok);
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            CHECK(WaitFor([&]() {
                now += 3000;
                mode.Tick();
                return mode.GetSnapshot().status.viewers.empty();
            }));

            // The service stops: after three failed polls the tray icon runs its own server again.
            server->Stop();
            server.reset();
            for (int i = 0; i < 3; ++i)
            {
                now += 3000;
                mode.Tick();
            }
            CHECK(mode.GetMode() == Mode::OwnServer);

            // It starts again, with administrators this time: status-only again, and Set password goes through.
            server = std::make_unique<Control::ControlSocketServer>(host, "1.0.0.0", Control::SocketAdmins::Everybody);
            CHECK(server->Start(socketPath, error));
            now += 3000;
            mode.Tick();
            CHECK(mode.GetMode() == Mode::StatusOnly);
            ActionResult accepted = mode.SetPasswordHash("pbkdf2$new");
            CHECK(accepted.ok);
            CHECK(changedTo == "pbkdf2$new" && mode.GetSnapshot().status.passwordSet);
            CHECK(seen.size() == 3 && seen[0] == Mode::StatusOnly && seen[1] == Mode::OwnServer &&
                  seen[2] == Mode::StatusOnly);

            server->Stop();
            listener.Stop();
            serverThread.join();
        });
#endif
    }
} // namespace MarconiServerTest
