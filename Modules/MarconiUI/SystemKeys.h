// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Catches the keyboard shortcuts the local system would otherwise act on (Alt+Tab, the Windows key, Cmd+Tab) so
// they can be sent to the remote computer instead

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace MarconiUI
{
    class SystemKeyCapture
    {
    public:
        using SendKey = std::function<void(uint32_t keysym, bool down)>;

        // Whether this platform can capture system shortcuts at all, and if not, why not (in plain words).
        static bool IsSupported();
        static std::string GetUnsupportedReason();

        // `send` is called on the UI thread with the keysym of each captured key, down and up.
        static std::unique_ptr<SystemKeyCapture> Create(SendKey send);

        virtual ~SystemKeyCapture() = default;

        // Starts catching shortcuts. False (with `problem`) if the system won't allow it, for example without the
        // Accessibility permission on macOS; the platform code asks the user for the permission where it can.
        virtual bool Start(std::string& problem) = 0;
        virtual void Stop() = 0;
        virtual bool IsActive() const = 0;
    };
} // namespace MarconiUI
