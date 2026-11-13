// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the Marconi protocol's file transfer messages: each one round-trips, and the decoders refuse rubbish
// (random bytes, cut-short and corrupted real messages) with a WireError instead of crashing

#include "TestSupport.h"

#include <MarconiCore/Wire/WireFileMessages.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // Encodes, checks the envelope, decodes, checks the whole body was used.
        template <class Message>
        Message RoundTripFile(const Message& message, Wire::Type expected)
        {
            Wire::Bytes framed = message.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            CHECK(length == outer.Remaining());
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            CHECK(Wire::ReadType(body) == expected);
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

        // Runs the decoder for `type` on `body`; a WireError is the right way to refuse, anything else escapes.
        void Decode(Wire::Type type, const Wire::Bytes& body)
        {
            Wire::Reader reader(body);
            try
            {
                switch (type)
                {
                case Wire::Type::FileRequest: Wire::FileRequest::Decode(reader); break;
                case Wire::Type::FileResult: Wire::FileResult::Decode(reader); break;
                case Wire::Type::FileOpen: Wire::FileOpen::Decode(reader); break;
                case Wire::Type::FileOpened: Wire::FileOpened::Decode(reader); break;
                case Wire::Type::FileRead: Wire::FileRead::Decode(reader); break;
                case Wire::Type::FileData: Wire::FileData::Decode(reader); break;
                case Wire::Type::FileWrite: Wire::FileWrite::Decode(reader); break;
                case Wire::Type::FileClose: Wire::FileClose::Decode(reader); break;
                case Wire::Type::FileWriteFailed: Wire::FileWriteFailed::Decode(reader); break;
                default: break;
                }
            }
            catch (const Wire::WireError&)
            {
            }
        }

        // A message's body (type byte removed) from its framed bytes.
        Wire::Bytes BodyOf(const Wire::Bytes& framed)
        {
            Wire::Reader outer(framed);
            outer.Varint();
            Wire::Bytes rest(framed.end() - static_cast<ptrdiff_t>(outer.Remaining()), framed.end());
            return Wire::Bytes(rest.begin() + 1, rest.end());
        }
    } // namespace

    void RunWireFileTests()
    {
        RunTest("file requests and their answers round-trip", []() {
            Wire::FileRequest request;
            request.id = 7;
            request.op = Wire::FileRequest::Op::Rename;
            request.path = "/home/sem/a.txt";
            request.path2 = "/home/sem/b \xC3\xA9.txt";
            Wire::FileRequest requestBack = RoundTripFile(request, Wire::Type::FileRequest);
            CHECK(requestBack.id == 7 && requestBack.op == Wire::FileRequest::Op::Rename);
            CHECK(requestBack.path == request.path && requestBack.path2 == request.path2);

            Wire::FileResult listing;
            listing.id = 7;
            listing.text = "/home/sem";
            listing.entries.push_back({"a.txt", 12345678901ull, 1700000000, false});
            listing.entries.push_back({"folder", 0, -5, true});
            Wire::FileResult listingBack = RoundTripFile(listing, Wire::Type::FileResult);
            CHECK(listingBack.status == Wire::FileStatus::Ok && listingBack.text == "/home/sem");
            CHECK(listingBack.entries.size() == 2);
            CHECK(listingBack.entries[0].name == "a.txt" && listingBack.entries[0].size == 12345678901ull);
            CHECK(listingBack.entries[1].isFolder && listingBack.entries[1].modified == -5);

            Wire::FileResult failed;
            failed.id = 9;
            failed.status = Wire::FileStatus::BadPath;
            failed.error = "no";
            Wire::FileResult failedBack = RoundTripFile(failed, Wire::Type::FileResult);
            CHECK(failedBack.status == Wire::FileStatus::BadPath && failedBack.error == "no");
        });

        RunTest("file open, read, write and close messages round-trip", []() {
            Wire::FileOpen open;
            open.id = 3;
            open.mode = Wire::FileOpen::Mode::Resume;
            open.path = "/x";
            open.offset = 1ull << 40;
            Wire::FileOpen openBack = RoundTripFile(open, Wire::Type::FileOpen);
            CHECK(openBack.mode == Wire::FileOpen::Mode::Resume && openBack.offset == (1ull << 40));

            Wire::FileOpened opened;
            opened.id = 3;
            opened.handle = 11;
            opened.size = 99;
            Wire::FileOpened openedBack = RoundTripFile(opened, Wire::Type::FileOpened);
            CHECK(openedBack.handle == 11 && openedBack.size == 99);

            Wire::FileRead read;
            read.id = 4;
            read.handle = 11;
            read.offset = 5;
            read.length = 777;
            Wire::FileRead readBack = RoundTripFile(read, Wire::Type::FileRead);
            CHECK(readBack.offset == 5 && readBack.length == 777 && readBack.handle == 11);

            Wire::FileData data;
            data.id = 4;
            data.end = true;
            data.data = {1, 2, 3, 0, 255};
            Wire::FileData dataBack = RoundTripFile(data, Wire::Type::FileData);
            CHECK(dataBack.end && dataBack.data == data.data);

            Wire::FileWrite write;
            write.handle = 11;
            write.offset = 123;
            write.data = {9, 8, 7};
            Wire::FileWrite writeBack = RoundTripFile(write, Wire::Type::FileWrite);
            CHECK(writeBack.offset == 123 && writeBack.data == write.data);

            Wire::FileClose close;
            close.id = 5;
            close.handle = 11;
            Wire::FileClose closeBack = RoundTripFile(close, Wire::Type::FileClose);
            CHECK(closeBack.id == 5 && closeBack.handle == 11);

            Wire::FileWriteFailed failed;
            failed.handle = 11;
            failed.error = "disk full";
            Wire::FileWriteFailed failedBack = RoundTripFile(failed, Wire::Type::FileWriteFailed);
            CHECK(failedBack.error == "disk full" && failedBack.handle == 11);
        });

        RunTest("file messages refuse bad values", []() {
            // A range over the limit, text that isn't UTF-8, an unknown operation and a huge entry count.
            Wire::FileWrite big;
            big.data.assign(Wire::kMaxFileChunk + 1, 0);
            Wire::Bytes bigBody = BodyOf(big.Encode());
            CHECK(ThrowsWireError([&]() {
                Wire::Reader reader(bigBody);
                Wire::FileWrite::Decode(reader);
            }));

            Wire::FileRequest request;
            request.id = 1;
            request.path = std::string("\xFF\xFE", 2);
            Wire::Bytes badTextBody = BodyOf(request.Encode());
            CHECK(ThrowsWireError([&]() {
                Wire::Reader reader(badTextBody);
                Wire::FileRequest::Decode(reader);
            }));

            Wire::Bytes unknownOp = {1, 0, 0, 0, 99, 0, 0};
            CHECK(ThrowsWireError([&]() {
                Wire::Reader reader(unknownOp);
                Wire::FileRequest::Decode(reader);
            }));

            Wire::Writer w;
            w.U32(1).U8(0).String("").Varint(1000000000ull);
            Wire::Bytes hugeCount = w.Take();
            CHECK(ThrowsWireError([&]() {
                Wire::Reader reader(hugeCount);
                Wire::FileResult::Decode(reader);
            }));
        });

        RunTest("UTF-8 checking refuses overlong, surrogate and cut-off sequences", []() {
            CHECK(Wire::IsValidUtf8(""));
            CHECK(Wire::IsValidUtf8("plain"));
            CHECK(Wire::IsValidUtf8("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"));
            CHECK(!Wire::IsValidUtf8("\xC0\x80"));         // overlong NUL
            CHECK(!Wire::IsValidUtf8("\xED\xA0\x80"));     // a surrogate
            CHECK(!Wire::IsValidUtf8("\xF4\x90\x80\x80")); // past U+10FFFF
            CHECK(!Wire::IsValidUtf8("abc\xE2\x82"));      // cut off
            CHECK(!Wire::IsValidUtf8("\x80"));             // a stray continuation byte
        });

        RunTest("random and damaged file messages are decoded or refused, never crash", []() {
            Random random(0xF11E5u);
            const Wire::Type types[] = {Wire::Type::FileRequest, Wire::Type::FileResult, Wire::Type::FileOpen,
                                        Wire::Type::FileOpened,  Wire::Type::FileRead,   Wire::Type::FileData,
                                        Wire::Type::FileWrite,   Wire::Type::FileClose,  Wire::Type::FileWriteFailed};
            for (Wire::Type type : types)
            {
                for (int i = 0; i < 3000; ++i)
                {
                    Wire::Bytes body(random.Next() % 80);
                    for (uint8_t& byte : body)
                    {
                        byte = static_cast<uint8_t>(random.Next() >> 24);
                    }
                    Decode(type, body);
                }
            }

            // Real messages, cut short at every length and with a byte changed at every position.
            Wire::FileResult listing;
            listing.id = 1;
            listing.text = "/h";
            listing.entries.push_back({"a", 1, 2, false});
            listing.entries.push_back({"b\xC3\xA9", 3, 4, true});
            Wire::FileData data;
            data.id = 2;
            data.data = {1, 2, 3, 4};
            Wire::FileRequest request;
            request.id = 3;
            request.path = "/a/b";
            request.path2 = "/c";
            const std::pair<Wire::Type, Wire::Bytes> samples[] = {{Wire::Type::FileResult, BodyOf(listing.Encode())},
                                                                  {Wire::Type::FileData, BodyOf(data.Encode())},
                                                                  {Wire::Type::FileRequest, BodyOf(request.Encode())}};
            for (const auto& sample : samples)
            {
                for (size_t cut = 0; cut <= sample.second.size(); ++cut)
                {
                    Decode(sample.first, Wire::Bytes(sample.second.begin(), sample.second.begin() + cut));
                }
                for (size_t at = 0; at < sample.second.size(); ++at)
                {
                    for (int k = 0; k < 20; ++k)
                    {
                        Wire::Bytes damaged = sample.second;
                        damaged[at] = static_cast<uint8_t>(random.Next() >> 24);
                        Decode(sample.first, damaged);
                    }
                }
            }
            CHECK(true);
        });
    }
} // namespace MarconiTest
