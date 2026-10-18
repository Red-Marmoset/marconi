// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server tests' check harness and in-memory viewer

#include "TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace MarconiServerTest
{
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

    void Pipe::Write(const void* data, size_t length)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        m_data.insert(m_data.end(), bytes, bytes + length);
        m_changed.notify_all();
    }

    ptrdiff_t Pipe::Read(void* buffer, size_t length)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_changed.wait(lock, [this]() { return m_position < m_data.size() || m_closed; });
        if (m_position == m_data.size())
        {
            return 0; // closed and drained
        }
        size_t available = m_data.size() - m_position;
        size_t take = std::min(available, length);
        std::memcpy(buffer, m_data.data() + m_position, take);
        m_position += take;
        if (m_position == m_data.size())
        {
            m_data.clear();
            m_position = 0;
        }
        return static_cast<ptrdiff_t>(take);
    }

    void Pipe::Close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_changed.notify_all();
    }

    void RecordingInput::MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX)
    {
        std::lock_guard<std::mutex> lock(mutex);
        pointers.push_back({x, y, buttons, wheel, wheelX});
    }

    void RecordingInput::SendKey(uint32_t keysym, uint32_t scancode, bool down)
    {
        std::lock_guard<std::mutex> lock(mutex);
        keys.push_back({keysym, scancode, down});
    }

    void RecordingInput::TypeText(const std::string& utf8)
    {
        std::lock_guard<std::mutex> lock(mutex);
        texts.push_back(utf8);
    }

    void RecordingClientListener::OnConnected(const Marconi::Rfb::ServerInit& init)
    {
        std::lock_guard<std::mutex> lock(mutex);
        connected = true;
        serverInit = init;
        changed.notify_all();
    }

    void RecordingClientListener::OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>&)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++updates;
        changed.notify_all();
    }

    void RecordingClientListener::OnScreenLayout(const std::vector<Marconi::Rfb::Screen>&)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++screenLayouts;
        changed.notify_all();
    }

    void RecordingClientListener::OnDisconnected(const Marconi::DisconnectInfo& info)
    {
        std::lock_guard<std::mutex> lock(mutex);
        disconnected = true;
        disconnectError = info.error;
        changed.notify_all();
    }

    void ReadMagic(Marconi::Wire::Transport& transport)
    {
        uint8_t magic[4] = {};
        size_t got = 0;
        while (got < sizeof(magic))
        {
            ptrdiff_t n = transport.Read(magic + got, sizeof(magic) - got);
            if (n <= 0)
            {
                break;
            }
            got += static_cast<size_t>(n);
        }
        CHECK(got == 4 && magic[0] == 'M' && magic[1] == 'R' && magic[2] == 'C' && magic[3] == 'N');
    }

    bool RecordingClientListener::WaitFor(const std::function<bool()>& predicate, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::milliseconds(timeoutMs), predicate);
    }
} // namespace MarconiServerTest
