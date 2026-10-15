// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Shared helpers for RmVncCoreTest: check macros, a scripted fake RFB server and a recording listener

#include "TestSupport.h"

#include <RmVncCore/Rfb/Messages.h>

namespace RmVncTest
{
    using namespace RmVnc;

    int g_passed = 0;
    int g_failed = 0;

    void Check(bool condition, const char* what, const char* file, int line)
    {
        if (condition)
        {
            ++g_passed;
            return;
        }
        ++g_failed;
        std::string path = file;
        size_t slash = path.find_last_of("/\\");
        printf("  FAIL: %s:%d: %s\n", slash == std::string::npos ? file : file + slash + 1, line, what);
    }

    void RunTest(const char* name, const std::function<void()>& body)
    {
        printf("%s\n", name);
        try
        {
            body();
        }
        catch (const std::exception& exception)
        {
            ++g_failed;
            printf("  FAIL: unexpected exception: %s\n", exception.what());
        }
    }

    Rfb::Bytes HexBytes(const std::string& hex)
    {
        Rfb::Bytes bytes;
        std::string digits;
        for (char c : hex)
        {
            if (std::isxdigit(static_cast<unsigned char>(c)))
            {
                digits.push_back(c);
            }
        }
        for (size_t i = 0; i + 1 < digits.size(); i += 2)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
        }
        return bytes;
    }

    FakeServer::FakeServer()
    {
        std::string error;
        if (!m_listener.Listen("127.0.0.1", 0, error))
        {
            throw std::runtime_error("FakeServer listen failed: " + error);
        }
        m_port = m_listener.GetLocalPort();
    }

    bool FakeServer::AcceptClient(int timeoutMs)
    {
        std::string error;
        if (!m_listener.Accept(m_client, timeoutMs, error))
        {
            printf("  FakeServer accept failed: %s\n", error.c_str());
            return false;
        }
        m_source = std::make_unique<Rfb::SocketSource>(m_client);
        m_in = std::make_unique<Rfb::InStream>(*m_source);
        return true;
    }

    void FakeServer::Send(const Rfb::Bytes& data)
    {
        if (!m_client.SendAll(data.data(), data.size()))
        {
            throw Rfb::RfbError("FakeServer send failed");
        }
    }

    void FakeServer::SendString(const std::string& text)
    {
        Send(Rfb::Bytes(text.begin(), text.end()));
    }

    void FakeServer::Close()
    {
        m_client.Shutdown();
        m_client.Close();
    }

    void FakeServer::HandshakeNone(const std::string& banner, int width, int height, const std::string& name)
    {
        SendString(banner);
        In().ReadString(12); // client version
        Rfb::OutBuffer security;
        security.U8(1).U8(Rfb::SecurityType::None);
        Send(security);
        In().U8(); // chosen type
        Rfb::OutBuffer result;
        result.U32(0);
        Send(result);
        In().U8(); // ClientInit shared flag

        Rfb::OutBuffer init;
        init.U16(static_cast<uint16_t>(width)).U16(static_cast<uint16_t>(height));
        Rfb::PixelFormat().Write(init);
        init.U32(static_cast<uint32_t>(name.size())).Append(name);
        Send(init);
    }

    std::vector<int32_t> FakeServer::ReadClientSetup()
    {
        CHECK(In().U8() == Rfb::ClientMessage::SetPixelFormat);
        In().Skip(3);
        Rfb::PixelFormat format = Rfb::PixelFormat::Read(In());
        CHECK(format.bitsPerPixel == 32 && format.trueColour && !format.bigEndian && format.redShift == 16);

        CHECK(In().U8() == Rfb::ClientMessage::SetEncodings);
        In().Skip(1);
        uint16_t count = In().U16();
        std::vector<int32_t> encodings;
        for (uint16_t i = 0; i < count; ++i)
        {
            encodings.push_back(In().S32());
        }

        CHECK(In().U8() == Rfb::ClientMessage::FramebufferUpdateRequest);
        CHECK(In().U8() == 0); // first request is non-incremental
        In().Skip(8);
        return encodings;
    }

    void RecordingListener::OnConnected(const Rfb::ServerInit& init)
    {
        std::lock_guard<std::mutex> lock(mutex);
        connected = true;
        serverInit = init;
        changed.notify_all();
    }

    void RecordingListener::OnFramebufferUpdated(const std::vector<Rfb::Rect>& rects)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++updates;
        lastRects = rects;
        changed.notify_all();
    }

    void RecordingListener::OnDesktopResized(int width, int height)
    {
        std::lock_guard<std::mutex> lock(mutex);
        resizedWidth = width;
        resizedHeight = height;
        changed.notify_all();
    }

    void RecordingListener::OnServerCutText(const std::string& text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        cutTexts.push_back(text);
        changed.notify_all();
    }

    void RecordingListener::OnCursorShape(const Rfb::CursorShape& shape)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++cursorUpdates;
        cursor = shape;
        changed.notify_all();
    }

    void RecordingListener::OnBell()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++bells;
        changed.notify_all();
    }

    void RecordingListener::OnDisconnected(const Rfb::DisconnectInfo& info)
    {
        std::lock_guard<std::mutex> lock(mutex);
        disconnected = true;
        disconnectError = info.error;
        authenticationFailed = info.authenticationFailed;
        changed.notify_all();
    }

    bool RecordingListener::OnCredentialsRequired(bool needUsername, Rfb::Credentials& credentials)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++credentialRequests;
        lastNeedUsername = needUsername;
        if (credentialsToGive.password.empty())
        {
            return false;
        }
        credentials = credentialsToGive;
        return true;
    }

    bool RecordingListener::WaitFor(const std::function<bool()>& predicate, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::milliseconds(timeoutMs), predicate);
    }
} // namespace RmVncTest
