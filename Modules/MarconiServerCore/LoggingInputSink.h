// Copyright (c) 2026 Red Marmoset Studios Ltd.
// An InputSink that only logs what it receives: for the test-pattern server, which has nothing to control. The
// real one (SendInput on Windows, and so on) arrives with screen capture.

#pragma once

#include "InputSink.h"

#include <Log/Log.h>

namespace Marconi
{
    namespace Server
    {
        class LoggingInputSink : public InputSink
        {
        public:
            void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override
            {
                LOG_INFO("Server", "Pointer %d,%d buttons %u wheel %d/%d", x, y, static_cast<unsigned>(buttons),
                         static_cast<int>(wheel), static_cast<int>(wheelX));
            }

            void SendKey(uint32_t keysym, uint32_t scancode, bool down) override
            {
                LOG_INFO("Server", "Key keysym 0x%X scancode 0x%X %s", keysym, scancode, down ? "down" : "up");
            }

            void TypeText(const std::string& utf8) override
            {
                LOG_INFO("Server", "Text '%s'", utf8.c_str());
            }
        };
    } // namespace Server
} // namespace Marconi
