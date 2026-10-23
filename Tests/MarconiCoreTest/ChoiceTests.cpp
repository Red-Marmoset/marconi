// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for ChooseProtocol and its user-facing messages

#include "TestSupport.h"

#include <MarconiCore/Net/ProtocolChoice.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        Net::ProbeResult Found(uint16_t port, Protocol protocol)
        {
            Net::ProbeResult result;
            result.port = port;
            result.reachable = true;
            result.protocol = protocol;
            return result;
        }

        Net::ProbeResult Missing(uint16_t port)
        {
            Net::ProbeResult result;
            result.port = port;
            result.detail = "refused";
            return result;
        }
    } // namespace

    void RunChoiceTests()
    {
        RunTest("ChooseProtocol picks the best supported protocol", []() {
            Address host{"box"};
            Net::Choice c =
                Net::ChooseProtocol(host, {Found(5900, Protocol::Vnc), Missing(3389), Found(22, Protocol::Ssh)});
            CHECK(c.kind == Net::Choice::Kind::Connect && c.protocol == Protocol::Vnc && c.port == 5900);
            // A computer offering both Remote Desktop and VNC: Remote Desktop ranks higher.
            c = Net::ChooseProtocol(host, {Found(5900, Protocol::Vnc), Found(3389, Protocol::Rdp)});
            CHECK(c.kind == Net::Choice::Kind::Connect && c.protocol == Protocol::Rdp && c.port == 3389);
            CHECK(c.found.size() == 2 && c.found[0].protocol == Protocol::Rdp); // best first
            // VNC on another display (snorkels: 5901) is found, and the usual port wins a tie.
            c = Net::ChooseProtocol(host, {Missing(5900), Found(3389, Protocol::Rdp), Found(5901, Protocol::Vnc)});
            CHECK(c.protocol == Protocol::Rdp && c.fallbacks.size() == 1 && c.fallbacks[0].port == 5901);
            c = Net::ChooseProtocol(host, {Found(5900, Protocol::Vnc), Found(5901, Protocol::Vnc)});
            CHECK(c.port == 5900 && c.fallbacks.size() == 1 && c.fallbacks[0].port == 5901);
            c = Net::ChooseProtocol(host, {Found(5900, Protocol::Vnc), Found(3389, Protocol::Rdp)});
            // VNC is kept to try if Remote Desktop can't connect.
            CHECK(c.fallbacks.size() == 1 && c.fallbacks[0].protocol == Protocol::Vnc && c.fallbacks[0].port == 5900);
            // Remote Desktop on its own (a Windows PC, or a Linux desktop sharing its screen that way) connects.
            c = Net::ChooseProtocol(host, {Missing(5900), Found(3389, Protocol::Rdp), Found(22, Protocol::Ssh)});
            CHECK(c.kind == Net::Choice::Kind::Connect && c.protocol == Protocol::Rdp && c.port == 3389);
            CHECK(c.fallbacks.empty()); // SSH can't be a fallback
            // Everything supported is a fallback, best first.
            c = Net::ChooseProtocol(host, {Found(5900, Protocol::Vnc), Found(3389, Protocol::Rdp),
                                           Found(1895, Protocol::Marconi)});
            CHECK(c.protocol == Protocol::Marconi && c.fallbacks.size() == 2 &&
                  c.fallbacks[0].protocol == Protocol::Rdp && c.fallbacks[1].protocol == Protocol::Vnc);
            // A VNC server on an odd port, identified by greeting.
            c = Net::ChooseProtocol({"box", 5905}, {Found(5905, Protocol::Vnc)});
            CHECK(c.kind == Net::Choice::Kind::Connect && c.port == 5905);
        });

        RunTest("ChooseProtocol reports unsupported and missing services", []() {
            Address host{"drips"};
            Net::Choice c = Net::ChooseProtocol(host, {Missing(5900), Missing(3389), Found(22, Protocol::Ssh)});
            CHECK(c.kind == Net::Choice::Kind::Unsupported);
            CHECK(Net::DescribeChoice(c, host) ==
                  "drips only offers SSH (a text terminal), which Marconi doesn't support yet.");
            c = Net::ChooseProtocol(host, {Missing(5900), Missing(3389), Missing(22)});
            CHECK(c.kind == Net::Choice::Kind::NothingFound);
            CHECK(Net::DescribeChoice(c, host) ==
                  "Couldn't find a remote desktop on drips. Tried Marconi (1895), VNC (5900 to 5910), Remote Desktop "
                  "(3389) and SSH (22).");
            c = Net::ChooseProtocol(host, {Found(7000, Protocol::Auto), Found(22, Protocol::Ssh)});
            CHECK(c.kind == Net::Choice::Kind::Unsupported && c.found.front().protocol == Protocol::Ssh);
            Net::ProbeResult unresolved = Missing(5900);
            unresolved.detail = "Could not resolve 'nope': No such host is known";
            c = Net::ChooseProtocol({"nope"}, {unresolved, unresolved, unresolved});
            CHECK(c.kind == Net::Choice::Kind::NothingFound);
            CHECK(Net::DescribeChoice(c, {"nope"}) == "Could not resolve 'nope': No such host is known");
            c = Net::ChooseProtocol({"drips", 7000}, {Found(7000, Protocol::Auto)});
            CHECK(c.kind == Net::Choice::Kind::NothingFound);
            CHECK(Net::DescribeChoice(c, {"drips", 7000}) ==
                  "Something is listening on port 7000 of drips, but it isn't a remote desktop Marconi recognises.");
        });

        RunTest("ChooseProtocol prefers a Marconi server", []() {
            Address studio{"studio"};
            // A box running both Marconi and VNC: Marconi wins, since it is the better protocol.
            Net::Choice both = Net::ChooseProtocol(studio, {Found(5900, Protocol::Vnc), Found(1895, Protocol::Marconi)});
            CHECK(both.kind == Net::Choice::Kind::Connect && both.protocol == Protocol::Marconi && both.port == 1895);
            CHECK(both.found.front().protocol == Protocol::Marconi); // best first
            // A box running only a Marconi server connects over Marconi.
            Net::Choice only = Net::ChooseProtocol(studio, {Found(1895, Protocol::Marconi), Missing(5900)});
            CHECK(only.kind == Net::Choice::Kind::Connect && only.protocol == Protocol::Marconi);
        });

        RunTest("ChooseProtocol honours a forced protocol", []() {
            Address forced{"box", 0, "", Protocol::Ssh};
            Net::Choice c = Net::ChooseProtocol(forced, {Found(22, Protocol::Ssh)});
            CHECK(c.kind == Net::Choice::Kind::Unsupported);
            Address vnc{"box", 0, "", Protocol::Vnc};
            c = Net::ChooseProtocol(vnc, {Found(5900, Protocol::Vnc)});
            CHECK(c.kind == Net::Choice::Kind::Connect && c.port == 5900);
        });
    }
} // namespace MarconiTest
