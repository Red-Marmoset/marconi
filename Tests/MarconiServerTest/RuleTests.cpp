// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server's connection rules: the allow list (parser and matcher, and refusing an address on a real loopback
// socket), the idle disconnect (with a clock moved by hand) and the notices to the person at the computer

#include "TestSupport.h"

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/AllowList.h>
#include <MarconiServerCore/Auth.h>
#include <MarconiServerCore/ConnectionNotifier.h>
#include <MarconiServerCore/IdleWatch.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        bool Parses(const std::string& text)
        {
            Server::AddressRule rule;
            std::string error;
            return Server::ParseAddressRule(text, rule, error);
        }

        // A list of the given entries, which must all be readable.
        Server::AllowList ListOf(const std::vector<std::string>& entries)
        {
            Server::AllowList list;
            std::string error;
            CHECK(list.Set(entries, error));
            return list;
        }

        // A clock that only moves when the test says so.
        class ManualClock
        {
        public:
            ManualClock() : m_start(std::chrono::steady_clock::now()) {}
            Server::IdleClock Get()
            {
                return [this]() { return m_start + std::chrono::milliseconds(m_offsetMs.load()); };
            }
            void AdvanceMinutes(int minutes) { m_offsetMs += static_cast<int64_t>(minutes) * 60 * 1000; }

        private:
            std::chrono::steady_clock::time_point m_start;
            std::atomic<int64_t> m_offsetMs{0};
        };

        // The viewer's side of an in-memory connection, enough to log in and read what the server sends.
        class PipeViewer
        {
        public:
            explicit PipeViewer(Wire::Transport& transport) : m_reader(transport), m_transport(transport) {}

            void Send(const Wire::Bytes& framed) { m_transport.Write(framed.data(), framed.size()); }

            bool Login()
            {
                Wire::Hello hello;
                hello.clientName = "rule viewer";
                Send(hello.Encode());
                Wire::Bytes bytes;
                Wire::Reader body(nullptr, 0);
                if (Next(bytes, body) != Wire::Type::Welcome)
                {
                    return false;
                }
                Send(Wire::Login{""}.Encode());
                if (Next(bytes, body) != Wire::Type::LoginResult)
                {
                    return false;
                }
                return Wire::LoginResult::Decode(body).status == Wire::LoginResult::Status::Ok;
            }

            Wire::Type Next(Wire::Bytes& bytesOut, Wire::Reader& readerOut)
            {
                bytesOut = m_reader.Next();
                readerOut = Wire::Reader(bytesOut);
                return Wire::ReadType(readerOut);
            }

            // Sends a Ping and reads (skipping pictures) until its Pong: proves the session is still running.
            bool StillAnswering(uint64_t token)
            {
                Wire::Ping ping;
                ping.token = token;
                Send(ping.Encode());
                try
                {
                    for (;;)
                    {
                        Wire::Bytes bytes;
                        Wire::Reader body(nullptr, 0);
                        Wire::Type type = Next(bytes, body);
                        if (type == Wire::Type::Pong)
                        {
                            return Wire::Pong::Decode(body).token == token;
                        }
                        if (type == Wire::Type::Bye)
                        {
                            return false;
                        }
                    }
                }
                catch (const Wire::WireError&)
                {
                    return false;
                }
            }

            // Reads (skipping pictures) until a Bye and returns its reason; "<none>" if the stream ended first.
            std::string ReadBye()
            {
                try
                {
                    for (;;)
                    {
                        Wire::Bytes bytes;
                        Wire::Reader body(nullptr, 0);
                        if (Next(bytes, body) == Wire::Type::Bye)
                        {
                            return Wire::Bye::Decode(body).reason;
                        }
                    }
                }
                catch (const Wire::WireError&)
                {
                    return "<none>";
                }
            }

        private:
            Wire::MessageReader m_reader;
            Wire::Transport& m_transport;
        };

        // A session on in-memory pipes with a manual clock and a recording notifier, running on its own thread.
        struct IdleRig
        {
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport{clientToServer, serverToClient};
            PipeTransport clientTransport{serverToClient, clientToServer};
            Server::TestPatternScreen screen{4, 3};
            RecordingInput input;
            Server::Settings settings;
            ManualClock clock;
            Server::RecordingNotifier notifier;
            std::unique_ptr<Server::Session> session;
            std::thread thread;
            std::unique_ptr<PipeViewer> viewer;

            explicit IdleRig(int idleMinutes)
            {
                settings.allowNoPassword = true;
                settings.idleMinutes = idleMinutes;
                session = std::make_unique<Server::Session>(serverTransport, screen, input, settings);
                session->SetFrameIntervalMs(1);
                session->SetIdleClock(clock.Get());
                session->SetNotifier(&notifier, "10.1.2.3");
                session->SetEndHandler([this]() { clientToServer.Close(); }); // what the listener does with the socket
                thread = std::thread([this]() { session->Run(); });
                viewer = std::make_unique<PipeViewer>(clientTransport);
            }

            ~IdleRig()
            {
                clientToServer.Close();
                serverToClient.Close();
                if (thread.joinable())
                {
                    thread.join();
                }
            }
        };

        std::vector<uint8_t> FromHex(const std::string& hex)
        {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return 0;
            };
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i + 1 < hex.size(); i += 2)
            {
                bytes.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
            }
            return bytes;
        }

        // Starts a listener on loopback with the given rules. The caller serves one connection.
        struct SocketRig
        {
            Server::TestPatternScreen screen{8, 8};
            RecordingInput input;
            Server::Settings settings;
            std::unique_ptr<Server::Listener> listener;
            Server::RecordingNotifier notifier;
            ManualClock clock;
            uint16_t port = 0;
            std::string fingerprint;

            void Start(const std::vector<std::string>& allowed, int idleMinutes = 0)
            {
                settings.allowNoPassword = true;
                settings.port = 0;
                settings.serverName = "Rules PC";
                settings.allowedAddresses = allowed;
                settings.idleMinutes = idleMinutes;
                listener = std::make_unique<Server::Listener>(screen, input, settings);
                listener->SetFrameIntervalMs(1);
                listener->SetNotifier(&notifier);
                listener->SetIdleClock(clock.Get());
                std::string error;
                CHECK(listener->Start(error, "127.0.0.1"));
                port = listener->GetPort();
                fingerprint = listener->GetCertificateFingerprint();
            }
        };
    } // namespace

    void RunRuleTests()
    {
        // ---- The allow list: reading entries ----

        RunTest("Allow list: readable entries", []() {
            CHECK(Parses("192.168.1.20"));
            CHECK(Parses("192.168.1.0/24"));
            CHECK(Parses("  10.0.0.1  "));
            CHECK(Parses("0.0.0.0/0"));
            CHECK(Parses("255.255.255.255/32"));
            CHECK(Parses("::1"));
            CHECK(Parses("::"));
            CHECK(Parses("fd00::/8"));
            CHECK(Parses("::/0"));
            CHECK(Parses("2001:db8::1/128"));
            CHECK(Parses("::ffff:10.0.0.1"));
            CHECK(Parses("127.0.0.1"));
        });

        RunTest("Allow list: unreadable entries are refused with a plain message", []() {
            const char* bad[] = {"",         "   ",          "abc",        "1.2.3",       "1.2.3.4.5",    "256.1.1.1",
                                 "1.2.3.4/33", "1.2.3.4/",   "/24",        "1.2.3.4/-1",  "1.2.3.4/a",    "::1/129",
                                 "1.2.3.4/24/8", "1.2.3.4 5.6.7.8", "fe80::1%eth0", "localhost", "::g",   "1.2.3.4/+8",
                                 "1.2.3.4/ 8", "1:2:3:4:5:6:7:8:9", "10.0.0.0/1000", "1.2.3.4,5.6.7.8", "*"};
            for (const char* text : bad)
            {
                Server::AddressRule rule;
                std::string error;
                CHECK(!Server::ParseAddressRule(text, rule, error));
                CHECK(!error.empty());
            }
            Server::AddressRule rule;
            std::string error;
            Server::ParseAddressRule("not-an-address", rule, error);
            CHECK(error.find("not-an-address") != std::string::npos); // names the entry
        });

        // ---- The allow list: matching ----

        RunTest("Allow list: an empty list lets everyone in; a list with entries lets in only matches", []() {
            Server::AllowList empty;
            CHECK(empty.IsEmpty());
            CHECK(empty.Allows("8.8.8.8"));
            CHECK(empty.Allows("2001:db8::1"));
            CHECK(empty.Allows("")); // unknown peers too: that is how the server behaved before there was a list
            CHECK(empty.Allows("garbage"));

            Server::AllowList list = ListOf({"192.168.1.20"});
            CHECK(!list.IsEmpty());
            CHECK(list.Allows("192.168.1.20"));
            CHECK(!list.Allows("192.168.1.21"));
            CHECK(!list.Allows("")); // an address that can't be read is never on a list
            CHECK(!list.Allows("garbage"));
        });

        RunTest("Allow list: IPv4 networks, at and around their edges", []() {
            Server::AllowList list = ListOf({"192.168.1.0/24"});
            CHECK(list.Allows("192.168.1.0"));
            CHECK(list.Allows("192.168.1.255"));
            CHECK(!list.Allows("192.168.2.0"));
            CHECK(!list.Allows("192.168.0.255"));
            CHECK(!list.Allows("10.0.0.1"));

            Server::AllowList odd = ListOf({"10.20.16.0/20"}); // not on a byte boundary
            CHECK(odd.Allows("10.20.16.0"));
            CHECK(odd.Allows("10.20.31.255"));
            CHECK(!odd.Allows("10.20.32.0"));
            CHECK(!odd.Allows("10.20.15.255"));

            Server::AllowList half = ListOf({"192.168.1.128/25"});
            CHECK(!half.Allows("192.168.1.127"));
            CHECK(half.Allows("192.168.1.128"));
            CHECK(half.Allows("192.168.1.255"));

            Server::AllowList host = ListOf({"1.2.3.4/32"});
            CHECK(host.Allows("1.2.3.4"));
            CHECK(!host.Allows("1.2.3.5"));

            Server::AllowList all4 = ListOf({"0.0.0.0/0"});
            CHECK(all4.Allows("203.0.113.9"));
            CHECK(!all4.Allows("2001:db8::1")); // every IPv4 address, not IPv6 ones

            Server::AllowList loose = ListOf({"192.168.1.77/24"}); // host bits past the network size are ignored
            CHECK(loose.Allows("192.168.1.1"));
            CHECK(!loose.Allows("192.168.2.1"));
        });

        RunTest("Allow list: IPv6 networks", []() {
            Server::AllowList list = ListOf({"fd00::/8", "2001:db8:1::/48"});
            CHECK(list.Allows("fd12:3456::1"));
            CHECK(!list.Allows("fe80::1"));
            CHECK(list.Allows("2001:db8:1:ffff::1"));
            CHECK(!list.Allows("2001:db8:2::1"));

            Server::AllowList host = ListOf({"2001:db8::5"});
            CHECK(host.Allows("2001:db8::5"));
            CHECK(host.Allows("2001:0db8:0000:0000:0000:0000:0000:0005")); // the same address written out
            CHECK(!host.Allows("2001:db8::6"));

            Server::AllowList all6 = ListOf({"::/0"});
            CHECK(all6.Allows("fe80::1"));
            CHECK(all6.Allows("::1"));
        });

        RunTest("Allow list: IPv4-mapped IPv6 addresses match IPv4 entries and the other way round", []() {
            Server::AllowList net = ListOf({"192.168.1.0/24"});
            CHECK(net.Allows("::ffff:192.168.1.7"));
            CHECK(net.Allows("::ffff:c0a8:0107")); // the same address in hex form
            CHECK(!net.Allows("::ffff:192.168.2.7"));

            Server::AllowList mapped = ListOf({"::ffff:10.0.0.0/104"});
            CHECK(mapped.Allows("10.9.9.9"));
            CHECK(mapped.Allows("::ffff:10.1.1.1"));
            CHECK(!mapped.Allows("11.0.0.1"));
        });

        RunTest("Allow list: 127.0.0.1 and ::1 are different addresses", []() {
            Server::AllowList v4 = ListOf({"127.0.0.1"});
            CHECK(v4.Allows("127.0.0.1"));
            CHECK(v4.Allows("::ffff:127.0.0.1"));
            CHECK(!v4.Allows("::1"));
            CHECK(!v4.Allows("127.0.0.2"));

            Server::AllowList v6 = ListOf({"::1"});
            CHECK(v6.Allows("::1"));
            CHECK(!v6.Allows("127.0.0.1"));

            Server::AllowList both = ListOf({"127.0.0.1", "::1"});
            CHECK(both.Allows("127.0.0.1"));
            CHECK(both.Allows("::1"));
            CHECK(!both.Allows("10.0.0.1"));

            Server::AllowList range = ListOf({"127.0.0.0/8"});
            CHECK(range.Allows("127.5.5.5"));
        });

        RunTest("Allow list: Set keeps the old list when an entry is bad, and skips blanks", []() {
            Server::AllowList list;
            std::string error;
            CHECK(list.Set({"10.0.0.1", "", "  ", "10.0.1.0/24"}, error));
            CHECK(list.Entries().size() == 2);
            CHECK(list.Allows("10.0.1.9"));

            CHECK(!list.Set({"192.168.0.1", "oops"}, error));
            CHECK(error.find("oops") != std::string::npos);
            CHECK(list.Entries().size() == 2); // unchanged
            CHECK(list.Allows("10.0.0.1"));
            CHECK(!list.Allows("192.168.0.1"));

            CHECK(list.Set({}, error)); // clearing the list lets everyone in again
            CHECK(list.IsEmpty());
            CHECK(list.Allows("192.168.0.1"));
        });

        RunTest("Allow list: typed text is split into entries", []() {
            std::vector<std::string> a = Server::SplitAllowEntries("10.0.0.1\n192.168.1.0/24\r\n  ::1 ;fd00::/8,\n\n");
            CHECK(a.size() == 4);
            CHECK(a.size() == 4 && a[0] == "10.0.0.1" && a[1] == "192.168.1.0/24" && a[2] == "::1" &&
                  a[3] == "fd00::/8");
            CHECK(Server::SplitAllowEntries("").empty());
            CHECK(Server::SplitAllowEntries("  \n ,; ").empty());
        });

        // ---- The idle clock ----

        RunTest("Idle watch: expires after the limit, is reset by activity, and 0 means never", []() {
            ManualClock clock;
            Server::IdleWatch watch(clock.Get());
            CHECK(!watch.Expired()); // no limit set yet
            watch.SetLimitMinutes(10);
            CHECK(!watch.Expired());
            clock.AdvanceMinutes(9);
            CHECK(!watch.Expired());
            watch.Touch();
            clock.AdvanceMinutes(9);
            CHECK(!watch.Expired()); // only nine minutes since the touch
            clock.AdvanceMinutes(1);
            CHECK(watch.Expired());
            watch.SetLimitMinutes(0);
            CHECK(!watch.Expired());
            watch.SetLimitMinutes(-5); // nonsense counts as never
            CHECK(!watch.Expired());
            CHECK(watch.GetLimitMinutes() == 0);
        });

        // ---- The idle disconnect in a session ----

        RunTest("Idle disconnect: the viewer is told, then ended, once nothing has been sent for the limit", []() {
            IdleRig rig(5);
            CHECK(rig.viewer->Login());
            CHECK(rig.viewer->StillAnswering(1));
            rig.clock.AdvanceMinutes(4);
            CHECK(rig.viewer->StillAnswering(2)); // a keep-alive doesn't count as being there, but 4 < 5 anyway
            rig.clock.AdvanceMinutes(1);
            std::string reason = rig.viewer->ReadBye();
            CHECK(reason == "Disconnected because nothing was done for 5 minutes.");
            rig.thread.join();

            auto notices = rig.notifier.Notices();
            CHECK(notices.size() == 2);
            CHECK(notices.size() == 2 && notices[0].event == Server::ConnectionEvent::Connected);
            CHECK(notices.size() == 2 && notices[0].viewerName == "rule viewer" && notices[0].address == "10.1.2.3");
            CHECK(notices.size() == 2 && notices[1].event == Server::ConnectionEvent::IdleDisconnected);
        });

        RunTest("Idle disconnect: a keep-alive does not count as activity", []() {
            IdleRig rig(5);
            CHECK(rig.viewer->Login());
            rig.clock.AdvanceMinutes(3);
            CHECK(rig.viewer->StillAnswering(1)); // pings keep the connection up but are not "someone using it"
            rig.clock.AdvanceMinutes(3);
            CHECK(rig.viewer->ReadBye() != "<none>");
            rig.thread.join();
        });

        RunTest("Idle disconnect: input resets the count", []() {
            IdleRig rig(5);
            CHECK(rig.viewer->Login());
            rig.clock.AdvanceMinutes(4);
            Wire::Pointer pointer;
            pointer.x = 1;
            pointer.y = 1;
            rig.viewer->Send(pointer.Encode());
            // Wait until the server has applied it, so the touch has happened before the clock moves on.
            for (int i = 0; i < 500; ++i)
            {
                {
                    std::lock_guard<std::mutex> lock(rig.input.mutex);
                    if (!rig.input.pointers.empty())
                    {
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            rig.clock.AdvanceMinutes(4);
            CHECK(rig.viewer->StillAnswering(1)); // eight minutes since the start, four since the pointer moved
            rig.clock.AdvanceMinutes(1);
            CHECK(rig.viewer->ReadBye() == "Disconnected because nothing was done for 5 minutes.");
            rig.thread.join();
        });

        RunTest("Idle disconnect: 0 means never", []() {
            IdleRig rig(0);
            CHECK(rig.viewer->Login());
            rig.clock.AdvanceMinutes(100000);
            CHECK(rig.viewer->StillAnswering(1));
            rig.clock.AdvanceMinutes(100000);
            CHECK(rig.viewer->StillAnswering(2));
            Wire::Bye bye;
            rig.viewer->Send(bye.Encode());
            rig.thread.join();
            auto notices = rig.notifier.Notices();
            CHECK(notices.size() == 2 && notices[1].event == Server::ConnectionEvent::Disconnected);
        });

        RunTest("Idle disconnect: one minute reads as 'minute', and the limit can change while connected", []() {
            IdleRig rig(0);
            CHECK(rig.viewer->Login());
            rig.session->SetIdleMinutes(1);
            rig.clock.AdvanceMinutes(1);
            CHECK(rig.viewer->ReadBye() == "Disconnected because nothing was done for 1 minute.");
            rig.thread.join();
        });

        RunTest("Idle disconnect: an idle viewer that locks on disconnect locks the screen", []() {
            Pipe up, down;
            PipeTransport serverSide(up, down);
            PipeTransport clientSide(down, up);
            Server::TestPatternScreen screen(4, 3);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true;
            settings.idleMinutes = 1;
            settings.lockOnDisconnect = true;
            ManualClock clock;
            Server::Session session(serverSide, screen, input, settings);
            session.SetFrameIntervalMs(1);
            session.SetIdleClock(clock.Get());
            session.SetEndHandler([&]() { up.Close(); });
            std::thread thread([&]() { session.Run(); });
            PipeViewer viewer(clientSide);
            CHECK(viewer.Login());
            clock.AdvanceMinutes(1);
            CHECK(viewer.ReadBye() != "<none>");
            thread.join();
            CHECK(input.locks == 1);
            up.Close();
            down.Close();
        });

        // ---- Notices ----

        RunTest("Notices: a viewer that is refused at login is never announced", []() {
            Pipe up, down;
            PipeTransport serverSide(up, down);
            PipeTransport clientSide(down, up);
            Server::TestPatternScreen screen(4, 3);
            RecordingInput input;
            Server::Settings settings; // no password, and allowNoPassword off: every login is refused
            Server::Auth auth("");
            Server::RecordingNotifier notifier;
            Server::Session session(serverSide, screen, input, settings, &auth);
            session.SetNotifier(&notifier, "10.1.2.3");
            std::thread thread([&]() { session.Run(); });
            PipeViewer viewer(clientSide);
            CHECK(!viewer.Login());
            up.Close();
            down.Close();
            thread.join();
            CHECK(notifier.Notices().empty());
        });

        RunTest("Notices: the plain-words text", []() {
            Server::ConnectionNotice notice;
            notice.viewerName = "Anna's laptop";
            notice.address = "192.168.1.20";
            notice.event = Server::ConnectionEvent::Connected;
            CHECK(Server::DescribeNotice(notice) == "Anna's laptop (192.168.1.20) connected to this computer.");
            notice.event = Server::ConnectionEvent::Disconnected;
            CHECK(Server::DescribeNotice(notice) == "Anna's laptop (192.168.1.20) disconnected from this computer.");
            notice.event = Server::ConnectionEvent::IdleDisconnected;
            CHECK(Server::DescribeNotice(notice).find("because it had not been used") != std::string::npos);
            notice.viewerName.clear();
            notice.address.clear();
            notice.event = Server::ConnectionEvent::Connected;
            CHECK(Server::DescribeNotice(notice) == "A viewer connected to this computer.");
        });

        // ---- Over a real loopback socket ----

        RunTest("Allow list over a socket: an address outside the list is refused, and told nothing", []() {
            SocketRig rig;
            rig.Start({"10.99.99.0/24"}); // 127.0.0.1 is not in it
            std::thread serverThread([&]() { rig.listener->ServeOne(5000); });

            // A bare connection gets nothing at all, not even the four-byte marker.
            Net::Socket raw;
            std::string error;
            CHECK(raw.Connect("127.0.0.1", rig.port, 2000, error));
            raw.SetReceiveTimeout(3000);
            uint8_t buffer[16];
            ptrdiff_t got = raw.Receive(buffer, sizeof(buffer));
            CHECK(got <= 0); // closed (0), or reset (-1): never any bytes
            raw.Close();
            serverThread.join();

            // A real viewer never connects either.
            std::thread secondServer([&]() { rig.listener->ServeOne(5000); });
            RecordingClientListener clientListener;
            clientListener.trustCertificate = true;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = rig.port;
            options.clientName = "outsider";
            options.trustedCertificateSha256 = FromHex(rig.fingerprint);
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            CHECK(!clientListener.connected);
            CHECK(!client.IsConnected());
            client.Disconnect();
            secondServer.join();
            CHECK(rig.notifier.Notices().empty()); // a refused address is not announced as a viewer
            rig.listener->Stop();
        });

        RunTest("Allow list over a socket: an address on the list connects, with a notice either side", []() {
            SocketRig rig;
            rig.Start({"10.99.99.1", "127.0.0.0/8"});
            std::thread serverThread([&]() { rig.listener->ServeOne(5000); });

            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = rig.port;
            options.clientName = "welcome viewer";
            options.trustedCertificateSha256 = FromHex(rig.fingerprint);
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(clientListener.WaitFor([&]() { return clientListener.updates >= 1; }));
            CHECK(rig.notifier.Notices().size() == 1);

            client.Disconnect();
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            serverThread.join();

            auto notices = rig.notifier.Notices();
            CHECK(notices.size() == 2);
            CHECK(notices.size() == 2 && notices[0].event == Server::ConnectionEvent::Connected);
            CHECK(notices.size() == 2 && notices[0].viewerName == "welcome viewer");
            CHECK(notices.size() == 2 && notices[0].address == "127.0.0.1");
            CHECK(notices.size() == 2 && notices[1].event == Server::ConnectionEvent::Disconnected);
            rig.listener->Stop();
        });

        RunTest("Allow list over a socket: an empty list lets anyone in (unchanged behaviour)", []() {
            SocketRig rig;
            rig.Start({});
            std::thread serverThread([&]() { rig.listener->ServeOne(5000); });
            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = rig.port;
            options.clientName = "anyone";
            options.trustedCertificateSha256 = FromHex(rig.fingerprint);
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            client.Disconnect();
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            serverThread.join();
            rig.listener->Stop();
        });

        RunTest("Allow list over a socket: the list can be changed while the server runs", []() {
            SocketRig rig;
            rig.Start({"127.0.0.1"});
            std::string error;
            CHECK(!rig.listener->SetAllowedAddresses({"nonsense"}, error));
            CHECK(!error.empty());
            CHECK(rig.listener->SetAllowedAddresses({"10.99.99.1"}, error)); // now excludes loopback
            std::thread serverThread([&]() { rig.listener->ServeOne(5000); });
            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = rig.port;
            options.clientName = "late";
            options.trustedCertificateSha256 = FromHex(rig.fingerprint);
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            CHECK(!clientListener.connected);
            client.Disconnect();
            serverThread.join();
            rig.listener->Stop();
        });

        RunTest("Allow list over a socket: a list that can't be read stops the server starting", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.port = 0;
            settings.allowedAddresses = {"127.0.0.1", "banana"};
            Server::Listener listener(screen, input, settings);
            std::string error;
            CHECK(!listener.Start(error, "127.0.0.1"));
            CHECK(error.find("banana") != std::string::npos);
        });

        RunTest("Idle disconnect over a socket: the viewer is told why it was disconnected", []() {
            SocketRig rig;
            rig.Start({}, 10);
            std::thread serverThread([&]() { rig.listener->ServeOne(5000); });
            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = rig.port;
            options.clientName = "sleepy viewer";
            options.trustedCertificateSha256 = FromHex(rig.fingerprint);
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(clientListener.WaitFor([&]() { return clientListener.updates >= 1; }));
            rig.clock.AdvanceMinutes(10);
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            {
                std::lock_guard<std::mutex> lock(clientListener.mutex);
                CHECK(clientListener.disconnectError == "Disconnected because nothing was done for 10 minutes.");
            }
            serverThread.join(); // the listener's session ended by itself, no Stop needed
            auto notices = rig.notifier.Notices();
            CHECK(notices.size() == 2 && notices[1].event == Server::ConnectionEvent::IdleDisconnected);
            rig.listener->Stop();
        });
    }
} // namespace MarconiServerTest
