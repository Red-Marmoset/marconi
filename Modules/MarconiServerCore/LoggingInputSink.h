// Copyright (c) 2026 Red Marmoset Studios Ltd.
// An InputSink that ignores what it receives: for the test-pattern server, which has nothing to control. The
// real one (SendInput on Windows, and so on) arrives with screen capture.

#pragma once

#include "InputSink.h"

#include <Log/Log.h>

#include <atomic>

namespace Marconi
{
    namespace Server
    {
        // It never logs what was typed or where the pointer went: a viewer may be typing a password, and a log file
        // must not become a record of it. It says once that input is arriving and being ignored.
        class LoggingInputSink : public InputSink
        {
        public:
            void MovePointer(int32_t, int32_t, uint8_t, int16_t, int16_t) override { NoteOnce(); }

            void SendKey(uint32_t, uint32_t, bool) override { NoteOnce(); }

            void TypeText(const std::string&) override { NoteOnce(); }

        private:
            void NoteOnce()
            {
                if (!m_noted.exchange(true))
                {
                    LOG_INFO("Server", "A viewer is sending mouse and keyboard input; it is ignored here (nothing to "
                                       "control)");
                }
            }

            std::atomic<bool> m_noted{false};
        };
    } // namespace Server
} // namespace Marconi
