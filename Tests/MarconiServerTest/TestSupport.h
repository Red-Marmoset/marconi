// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A tiny check harness and an in-memory viewer for the Marconi server tests

#pragma once

#include <MarconiCore/Wire/WireChannel.h>
#include <MarconiServerCore/InputSink.h>

#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace MarconiServerTest
{
    extern int g_passed;
    extern int g_failed;

    void Check(bool condition, const char* what, const char* file, int line);

#define CHECK(cond) ::MarconiServerTest::Check((cond), #cond, __FILE__, __LINE__)

    void RunTest(const char* name, const std::function<void()>& body);

    // One direction of a byte pipe: Write appends, Read blocks until there are bytes or the end is closed. Closing
    // makes a blocked or later Read return 0, the way a shut-down socket does.
    class Pipe
    {
    public:
        void Write(const void* data, size_t length);
        ptrdiff_t Read(void* buffer, size_t length);
        void Close();

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        std::vector<uint8_t> m_data;
        size_t m_position = 0;
        bool m_closed = false;
    };

    // A transport over two pipes: reads from one, writes to the other. The server and the test client hold mirror
    // halves of the same pair.
    class PipeTransport : public Marconi::Wire::Transport
    {
    public:
        PipeTransport(Pipe& in, Pipe& out) : m_in(in), m_out(out) {}
        ptrdiff_t Read(void* buffer, size_t length) override { return m_in.Read(buffer, length); }
        bool Write(const void* data, size_t length) override
        {
            m_out.Write(data, length);
            return true;
        }

    private:
        Pipe& m_in;
        Pipe& m_out;
    };

    // Records the input the session applied, so a test can check what reached the computer.
    class RecordingInput : public Marconi::Server::InputSink
    {
    public:
        void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override;
        void SendKey(uint32_t keysym, uint32_t scancode, bool down) override;
        void TypeText(const std::string& utf8) override;

        std::mutex mutex;
        struct PointerEvent
        {
            int32_t x, y;
            uint8_t buttons;
            int16_t wheel, wheelX;
        };
        struct KeyEvent
        {
            uint32_t keysym, scancode;
            bool down;
        };
        std::vector<PointerEvent> pointers;
        std::vector<KeyEvent> keys;
        std::vector<std::string> texts;
    };

    void RunSessionTests();
    void RunListenerTests();
} // namespace MarconiServerTest
