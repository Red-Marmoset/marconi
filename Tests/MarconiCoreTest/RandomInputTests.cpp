// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Feeds pseudo-random bytes to every decoder and the DNS parser: each input must be decoded or refused cleanly

#include "TestSupport.h"

#include <MarconiCore/Net/DnsMessage.h>
#include <MarconiCore/Rfb/Decoders/Decoders.h>

#include <zlib.h>

#include <chrono>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        constexpr int kInputsPerCase = 3000;
        constexpr int kFramebufferSide = 16;

        // xorshift32 with a fixed seed: the same inputs on every run and platform, so a failure can be replayed.
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

            int Below(int limit) { return static_cast<int>(Next() % static_cast<uint32_t>(limit)); }

            Rfb::Bytes Bytes(size_t count)
            {
                Rfb::Bytes bytes(count);
                for (uint8_t& byte : bytes)
                {
                    byte = static_cast<uint8_t>(Next() >> 24);
                }
                return bytes;
            }

            // A rectangle inside the framebuffer, sometimes empty, as the client checks before decoding.
            Rfb::Rect Rect()
            {
                Rfb::Rect rect;
                rect.x = Below(kFramebufferSide + 1);
                rect.y = Below(kFramebufferSide + 1);
                rect.width = Below(kFramebufferSide - rect.x + 1);
                rect.height = Below(kFramebufferSide - rect.y + 1);
                return rect;
            }

        private:
            uint32_t m_state;
        };

        // A complete zlib stream, so random bytes get past zlib and reach the ZRLE and Tight parsers. Level 0 stores
        // the bytes as they are: compressing thousands of random buffers would take most of the test's time.
        Rfb::Bytes Compress(const Rfb::Bytes& data)
        {
            z_stream stream = {};
            deflateInit(&stream, Z_NO_COMPRESSION);
            Rfb::Bytes compressed(deflateBound(&stream, static_cast<uLong>(data.size())));
            stream.next_in = const_cast<Bytef*>(data.data());
            stream.avail_in = static_cast<uInt>(data.size());
            stream.next_out = compressed.data();
            stream.avail_out = static_cast<uInt>(compressed.size());
            deflate(&stream, Z_FINISH);
            compressed.resize(compressed.size() - stream.avail_out);
            deflateEnd(&stream);
            return compressed;
        }

        void AppendCompactLength(Rfb::OutBuffer& out, size_t length)
        {
            out.U8(static_cast<uint8_t>((length & 0x7F) | (length > 0x7F ? 0x80 : 0)));
            if (length > 0x7F)
            {
                out.U8(static_cast<uint8_t>(((length >> 7) & 0x7F) | (length > 0x3FFF ? 0x80 : 0)));
                if (length > 0x3FFF)
                {
                    out.U8(static_cast<uint8_t>(length >> 14));
                }
            }
        }

        // Up to 63 random bytes.
        Rfb::Bytes ShortRandomBytes(Random& random)
        {
            return random.Bytes(static_cast<size_t>(random.Below(64)));
        }

        // RRE with a small subrectangle count, so the random bytes reach the subrectangles.
        Rfb::Bytes RreWithFewSubrects(Random& random)
        {
            Rfb::OutBuffer out;
            out.U32(static_cast<uint32_t>(random.Below(4)));
            Rfb::Bytes rest = ShortRandomBytes(random);
            out.Append(rest.data(), rest.size());
            return out.Data();
        }

        // CopyRect from near the framebuffer, so some copies happen.
        Rfb::Bytes CopyRectNearby(Random& random)
        {
            Rfb::OutBuffer out;
            out.U16(static_cast<uint16_t>(random.Below(kFramebufferSide + 4)));
            out.U16(static_cast<uint16_t>(random.Below(kFramebufferSide + 4)));
            return out.Data();
        }

        // ZRLE with random compressed data. A random length would almost always be over the limit or name data that
        // isn't there (and make the decoder set aside up to 64 MiB for it), so the length matches what follows;
        // half the time the data starts with a valid zlib header so zlib reads on into random blocks.
        Rfb::Bytes ZrleWithRandomData(Random& random)
        {
            Rfb::Bytes data = ShortRandomBytes(random);
            if (random.Below(2) == 0)
            {
                data.insert(data.begin(), {0x78, 0x9c});
            }
            Rfb::OutBuffer out;
            out.U32(static_cast<uint32_t>(data.size())).Append(data.data(), data.size());
            return out.Data();
        }

        // ZRLE: a correct length and zlib stream around random tile data.
        Rfb::Bytes ZrleWithRandomTiles(Random& random)
        {
            Rfb::Bytes compressed = Compress(random.Bytes(static_cast<size_t>(random.Below(256))));
            Rfb::OutBuffer out;
            out.U32(static_cast<uint32_t>(compressed.size())).Append(compressed.data(), compressed.size());
            return out.Data();
        }

        // Tight: a basic rectangle (random stream, reset bits and filter, maybe a palette) around random pixel data.
        Rfb::Bytes TightWithRandomData(Random& random)
        {
            Rfb::OutBuffer out;
            out.U8(static_cast<uint8_t>((random.Below(8) << 4) | random.Below(16)));
            int filter = random.Below(4);
            out.U8(static_cast<uint8_t>(filter)); // only read when the control byte says a filter follows
            if (filter == 1)
            {
                int colours = 1 + random.Below(256);
                out.U8(static_cast<uint8_t>(colours - 1));
                Rfb::Bytes palette = random.Bytes(static_cast<size_t>(colours) * 3);
                out.Append(palette.data(), palette.size());
            }
            Rfb::Bytes compressed = Compress(random.Bytes(static_cast<size_t>(random.Below(800))));
            AppendCompactLength(out, compressed.size());
            out.Append(compressed.data(), compressed.size());
            return out.Data();
        }

        // Decodes `count` inputs from `makeInput` with a decoder for `encoding`. Every input must either decode or
        // throw RfbError; after an error the decoder starts afresh, as it would on a new connection.
        void FeedDecoder(const char* name, int32_t encoding, uint32_t seed, int count,
                         const std::function<Rfb::Bytes(Random&)>& makeInput)
        {
            Random random(seed);
            Rfb::Framebuffer fb;
            fb.Resize(kFramebufferSide, kFramebufferSide);
            std::unique_ptr<Rfb::Decoder> decoder = Rfb::CreateDecoder(encoding);
            int refused = 0;
            int failures = 0;
            for (int i = 0; i < count; ++i)
            {
                Rfb::Rect rect = random.Rect();
                Rfb::MemorySource source(makeInput(random));
                Rfb::InStream in(source);
                try
                {
                    decoder->Decode(in, rect, fb);
                }
                catch (const Rfb::RfbError&)
                {
                    ++refused;
                    decoder = Rfb::CreateDecoder(encoding);
                }
                catch (const std::exception& exception)
                {
                    printf("  %s input %d (seed %u): unexpected exception: %s\n", name, i, seed, exception.what());
                    ++failures;
                    decoder = Rfb::CreateDecoder(encoding);
                }
            }
            printf("  %s: %d decoded, %d refused\n", name, count - refused - failures, refused);
            CHECK(failures == 0);
        }

        // Parses `count` inputs from `makeInput`; ParseMessage must return true or false and never throw.
        void FeedDns(const char* name, uint32_t seed, int count, const std::function<Rfb::Bytes(Random&)>& makeInput)
        {
            Random random(seed);
            int parsed = 0;
            int failures = 0;
            for (int i = 0; i < count; ++i)
            {
                Rfb::Bytes message = makeInput(random);
                std::vector<Net::Dns::Record> records;
                try
                {
                    parsed += Net::Dns::ParseMessage(message.data(), message.size(), records) ? 1 : 0;
                }
                catch (const std::exception& exception)
                {
                    printf("  %s input %d (seed %u): unexpected exception: %s\n", name, i, seed, exception.what());
                    ++failures;
                }
            }
            printf("  %s: %d parsed, %d rejected\n", name, parsed, count - parsed - failures);
            CHECK(failures == 0);
        }

        // A real response with a few bytes changed and maybe cut short, so the parser sees nearly valid records.
        Rfb::Bytes DamagedDnsResponse(Random& random)
        {
            Net::Dns::Record ptr;
            ptr.name = "_rfb._tcp.local";
            ptr.type = Net::Dns::kTypePtr;
            ptr.target = "Ford._rfb._tcp.local";
            Net::Dns::Record srv;
            srv.name = "Ford._rfb._tcp.local";
            srv.type = Net::Dns::kTypeSrv;
            srv.target = "Mac.local";
            srv.port = 5900;
            Net::Dns::Record a;
            a.name = "Mac.local";
            a.type = Net::Dns::kTypeA;
            a.ipv4 = "192.168.1.20";
            Rfb::Bytes message = Net::Dns::EncodeResponse(0, {ptr, srv, a});
            for (int changes = 1 + random.Below(4); changes > 0; --changes)
            {
                message[static_cast<size_t>(random.Below(static_cast<int>(message.size())))] =
                    static_cast<uint8_t>(random.Next() >> 24);
            }
            if (random.Below(2) == 0)
            {
                message.resize(static_cast<size_t>(random.Below(static_cast<int>(message.size()) + 1)));
            }
            return message;
        }

        // A header with small counts, then random records.
        Rfb::Bytes RandomDnsRecords(Random& random)
        {
            Rfb::OutBuffer out;
            out.U16(0).U16(0x8400);
            for (int section = 0; section < 4; ++section)
            {
                out.U16(static_cast<uint16_t>(random.Below(3)));
            }
            Rfb::Bytes body = random.Bytes(static_cast<size_t>(random.Below(96)));
            out.Append(body.data(), body.size());
            return out.Data();
        }
    } // namespace

    void RunRandomInputTests()
    {
        RunTest("Decoders decode or refuse random bytes", []() {
            auto start = std::chrono::steady_clock::now();
            FeedDecoder("Raw", Rfb::Encoding::Raw, 1, kInputsPerCase, ShortRandomBytes);
            FeedDecoder("CopyRect", Rfb::Encoding::CopyRect, 2, kInputsPerCase, ShortRandomBytes);
            FeedDecoder("CopyRect, nearby sources", Rfb::Encoding::CopyRect, 3, kInputsPerCase, CopyRectNearby);
            FeedDecoder("RRE", Rfb::Encoding::Rre, 4, kInputsPerCase, ShortRandomBytes);
            FeedDecoder("RRE, few subrectangles", Rfb::Encoding::Rre, 5, kInputsPerCase, RreWithFewSubrects);
            FeedDecoder("Hextile", Rfb::Encoding::Hextile, 6, kInputsPerCase, ShortRandomBytes);
            FeedDecoder("ZRLE, random compressed data", Rfb::Encoding::Zrle, 7, kInputsPerCase, ZrleWithRandomData);
            FeedDecoder("ZRLE, random tiles", Rfb::Encoding::Zrle, 8, kInputsPerCase, ZrleWithRandomTiles);
            FeedDecoder("Tight", Rfb::Encoding::Tight, 9, kInputsPerCase, ShortRandomBytes);
            FeedDecoder("Tight, random pixel data", Rfb::Encoding::Tight, 10, kInputsPerCase, TightWithRandomData);
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            printf("  (%lld ms)\n", static_cast<long long>(elapsed.count()));
        });

        RunTest("DNS parser accepts or rejects random bytes", []() {
            auto start = std::chrono::steady_clock::now();
            FeedDns("Random bytes", 11, kInputsPerCase, [](Random& random) {
                return random.Bytes(static_cast<size_t>(random.Below(128)));
            });
            FeedDns("Random records", 12, kInputsPerCase, RandomDnsRecords);
            FeedDns("Damaged response", 13, kInputsPerCase, DamagedDnsResponse);
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            printf("  (%lld ms)\n", static_cast<long long>(elapsed.count()));
        });
    }
} // namespace MarconiTest
