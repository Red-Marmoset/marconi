// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for QuickConnect: which client and options a typed address gets in the phone and tablet apps

#include "TestSupport.h"

#include <MarconiCore/QuickConnect.h>

#include <string>

namespace MarconiTest
{
    namespace
    {
        Marconi::Address Parse(const std::string& text)
        {
            Marconi::Address address;
            std::string error;
            CHECK(Marconi::ParseAddress(text, address, error));
            return address;
        }

        class CountingListener : public Marconi::SessionListener
        {
        };
    } // namespace

    void RunQuickConnectTests()
    {
        RunTest("QuickConnect: a bare host is VNC on its usual port", []() {
            Marconi::QuickConnectOptions options;
            options.password = "secret";
            Marconi::Rfb::ConnectionOptions vnc = Marconi::QuickConnectVncOptions(Parse("studio-pc"), options);
            CHECK(vnc.host == "studio-pc");
            CHECK(vnc.port == 5900);
            CHECK(vnc.credentials.password == "secret");
            CHECK(vnc.credentials.username.empty());
            CHECK(!vnc.sshTunnel);
        });

        RunTest("QuickConnect: a display number and a user name in the address", []() {
            Marconi::QuickConnectOptions options;
            Marconi::Rfb::ConnectionOptions vnc = Marconi::QuickConnectVncOptions(Parse("sem@studio-pc:1"), options);
            CHECK(vnc.port == 5901);
            CHECK(vnc.credentials.username == "sem");
        });

        RunTest("QuickConnect: ssh:// goes through the SSH server to the usual VNC port", []() {
            Marconi::QuickConnectOptions options;
            Marconi::Rfb::ConnectionOptions vnc = Marconi::QuickConnectVncOptions(Parse("ssh://sem@studio-pc"), options);
            CHECK(vnc.sshTunnel);
            CHECK(vnc.sshPort == 22);
            CHECK(vnc.port == 5900);
            CHECK(vnc.credentials.username == "sem");
            vnc = Marconi::QuickConnectVncOptions(Parse("ssh://studio-pc:2222"), options);
            CHECK(vnc.sshPort == 2222);
            CHECK(vnc.port == 5900);
        });

        RunTest("QuickConnect: marconi:// options carry the password, name and trusted certificate", []() {
            Marconi::QuickConnectOptions options;
            options.password = "pw";
            options.clientName = "Marconi for Android";
            options.trustedCertificateSha256 = {1, 2, 3};
            Marconi::Wire::ClientOptions wire = Marconi::QuickConnectMarconiOptions(Parse("marconi://studio-pc"), options);
            CHECK(wire.host == "studio-pc");
            CHECK(wire.port == 1895);
            CHECK(wire.password == "pw");
            CHECK(wire.clientName == "Marconi for Android");
            CHECK(wire.trustedCertificateSha256.size() == 3);
            wire = Marconi::QuickConnectMarconiOptions(Parse("marconi://studio-pc:2000"), options);
            CHECK(wire.port == 2000);
        });

        RunTest("QuickConnect: addresses that can't be used say why", []() {
            CountingListener listener;
            Marconi::QuickConnectOptions options;
            std::string error;
            options.address = "";
            CHECK(Marconi::QuickConnect(options, listener, error) == nullptr);
            CHECK(!error.empty());
            options.address = "rdp://studio-pc";
            CHECK(Marconi::QuickConnect(options, listener, error) == nullptr);
            CHECK(error.find("Remote Desktop") != std::string::npos);
        });

        RunTest("QuickConnect: a VNC address starts a VNC session that can be disconnected", []() {
            // 127.0.0.1 on a port nothing listens on: the connection fails on the worker thread and the session
            // reports it, which is what the apps rely on. Disconnect waits for that thread.
            CountingListener listener;
            Marconi::QuickConnectOptions options;
            options.address = "vnc://127.0.0.1::1";
            std::string error;
            std::unique_ptr<Marconi::RemoteSession> session = Marconi::QuickConnect(options, listener, error);
            CHECK(session != nullptr);
            CHECK(error.empty());
            session->Disconnect();
            CHECK(!session->IsConnected());
        });
    }
} // namespace MarconiTest
