// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Encoding DNS queries and parsing DNS responses (RFC 1035, with name compression), for mDNS discovery

#include <MarconiCore/Net/DnsMessage.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Marconi
{
    namespace Net
    {
        namespace Dns
        {
            namespace
            {
                constexpr uint16_t kClassIn = 1;
                constexpr int kMaxJumps = 16;
                constexpr size_t kMaxName = 255;

                void PutU16(std::vector<uint8_t>& out, uint16_t value)
                {
                    out.push_back(static_cast<uint8_t>(value >> 8));
                    out.push_back(static_cast<uint8_t>(value & 0xFF));
                }

                void PutU32(std::vector<uint8_t>& out, uint32_t value)
                {
                    PutU16(out, static_cast<uint16_t>(value >> 16));
                    PutU16(out, static_cast<uint16_t>(value & 0xFFFF));
                }

                // Length-prefixed labels, no compression.
                void PutName(std::vector<uint8_t>& out, const std::string& name)
                {
                    size_t start = 0;
                    while (start < name.size())
                    {
                        size_t dot = name.find('.', start);
                        if (dot == std::string::npos)
                        {
                            dot = name.size();
                        }
                        size_t length = std::min<size_t>(dot - start, 63);
                        out.push_back(static_cast<uint8_t>(length));
                        out.insert(out.end(), name.begin() + static_cast<ptrdiff_t>(start),
                                   name.begin() + static_cast<ptrdiff_t>(start + length));
                        start = dot + 1;
                    }
                    out.push_back(0);
                }

                void PutHeader(std::vector<uint8_t>& out, uint16_t id, uint16_t flags, size_t questions, size_t answers)
                {
                    PutU16(out, id);
                    PutU16(out, flags);
                    PutU16(out, static_cast<uint16_t>(questions));
                    PutU16(out, static_cast<uint16_t>(answers));
                    PutU16(out, 0);
                    PutU16(out, 0);
                }

                bool ParseIpv4(const std::string& text, uint8_t (&out)[4])
                {
                    unsigned parts[4] = {};
                    char extra = 0;
                    int fields = std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3],
                                             &extra);
                    if (fields != 4)
                    {
                        return false;
                    }
                    for (int i = 0; i < 4; ++i)
                    {
                        if (parts[i] > 255)
                        {
                            return false;
                        }
                        out[i] = static_cast<uint8_t>(parts[i]);
                    }
                    return true;
                }

                class Reader
                {
                public:
                    Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

                    bool U16(uint16_t& out)
                    {
                        if (m_pos + 2 > m_size)
                        {
                            return false;
                        }
                        out = static_cast<uint16_t>(m_data[m_pos] << 8 | m_data[m_pos + 1]);
                        m_pos += 2;
                        return true;
                    }

                    bool U32(uint32_t& out)
                    {
                        uint16_t high = 0;
                        uint16_t low = 0;
                        if (!U16(high) || !U16(low))
                        {
                            return false;
                        }
                        out = static_cast<uint32_t>(high) << 16 | low;
                        return true;
                    }

                    bool Skip(size_t count)
                    {
                        if (m_pos + count > m_size)
                        {
                            return false;
                        }
                        m_pos += count;
                        return true;
                    }

                    // Reads a possibly compressed name at the current position and moves past it.
                    bool Name(std::string& out)
                    {
                        out.clear();
                        size_t cursor = m_pos;
                        bool jumped = false;
                        for (int jumps = 0; jumps < kMaxJumps;)
                        {
                            if (cursor >= m_size)
                            {
                                return false;
                            }
                            uint8_t length = m_data[cursor];
                            if ((length & 0xC0) == 0xC0)
                            {
                                if (cursor + 1 >= m_size)
                                {
                                    return false;
                                }
                                size_t target = static_cast<size_t>((length & 0x3F) << 8 | m_data[cursor + 1]);
                                if (!jumped)
                                {
                                    m_pos = cursor + 2;
                                }
                                jumped = true;
                                cursor = target;
                                ++jumps;
                                continue;
                            }
                            if (length == 0)
                            {
                                if (!jumped)
                                {
                                    m_pos = cursor + 1;
                                }
                                return true;
                            }
                            if (length > 63 || cursor + 1 + length > m_size || out.size() + length + 1 > kMaxName)
                            {
                                return false;
                            }
                            if (!out.empty())
                            {
                                out += '.';
                            }
                            out.append(reinterpret_cast<const char*>(m_data + cursor + 1), length);
                            cursor += 1 + length;
                        }
                        return false; // too many jumps: a loop
                    }

                    size_t Position() const { return m_pos; }
                    void Seek(size_t pos) { m_pos = pos; }
                    const uint8_t* At(size_t pos) const { return m_data + pos; }

                private:
                    const uint8_t* m_data;
                    size_t m_size;
                    size_t m_pos = 0;
                };
            } // namespace

            std::vector<uint8_t> EncodeQuery(uint16_t id, const std::vector<Question>& questions)
            {
                std::vector<uint8_t> out;
                PutHeader(out, id, 0, questions.size(), 0);
                for (const Question& question : questions)
                {
                    PutName(out, question.name);
                    PutU16(out, question.type);
                    PutU16(out, kClassIn);
                }
                return out;
            }

            std::vector<uint8_t> EncodeResponse(uint16_t id, const std::vector<Record>& answers)
            {
                std::vector<uint8_t> out;
                PutHeader(out, id, 0x8400, 0, answers.size());
                for (const Record& record : answers)
                {
                    PutName(out, record.name);
                    PutU16(out, record.type);
                    PutU16(out, kClassIn);
                    PutU32(out, record.ttl);
                    std::vector<uint8_t> data;
                    if (record.type == kTypePtr)
                    {
                        PutName(data, record.target);
                    }
                    else if (record.type == kTypeSrv)
                    {
                        PutU16(data, 0); // priority
                        PutU16(data, 0); // weight
                        PutU16(data, record.port);
                        PutName(data, record.target);
                    }
                    else if (record.type == kTypeA)
                    {
                        uint8_t address[4] = {};
                        ParseIpv4(record.ipv4, address);
                        data.assign(address, address + 4);
                    }
                    else if (record.type == kTypeTxt)
                    {
                        data.push_back(0);
                    }
                    PutU16(out, static_cast<uint16_t>(data.size()));
                    out.insert(out.end(), data.begin(), data.end());
                }
                return out;
            }

            bool ParseMessage(const uint8_t* data, size_t size, std::vector<Record>& outRecords)
            {
                outRecords.clear();
                if (data == nullptr || size < 12)
                {
                    return false;
                }
                Reader reader(data, size);
                uint16_t header[6] = {};
                for (uint16_t& field : header)
                {
                    reader.U16(field);
                }
                std::string name;
                for (uint16_t i = 0; i < header[2]; ++i)
                {
                    if (!reader.Name(name) || !reader.Skip(4))
                    {
                        return false;
                    }
                }
                size_t recordCount = static_cast<size_t>(header[3]) + header[4] + header[5];
                std::vector<Record> records;
                for (size_t i = 0; i < recordCount; ++i)
                {
                    Record record;
                    uint16_t recordClass = 0;
                    uint16_t length = 0;
                    if (!reader.Name(record.name) || !reader.U16(record.type) || !reader.U16(recordClass) ||
                        !reader.U32(record.ttl) || !reader.U16(length))
                    {
                        return false;
                    }
                    size_t start = reader.Position();
                    if (start + length > size)
                    {
                        return false;
                    }
                    if (record.type == kTypePtr)
                    {
                        if (!reader.Name(record.target))
                        {
                            return false;
                        }
                    }
                    else if (record.type == kTypeSrv)
                    {
                        if (!reader.Skip(4) || !reader.U16(record.port) || !reader.Name(record.target))
                        {
                            return false;
                        }
                    }
                    else if (record.type == kTypeA && length == 4)
                    {
                        const uint8_t* address = reader.At(start);
                        record.ipv4 = std::to_string(address[0]) + "." + std::to_string(address[1]) + "." +
                                      std::to_string(address[2]) + "." + std::to_string(address[3]);
                    }
                    reader.Seek(start + length);
                    if (record.type == kTypeA || record.type == kTypePtr || record.type == kTypeSrv ||
                        record.type == kTypeTxt)
                    {
                        records.push_back(record);
                    }
                }
                outRecords = std::move(records);
                return true;
            }
        } // namespace Dns
    } // namespace Net
} // namespace Marconi
