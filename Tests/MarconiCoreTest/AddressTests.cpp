// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for VNC address parsing

#include "TestSupport.h"

#include <MarconiCore/Address.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        bool Parses(const std::string& text, const std::string& host, uint16_t port, Protocol protocol)
        {
            Address address;
            std::string error;
            if (!ParseAddress(text, address, error))
            {
                printf("  '%s' failed: %s\n", text.c_str(), error.c_str());
                return false;
            }
            bool ok = address.host == host && address.port == port && address.protocol == protocol;
            if (!ok)
            {
                printf("  '%s' gave host '%s' port %u protocol %d\n", text.c_str(), address.host.c_str(),
                       address.port, static_cast<int>(address.protocol));
            }
            return ok;
        }

        bool Rejects(const std::string& text)
        {
            Address address;
            std::string error;
            return !ParseAddress(text, address, error) && !error.empty();
        }

        bool RoundTrips(const std::string& text)
        {
            Address first;
            Address second;
            std::string error;
            if (!ParseAddress(text, first, error) || !ParseAddress(FormatAddress(first), second, error))
            {
                return false;
            }
            return first.host == second.host && EffectivePort(first) == EffectivePort(second) &&
                   first.protocol == second.protocol &&
                   first.username == second.username;
        }
    } // namespace

    void RunAddressTests()
    {
        RunTest("user@host addresses", []() {
            Address a;
            std::string error;
            CHECK(ParseAddress("alice@studio-mac.local", a, error));
            CHECK(a.username == "alice" && a.host == "studio-mac.local" && a.port == 0);
            CHECK(a.protocol == Protocol::Auto);
            CHECK(FormatAddress(a) == "alice@studio-mac.local");
            CHECK(ParseAddress("vnc://Jane Doe@[fe80::1]:5901", a, error));
            CHECK(a.username == "Jane Doe" && a.host == "fe80::1" && a.port == 5901 && a.protocol == Protocol::Vnc);
            CHECK(FormatAddress(a) == "Jane Doe@[fe80::1]:1");
            CHECK(ParseAddress("me@host:1", a, error) && a.port == 5901 && a.protocol == Protocol::Vnc);
            CHECK(ParseAddress("ssh://me@box", a, error) && a.username == "me" && a.protocol == Protocol::Ssh);
            CHECK(!ParseAddress("@host", a, error));
            CHECK(!ParseAddress("me@", a, error));
            CHECK(ParseAddress("host", a, error) && a.username.empty());
        });

        RunTest("ParseAddress", []() {
            CHECK(Parses("myhost", "myhost", 0, Protocol::Auto));
            CHECK(Parses("  myhost.local  ", "myhost.local", 0, Protocol::Auto));
            CHECK(Parses("myhost:1", "myhost", 5901, Protocol::Vnc));
            CHECK(Parses("myhost:99", "myhost", 5999, Protocol::Vnc));
            CHECK(Parses("myhost:5905", "myhost", 5905, Protocol::Auto));
            CHECK(Parses("myhost::1", "myhost", 1, Protocol::Auto));
            CHECK(Parses("myhost::2222", "myhost", 2222, Protocol::Auto));
            CHECK(Parses("192.168.1.10:2", "192.168.1.10", 5902, Protocol::Vnc));
            CHECK(Parses("[::1]", "::1", 0, Protocol::Auto));
            CHECK(Parses("[fe80::1]:3", "fe80::1", 5903, Protocol::Vnc));
            CHECK(Parses("[fe80::1]::6000", "fe80::1", 6000, Protocol::Auto));
            CHECK(Parses("fe80::1:2", "fe80::1:2", 0, Protocol::Auto));
            CHECK(Parses("vnc://mac.local", "mac.local", 0, Protocol::Vnc));
            CHECK(Parses("VNC://mac.local:5", "mac.local", 5, Protocol::Vnc));
            CHECK(Parses("vnc://mac.local:5901/", "mac.local", 5901, Protocol::Vnc));
            CHECK(Parses("rdp://drips", "drips", 0, Protocol::Rdp));
            CHECK(Parses("RDP://drips:3390", "drips", 3390, Protocol::Rdp));
            CHECK(Parses("ssh://box", "box", 0, Protocol::Ssh));
            CHECK(Parses("ssh://box:2222", "box", 2222, Protocol::Ssh));

            CHECK(Rejects(""));
            CHECK(Rejects("   "));
            CHECK(Rejects(":1"));
            CHECK(Rejects("host:abc"));
            CHECK(Rejects("host::70000"));
            CHECK(Rejects("host::0"));
            CHECK(Rejects("[::1"));
            CHECK(Rejects("[::1]x"));
            CHECK(Rejects("my host"));
            CHECK(Rejects("rdp://"));
        });

        RunTest("FormatAddress", []() {
            CHECK(FormatAddress({"host"}) == "host");
            CHECK(FormatAddress({"host", 5900}) == "host::5900");
            CHECK(FormatAddress({"host", 2222}) == "host::2222");
            CHECK(FormatAddress({"host", 5901, "", Protocol::Vnc}) == "host:1");
            CHECK(FormatAddress({"host", 0, "", Protocol::Vnc}) == "vnc://host");
            CHECK(FormatAddress({"host", 5900, "", Protocol::Vnc}) == "vnc://host");
            CHECK(FormatAddress({"host", 6000, "", Protocol::Vnc}) == "vnc://host:6000");
            CHECK(FormatAddress({"host", 3389, "", Protocol::Rdp}) == "rdp://host");
            CHECK(FormatAddress({"host", 3390, "me", Protocol::Rdp}) == "rdp://me@host:3390");
            CHECK(FormatAddress({"::1"}) == "[::1]");
            CHECK(FormatAddress({"::1", 6000}) == "[::1]::6000");
            for (const char* text : {"host", "host:1", "host::2222", "vnc://h", "vnc://h:6000", "rdp://u@h",
                                     "ssh://h:22", "[fe80::1]:3", "a@[::1]::6000"})
            {
                CHECK(RoundTrips(text));
            }
        });

        RunTest("DefaultPort and EffectivePort", []() {
            CHECK(DefaultPort(Protocol::Vnc) == 5900 && DefaultPort(Protocol::Rdp) == 3389);
            CHECK(DefaultPort(Protocol::Ssh) == 22 && DefaultPort(Protocol::Auto) == 0);
            CHECK(EffectivePort({"h"}) == 5900);
            CHECK(EffectivePort({"h", 0, "", Protocol::Rdp}) == 3389);
            CHECK(EffectivePort({"h", 7000}) == 7000);
        });
    }
} // namespace MarconiTest
