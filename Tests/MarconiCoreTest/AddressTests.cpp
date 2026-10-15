// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for VNC address parsing

#include "TestSupport.h"

#include <MarconiCore/Address.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        bool Parses(const std::string& text, const std::string& host, uint16_t port)
        {
            Address address;
            std::string error;
            if (!ParseAddress(text, address, error))
            {
                printf("  '%s' failed: %s\n", text.c_str(), error.c_str());
                return false;
            }
            return address.host == host && address.port == port;
        }

        bool Rejects(const std::string& text)
        {
            Address address;
            std::string error;
            return !ParseAddress(text, address, error) && !error.empty();
        }
    } // namespace

    void RunAddressTests()
    {
        RunTest("user@host addresses", []() {
            Address a;
            std::string error;
            CHECK(ParseAddress("alice@studio-mac.local", a, error));
            CHECK(a.username == "alice" && a.host == "studio-mac.local" && a.port == 5900);
            CHECK(FormatAddress(a) == "alice@studio-mac.local");
            CHECK(ParseAddress("vnc://Jane Doe@[fe80::1]:5901", a, error));
            CHECK(a.username == "Jane Doe" && a.host == "fe80::1" && a.port == 5901);
            CHECK(FormatAddress(a) == "Jane Doe@[fe80::1]::5901");
            CHECK(ParseAddress("me@host:1", a, error) && a.port == 5901);
            CHECK(!ParseAddress("@host", a, error));
            CHECK(!ParseAddress("me@", a, error));
            CHECK(ParseAddress("host", a, error) && a.username.empty());
        });

        RunTest("ParseAddress", []() {
            CHECK(Parses("myhost", "myhost", 5900));
            CHECK(Parses("  myhost.local  ", "myhost.local", 5900));
            CHECK(Parses("myhost:1", "myhost", 5901));
            CHECK(Parses("myhost:99", "myhost", 5999));
            CHECK(Parses("myhost:5905", "myhost", 5905));
            CHECK(Parses("myhost::1", "myhost", 1));
            CHECK(Parses("myhost::5900", "myhost", 5900));
            CHECK(Parses("192.168.1.10:2", "192.168.1.10", 5902));
            CHECK(Parses("[::1]", "::1", 5900));
            CHECK(Parses("[fe80::1]:3", "fe80::1", 5903));
            CHECK(Parses("[fe80::1]::6000", "fe80::1", 6000));
            CHECK(Parses("fe80::1:2", "fe80::1:2", 5900));
            CHECK(Parses("vnc://mac.local", "mac.local", 5900));
            CHECK(Parses("VNC://mac.local:5", "mac.local", 5));
            CHECK(Parses("vnc://mac.local:5901/", "mac.local", 5901));

            CHECK(Rejects(""));
            CHECK(Rejects("   "));
            CHECK(Rejects(":1"));
            CHECK(Rejects("host:abc"));
            CHECK(Rejects("host::70000"));
            CHECK(Rejects("host::0"));
            CHECK(Rejects("[::1"));
            CHECK(Rejects("[::1]x"));
            CHECK(Rejects("my host"));
        });

        RunTest("FormatAddress", []() {
            CHECK(FormatAddress({"host", 5900}) == "host");
            CHECK(FormatAddress({"host", 5901}) == "host::5901");
            CHECK(FormatAddress({"::1", 5900}) == "[::1]");
            CHECK(FormatAddress({"::1", 6000}) == "[::1]::6000");
        });
    }
} // namespace MarconiTest
