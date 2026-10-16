// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turns probe results into a decision: which protocol to connect with, or what to tell the user

#include <MarconiCore/Net/ProtocolChoice.h>

#include <algorithm>
#include <string>

namespace Marconi
{
    namespace Net
    {
        namespace
        {
            int Rank(Protocol protocol)
            {
                switch (protocol)
                {
                    case Protocol::Rdp: return 0;
                    case Protocol::Vnc: return 1;
                    case Protocol::Ssh: return 2;
                    case Protocol::Auto: break;
                }
                return 3;
            }
        } // namespace

        bool IsSupported(Protocol protocol)
        {
            return protocol == Protocol::Vnc;
        }

        Choice ChooseProtocol(const Address& address, const std::vector<ProbeResult>& results)
        {
            Choice choice;
            for (const ProbeResult& result : results)
            {
                if (result.reachable && (address.protocol == Protocol::Auto || result.protocol == address.protocol))
                {
                    choice.found.push_back(result);
                }
            }
            std::stable_sort(choice.found.begin(), choice.found.end(), [](const ProbeResult& a, const ProbeResult& b) {
                return Rank(a.protocol) < Rank(b.protocol);
            });
            for (const ProbeResult& result : choice.found)
            {
                if (IsSupported(result.protocol))
                {
                    choice.kind = Choice::Kind::Connect;
                    choice.protocol = result.protocol;
                    choice.port = result.port;
                    return choice;
                }
            }
            bool anyKnown = std::any_of(choice.found.begin(), choice.found.end(),
                                        [](const ProbeResult& r) { return r.protocol != Protocol::Auto; });
            choice.kind = anyKnown ? Choice::Kind::Unsupported : Choice::Kind::NothingFound;
            if (choice.found.empty() && !results.empty() &&
                std::all_of(results.begin(), results.end(), [](const ProbeResult& r) {
                    return r.detail.compare(0, 17, "Could not resolve") == 0;
                }))
            {
                choice.error = results.front().detail; // the spec: a name that can't be resolved keeps its own error
            }
            return choice;
        }

        std::string DescribeChoice(const Choice& choice, const Address& address)
        {
            const std::string& host = address.host;
            switch (choice.kind)
            {
                case Choice::Kind::Connect:
                    return "";
                case Choice::Kind::Unsupported:
                    if (choice.found.front().protocol == Protocol::Ssh)
                    {
                        return host + " only offers SSH (a text terminal), which Marconi doesn't support yet.";
                    }
                    return host + " offers " + ProtocolName(choice.found.front().protocol) +
                           ", which Marconi doesn't support yet.";
                case Choice::Kind::NothingFound:
                    break;
            }
            if (!choice.error.empty())
            {
                return choice.error;
            }
            if (!choice.found.empty())
            {
                return "Something is listening on port " + std::to_string(choice.found.front().port) + " of " + host +
                       ", but it isn't a remote desktop Marconi recognises.";
            }
            if (address.protocol == Protocol::Auto && address.port == 0)
            {
                return "Couldn't find a remote desktop on " + host +
                       ". Tried VNC (5900), Remote Desktop (3389) and SSH (22).";
            }
            uint16_t port = address.port != 0 ? address.port : DefaultPort(address.protocol);
            return "Couldn't find a remote desktop on " + host + " (port " + std::to_string(port) + ").";
        }
    } // namespace Net
} // namespace Marconi
