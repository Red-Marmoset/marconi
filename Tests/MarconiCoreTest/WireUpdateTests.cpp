// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for updating a Marconi Server from the viewer: the update messages (round trips, limits, a fuzz pass), the
// Hello's added version field (old and new layouts both read), the viewer's offer decision and wording, and the
// uploader driven against a scripted server

#include "TestSupport.h"

#include <MarconiCore/ServerUpdate.h>
#include <MarconiCore/Wire/WireMessages.h>
#include <MarconiCore/Wire/WireUpdateClient.h>
#include <MarconiCore/Wire/WireUpdateMessages.h>

#include <Crypto/Crypto.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;
    namespace fs = std::filesystem;

    namespace
    {
        // The body of a framed message (after its length), positioned after the type byte.
        Wire::Reader BodyOf(const Wire::Bytes& framed, Wire::Type& type)
        {
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            type = Wire::ReadType(body);
            return body;
        }

        template <class Message>
        Message RoundTrip(const Message& message, Wire::Type expected)
        {
            Wire::Bytes framed = message.Encode();
            Wire::Type type;
            Wire::Reader body = BodyOf(framed, type);
            CHECK(type == expected);
            Message back = Message::Decode(body);
            CHECK(body.AtEnd());
            return back;
        }

        class Random
        {
        public:
            explicit Random(uint32_t seed) : m_state(seed) {}
            uint32_t Next()
            {
                m_state ^= m_state << 13;
                m_state ^= m_state >> 17;
                m_state ^= m_state << 5;
                return m_state;
            }

        private:
            uint32_t m_state;
        };

        // Collects what the uploader sends and what it reports.
        struct Script
        {
            std::mutex mutex;
            std::condition_variable changed;
            std::vector<Wire::Bytes> sent;
            std::vector<Wire::UpdateUploader::Progress> reports;
            bool linkUp = true;

            bool Send(const Wire::Bytes& framed)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!linkUp)
                {
                    return false;
                }
                sent.push_back(framed);
                changed.notify_all();
                return true;
            }
            void OnReport(const Wire::UpdateUploader::Progress& progress)
            {
                std::lock_guard<std::mutex> lock(mutex);
                reports.push_back(progress);
                changed.notify_all();
            }
            bool WaitFor(const std::function<bool()>& predicate)
            {
                std::unique_lock<std::mutex> lock(mutex);
                return changed.wait_for(lock, std::chrono::seconds(5), predicate);
            }
        };

        void Deliver(Wire::UpdateUploader& uploader, const Wire::Bytes& framed)
        {
            Wire::Type type;
            Wire::Reader body = BodyOf(framed, type);
            CHECK(uploader.OnMessage(type, body));
        }

        std::string WriteTempFile(const std::string& name, const Wire::Bytes& content)
        {
            fs::path path = fs::temp_directory_path() /
                            (name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
            return path.string();
        }
    } // namespace

    void RunWireUpdateTests()
    {
        RunTest("update messages round-trip", []() {
            Wire::UpdateOffer offer;
            offer.version = "0.10.0.512";
            offer.size = 4321;
            offer.sha256.assign(32, 0xAB);
            Wire::UpdateOffer offerBack = RoundTrip(offer, Wire::Type::UpdateOffer);
            CHECK(!offerBack.IsCheckOnly());
            CHECK(offerBack.product == "Marconi Server" && offerBack.version == "0.10.0.512");
            CHECK(offerBack.platform == Wire::kUpdatePlatformWindows && offerBack.size == 4321);
            CHECK(offerBack.sha256 == offer.sha256);

            Wire::UpdateOffer check;
            check.flags = Wire::UpdateOffer::kCheckOnly;
            check.version = "1.2.3";
            Wire::UpdateOffer checkBack = RoundTrip(check, Wire::Type::UpdateOffer);
            CHECK(checkBack.IsCheckOnly() && checkBack.sha256.empty() && checkBack.version == "1.2.3");

            Wire::UpdateAnswer answer;
            answer.status = Wire::UpdateAnswer::Status::AskingPerson;
            answer.serverVersion = "0.9.7.400";
            answer.detail = "asking";
            Wire::UpdateAnswer answerBack = RoundTrip(answer, Wire::Type::UpdateAnswer);
            CHECK(answerBack.status == Wire::UpdateAnswer::Status::AskingPerson);
            CHECK(answerBack.serverVersion == "0.9.7.400" && answerBack.detail == "asking");

            Wire::UpdateChunk chunk;
            chunk.offset = 65536;
            chunk.data = {1, 2, 3, 4, 5};
            Wire::UpdateChunk chunkBack = RoundTrip(chunk, Wire::Type::UpdateChunk);
            CHECK(chunkBack.offset == 65536 && chunkBack.data == chunk.data);

            Wire::UpdateResult result;
            result.status = Wire::UpdateResult::Status::SignatureRefused;
            result.detail = "other publisher";
            Wire::UpdateResult resultBack = RoundTrip(result, Wire::Type::UpdateResult);
            CHECK(resultBack.status == Wire::UpdateResult::Status::SignatureRefused);
            CHECK(resultBack.detail == "other publisher");
        });

        RunTest("update messages refuse bad hashes, oversize pieces, unknown statuses and long text", []() {
            auto throws = [](const Wire::Bytes& framed, Wire::Type expected) {
                Wire::Type type;
                Wire::Reader body = BodyOf(framed, type);
                CHECK(type == expected);
                try
                {
                    switch (type)
                    {
                    case Wire::Type::UpdateOffer: Wire::UpdateOffer::Decode(body); break;
                    case Wire::Type::UpdateAnswer: Wire::UpdateAnswer::Decode(body); break;
                    case Wire::Type::UpdateChunk: Wire::UpdateChunk::Decode(body); break;
                    default: Wire::UpdateResult::Decode(body); break;
                    }
                }
                catch (const Wire::WireError&)
                {
                    return true;
                }
                return false;
            };
            Wire::UpdateOffer offer;
            offer.version = "1.0.0";
            offer.sha256.assign(31, 0); // not 32 bytes
            CHECK(throws(offer.Encode(), Wire::Type::UpdateOffer));
            offer.sha256.assign(32, 0);
            offer.version = std::string(2000, '9');
            CHECK(throws(offer.Encode(), Wire::Type::UpdateOffer));

            Wire::UpdateChunk chunk;
            chunk.data.assign(Wire::kUpdateChunkBytes + 1, 7);
            CHECK(throws(chunk.Encode(), Wire::Type::UpdateChunk));

            Wire::Bytes badAnswer = Wire::Build(Wire::Type::UpdateAnswer, [](Wire::Writer& w) {
                w.U8(200).String("").String("");
            });
            CHECK(throws(badAnswer, Wire::Type::UpdateAnswer));
            Wire::Bytes badResult =
                Wire::Build(Wire::Type::UpdateResult, [](Wire::Writer& w) { w.U8(99).String(""); });
            CHECK(throws(badResult, Wire::Type::UpdateResult));
        });

        RunTest("random bytes in update messages are decoded or refused, never crash", []() {
            Random random(0x5E1F0DDu);
            for (int round = 0; round < 4000; ++round)
            {
                Wire::Bytes body(random.Next() % 80);
                for (uint8_t& byte : body)
                {
                    byte = static_cast<uint8_t>(random.Next() >> 24);
                }
                for (int kind = 0; kind < 4; ++kind)
                {
                    Wire::Reader reader(body);
                    try
                    {
                        switch (kind)
                        {
                        case 0: Wire::UpdateOffer::Decode(reader); break;
                        case 1: Wire::UpdateAnswer::Decode(reader); break;
                        case 2: Wire::UpdateChunk::Decode(reader); break;
                        default: Wire::UpdateResult::Decode(reader); break;
                        }
                    }
                    catch (const Wire::WireError&)
                    {
                    }
                }
            }
            CHECK(true);
        });

        RunTest("Hello carries the viewer's version, and a Hello without it still reads", []() {
            Wire::Hello hello;
            hello.clientName = "Anna's laptop";
            hello.clientVersion = "0.10.0.512";
            Wire::Hello back = RoundTrip(hello, Wire::Type::Hello);
            CHECK(back.clientName == "Anna's laptop" && back.clientVersion == "0.10.0.512");

            // What an older viewer sends: no version at the end.
            Wire::Bytes old = Wire::Build(Wire::Type::Hello, [](Wire::Writer& w) {
                w.U16(Wire::kProtocolVersion).U64(Wire::Capability::Clipboard).String("old viewer");
            });
            Wire::Type type;
            Wire::Reader body = BodyOf(old, type);
            Wire::Hello oldBack = Wire::Hello::Decode(body);
            CHECK(oldBack.clientName == "old viewer" && oldBack.clientVersion.empty());
            CHECK(body.AtEnd());
        });

        RunTest("versions compare strictly: same or older is not newer", []() {
            CHECK(ServerUpdate::IsNewer("0.10.0.1", "0.9.7.999"));
            CHECK(ServerUpdate::IsNewer("0.9.7.401", "0.9.7.400"));
            CHECK(!ServerUpdate::IsNewer("0.9.7.400", "0.9.7.400"));
            CHECK(!ServerUpdate::IsNewer("0.9.6.900", "0.9.7.1"));
            CHECK(!ServerUpdate::IsNewer("banana", "0.9.7"));
            CHECK(!ServerUpdate::IsNewer("1.0.0", ""));
            CHECK(ServerUpdate::IsSameVersion("v1.2.3", "1.2.3.0"));
            CHECK(ServerUpdate::InstallerVersion("/x/MarconiServer-0.10.0.512-win64-setup.exe") == "0.10.0.512");
            CHECK(ServerUpdate::InstallerVersion("/x/Marconi-0.10.0.512-win64-setup.exe").empty());
        });

        RunTest("the viewer offers an update only when its installer is newer and the server can take it", []() {
            using Decision = ServerUpdate::OfferDecision;
            ServerUpdate::OfferInputs in;
            in.serverVersion = "0.9.7.400";
            in.serverKnowsUpdates = true;
            in.installerPath = "C:/Program Files/Marconi/MarconiServer-0.10.0.512-win64-setup.exe";
            CHECK(ServerUpdate::DecideOffer(in) == Decision::Offer);

            ServerUpdate::OfferInputs same = in;
            same.serverVersion = "0.10.0.512";
            CHECK(ServerUpdate::DecideOffer(same) == Decision::UpToDate);
            same.serverVersion = "0.11.0.1"; // the server is newer than this Marconi
            CHECK(ServerUpdate::DecideOffer(same) == Decision::UpToDate);

            ServerUpdate::OfferInputs old = in;
            old.serverKnowsUpdates = false;
            CHECK(ServerUpdate::DecideOffer(old) == Decision::ServerTooOld);

            ServerUpdate::OfferInputs none = in;
            none.installerPath.clear();
            CHECK(ServerUpdate::DecideOffer(none) == Decision::NoInstaller);

            ServerUpdate::OfferInputs unknown = in;
            unknown.serverVersion = "";
            CHECK(ServerUpdate::DecideOffer(unknown) == Decision::UnknownVersion);

            ServerUpdate::OfferInputs quiet = in;
            quiet.dontAsk = true;
            CHECK(ServerUpdate::DecideOffer(quiet) == Decision::DontAsk);
            quiet.fromMenu = true; // the menu item asks anyway
            CHECK(ServerUpdate::DecideOffer(quiet) == Decision::Offer);
        });

        RunTest("after reconnecting the viewer tells a finished update from one that didn't take", []() {
            CHECK(ServerUpdate::CheckAfterReconnect("0.10.0.512", "0.10.0.512") == ServerUpdate::Outcome::Updated);
            CHECK(ServerUpdate::CheckAfterReconnect("0.10.0.512", "0.9.7.400") == ServerUpdate::Outcome::StillOld);
            CHECK(ServerUpdate::CheckAfterReconnect("0.10.0.512", "0.11.0.1") == ServerUpdate::Outcome::Other);
            std::string text = ServerUpdate::DescribeOutcome(ServerUpdate::Outcome::StillOld, "Studio PC",
                                                             "0.10.0.512", "0.9.7.400");
            CHECK(text.find("still version 0.9.7.400") != std::string::npos);
            CHECK(ServerUpdate::OfferQuestion("Studio PC", "0.9.7.400", "0.10.0.512") ==
                  "Marconi Server on Studio PC is older (0.9.7.400) than the one that came with this Marconi "
                  "(0.10.0.512). Update it now?");
            // Every status has words of its own.
            for (int s = 0; s <= static_cast<int>(Wire::UpdateAnswer::Status::BadOffer); ++s)
            {
                CHECK(!ServerUpdate::DescribeAnswer(static_cast<Wire::UpdateAnswer::Status>(s), "X").empty());
            }
            for (int s = 0; s <= static_cast<int>(Wire::UpdateResult::Status::NotNewer); ++s)
            {
                CHECK(!ServerUpdate::DescribeResult(static_cast<Wire::UpdateResult::Status>(s), "X").empty());
            }
        });

        RunTest("the uploader offers the file with its SHA-256, sends it in order once told, and reports", []() {
            Wire::Bytes content(Wire::kUpdateChunkBytes * 2 + 100);
            for (size_t i = 0; i < content.size(); ++i)
            {
                content[i] = static_cast<uint8_t>(i * 7 + 3);
            }
            const std::string path = WriteTempFile("marconi-uploader-test", content);
            Script script;
            Wire::UpdateUploader uploader([&](const Wire::Bytes& framed) { return script.Send(framed); });
            uploader.SetListener([&](const Wire::UpdateUploader::Progress& p) { script.OnReport(p); });
            std::string error;
            CHECK(uploader.Start(path, "0.10.0.512", error));
            CHECK(!uploader.Start(path, "0.10.0.512", error)); // one at a time

            Wire::Type type;
            Wire::Reader body = BodyOf(script.sent.at(0), type);
            CHECK(type == Wire::Type::UpdateOffer);
            Wire::UpdateOffer offer = Wire::UpdateOffer::Decode(body);
            uint8_t expected[32];
            Crypto::Sha256(content.data(), content.size(), expected);
            CHECK(offer.size == content.size() && offer.sha256 == Wire::Bytes(expected, expected + 32));
            CHECK(!offer.IsCheckOnly() && offer.version == "0.10.0.512");

            Wire::UpdateAnswer asking;
            asking.status = Wire::UpdateAnswer::Status::AskingPerson;
            Deliver(uploader, asking.Encode());
            CHECK(uploader.GetProgress().stage == Wire::UpdateUploader::Stage::WaitingForPerson);

            Wire::UpdateAnswer go;
            go.status = Wire::UpdateAnswer::Status::SendIt;
            Deliver(uploader, go.Encode());
            CHECK(script.WaitFor([&]() { return script.sent.size() == 4; }));
            CHECK(script.WaitFor([&]() {
                return !script.reports.empty() &&
                       script.reports.back().stage == Wire::UpdateUploader::Stage::ServerChecking;
            }));
            Wire::Bytes rebuilt;
            for (size_t i = 1; i < script.sent.size(); ++i)
            {
                Wire::Reader piece = BodyOf(script.sent[i], type);
                CHECK(type == Wire::Type::UpdateChunk);
                Wire::UpdateChunk chunk = Wire::UpdateChunk::Decode(piece);
                CHECK(chunk.offset == rebuilt.size());
                rebuilt.insert(rebuilt.end(), chunk.data.begin(), chunk.data.end());
            }
            CHECK(rebuilt == content);

            Wire::UpdateResult installing;
            installing.status = Wire::UpdateResult::Status::Installing;
            Deliver(uploader, installing.Encode());
            CHECK(uploader.GetProgress().stage == Wire::UpdateUploader::Stage::Installing);
            CHECK(uploader.IsInstalling());
            uploader.Reset(); // the connection drops as the server stops
            CHECK(uploader.IsInstalling()); // remembered for the reconnect
            std::error_code code;
            fs::remove(path, code);
        });

        RunTest("the uploader stops at a refusal, a check-only answer, a missing file, and a lost link", []() {
            Script script;
            Wire::UpdateUploader uploader([&](const Wire::Bytes& framed) { return script.Send(framed); });
            CHECK(uploader.Check("0.10.0.512"));
            Wire::UpdateAnswer would;
            would.status = Wire::UpdateAnswer::Status::NeedsApproval;
            would.serverVersion = "0.9.7.400";
            Deliver(uploader, would.Encode());
            Wire::UpdateUploader::Progress progress = uploader.GetProgress();
            CHECK(progress.stage == Wire::UpdateUploader::Stage::CheckAnswered);
            CHECK(progress.answer == Wire::UpdateAnswer::Status::NeedsApproval && progress.serverVersion == "0.9.7.400");
            CHECK(script.sent.size() == 1);

            std::string error;
            CHECK(!uploader.Start("/no/such/MarconiServer-1.0.0.0-win64-setup.exe", "1.0.0.0", error));
            CHECK(!error.empty());

            const std::string path = WriteTempFile("marconi-uploader-refusal", Wire::Bytes(1000, 1));
            CHECK(uploader.Start(path, "1.0.0.0", error));
            Wire::UpdateAnswer no;
            no.status = Wire::UpdateAnswer::Status::TurnedOff;
            Deliver(uploader, no.Encode());
            CHECK(uploader.GetProgress().stage == Wire::UpdateUploader::Stage::Refused);
            CHECK(script.sent.size() == 2); // the check and the offer, no pieces

            CHECK(uploader.Start(path, "1.0.0.0", error));
            uploader.Reset(); // the connection ended while the server was deciding
            CHECK(uploader.GetProgress().stage == Wire::UpdateUploader::Stage::Failed);
            CHECK(!uploader.IsInstalling());

            {
                std::lock_guard<std::mutex> lock(script.mutex);
                script.linkUp = false;
            }
            CHECK(!uploader.Start(path, "1.0.0.0", error)); // nothing to send it on
            std::error_code code;
            fs::remove(path, code);
        });
    }
} // namespace MarconiTest
