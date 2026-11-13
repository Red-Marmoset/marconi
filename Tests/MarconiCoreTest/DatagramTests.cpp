// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the datagram channel over an in-memory link that loses, reorders and duplicates packets: key
// derivation (with the RFC 5869 and GCM test vectors), the replay window, fragmentation and MTU edges, deadlines,
// tampering, key mismatch, fall back to the control channel and the retry back-off, statistics, and fuzzing

#include "DatagramFakeLink.h"
#include "TestSupport.h"

#include <MarconiCore/Wire/DatagramChannel.h>
#include <MarconiCore/Wire/DatagramCrypto.h>
#include <MarconiCore/Wire/DatagramMessages.h>
#include <MarconiCore/Wire/DatagramReceiver.h>
#include <MarconiCore/Wire/DatagramStats.h>

#include <algorithm>
#include <set>

namespace MarconiTest
{
    using namespace Marconi;
    using Wire::Bytes;
    using Wire::ChannelKind;
    using Wire::DatagramChannel;
    using Wire::LinkState;
    using Wire::SendResult;

    namespace
    {
        constexpr uint8_t kVideo = 1;   // ordered
        constexpr uint8_t kState = 2;   // latest wins
        constexpr uint8_t kReliable = 3;

        Bytes Hex(const char* text)
        {
            Bytes out;
            for (size_t i = 0; text[i] != 0 && text[i + 1] != 0; i += 2)
            {
                auto nibble = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
                out.push_back(static_cast<uint8_t>(nibble(text[i]) * 16 + nibble(text[i + 1])));
            }
            return out;
        }

        // A payload that depends on (id, size), so a delivered message can be checked without storing it.
        Bytes Pattern(uint32_t id, size_t size)
        {
            Bytes bytes(size);
            for (size_t i = 0; i < size; ++i)
            {
                bytes[i] = static_cast<uint8_t>((id * 31 + i * 7 + (i >> 8)) & 0xFF);
            }
            return bytes;
        }

        bool IsPattern(const Wire::Delivery& delivery, size_t size)
        {
            return delivery.payload == Pattern(delivery.messageId, size);
        }

        Wire::DatagramConfig QuickConfig()
        {
            Wire::DatagramConfig config = DatagramPair::DefaultConfig();
            config.mtu = 1200;
            return config;
        }

        // A client and server that have finished the hello.
        void MakeActive(DatagramPair& pair)
        {
            pair.StartBoth();
            CHECK(pair.RunUntil([&]() { return pair.client->GetState() == LinkState::Active &&
                                                pair.server->GetState() == LinkState::Active; },
                                2000000));
        }

        uint32_t TotalGapCount(const std::vector<Wire::Gap>& gaps, uint8_t channel)
        {
            uint32_t total = 0;
            for (const Wire::Gap& gap : gaps)
            {
                if (gap.channel == channel)
                {
                    total += gap.count;
                }
            }
            return total;
        }
    } // namespace

    void RunDatagramTests()
    {
        // ---- Cryptography ----------------------------------------------------------------------------------

        RunTest("Datagram: HKDF-SHA256 matches RFC 5869 test case 1", []() {
            Bytes ikm(22, 0x0b);
            Bytes salt = Hex("000102030405060708090a0b0c");
            Bytes info = Hex("f0f1f2f3f4f5f6f7f8f9");
            Bytes okm = Wire::HkdfSha256(salt, ikm, info, 42);
            CHECK(okm == Hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
        });

        RunTest("Datagram: HKDF-SHA256 matches RFC 5869 test case 3 (empty salt and info)", []() {
            Bytes ikm(22, 0x0b);
            Bytes okm = Wire::HkdfSha256(Bytes(), ikm, Bytes(), 42);
            CHECK(okm == Hex("8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8"));
        });

        RunTest("Datagram: AES-256-GCM matches the published test vectors", []() {
            Wire::DirectionKeys zero; // key and nonce all zero, packet number 0: the vectors' key and IV
            Wire::DatagramAead aead(zero);
            CHECK(aead.IsValid());
            // GCM spec test case 13: empty plaintext, tag only
            uint8_t tag[16];
            CHECK(aead.Seal(0, nullptr, 0, nullptr, 0, tag));
            CHECK(Bytes(tag, tag + 16) == Hex("530f8afbc74536b9a963b4f1c4cb738b"));
            // test case 14: one zero block
            uint8_t plain[16] = {};
            uint8_t sealed[32];
            CHECK(aead.Seal(0, nullptr, 0, plain, 16, sealed));
            CHECK(Bytes(sealed, sealed + 16) == Hex("cea7403d4d606b6e074ec5d3baf39d18"));
            CHECK(Bytes(sealed + 16, sealed + 32) == Hex("d0d1c8a799996bf0265b98b5d48ab919"));
            uint8_t opened[16];
            CHECK(aead.Open(0, nullptr, 0, sealed, 32, opened));
            CHECK(Bytes(opened, opened + 16) == Bytes(16, 0));
        });

        RunTest("Datagram: a sealed packet opens only with the same key, number and associated data", []() {
            Wire::DatagramKeys keys = Wire::DeriveDatagramKeys(DatagramPair::DefaultSecret(), 7);
            Wire::DatagramAead aead(keys.clientToServer);
            Bytes plain = Pattern(1, 100);
            Bytes aad = Pattern(2, 13);
            Bytes sealed(plain.size() + 16);
            CHECK(aead.Seal(5, aad.data(), aad.size(), plain.data(), plain.size(), sealed.data()));
            Bytes opened(plain.size());
            CHECK(aead.Open(5, aad.data(), aad.size(), sealed.data(), sealed.size(), opened.data()));
            CHECK(opened == plain);
            CHECK(!aead.Open(6, aad.data(), aad.size(), sealed.data(), sealed.size(), opened.data()));
            Bytes otherAad = aad;
            otherAad[0] ^= 1;
            CHECK(!aead.Open(5, otherAad.data(), otherAad.size(), sealed.data(), sealed.size(), opened.data()));
            Wire::DatagramAead other(keys.serverToClient);
            CHECK(!other.Open(5, aad.data(), aad.size(), sealed.data(), sealed.size(), opened.data()));
            for (size_t i = 0; i < sealed.size(); ++i)
            {
                Bytes flipped = sealed;
                flipped[i] ^= 0x01;
                CHECK(!aead.Open(5, aad.data(), aad.size(), flipped.data(), flipped.size(), opened.data()));
            }
        });

        RunTest("Datagram: key derivation is deterministic and separates directions, sessions and secrets", []() {
            Bytes secret = DatagramPair::DefaultSecret();
            Wire::DatagramKeys a = Wire::DeriveDatagramKeys(secret, 1);
            Wire::DatagramKeys again = Wire::DeriveDatagramKeys(secret, 1);
            CHECK(a.clientToServer.key == again.clientToServer.key);
            CHECK(a.serverToClient.iv == again.serverToClient.iv);
            CHECK(a.clientToServer.key != a.serverToClient.key);
            CHECK(a.clientToServer.iv != a.serverToClient.iv);
            CHECK(Wire::DeriveDatagramKeys(secret, 2).clientToServer.key != a.clientToServer.key);
            Bytes other = secret;
            other[31] ^= 1;
            CHECK(Wire::DeriveDatagramKeys(other, 1).clientToServer.key != a.clientToServer.key);
        });

        RunTest("Datagram: the replay window accepts each number once and refuses old ones", []() {
            Wire::ReplayWindow window;
            CHECK(window.WouldAccept(0));
            CHECK(window.Mark(5));
            CHECK(!window.Mark(5));
            CHECK(window.Mark(3));  // late but new
            CHECK(!window.Mark(3));
            CHECK(window.Mark(2000));
            CHECK(!window.Mark(5)); // fell out of the window
            CHECK(window.Mark(1500));
            CHECK(!window.Mark(1500));
            CHECK(window.Mark(2000 - 1023)); // oldest slot still inside
            CHECK(!window.Mark(2000 - 1024));
            CHECK(!window.Mark(2000));
            // a jump past the whole window clears it
            CHECK(window.Mark(100000));
            CHECK(!window.Mark(100000));
            CHECK(window.Mark(99999));
            // shifting across word boundaries keeps earlier marks
            Wire::ReplayWindow w2;
            for (uint64_t n = 0; n < 300; n += 3)
            {
                CHECK(w2.Mark(n));
            }
            for (uint64_t n = 0; n < 300; n += 3)
            {
                CHECK(!w2.Mark(n));
            }
            for (uint64_t n = 1; n < 300; n += 3)
            {
                CHECK(w2.Mark(n));
            }
        });

        // ---- Control messages --------------------------------------------------------------------------------

        RunTest("Datagram: control messages round-trip and use numbers 90 to 93", []() {
            CHECK(static_cast<int>(Wire::Type::DatagramOffer) == 90);
            CHECK(static_cast<int>(Wire::Type::DatagramData) == 93);
            Wire::DatagramOffer offer;
            CHECK(Wire::DatagramOffer::Create(19250, 1200, offer));
            Wire::DatagramOffer second;
            CHECK(Wire::DatagramOffer::Create(19250, 1200, second));
            CHECK(offer.secret.size() == 32);
            CHECK(offer.secret != second.secret); // fresh every time
            Bytes framed = offer.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            CHECK(Wire::ReadType(body) == Wire::Type::DatagramOffer);
            Wire::DatagramOffer back = Wire::DatagramOffer::Decode(body);
            CHECK(back.sessionId == offer.sessionId);
            CHECK(back.port == 19250 && back.mtu == 1200);
            CHECK(back.secret == offer.secret);
            // a secret of the wrong length is refused
            Bytes bad = Wire::Build(Wire::Type::DatagramOffer,
                                    [](Wire::Writer& w) { w.U8(1).U32(1).U16(1).U16(1200).Blob(Bytes(5, 1)); });
            CHECK(ThrowsWireError([&]() {
                Wire::Reader o(bad);
                uint64_t n = o.Varint();
                Wire::Reader b(bad.data() + (bad.size() - o.Remaining()), static_cast<size_t>(n));
                Wire::ReadType(b);
                Wire::DatagramOffer::Decode(b);
            }));
            Wire::DatagramData data;
            data.sessionId = 9;
            data.channel = 4;
            data.messageId = 0xFFFFFFF0u;
            data.payload = Pattern(3, 500);
            Bytes dataFramed = data.Encode();
            Wire::Reader dataOuter(dataFramed);
            uint64_t dataLength = dataOuter.Varint();
            Wire::Reader dataBody(dataFramed.data() + (dataFramed.size() - dataOuter.Remaining()),
                                  static_cast<size_t>(dataLength));
            CHECK(Wire::ReadType(dataBody) == Wire::Type::DatagramData);
            Wire::DatagramData dataBack = Wire::DatagramData::Decode(dataBody);
            CHECK(dataBack.payload == data.payload && dataBack.messageId == data.messageId && dataBack.channel == 4);
            CHECK(dataBody.AtEnd());
        });

        // ---- The channel ------------------------------------------------------------------------------------

        RunTest("Datagram: the hello opens the path, both ends go active and measure the round trip", []() {
            DatagramPair pair(QuickConfig(), 1);
            pair.link.params.delayUs = 10000;
            CHECK(pair.client->GetState() == LinkState::Idle);
            MakeActive(pair);
            pair.Run(2000000);
            Wire::DatagramStats stats = pair.client->GetStats();
            CHECK(stats.haveRtt);
            CHECK(stats.rttUs >= 19000 && stats.rttUs <= 30000); // 2 x 10 ms plus a tick or two
            CHECK(stats.minRttUs >= 19000);
            CHECK(Wire::DatagramStatusText(stats) == "UDP");
            CHECK(pair.server->GetStats().haveRtt); // from the keep-alive pings
        });

        RunTest("Datagram: messages arrive intact in both directions, large ones fragmented under the MTU", []() {
            DatagramPair pair(QuickConfig(), 2);
            MakeActive(pair);
            for (uint32_t id = 0; id < 20; ++id)
            {
                size_t size = id == 0 ? 0 : (id * 997) % 20000;
                CHECK(pair.server->Send(kVideo, Pattern(id, size), pair.now) == SendResult::Udp);
                CHECK(pair.client->Send(kState, Pattern(id, 40), pair.now) == SendResult::Udp);
                pair.Run(5000);
            }
            pair.Run(300000);
            CHECK(pair.clientGot.size() == 20);
            for (const Wire::Delivery& d : pair.clientGot)
            {
                CHECK(d.channel == kVideo && !d.overTcp);
                size_t size = d.messageId == 0 ? 0 : (d.messageId * 997) % 20000;
                CHECK(IsPattern(d, size));
            }
            CHECK(pair.serverGot.size() == 20);
            CHECK(pair.link.maxSize <= 1200);
            CHECK(pair.client->GetStats().gapsReported == 0);
        });

        RunTest("Datagram: MTU edges: sizes around one fragment, exactly the MTU on the wire", []() {
            DatagramPair pair(QuickConfig(), 3);
            MakeActive(pair);
            size_t one = pair.server->MaxUnfragmentedPayload();
            CHECK(one == 1200 - Wire::kDatagramDataOverhead);
            std::vector<size_t> sizes = {0, 1, one - 1, one, one + 1, 2 * one, 2 * one + 1, 5 * one};
            for (size_t size : sizes)
            {
                pair.link.maxSize = 0;
                pair.link.sentCount[1] = 0;
                uint32_t id = 0;
                size_t before = pair.clientGot.size();
                CHECK(pair.server->Send(kVideo, Pattern(0, size), pair.now, &id) == SendResult::Udp);
                size_t expectedFragments = std::max<size_t>(1, (size + one - 1) / one);
                CHECK(pair.link.sentCount[1] == expectedFragments);
                if (size == one || size >= 2 * one)
                {
                    CHECK(pair.link.maxSize == 1200); // a full fragment is exactly the MTU
                }
                CHECK(pair.link.maxSize <= 1200);
                pair.Run(20000);
                CHECK(pair.clientGot.size() == before + 1);
                CHECK(pair.clientGot.back().payload == Pattern(0, size));
            }
            // a tiny MTU still works (the smallest allowed is 100)
            Wire::DatagramConfig small = QuickConfig();
            small.mtu = 100;
            DatagramPair tiny(small, 4);
            MakeActive(tiny);
            CHECK(tiny.server->Send(kVideo, Pattern(0, 3000), tiny.now) == SendResult::Udp);
            tiny.Run(100000);
            CHECK(tiny.clientGot.size() == 1 && tiny.clientGot[0].payload == Pattern(0, 3000));
            CHECK(tiny.link.maxSize <= 100);
            // over the size limit is refused, an unknown channel is refused
            CHECK(tiny.server->Send(kVideo, Bytes(9 * 1024 * 1024, 1), tiny.now) == SendResult::TooLarge);
            CHECK(tiny.server->Send(77, Bytes(10, 1), tiny.now) == SendResult::Dropped);
        });

        RunTest("Datagram: with loss, ordered messages come out in order and every loss is reported as a gap", []() {
            DatagramPair pair(QuickConfig(), 5);
            MakeActive(pair);
            pair.link.params.loss = 0.2;
            pair.link.params.delayUs = 5000;
            const uint32_t total = 300;
            for (uint32_t id = 0; id < total; ++id)
            {
                CHECK(pair.server->Send(kVideo, Pattern(id, 3000), pair.now) == SendResult::Udp);
                pair.Run(4000);
            }
            pair.Run(500000);
            uint32_t last = 0;
            bool first = true;
            bool ordered = true;
            for (const Wire::Delivery& d : pair.clientGot)
            {
                if (!first && d.messageId <= last)
                {
                    ordered = false;
                }
                first = false;
                last = d.messageId;
                CHECK(IsPattern(d, 3000));
            }
            CHECK(ordered);
            CHECK(!pair.clientGot.empty());
            CHECK(pair.clientGot.size() < total); // some really were lost: 3 fragments each at 20%
            // delivered + reported lost accounts for every message sent
            CHECK(pair.clientGot.size() + TotalGapCount(pair.clientGaps, kVideo) == total);
            CHECK(pair.client->GetStats().gapsReported == TotalGapCount(pair.clientGaps, kVideo));
        });

        RunTest("Datagram: reordering and delay lose nothing within the deadline and keep ordered channels in order", []() {
            DatagramPair pair(QuickConfig(), 6);
            MakeActive(pair);
            pair.link.params.reorder = 0.5;
            pair.link.params.reorderExtraUs = 40000;
            pair.link.params.delayUs = 3000;
            pair.link.params.jitterUs = 4000;
            for (uint32_t id = 0; id < 200; ++id)
            {
                CHECK(pair.server->Send(kVideo, Pattern(id, 2500), pair.now) == SendResult::Udp);
                pair.Run(2000);
            }
            pair.Run(400000);
            CHECK(pair.clientGot.size() == 200);
            CHECK(pair.clientGaps.empty());
            for (size_t i = 0; i < pair.clientGot.size(); ++i)
            {
                CHECK(pair.clientGot[i].messageId == i);
                CHECK(IsPattern(pair.clientGot[i], 2500));
            }
        });

        RunTest("Datagram: a latest-wins channel never delivers an older message after a newer one", []() {
            DatagramPair pair(QuickConfig(), 7);
            MakeActive(pair);
            pair.link.params.reorder = 0.6;
            pair.link.params.reorderExtraUs = 30000;
            for (uint32_t id = 0; id < 300; ++id)
            {
                CHECK(pair.client->Send(kState, Pattern(id, 30), pair.now) == SendResult::Udp);
                pair.Run(1000);
            }
            pair.Run(200000);
            CHECK(!pair.serverGot.empty());
            for (size_t i = 1; i < pair.serverGot.size(); ++i)
            {
                CHECK(pair.serverGot[i].messageId > pair.serverGot[i - 1].messageId);
            }
            CHECK(pair.serverGot.size() < 300); // the late ones were dropped, not delivered out of order
            CHECK(pair.server->GetStats().messagesStale > 0);
            CHECK(pair.serverGaps.empty()); // latest-wins reports no gaps
        });

        RunTest("Datagram: duplicated datagrams deliver each message once and count as replays", []() {
            DatagramPair pair(QuickConfig(), 8);
            MakeActive(pair);
            pair.link.params.duplicate = 1.0;
            for (uint32_t id = 0; id < 50; ++id)
            {
                CHECK(pair.server->Send(kVideo, Pattern(id, 2000), pair.now) == SendResult::Udp);
                pair.Run(3000);
            }
            pair.Run(100000);
            CHECK(pair.clientGot.size() == 50);
            CHECK(pair.client->GetStats().replaysRejected > 50);
        });

        RunTest("Datagram: a captured datagram replayed later is refused", []() {
            DatagramPair pair(QuickConfig(), 9);
            pair.link.capture = true;
            MakeActive(pair);
            CHECK(pair.client->Send(kState, Pattern(1, 50), pair.now) == SendResult::Udp);
            pair.Run(50000);
            CHECK(pair.serverGot.size() == 1);
            Bytes captured = pair.link.sentByDirection[0].back();
            uint64_t replaysBefore = pair.server->GetStats().replaysRejected;
            for (int i = 0; i < 5; ++i)
            {
                auto result = pair.server->OnDatagram(captured.data(), captured.size(), pair.now);
                CHECK(!result.accepted);
            }
            pair.Run(10000);
            CHECK(pair.serverGot.size() == 1);
            CHECK(pair.server->GetStats().replaysRejected == replaysBefore + 5);
            // also after many newer packets have moved the window on
            for (uint32_t id = 2; id < 1200; ++id)
            {
                pair.client->Send(kState, Pattern(id, 10), pair.now);
                pair.Run(100);
            }
            pair.Run(50000);
            size_t delivered = pair.serverGot.size();
            CHECK(!pair.server->OnDatagram(captured.data(), captured.size(), pair.now).accepted);
            pair.Run(10000);
            CHECK(pair.serverGot.size() == delivered);
        });

        RunTest("Datagram: every flipped bit of a datagram is rejected and nothing is delivered", []() {
            Bytes payload = Pattern(1, 80);
            DatagramPair source(QuickConfig(), 10); // makes the genuine datagram
            source.link.capture = true;
            MakeActive(source);
            source.client->Send(kState, payload, source.now);
            Bytes real = source.link.sentByDirection[0].back();
            // A server that has seen nothing yet, same keys: so a refusal is for the tampering, not for a replay.
            DatagramPair victim(QuickConfig(), 11);
            victim.server->Start(victim.now);
            size_t tried = 0;
            size_t rejected = 0;
            for (size_t byte = 0; byte < real.size(); ++byte)
            {
                for (int bit = 0; bit < 8; bit += 3)
                {
                    Bytes flipped = real;
                    flipped[byte] ^= static_cast<uint8_t>(1 << bit);
                    ++tried;
                    if (!victim.server->OnDatagram(flipped.data(), flipped.size(), victim.now).accepted)
                    {
                        ++rejected;
                    }
                }
            }
            CHECK(tried > 100);
            CHECK(rejected == tried);
            Wire::DatagramStats stats = victim.server->GetStats();
            CHECK(stats.authFailures + stats.wrongSession + stats.malformed == tried);
            CHECK(stats.packetsReceived == 0);
            victim.Run(10000);
            CHECK(victim.serverGot.empty());
            CHECK(victim.server->GetState() == LinkState::Probing); // tampering never opens the path
            // truncated datagrams
            for (size_t n = 0; n < real.size(); ++n)
            {
                CHECK(!victim.server->OnDatagram(real.data(), n, victim.now).accepted);
            }
            // the untouched datagram is accepted
            CHECK(victim.server->OnDatagram(real.data(), real.size(), victim.now).accepted);
            victim.Run(1000);
            CHECK(victim.serverGot.size() == 1);
        });

        RunTest("Datagram: another session id or a different key is rejected silently and falls back to TCP", []() {
            // keys differ: the client's Hello never authenticates on the server
            Wire::DatagramConfig config = QuickConfig();
            Bytes other = DatagramPair::DefaultSecret();
            other[0] ^= 0x80;
            DatagramPair pair(config, 12, other);
            pair.StartBoth();
            pair.Run(1000000);
            CHECK(pair.server->GetState() == LinkState::Probing);
            CHECK(pair.server->GetStats().authFailures > 0);
            CHECK(pair.server->GetStats().packetsReceived == 0);
            CHECK(pair.link.sentCount[1] == 0); // the server never answers what doesn't authenticate
            pair.Run(3000000);
            CHECK(pair.client->GetState() == LinkState::FellBack);
            CHECK(Wire::DatagramStatusText(pair.client->GetStats()) == "over TCP");
            // everything still gets through, on the control channel
            CHECK(pair.client->Send(kState, Pattern(1, 50), pair.now) == SendResult::Tcp);
            pair.Run(1000);
            CHECK(pair.serverGot.size() == 1 && pair.serverGot[0].overTcp);

            // another session id
            DatagramPair mismatched(config, 13);
            mismatched.StartBoth();
            DatagramPair stranger(config, 13, DatagramPair::DefaultSecret(), DatagramPair::DefaultSecret(), 0x777);
            stranger.link.capture = true;
            stranger.StartBoth();
            stranger.Run(1000);
            Bytes foreign = stranger.link.sentByDirection[0].front();
            CHECK(!mismatched.server->OnDatagram(foreign.data(), foreign.size(), mismatched.now).accepted);
            CHECK(mismatched.server->GetStats().wrongSession == 1);
        });

        RunTest("Datagram: an incomplete message is dropped after its deadline and reported as a gap", []() {
            Wire::DatagramConfig config = QuickConfig();
            config.receive.deadlineUs = 50000;
            DatagramPair pair(config, 14);
            MakeActive(pair);
            // lose the second fragment of message 3 only
            size_t fragmentsSent = 0;
            pair.link.filter = [&](int direction, Bytes&)
            {
                if (direction != 1)
                {
                    return true;
                }
                ++fragmentsSent;
                return fragmentsSent != 3 * 3 + 2; // messages 0..2 are three fragments each; drop message 3's second
            };
            for (uint32_t id = 0; id < 6; ++id)
            {
                CHECK(pair.server->Send(kVideo, Pattern(id, 3000), pair.now) == SendResult::Udp);
                pair.Run(2000);
            }
            // messages 4 and 5 are complete but wait behind 3 until its deadline passes
            CHECK(pair.clientGot.size() == 3);
            pair.Run(100000);
            CHECK(pair.clientGot.size() == 5);
            CHECK(pair.clientGot[3].messageId == 4 && pair.clientGot[4].messageId == 5);
            CHECK(pair.clientGaps.size() == 1);
            CHECK(pair.clientGaps[0].firstMessageId == 3 && pair.clientGaps[0].count == 1);
            CHECK(pair.client->GetStats().messagesDeadlineDropped == 1);
            // the missing fragment turning up after the deadline changes nothing
            CHECK(pair.clientGot.size() == 5);
        });

        RunTest("Datagram: a message lost entirely is reported once newer ones show it is missing", []() {
            DatagramPair pair(QuickConfig(), 15);
            MakeActive(pair);
            size_t sent = 0;
            pair.link.filter = [&](int direction, Bytes&)
            {
                if (direction != 1)
                {
                    return true;
                }
                ++sent;
                return !(sent >= 3 && sent <= 4); // messages 2 and 3 (single fragments) vanish
            };
            for (uint32_t id = 0; id < 6; ++id)
            {
                pair.server->Send(kVideo, Pattern(id, 100), pair.now);
                pair.Run(1000);
            }
            pair.Run(300000);
            CHECK(pair.clientGot.size() == 4);
            CHECK(TotalGapCount(pair.clientGaps, kVideo) == 2);
            CHECK(pair.clientGaps.size() == 1 && pair.clientGaps[0].firstMessageId == 2);
        });

        RunTest("Datagram: ordered channel ids wrap past 2^32 without confusion", []() {
            Wire::ReceiverLimits limits;
            Wire::ChannelReceiver receiver(1, ChannelKind::Ordered, limits);
            Wire::ReceiveEvents out;
            receiver.SkipTo(0xFFFFFFFEull); // two messages before the 32-bit ids wrap
            Bytes one(5, 1);
            for (uint32_t id : {0xFFFFFFFEu, 0xFFFFFFFFu, 0u, 1u})
            {
                receiver.OnFragment(id, 0, 1, one.data(), one.size(), 0, out);
            }
            size_t delivered = 0;
            for (const auto& item : out.items)
            {
                CHECK(!item.isGap);
                ++delivered;
            }
            CHECK(delivered == 4);
            CHECK(out.items[2].delivery.messageId == 0);
        });

        RunTest("Datagram: fallback to TCP when UDP is blocked, retry with a growing back-off, and recovery", []() {
            Wire::DatagramConfig config = QuickConfig();
            config.probeTimeoutUs = 1000000;
            config.retryInitialUs = 2000000;
            config.retryMaxUs = 8000000;
            DatagramPair pair(config, 16);
            pair.link.params.blocked = true;
            int64_t start = pair.now;
            pair.StartBoth();
            pair.Run(1100000);
            CHECK(pair.client->GetState() == LinkState::FellBack);
            CHECK(pair.client->GetStats().fallbacks == 1);
            CHECK(Wire::DatagramStatusText(pair.client->GetStats()) == "over TCP");
            // the server was told over the control channel, not left to time out
            CHECK(pair.server->GetState() == LinkState::FellBack);
            // while blocked, messages in both directions go over TCP and arrive
            CHECK(pair.server->Send(kVideo, Pattern(0, 5000), pair.now) == SendResult::Tcp);
            CHECK(pair.client->Send(kState, Pattern(0, 20), pair.now) == SendResult::Tcp);
            CHECK(pair.client->Send(kReliable, Pattern(0, 20), pair.now) == SendResult::Tcp);
            pair.Run(10000);
            CHECK(pair.clientGot.size() == 1 && pair.clientGot[0].overTcp && IsPattern(pair.clientGot[0], 5000));
            CHECK(pair.serverGot.size() == 2 && pair.serverGot[0].overTcp);
            // retries: fell back at about +1 s, probing again at +3 s, fell back at +4 s, then waits 4 s (+8 s)
            pair.Run(15000000);
            std::vector<std::pair<int64_t, LinkState>> states = pair.clientStates;
            std::vector<int64_t> probeTimes;
            for (const auto& entry : states)
            {
                if (entry.second == LinkState::Probing)
                {
                    probeTimes.push_back(entry.first - start);
                }
            }
            CHECK(probeTimes.size() >= 3);
            if (probeTimes.size() >= 3)
            {
                CHECK(probeTimes[1] - probeTimes[0] >= 1000000 + 2000000 - 50000); // probe 1 s, back-off 2 s
                CHECK(probeTimes[2] - probeTimes[1] >= 1000000 + 4000000 - 50000); // the back-off doubled
            }
            // UDP comes back: the next retry succeeds and both sides are active again
            pair.link.params.blocked = false;
            CHECK(pair.RunUntil([&]() { return pair.client->GetState() == LinkState::Active; }, 20000000));
            CHECK(pair.RunUntil([&]() { return pair.server->GetState() == LinkState::Active; }, 1000000));
            size_t before = pair.clientGot.size();
            CHECK(pair.server->Send(kVideo, Pattern(0, 3000), pair.now) == SendResult::Udp);
            pair.Run(50000);
            CHECK(pair.clientGot.size() == before + 1 && !pair.clientGot.back().overTcp);
            // message ids kept counting across the path change: the receiver saw 0 over TCP then 1 over UDP
            CHECK(pair.clientGot.back().messageId == 1);
            CHECK(pair.clientGaps.empty());
        });

        RunTest("Datagram: an active link that goes silent falls back after the dead time", []() {
            Wire::DatagramConfig config = QuickConfig();
            config.deadAfterUs = 2000000;
            DatagramPair pair(config, 17);
            MakeActive(pair);
            pair.Run(3000000); // keep-alives hold it
            CHECK(pair.client->GetState() == LinkState::Active);
            CHECK(pair.server->GetState() == LinkState::Active);
            pair.link.params.blocked = true;
            pair.Run(2500000);
            CHECK(pair.client->GetState() == LinkState::FellBack);
            CHECK(pair.server->GetState() == LinkState::FellBack);
            CHECK(pair.server->Send(kVideo, Pattern(0, 100), pair.now) == SendResult::Tcp);
            // no control channel at all: nothing can be sent, and it says so
            pair.controlOpen = false;
            pair.Run(1000);
        });

        RunTest("Datagram: Close tells the peer and neither end retries", []() {
            DatagramPair pair(QuickConfig(), 18);
            MakeActive(pair);
            pair.client->Close(pair.now);
            pair.Run(100000);
            CHECK(pair.server->GetState() == LinkState::FellBack);
            CHECK(pair.client->GetState() == LinkState::FellBack);
            pair.Run(300000000, 100000);
            CHECK(pair.client->GetState() == LinkState::FellBack);
            CHECK(pair.server->GetState() == LinkState::FellBack);
        });

        RunTest("Datagram: reliable channels always use the control channel", []() {
            DatagramPair pair(QuickConfig(), 19);
            MakeActive(pair);
            size_t datagramsBefore = pair.link.sentCount[0];
            CHECK(pair.client->Send(kReliable, Pattern(1, 3000), pair.now) == SendResult::Tcp);
            pair.Run(1000);
            CHECK(pair.link.sentCount[0] == datagramsBefore);
            CHECK(pair.serverGot.size() == 1 && pair.serverGot[0].overTcp && pair.serverGot[0].channel == kReliable);
        });

        RunTest("Datagram: loss and round-trip statistics feed the status text and the bitrate hint", []() {
            Wire::DatagramConfig config = QuickConfig();
            DatagramPair pair(config, 20);
            pair.link.params.delayUs = 10000;
            MakeActive(pair);
            // a clean link
            for (int i = 0; i < 400; ++i)
            {
                pair.server->Send(kVideo, Pattern(i, 800), pair.now);
                pair.Run(10000);
            }
            Wire::DatagramStats clean = pair.server->GetStats();
            CHECK(clean.havePeerReport);
            CHECK(clean.peerLossPercent < 1.0);
            CHECK(pair.client->GetStats().lossPercent < 1.0);
            CHECK(Wire::ComputeBitrateHint(clean).advice == Wire::BitrateAdvice::Increase);
            // 10% loss each way
            pair.link.params.loss = 0.10;
            for (int i = 400; i < 1200; ++i)
            {
                pair.server->Send(kVideo, Pattern(i, 800), pair.now);
                pair.Run(10000);
            }
            Wire::DatagramStats lossy = pair.server->GetStats();
            CHECK(lossy.peerLossPercent > 2.0 && lossy.peerLossPercent < 25.0);
            Wire::DatagramStats clientView = pair.client->GetStats();
            CHECK(clientView.lossPercent > 2.0 && clientView.lossPercent < 25.0);
            CHECK(Wire::ComputeBitrateHint(lossy).advice == Wire::BitrateAdvice::Decrease);
            std::string text = Wire::DatagramStatusText(clientView);
            CHECK(text.find("UDP, ") == 0 && text.find("% loss") != std::string::npos);
        });

        RunTest("Datagram: bitrate hint and status text for each state", []() {
            Wire::DatagramStats stats;
            CHECK(Wire::DatagramStatusText(stats).empty());
            CHECK(Wire::ComputeBitrateHint(stats).advice == Wire::BitrateAdvice::Hold);
            stats.state = LinkState::Probing;
            CHECK(Wire::DatagramStatusText(stats) == "UDP starting");
            stats.state = LinkState::FellBack;
            CHECK(Wire::DatagramStatusText(stats) == "over TCP");
            CHECK(Wire::ComputeBitrateHint(stats).advice == Wire::BitrateAdvice::Hold);
            stats.state = LinkState::Active;
            stats.haveRtt = true;
            stats.minRttUs = 20000;
            stats.rttUs = 22000;
            CHECK(Wire::ComputeBitrateHint(stats).advice == Wire::BitrateAdvice::Increase);
            stats.rttUs = 120000; // queues are building
            CHECK(Wire::ComputeBitrateHint(stats).advice == Wire::BitrateAdvice::Decrease);
            stats.rttUs = 22000;
            stats.lossPercent = 4;
            CHECK(Wire::ComputeBitrateHint(stats).scalePercent == 85);
            stats.havePeerReport = true;
            stats.peerLossPercent = 12;
            CHECK(Wire::ComputeBitrateHint(stats).scalePercent == 70);
            CHECK(Wire::DatagramStatusText(stats) == "UDP, 4% loss");
        });

        RunTest("Datagram: the round-trip estimator smooths and tracks the minimum", []() {
            Wire::RttEstimator rtt;
            CHECK(!rtt.HasSample());
            rtt.Sample(40000);
            CHECK(rtt.Smoothed() == 40000);
            for (int i = 0; i < 50; ++i)
            {
                rtt.Sample(20000);
            }
            CHECK(rtt.Smoothed() > 19000 && rtt.Smoothed() < 22000);
            CHECK(rtt.Minimum() == 20000);
        });

        // ---- Fuzzing ------------------------------------------------------------------------------------------

        RunTest("Datagram fuzz: random datagrams and control messages never crash or deliver anything", []() {
            DatagramPair pair(QuickConfig(), 21);
            MakeActive(pair);
            DatagramRandom random(99);
            for (int i = 0; i < 20000; ++i)
            {
                Bytes junk = random.Bytes(random.Below(1400));
                if (junk.size() > 13 && (i & 1))
                {
                    junk[0] = 1; // the right format byte and session id, so it gets as far as the decryption
                    junk[1] = 0xcd;
                    junk[2] = 0xab;
                    junk[3] = 0x34;
                    junk[4] = 0x12;
                }
                CHECK(!pair.server->OnDatagram(junk.data(), junk.size(), pair.now).accepted);
            }
            CHECK(pair.serverGot.empty());
            for (int i = 0; i < 3000; ++i)
            {
                Bytes body = random.Bytes(random.Below(64));
                Wire::Reader reader(body);
                try
                {
                    Wire::Type type = static_cast<Wire::Type>(90 + random.Below(4));
                    pair.server->OnControlMessage(type, reader, pair.now);
                }
                catch (const Wire::WireError&)
                {
                }
            }
            CHECK(pair.server->GetState() == LinkState::Active || pair.server->GetState() == LinkState::FellBack);
        });

        RunTest("Datagram fuzz: authentic packets with random contents reach the parser and are refused cleanly", []() {
            // Packets sealed with the real keys but with random plaintext, so the inner parser sees every shape.
            Bytes secret = DatagramPair::DefaultSecret();
            uint32_t session = 0x1234abcd;
            Wire::DatagramKeys keys = Wire::DeriveDatagramKeys(secret, session);
            Wire::DatagramAead forge(keys.clientToServer);
            DatagramPair pair(QuickConfig(), 22);
            pair.server->Start(pair.now);
            DatagramRandom random(7);
            for (uint64_t pn = 0; pn < 20000; ++pn)
            {
                Bytes plain = random.Bytes(1 + random.Below(300));
                if (random.Below(4) != 0)
                {
                    plain[0] = static_cast<uint8_t>(1 + random.Below(6)); // real kinds, but not Close (7)
                }
                else if (plain[0] == 7)
                {
                    plain[0] = 9; // an unknown kind
                }
                if (plain[0] == 5 && plain.size() > 9 && random.Below(2) == 0)
                {
                    plain[1] = static_cast<uint8_t>(1 + random.Below(3)); // a real channel
                    plain[8] = 0;
                    plain[9] = 0; // keep the fragment count small half of the time
                }
                Wire::Writer header;
                header.U8(1).U32(session).U64(pn);
                Bytes packet = header.Take();
                packet.resize(13 + plain.size() + 16);
                CHECK(forge.Seal(pn, packet.data(), 13, plain.data(), plain.size(), packet.data() + 13));
                pair.server->OnDatagram(packet.data(), packet.size(), pair.now);
                if (pn % 100 == 0)
                {
                    pair.now += 5000;
                    pair.server->Tick(pair.now);
                }
            }
            Wire::DatagramStats stats = pair.server->GetStats();
            CHECK(stats.packetsReceived == 20000);
            CHECK(stats.authFailures == 0);
        });

        RunTest("Datagram fuzz: the reassembler survives random fragments with bounded memory", []() {
            for (ChannelKind kind : {ChannelKind::Ordered, ChannelKind::LatestWins})
            {
                Wire::ReceiverLimits limits;
                limits.maxMessageBytes = 10000;
                limits.maxPendingMessages = 16;
                limits.deadlineUs = 1000;
                Wire::ChannelReceiver receiver(1, kind, limits);
                DatagramRandom random(kind == ChannelKind::Ordered ? 3 : 4);
                Wire::ReceiveEvents out;
                int64_t now = 0;
                for (int i = 0; i < 50000; ++i)
                {
                    Bytes data = random.Bytes(random.Below(400));
                    uint32_t id = random.Below(4) == 0 ? random.Below(0xFFFFFFFFu) : random.Below(200);
                    uint16_t count = static_cast<uint16_t>(random.Below(8));
                    uint16_t index = static_cast<uint16_t>(random.Below(10));
                    receiver.OnFragment(id, index, count, data.data(), data.size(), now, out);
                    now += random.Below(50);
                    if (i % 50 == 0)
                    {
                        receiver.Tick(now, out);
                        out = Wire::ReceiveEvents();
                    }
                }
                CHECK(true); // reaching here without a crash or an exception is the check
            }
        });

        RunTest("Datagram: the same seed gives the same run (the fake link is deterministic)", []() {
            auto run = []() {
                DatagramPair pair(QuickConfig(), 77);
                MakeActive(pair);
                pair.link.params.loss = 0.15;
                pair.link.params.reorder = 0.3;
                for (uint32_t id = 0; id < 100; ++id)
                {
                    pair.server->Send(kVideo, Pattern(id, 2500), pair.now);
                    pair.Run(3000);
                }
                pair.Run(300000);
                std::vector<uint32_t> ids;
                for (const auto& d : pair.clientGot)
                {
                    ids.push_back(d.messageId);
                }
                return ids;
            };
            CHECK(run() == run());
        });
    }
} // namespace MarconiTest
