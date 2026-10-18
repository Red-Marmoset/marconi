// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finds machines on the local network that advertise Screen Sharing (VNC), RDP or SSH over mDNS

#include <MarconiCore/Net/Discovery.h>

#include <MarconiCore/Net/UdpSocket.h>

#include <Log/Log.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>

namespace Marconi
{
    namespace Net
    {
        namespace
        {
            struct ServiceType
            {
                const char* name;
                Protocol protocol;
            };
            constexpr ServiceType kServiceTypes[] = {{"_marconi._tcp.local", Protocol::Marconi},
                                                     {"_rfb._tcp.local", Protocol::Vnc},
                                                     {"_rdp._tcp.local", Protocol::Rdp},
                                                     {"_ssh._tcp.local", Protocol::Ssh}};

            constexpr int kFollowUpMs = 300;
            constexpr int kSliceMs = 100;

            using Clock = std::chrono::steady_clock;

            bool SameName(const std::string& a, const std::string& b)
            {
                auto lower = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
                return a.size() == b.size() &&
                       std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return lower(x) == lower(y); });
            }

            const Dns::Record* FindRecord(const std::vector<Dns::Record>& records, uint16_t type,
                                          const std::string& name)
            {
                for (const Dns::Record& record : records)
                {
                    if (record.type == type && SameName(record.name, name))
                    {
                        return &record;
                    }
                }
                return nullptr;
            }

            bool Send(UdpSocket& socket, const std::string& destination, uint16_t port,
                      const std::vector<Dns::Question>& questions)
            {
                std::vector<uint8_t> query = Dns::EncodeQuery(0, questions);
                return socket.SendTo(destination, port, query.data(), query.size());
            }

            // Collects records from every packet that arrives within `listenMs`.
            void Listen(UdpSocket& socket, int listenMs, const std::atomic<bool>* cancel,
                        std::vector<Dns::Record>& records)
            {
                Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(listenMs);
                uint8_t buffer[9000];
                while (cancel == nullptr || !cancel->load())
                {
                    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                    if (left <= 0)
                    {
                        break;
                    }
                    int slice = static_cast<int>(std::min<long long>(left, kSliceMs));
                    ptrdiff_t got = socket.ReceiveFrom(buffer, sizeof(buffer), slice);
                    if (got < 0)
                    {
                        break;
                    }
                    std::vector<Dns::Record> packet;
                    if (got > 0 && Dns::ParseMessage(buffer, static_cast<size_t>(got), packet))
                    {
                        records.insert(records.end(), packet.begin(), packet.end());
                    }
                }
            }

            bool IsOurServiceType(const std::string& name)
            {
                return std::any_of(std::begin(kServiceTypes), std::end(kServiceTypes),
                                   [&name](const ServiceType& type) { return SameName(name, type.name); });
            }

            // SRV questions for advertised instances with no SRV record, and A questions for hosts with no address.
            std::vector<Dns::Question> MissingQuestions(const std::vector<Dns::Record>& records)
            {
                std::vector<Dns::Question> questions;
                for (const Dns::Record& record : records)
                {
                    if (record.type == Dns::kTypePtr && IsOurServiceType(record.name))
                    {
                        if (FindRecord(records, Dns::kTypeSrv, record.target) == nullptr)
                        {
                            questions.push_back({record.target, Dns::kTypeSrv});
                        }
                    }
                    else if (record.type == Dns::kTypeSrv && FindRecord(records, Dns::kTypeA, record.target) == nullptr)
                    {
                        questions.push_back({record.target, Dns::kTypeA});
                    }
                }
                std::sort(questions.begin(), questions.end(), [](const Dns::Question& a, const Dns::Question& b) {
                    return a.name < b.name || (a.name == b.name && a.type < b.type);
                });
                questions.erase(std::unique(questions.begin(), questions.end(),
                                            [](const Dns::Question& a, const Dns::Question& b) {
                                                return a.name == b.name && a.type == b.type;
                                            }),
                                questions.end());
                return questions;
            }
        } // namespace

        std::vector<DiscoveredService> AssembleServices(const std::vector<Dns::Record>& records)
        {
            std::vector<DiscoveredService> services;
            std::vector<std::string> instanceNames; // the full "Ford._rfb._tcp.local", parallel to `services`
            for (const Dns::Record& record : records)
            {
                if (record.type != Dns::kTypePtr)
                {
                    continue;
                }
                const ServiceType* type = std::find_if(
                    std::begin(kServiceTypes), std::end(kServiceTypes),
                    [&record](const ServiceType& candidate) { return SameName(record.name, candidate.name); });
                bool seen = std::any_of(instanceNames.begin(), instanceNames.end(),
                                        [&record](const std::string& name) { return SameName(name, record.target); });
                if (type == std::end(kServiceTypes) || seen || record.target.empty())
                {
                    continue;
                }
                DiscoveredService service;
                service.instance = record.target.substr(0, record.target.find('.'));
                service.protocol = type->protocol;
                if (const Dns::Record* srv = FindRecord(records, Dns::kTypeSrv, record.target))
                {
                    service.port = srv->port;
                    service.hostName = srv->target;
                }
                if (const Dns::Record* a = FindRecord(records, Dns::kTypeA, service.hostName))
                {
                    service.ipv4 = a->ipv4;
                }
                services.push_back(service);
                instanceNames.push_back(record.target);
            }
            return services;
        }

        std::vector<DiscoveredService> Discover(int listenMs, const std::string& destination, uint16_t destinationPort,
                                                const std::atomic<bool>* cancel)
        {
            UdpSocket socket;
            std::string error;
            if (!socket.Open(error))
            {
                LOG_INFO("Discovery", "Can't search the network: %s", error.c_str());
                return {};
            }
            std::vector<Dns::Question> questions;
            for (const ServiceType& type : kServiceTypes)
            {
                questions.push_back({type.name, Dns::kTypePtr});
            }
            if (!Send(socket, destination, destinationPort, questions))
            {
                LOG_INFO("Discovery", "Can't send an mDNS query to %s", destination.c_str());
                return {};
            }
            std::vector<Dns::Record> records;
            Listen(socket, listenMs, cancel, records);

            // Some responders leave out the SRV or A records: ask for what's missing (an SRV answer can name a host
            // whose address is missing too, hence two rounds at most).
            for (int round = 0; round < 2 && (cancel == nullptr || !cancel->load()); ++round)
            {
                std::vector<Dns::Question> missing = MissingQuestions(records);
                if (missing.empty() || !Send(socket, destination, destinationPort, missing))
                {
                    break;
                }
                Listen(socket, kFollowUpMs, cancel, records);
            }
            return AssembleServices(records);
        }
    } // namespace Net
} // namespace Marconi
