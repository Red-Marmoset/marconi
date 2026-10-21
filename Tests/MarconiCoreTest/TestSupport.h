// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Shared helpers for MarconiCoreTest: check macros, a scripted fake RFB server and a recording listener

#pragma once

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Rfb/Client.h>
#include <MarconiCore/Rfb/Stream.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MarconiTest
{
    extern int g_passed;
    extern int g_failed;

    void Check(bool condition, const char* what, const char* file, int line);

#define CHECK(cond) ::MarconiTest::Check((cond), #cond, __FILE__, __LINE__)

    // Runs `body`, catching exceptions so one broken test doesn't stop the rest.
    void RunTest(const char* name, const std::function<void()>& body);

    // True if `body` throws an RfbError whose message contains `messagePart`; prints what happened otherwise.
    // Any other exception escapes, so RunTest reports it as a failure.
    bool ThrowsRfbError(const std::function<void()>& body, const std::string& messagePart);

    // True if `body` throws a Wire::WireError (the Marconi protocol's parse error); prints what happened otherwise.
    bool ThrowsWireError(const std::function<void()>& body);

    // A loopback server for one client. The test drives it synchronously while the client runs its own thread.
    class FakeServer : public Marconi::Rfb::ByteSink
    {
    public:
        FakeServer();

        uint16_t GetPort() const { return m_port; }
        bool AcceptClient(int timeoutMs = 5000);

        Marconi::Rfb::InStream& In() { return *m_in; }
        void Send(const Marconi::Rfb::Bytes& data) override;
        void Send(const Marconi::Rfb::OutBuffer& buffer) { Send(buffer.Data()); }
        void SendString(const std::string& text);
        void Close();

        Marconi::Net::Socket& GetClientSocket() { return m_client; }
        // From here on reads come through `source` and Send goes through `send` instead of the socket directly
        // (the server side of a fake encrypted stream). Nothing may be buffered in In() at this point.
        void Reroute(std::unique_ptr<Marconi::Rfb::ByteSource> source,
                     std::function<void(const Marconi::Rfb::Bytes&)> send);

        // Standard happy-path setup up to ServerInit for the given version banner and security type None.
        void HandshakeNone(const std::string& banner, int width, int height, const std::string& name);
        // Reads the client's SetPixelFormat + SetEncodings + first FramebufferUpdateRequest.
        std::vector<int32_t> ReadClientSetup();

    private:
        Marconi::Net::Socket m_listener;
        Marconi::Net::Socket m_client;
        std::unique_ptr<Marconi::Rfb::ByteSource> m_source;
        std::unique_ptr<Marconi::Rfb::InStream> m_in;
        std::function<void(const Marconi::Rfb::Bytes&)> m_send; // empty: straight to the socket
        uint16_t m_port = 0;
    };

    class RecordingListener : public Marconi::Rfb::ClientListener
    {
    public:
        void OnConnected(const Marconi::Rfb::ServerInit& serverInit) override;
        void OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects) override;
        void OnDesktopResized(int width, int height) override;
        void OnScreenLayout(const std::vector<Marconi::Rfb::Screen>& screens) override;
        void OnDesktopResizeRefused(int status) override;
        void OnServerCutText(const std::string& text) override;
        void OnServerClipboard(const Marconi::Rfb::ClipboardContent& content) override;
        void OnBell() override;
        void OnCursorShape(const Marconi::Rfb::CursorShape& cursor) override;
        void OnDisconnected(const Marconi::Rfb::DisconnectInfo& info) override;
        bool OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials) override;
        bool OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate) override;
        bool OnUntrustedSshHostKey(const Marconi::Net::SshHostKeyQuestion& question) override;

        // Waits until `predicate` (evaluated under the lock) holds. Returns false on timeout.
        bool WaitFor(const std::function<bool()>& predicate, int timeoutMs = 5000);

        std::mutex mutex;
        std::condition_variable changed;
        bool connected = false;
        bool disconnected = false;
        std::string disconnectError;
        bool authenticationFailed = false;
        bool tightFailed = false;
        bool encryptionUnusable = false;
        Marconi::Rfb::ServerInit serverInit;
        int updates = 0;
        int bells = 0;
        std::vector<Marconi::Rfb::Rect> lastRects;
        std::vector<std::string> cutTexts;
        std::vector<Marconi::Rfb::ClipboardContent> clipboards;
        int resizedWidth = 0;
        int resizedHeight = 0;
        int screenLayouts = 0;
        std::vector<Marconi::Rfb::Screen> screens;
        int resizeRefusedStatus = -1;
        int cursorUpdates = 0;
        Marconi::Rfb::CursorShape cursor;

        // Credentials handed out when asked; empty password means "cancel".
        Marconi::Rfb::Credentials credentialsToGive;
        int credentialRequests = 0;
        bool lastNeedUsername = false;
        Marconi::Rfb::CredentialsKind lastKind = Marconi::Rfb::CredentialsKind::Password;

        // Whether to trust a certificate this computer doesn't; what was asked about.
        bool trustCertificate = false;
        int certificateQuestions = 0;
        Marconi::Net::TlsCertificate lastCertificate;

        // The same for an SSH server's host key, when connecting through SSH.
        bool trustSshHostKey = false;
        int sshHostKeyQuestions = 0;
        Marconi::Net::SshHostKeyQuestion lastSshHostKey;
    };

    Marconi::Rfb::Bytes HexBytes(const std::string& hex);

    void RunAddressTests();
    void RunTextTests();
    void RunAuthTests();
    void RunStreamTests();
    void RunHandshakeTests();
    void RunClientTests();
    void RunDecoderTests();
    void RunVersionTests();
    void RunSocketTests();
    void RunProbeTests();
    void RunChoiceTests();
    void RunDiscoveryTests();
    void RunVeNCryptTests();
    void RunRandomInputTests();
    void RunLocalKeysTests();
    void RunFilesTests();
    void RunTightFilesTests();
    void RunFileViewTests();
    void RunExtendedClipboardTests();
    void RunReconnectTests();
    void RunDesktopSizeTests();
    void RunSettingsTests();
    void RunWireTests();
    void RunSshTunnelTests();
    void RunRdpTests();
    void RunRdpBitmapTests();
    void RunRdpNtlmTests();
    void RunRdpCredSspTests();
    void RunRdpCredSspExchangeTests();
    void RunRdpMcsTests();
    void RunRdpSequenceTests();
    void RunRdpInputOutputTests();
    void RunPerformanceTests();
} // namespace MarconiTest
