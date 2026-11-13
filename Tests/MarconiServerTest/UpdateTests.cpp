// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Updating this server from a newer viewer: the decision table, the signature rules, the checks on the file that
// arrived, the update service with stand-in inspector, launcher and approver, and the real client against the real
// server over an encrypted loopback connection with a stand-in installer file

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiCore/Wire/WireUpdateMessages.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/TestPatternScreen.h>
#include <MarconiServerCore/UpdateHost.h>
#include <MarconiServerCore/UpdatePolicy.h>
#include <MarconiServerCore/UpdateService.h>

#include <Crypto/Crypto.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;
    namespace fs = std::filesystem;

    namespace
    {
        using Answer = Wire::UpdateAnswer::Status;
        using Result = Wire::UpdateResult::Status;

        constexpr const char* kRunning = "0.9.0.10";
        constexpr const char* kNewer = "0.10.0.20";

        class TempDir
        {
        public:
            TempDir()
            {
                static int counter = 0;
                m_path = fs::temp_directory_path() /
                         ("marconi-update-test-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                          std::to_string(++counter));
                fs::create_directories(m_path);
            }
            ~TempDir()
            {
                std::error_code code;
                fs::remove_all(m_path, code);
            }
            std::string Sub(const std::string& name) const { return (m_path / name).string(); }

        private:
            fs::path m_path;
        };

        bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return predicate();
        }

        Wire::Bytes Sha(const Wire::Bytes& data)
        {
            Wire::Bytes out(32);
            Crypto::Sha256(data.data(), data.size(), out.data());
            return out;
        }

        Wire::Bytes Content(size_t size)
        {
            Wire::Bytes data(size);
            for (size_t i = 0; i < size; ++i)
            {
                data[i] = static_cast<uint8_t>((i * 31) ^ (i >> 7));
            }
            return data;
        }

        std::string ReadAll(const std::string& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        Server::InstallerFacts Facts(Server::SignatureState state, const std::string& signer, const std::string& version)
        {
            Server::InstallerFacts facts;
            facts.signature = state;
            facts.signer = signer;
            facts.product = Wire::kServerProductName;
            facts.version = version;
            return facts;
        }

        class FakeInspector : public Server::InstallerInspector
        {
        public:
            Server::InstallerFacts Inspect(const std::string& path) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                inspected = path;
                return candidate;
            }
            Server::InstallerFacts InspectRunningServer() override
            {
                std::lock_guard<std::mutex> lock(mutex);
                return running;
            }
            std::mutex mutex;
            Server::InstallerFacts running = Facts(Server::SignatureState::Missing, "", kRunning);
            Server::InstallerFacts candidate = Facts(Server::SignatureState::Missing, "", kNewer);
            std::string inspected;
        };

        class FakeLauncher : public Server::InstallerLauncher
        {
        public:
            bool Launch(const std::string& installerPath, const std::vector<std::string>& arguments,
                        std::string& error) override
            {
                while (block) // stands for Windows' permission prompt waiting for an answer
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                std::lock_guard<std::mutex> lock(mutex);
                ++calls;
                path = installerPath;
                args = arguments;
                contentAtLaunch = ReadAll(installerPath);
                if (!succeed)
                {
                    error = "the person said no";
                }
                return succeed;
            }
            std::mutex mutex;
            int calls = 0;
            std::string path;
            std::vector<std::string> args;
            std::string contentAtLaunch;
            bool succeed = true;
            std::atomic<bool> block{false};
        };

        class FakeApprover : public Server::UpdateApprover
        {
        public:
            enum class Mode
            {
                Yes,   // answers yes at once
                No,    // answers no at once
                Later, // keeps the answer for the test to give
                Never, // never answers
            };
            explicit FakeApprover(Mode modeIn) : mode(modeIn) {}
            void Ask(const Server::UpdateQuestion& question, std::function<void(bool)> answer) override
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    questions.push_back(question);
                    pending = answer;
                }
                if (mode == Mode::Yes || mode == Mode::No)
                {
                    answer(mode == Mode::Yes);
                }
            }
            void Withdraw() override
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++withdrawn;
            }
            void Give(bool allowed)
            {
                std::function<void(bool)> answer;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    answer = pending;
                }
                answer(allowed);
            }
            size_t QuestionCount()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return questions.size();
            }
            Mode mode;
            std::mutex mutex;
            std::vector<Server::UpdateQuestion> questions;
            std::function<void(bool)> pending;
            int withdrawn = 0;
        };

        // The update service without a network: messages go in, what it sends is collected.
        struct Direct
        {
            explicit Direct(Server::UpdatePermission permission = Server::UpdatePermission::Allow,
                            FakeApprover::Mode approverMode = FakeApprover::Mode::Yes)
                : approver(approverMode)
            {
                environment.stagingFolder = dir.Sub("updates");
                environment.inspector = &inspector;
                environment.launcher = &launcher;
                environment.approver = &approver;
                environment.onInstallerStarted = [this]() { ++started; };
                settings.runningVersion = kRunning;
                settings.permission = permission;
                settings.allowUnsigned = true;
                Make();
            }
            void Make()
            {
                service = std::make_unique<Server::UpdateService>(
                    &environment, settings, Server::UpdateViewer{"Anna's laptop", "0.10.0.20", "192.168.1.20"},
                    [this](const Wire::Bytes& framed) {
                        std::lock_guard<std::mutex> lock(mutex);
                        sent.push_back(framed);
                        return true;
                    },
                    [this]() { ++ended; });
            }
            void Feed(const Wire::Bytes& framed)
            {
                Wire::Reader outer(framed);
                uint64_t length = outer.Varint();
                Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
                Wire::Type type = Wire::ReadType(body);
                CHECK(service->Handle(type, body));
            }
            // The types and statuses sent so far, as "A3 A0 R0 B" (answer 3, answer 0, result 0, bye).
            std::string Log()
            {
                std::lock_guard<std::mutex> lock(mutex);
                std::string log;
                for (const Wire::Bytes& framed : sent)
                {
                    Wire::Reader outer(framed);
                    uint64_t length = outer.Varint();
                    Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()),
                                      static_cast<size_t>(length));
                    Wire::Type type = Wire::ReadType(body);
                    if (!log.empty())
                    {
                        log += ' ';
                    }
                    if (type == Wire::Type::UpdateAnswer)
                    {
                        log += "A" + std::to_string(static_cast<int>(Wire::UpdateAnswer::Decode(body).status));
                    }
                    else if (type == Wire::Type::UpdateResult)
                    {
                        log += "R" + std::to_string(static_cast<int>(Wire::UpdateResult::Decode(body).status));
                    }
                    else if (type == Wire::Type::Bye)
                    {
                        log += "B";
                    }
                    else
                    {
                        log += "?";
                    }
                }
                return log;
            }
            void Offer(const Wire::Bytes& file, const std::string& version = kNewer)
            {
                Wire::UpdateOffer offer;
                offer.version = version;
                offer.size = file.size();
                offer.sha256 = Sha(file);
                Feed(offer.Encode());
            }
            void SendFile(const Wire::Bytes& file)
            {
                for (size_t at = 0; at < file.size(); at += Wire::kUpdateChunkBytes)
                {
                    Wire::UpdateChunk chunk;
                    chunk.offset = at;
                    size_t count = std::min(Wire::kUpdateChunkBytes, file.size() - at);
                    chunk.data.assign(file.begin() + static_cast<ptrdiff_t>(at),
                                      file.begin() + static_cast<ptrdiff_t>(at + count));
                    Feed(chunk.Encode());
                }
            }
            // The installer is started on a thread of its own: wait until the log is what is expected.
            bool LogBecomes(const std::string& expected) { return WaitUntil([&]() { return Log() == expected; }); }
            std::string Staged() const
            {
                return (fs::path(environment.stagingFolder) / Server::UpdateService::kStagedFileName).string();
            }

            TempDir dir;
            FakeInspector inspector;
            FakeLauncher launcher;
            FakeApprover approver;
            Server::UpdateEnvironment environment;
            Server::UpdateSettings settings;
            std::unique_ptr<Server::UpdateService> service;
            std::mutex mutex;
            std::vector<Wire::Bytes> sent;
            std::atomic<int> ended{0};
            std::atomic<int> started{0};
        };

        std::string A(Answer status)
        {
            return "A" + std::to_string(static_cast<int>(status));
        }
        std::string R(Result status)
        {
            return "R" + std::to_string(static_cast<int>(status));
        }

        // A real server with the update environment, and the real client, over an encrypted loopback connection.
        class Rig
        {
        public:
            Rig(Server::UpdatePermission permission, FakeApprover::Mode approverMode, bool withLauncher = true)
                : m_screen(8, 8), approver(approverMode),
                  m_listener(m_screen, input, MakeSettings(permission)), m_client(clientListener)
            {
                environment.stagingFolder = dir.Sub("updates");
                environment.inspector = &inspector;
                environment.launcher = withLauncher ? &launcher : nullptr;
                environment.approver = &approver;
                environment.onInstallerStarted = [this]() { ++started; };
                m_listener.SetUpdateEnvironment(&environment);
                m_listener.SetFrameIntervalMs(50);
                std::string error;
                m_started = m_listener.Start(error, "127.0.0.1");
                m_serverThread = std::thread([this]() { m_listener.ServeOne(10000); });
                Wire::ClientOptions options;
                options.host = "127.0.0.1";
                options.port = m_listener.GetPort();
                options.clientName = "update test viewer";
                m_client.Connect(options);
                m_connected = clientListener.WaitFor([&]() { return clientListener.connected; }, 10000);
            }
            ~Rig()
            {
                m_client.Disconnect();
                clientListener.WaitFor([&]() { return clientListener.disconnected; }, 5000);
                m_listener.Stop();
                m_serverThread.join();
            }
            bool Ready() const { return m_started && m_connected; }
            Wire::Client& Client() { return m_client; }
            Wire::UpdateUploader& Uploader() { return m_client.GetServerUpdate(); }
            Wire::UpdateUploader::Stage Stage() { return Uploader().GetProgress().stage; }
            // Writes a stand-in installer with this content and returns its path.
            std::string Installer(const Wire::Bytes& content)
            {
                std::string path = dir.Sub(std::string("MarconiServer-") + kNewer + "-win64-setup.exe");
                std::ofstream out(path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
                return path;
            }

            TempDir dir;
            Server::TestPatternScreen m_screen;
            RecordingInput input;
            FakeInspector inspector;
            FakeLauncher launcher;
            FakeApprover approver;
            Server::UpdateEnvironment environment;
            std::atomic<int> started{0};
            RecordingClientListener clientListener;

        private:
            static Server::Settings MakeSettings(Server::UpdatePermission permission)
            {
                Server::Settings settings;
                settings.port = 0;
                settings.allowNoPassword = true;
                settings.serverName = "Update Test PC";
                settings.serverVersion = kRunning;
                settings.updatePermission = permission;
                settings.allowUnsignedUpdates = true;
                return settings;
            }

            Server::Listener m_listener;
            Wire::Client m_client;
            std::thread m_serverThread;
            bool m_started = false;
            bool m_connected = false;
        };
    } // namespace

    void RunUpdateTests()
    {
        RunTest("update decision: product, system, version, setting, signature, size, room and busy", []() {
            Wire::UpdateOffer offer;
            offer.version = kNewer;
            offer.size = 1000;
            offer.sha256.assign(32, 1);
            Server::OfferContext ok;
            ok.runningVersion = kRunning;
            ok.supported = true;
            ok.canAskPerson = true;
            ok.runningSigned = true;
            ok.permission = Server::UpdatePermission::Allow;
            CHECK(Server::DecideOffer(offer, ok) == Answer::SendIt);
            Server::OfferContext ask = ok;
            ask.permission = Server::UpdatePermission::Ask;
            CHECK(Server::DecideOffer(offer, ask) == Answer::AskingPerson);
            Server::OfferContext nobody = ask;
            nobody.canAskPerson = false;
            CHECK(Server::DecideOffer(offer, nobody) == Answer::NotSupportedHere);
            Server::OfferContext off = ok;
            off.permission = Server::UpdatePermission::Off;
            CHECK(Server::DecideOffer(offer, off) == Answer::TurnedOff);
            Server::OfferContext linux = ok;
            linux.supported = false;
            CHECK(Server::DecideOffer(offer, linux) == Answer::NotSupportedHere);
            Server::OfferContext unsignedServer = ok;
            unsignedServer.runningSigned = false;
            CHECK(Server::DecideOffer(offer, unsignedServer) == Answer::CannotVerify);
            unsignedServer.allowUnsigned = true;
            CHECK(Server::DecideOffer(offer, unsignedServer) == Answer::SendIt);
            Server::OfferContext busy = ok;
            busy.busy = true;
            CHECK(Server::DecideOffer(offer, busy) == Answer::Busy);
            Server::OfferContext full = ok;
            full.freeBytes = 1000 + Server::kSpareBytes - 1;
            CHECK(Server::DecideOffer(offer, full) == Answer::NoRoom);
            full.freeBytes = 1000 + Server::kSpareBytes;
            CHECK(Server::DecideOffer(offer, full) == Answer::SendIt);

            Wire::UpdateOffer older = offer;
            older.version = "0.8.9.999";
            CHECK(Server::DecideOffer(older, ok) == Answer::NotNewer); // no downgrades
            Wire::UpdateOffer same = offer;
            same.version = kRunning;
            CHECK(Server::DecideOffer(same, ok) == Answer::NotNewer); // no reinstalls either
            Wire::UpdateOffer other = offer;
            other.product = "Marconi";
            CHECK(Server::DecideOffer(other, ok) == Answer::WrongProduct);
            Wire::UpdateOffer mac = offer;
            mac.platform = "macos-arm64";
            CHECK(Server::DecideOffer(mac, ok) == Answer::WrongProduct);
            Wire::UpdateOffer garbled = offer;
            garbled.version = "soon";
            CHECK(Server::DecideOffer(garbled, ok) == Answer::BadOffer);
            Wire::UpdateOffer noHash = offer;
            noHash.sha256.clear();
            CHECK(Server::DecideOffer(noHash, ok) == Answer::BadOffer);
            Wire::UpdateOffer huge = offer;
            huge.size = Wire::kMaxUpdateBytes + 1;
            CHECK(Server::DecideOffer(huge, ok) == Answer::TooLarge);

            Wire::UpdateOffer check;
            check.flags = Wire::UpdateOffer::kCheckOnly;
            check.version = kNewer;
            CHECK(Server::DecideOffer(check, ok) == Answer::WouldAccept);
            CHECK(Server::DecideOffer(check, ask) == Answer::NeedsApproval);
            CHECK(Server::DecideOffer(check, off) == Answer::TurnedOff);
            CHECK(Server::DecideOffer(check, busy) == Answer::WouldAccept); // a check doesn't care who is busy

            CHECK(Server::ParseUpdatePermission("off") == Server::UpdatePermission::Off);
            CHECK(Server::ParseUpdatePermission("allow") == Server::UpdatePermission::Allow);
            CHECK(Server::ParseUpdatePermission("") == Server::UpdatePermission::Ask);
            CHECK(Server::ParseUpdatePermission("nonsense") == Server::UpdatePermission::Ask);
            CHECK(std::string(Server::UpdatePermissionName(Server::UpdatePermission::Allow)) == "allow");
        });

        RunTest("signature rules: same signer when signed, our publisher or 'allow unsigned' when not", []() {
            using S = Server::SignatureState;
            const std::string us = Server::kExpectedPublisher;
            std::string why;
            auto ok = [&](S running, const std::string& runningSigner, S candidate, const std::string& signer,
                          bool allowUnsigned) {
                return Server::SignatureAcceptable(Facts(running, runningSigner, kRunning),
                                                   Facts(candidate, signer, kNewer), allowUnsigned, why);
            };
            CHECK(ok(S::Valid, us, S::Valid, us, false));
            CHECK(!ok(S::Valid, us, S::Valid, "Someone Else Ltd", false));
            CHECK(!ok(S::Valid, us, S::Missing, "", true)); // a signed server never takes an unsigned update
            CHECK(!ok(S::Valid, us, S::Unavailable, "", true));
            CHECK(!ok(S::Valid, us, S::Invalid, us, true));
            CHECK(ok(S::Missing, "", S::Valid, us, false));
            CHECK(!ok(S::Missing, "", S::Valid, "Someone Else Ltd", true));
            CHECK(!ok(S::Missing, "", S::Missing, "", false));
            CHECK(ok(S::Missing, "", S::Missing, "", true));
            CHECK(!ok(S::Missing, "", S::Invalid, "", true)); // a broken signature is always refused
            CHECK(ok(S::Unavailable, "", S::Unavailable, "", true));
        });

        RunTest("the arrived file is checked for size, hash, product, version, downgrade and signature", []() {
            Wire::Bytes file = Content(5000);
            Wire::UpdateOffer offer;
            offer.version = kNewer;
            offer.size = file.size();
            offer.sha256 = Sha(file);
            const Server::InstallerFacts running = Facts(Server::SignatureState::Missing, "", kRunning);
            const Server::InstallerFacts good = Facts(Server::SignatureState::Missing, "", kNewer);
            std::string detail;
            auto check = [&](uint64_t size, const Wire::Bytes& sha, const Server::InstallerFacts& candidate,
                             bool allowUnsigned) {
                return Server::CheckArrivedInstaller(offer, size, sha, running, candidate, kRunning, allowUnsigned,
                                                     detail);
            };
            CHECK(check(file.size(), Sha(file), good, true) == Result::Installing);
            CHECK(check(file.size() - 1, Sha(file), good, true) == Result::BadUpload);
            Wire::Bytes changed = file;
            changed[100] ^= 1;
            CHECK(check(file.size(), Sha(changed), good, true) == Result::HashMismatch);
            Server::InstallerFacts client = good;
            client.product = "Marconi";
            CHECK(check(file.size(), Sha(file), client, true) == Result::VersionMismatch);
            // The offer said "newer" but the file itself is an older genuine installer: refused.
            Server::InstallerFacts liar = good;
            liar.version = "0.8.0.1";
            CHECK(check(file.size(), Sha(file), liar, true) == Result::VersionMismatch);
            Wire::UpdateOffer oldOffer = offer;
            oldOffer.version = "0.8.0.1";
            CHECK(Server::CheckArrivedInstaller(oldOffer, file.size(), Sha(file), running, liar, kRunning, true,
                                                detail) == Result::NotNewer);
            CHECK(check(file.size(), Sha(file), good, false) == Result::SignatureRefused);
        });

        RunTest("the file's SHA-256 is worked out from the disk", []() {
            TempDir dir;
            Wire::Bytes data = Content(200000);
            std::string path = dir.Sub("file.bin");
            {
                std::ofstream out(path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            }
            Wire::Bytes sha;
            uint64_t size = 0;
            CHECK(Server::HashFile(path, sha, size));
            CHECK(size == data.size() && sha == Sha(data));
            CHECK(!Server::HashFile(dir.Sub("missing.bin"), sha, size));
        });

        RunTest("allowed update: saved in the server's folder, checked, started with /S /restart, viewer told", []() {
            Direct d(Server::UpdatePermission::Allow);
            Wire::Bytes file = Content(Wire::kUpdateChunkBytes * 3 + 17);
            d.Offer(file);
            CHECK(d.Log() == A(Answer::SendIt));
            CHECK(!d.service->IsHoldingInput());
            d.SendFile(file);
            CHECK(d.LogBecomes(A(Answer::SendIt) + " " + R(Result::Installing) + " B"));
            CHECK(d.launcher.calls == 1);
            CHECK(d.launcher.path == d.Staged());
            CHECK(d.launcher.args == std::vector<std::string>({"/S", "/restart"}));
            CHECK(d.launcher.contentAtLaunch == std::string(file.begin(), file.end()));
            CHECK(d.inspector.inspected == d.Staged());
            CHECK(WaitUntil([&]() { return d.ended == 1 && d.started == 1; }));
            CHECK(d.service->HasStartedInstaller());
            d.Offer(file); // a second offer after starting the installer is refused as busy
            CHECK(d.Log().substr(d.Log().size() - 3) == A(Answer::Busy));
        });

        RunTest("ask: the viewer's input is held until the person here says yes; no turns it down", []() {
            Wire::Bytes file = Content(1000);
            {
                Direct d(Server::UpdatePermission::Ask, FakeApprover::Mode::Later);
                d.Offer(file);
                CHECK(d.Log() == A(Answer::AskingPerson));
                CHECK(d.service->IsHoldingInput());
                CHECK(d.approver.QuestionCount() == 1);
                CHECK(d.approver.questions[0].viewerName == "Anna's laptop");
                CHECK(d.approver.questions[0].currentVersion == kRunning && d.approver.questions[0].newVersion == kNewer);
                d.SendFile(file); // pieces before the yes are ignored
                CHECK(d.launcher.calls == 0);
                d.approver.Give(true);
                CHECK(!d.service->IsHoldingInput());
                CHECK(d.Log() == A(Answer::AskingPerson) + " " + A(Answer::SendIt));
                d.SendFile(file);
                CHECK(d.LogBecomes(A(Answer::AskingPerson) + " " + A(Answer::SendIt) + " " + R(Result::Installing) +
                                   " B"));
                CHECK(d.launcher.calls == 1);
            } // its started installer keeps the one-at-a-time mark until it goes

            Direct no(Server::UpdatePermission::Ask, FakeApprover::Mode::No);
            no.Offer(file);
            CHECK(no.Log() == A(Answer::AskingPerson) + " " + A(Answer::Declined));
            CHECK(!no.service->IsHoldingInput());
            no.SendFile(file);
            CHECK(no.launcher.calls == 0);
            no.Offer(file); // asked again: a new question (the earlier no isn't remembered)
            CHECK(no.approver.QuestionCount() == 2);
        });

        RunTest("ask: nobody answers in time, so it counts as no and the question is withdrawn", []() {
            Direct d(Server::UpdatePermission::Ask, FakeApprover::Mode::Never);
            d.environment.approvalTimeoutMs = 100;
            d.Make();
            d.Offer(Content(1000));
            CHECK(d.service->IsHoldingInput());
            CHECK(WaitUntil([&]() { return d.Log() == A(Answer::AskingPerson) + " " + A(Answer::Declined); }));
            CHECK(!d.service->IsHoldingInput());
            CHECK(WaitUntil([&]() {
                std::lock_guard<std::mutex> lock(d.approver.mutex);
                return d.approver.withdrawn == 1;
            }));
            d.approver.Give(true); // a late yes changes nothing
            CHECK(d.Log() == A(Answer::AskingPerson) + " " + A(Answer::Declined));
        });

        RunTest("a viewer that leaves mid-question or mid-upload leaves nothing behind", []() {
            Direct asking(Server::UpdatePermission::Ask, FakeApprover::Mode::Later);
            asking.Offer(Content(1000));
            asking.service.reset();
            {
                std::lock_guard<std::mutex> lock(asking.approver.mutex);
                CHECK(asking.approver.withdrawn == 1);
            }
            asking.approver.Give(true); // the person says yes after the viewer left: nothing happens
            CHECK(asking.Log() == A(Answer::AskingPerson));

            Direct sending(Server::UpdatePermission::Allow);
            Wire::Bytes file = Content(Wire::kUpdateChunkBytes * 2);
            sending.Offer(file);
            Wire::UpdateChunk first;
            first.offset = 0;
            first.data.assign(file.begin(), file.begin() + Wire::kUpdateChunkBytes);
            sending.Feed(first.Encode());
            CHECK(fs::exists(sending.Staged()));
            sending.service.reset();
            CHECK(!fs::exists(sending.Staged()));
            // The one-at-a-time mark went with it: a new viewer can update.
            sending.Make();
            sending.Offer(file);
            CHECK(sending.Log().substr(sending.Log().size() - 2) == A(Answer::SendIt));
        });

        RunTest("refusals: out of order, past the size, wrong hash, older file, bad signature, launch refused", []() {
            Wire::Bytes file = Content(Wire::kUpdateChunkBytes + 10);
            {
                Direct d;
                d.Offer(file);
                Wire::UpdateChunk skip;
                skip.offset = 5;
                skip.data = {1, 2};
                d.Feed(skip.Encode());
                CHECK(d.Log() == A(Answer::SendIt) + " " + R(Result::BadUpload));
                CHECK(!fs::exists(d.Staged()));
            }
            {
                Direct d;
                Wire::UpdateOffer offer;
                offer.version = kNewer;
                offer.size = 10;
                offer.sha256 = Sha(Content(10));
                d.Feed(offer.Encode());
                Wire::UpdateChunk tooMuch;
                tooMuch.data = Content(11);
                d.Feed(tooMuch.Encode());
                CHECK(d.Log() == A(Answer::SendIt) + " " + R(Result::BadUpload));
            }
            {
                Direct d;
                Wire::UpdateOffer offer;
                offer.version = kNewer;
                offer.size = file.size();
                offer.sha256 = Sha(Content(5)); // not this file's
                d.Feed(offer.Encode());
                d.SendFile(file);
                CHECK(d.Log() == A(Answer::SendIt) + " " + R(Result::HashMismatch));
                CHECK(d.launcher.calls == 0 && !fs::exists(d.Staged()));
            }
            {
                Direct d;
                d.inspector.candidate.version = "0.8.0.0"; // an older genuine installer, offered as newer
                d.Offer(file);
                d.SendFile(file);
                CHECK(d.Log() == A(Answer::SendIt) + " " + R(Result::VersionMismatch));
                CHECK(d.launcher.calls == 0);
            }
            {
                Direct d;
                d.settings.allowUnsigned = false;
                d.inspector.running = Facts(Server::SignatureState::Valid, Server::kExpectedPublisher, kRunning);
                d.inspector.candidate = Facts(Server::SignatureState::Valid, "Someone Else Ltd", kNewer);
                d.Make();
                d.Offer(file);
                d.SendFile(file);
                CHECK(d.Log() == A(Answer::SendIt) + " " + R(Result::SignatureRefused));
                CHECK(d.launcher.calls == 0);
            }
            {
                Direct d;
                d.settings.allowUnsigned = false;
                d.inspector.running = Facts(Server::SignatureState::Valid, Server::kExpectedPublisher, kRunning);
                d.inspector.candidate = Facts(Server::SignatureState::Valid, Server::kExpectedPublisher, kNewer);
                d.Make();
                d.Offer(file);
                d.SendFile(file);
                CHECK(d.LogBecomes(A(Answer::SendIt) + " " + R(Result::Installing) + " B")); // signed by the same
            }
            {
                Direct d;
                d.launcher.succeed = false;
                d.Offer(file);
                d.SendFile(file);
                CHECK(d.LogBecomes(A(Answer::SendIt) + " " + R(Result::LaunchFailed)));
                CHECK(d.ended == 0 && d.started == 0);
            }
            {
                Direct d;
                d.environment.freeSpace = [](const std::string&) { return uint64_t(1000); };
                d.Offer(file);
                CHECK(d.Log() == A(Answer::NoRoom));
            }
            {
                Direct d;
                d.settings.allowUnsigned = false; // this server isn't signed and won't take unsigned updates
                d.Make();
                d.Offer(file);
                CHECK(d.Log() == A(Answer::CannotVerify));
            }
        });

        RunTest("while the installer is being started the viewer's messages are still read", []() {
            Direct d;
            d.launcher.block = true;
            Wire::Bytes file = Content(1000);
            d.Offer(file);
            d.SendFile(file);
            d.Offer(file); // answered at once although the start is still waiting
            CHECK(d.Log() == A(Answer::SendIt) + " " + A(Answer::Busy));
            d.launcher.block = false;
            CHECK(d.LogBecomes(A(Answer::SendIt) + " " + A(Answer::Busy) + " " + R(Result::Installing) + " B"));
        });

        RunTest("one update at a time across viewers, and a server that can't update itself says so", []() {
            Wire::Bytes file = Content(1000);
            Direct first;
            first.Offer(file);
            Direct second;
            second.Offer(file);
            CHECK(second.Log() == A(Answer::Busy));

            std::vector<Wire::Bytes> sent;
            Server::UpdateService none(nullptr, Server::UpdateSettings{kRunning, Server::UpdatePermission::Allow, true},
                                       Server::UpdateViewer{}, [&](const Wire::Bytes& framed) {
                                           sent.push_back(framed);
                                           return true;
                                       },
                                       nullptr);
            Wire::UpdateOffer offer;
            offer.flags = Wire::UpdateOffer::kCheckOnly;
            offer.version = kNewer;
            Wire::Bytes framed = offer.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            Wire::ReadType(body);
            CHECK(none.Handle(Wire::Type::UpdateOffer, body));
            CHECK(sent.size() == 1);
            Wire::Reader answerOuter(sent[0]);
            uint64_t answerLength = answerOuter.Varint();
            Wire::Reader answerBody(sent[0].data() + (sent[0].size() - answerOuter.Remaining()),
                                    static_cast<size_t>(answerLength));
            Wire::ReadType(answerBody);
            Wire::UpdateAnswer answer = Wire::UpdateAnswer::Decode(answerBody);
            CHECK(answer.status == Answer::NotSupportedHere && answer.serverVersion == kRunning);
        });

        RunTest("loopback: the real client updates the real server, which starts the installer and says goodbye",
                []() {
                    Rig rig(Server::UpdatePermission::Allow, FakeApprover::Mode::Yes);
                    CHECK(rig.Ready());
                    CHECK(rig.Client().SupportsServerUpdate());
                    CHECK(rig.Client().GetServerVersion() == kRunning);
                    CHECK(rig.Uploader().Check(kNewer));
                    CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::CheckAnswered; }));
                    CHECK(rig.Uploader().GetProgress().answer == Answer::WouldAccept);

                    Wire::Bytes file = Content(Wire::kUpdateChunkBytes * 4 + 321);
                    std::string error;
                    CHECK(rig.Uploader().Start(rig.Installer(file), kNewer, error));
                    CHECK(WaitUntil([&]() { return rig.clientListener.disconnected; }, 10000));
                    CHECK(rig.Stage() == Wire::UpdateUploader::Stage::Installing);
                    CHECK(rig.Uploader().IsInstalling());
                    {
                        std::lock_guard<std::mutex> lock(rig.clientListener.mutex);
                        CHECK(rig.clientListener.disconnectServerUpdating);
                        CHECK(rig.clientListener.disconnectError.find("updating") != std::string::npos);
                    }
                    std::lock_guard<std::mutex> lock(rig.launcher.mutex);
                    CHECK(rig.launcher.calls == 1);
                    CHECK(rig.launcher.args == std::vector<std::string>({"/S", "/restart"}));
                    CHECK(rig.launcher.contentAtLaunch == std::string(file.begin(), file.end()));
                    CHECK(fs::path(rig.launcher.path).parent_path() == fs::path(rig.environment.stagingFolder));
                    CHECK(rig.started == 1);
                });

        RunTest("loopback: a wrong hash and an oversize offer from the real client are refused", []() {
            {
                Rig rig(Server::UpdatePermission::Allow, FakeApprover::Mode::Yes);
                CHECK(rig.Ready());
                rig.Uploader().SetOfferHookForTests([](Wire::UpdateOffer& offer) { offer.sha256[0] ^= 0xFF; });
                std::string error;
                CHECK(rig.Uploader().Start(rig.Installer(Content(70000)), kNewer, error));
                CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::Failed; }));
                CHECK(rig.Uploader().GetProgress().result == Result::HashMismatch);
                CHECK(rig.launcher.calls == 0);
                CHECK(rig.Client().IsConnected()); // a refusal doesn't end the connection
            }
            {
                Rig rig(Server::UpdatePermission::Allow, FakeApprover::Mode::Yes);
                CHECK(rig.Ready());
                rig.Uploader().SetOfferHookForTests(
                    [](Wire::UpdateOffer& offer) { offer.size = Wire::kMaxUpdateBytes + 1; });
                std::string error;
                CHECK(rig.Uploader().Start(rig.Installer(Content(1000)), kNewer, error));
                CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::Refused; }));
                CHECK(rig.Uploader().GetProgress().answer == Answer::TooLarge);
            }
        });

        RunTest("loopback: downgrade, setting off, no launcher, and a no from the person here are refused", []() {
            Wire::Bytes file = Content(1000);
            std::string error;
            {
                Rig rig(Server::UpdatePermission::Allow, FakeApprover::Mode::Yes);
                CHECK(rig.Ready());
                CHECK(rig.Uploader().Start(rig.Installer(file), "0.8.0.0", error));
                CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::Refused; }));
                CHECK(rig.Uploader().GetProgress().answer == Answer::NotNewer);
            }
            {
                Rig rig(Server::UpdatePermission::Off, FakeApprover::Mode::Yes);
                CHECK(rig.Ready());
                CHECK(rig.Uploader().Start(rig.Installer(file), kNewer, error));
                CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::Refused; }));
                CHECK(rig.Uploader().GetProgress().answer == Answer::TurnedOff);
                CHECK(rig.approver.QuestionCount() == 0);
            }
            {
                Rig rig(Server::UpdatePermission::Allow, FakeApprover::Mode::Yes, false);
                CHECK(rig.Ready());
                CHECK(rig.Uploader().Check(kNewer));
                CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::CheckAnswered; }));
                CHECK(rig.Uploader().GetProgress().answer == Answer::NotSupportedHere);
            }
            {
                Rig rig(Server::UpdatePermission::Ask, FakeApprover::Mode::No);
                CHECK(rig.Ready());
                CHECK(rig.Uploader().Start(rig.Installer(file), kNewer, error));
                CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::Refused; }));
                CHECK(rig.Uploader().GetProgress().answer == Answer::Declined);
                CHECK(rig.launcher.calls == 0);
            }
        });

        RunTest("loopback: while the person here is asked, the viewer's mouse and keys don't reach the computer",
                []() {
                    Rig rig(Server::UpdatePermission::Ask, FakeApprover::Mode::Later);
                    CHECK(rig.Ready());
                    std::string error;
                    CHECK(rig.Uploader().Start(rig.Installer(Content(5000)), kNewer, error));
                    CHECK(WaitUntil(
                        [&]() { return rig.Stage() == Wire::UpdateUploader::Stage::WaitingForPerson; }));
                    rig.Client().SendPointerEvent(3, 4, 1);
                    rig.Client().SendKeyEvent(0x61, true);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    {
                        std::lock_guard<std::mutex> lock(rig.input.mutex);
                        CHECK(rig.input.pointers.empty() && rig.input.keys.empty());
                    }
                    rig.approver.Give(true);
                    CHECK(WaitUntil([&]() { return rig.clientListener.disconnected; }, 10000));
                    CHECK(rig.launcher.calls == 1);
                });

        RunTest("loopback: with the person here asked, input comes back once they say no", []() {
            Rig rig(Server::UpdatePermission::Ask, FakeApprover::Mode::Later);
            CHECK(rig.Ready());
            std::string error;
            CHECK(rig.Uploader().Start(rig.Installer(Content(5000)), kNewer, error));
            CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::WaitingForPerson; }));
            rig.approver.Give(false);
            CHECK(WaitUntil([&]() { return rig.Stage() == Wire::UpdateUploader::Stage::Refused; }));
            rig.Client().SendPointerEvent(5, 6, 0);
            CHECK(WaitUntil([&]() {
                std::lock_guard<std::mutex> lock(rig.input.mutex);
                return !rig.input.pointers.empty();
            }));
            CHECK(rig.Client().IsConnected());
        });
    }
} // namespace MarconiServerTest
