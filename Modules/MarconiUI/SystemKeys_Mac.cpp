// Copyright (c) 2026 Red Marmoset Studios Ltd.
// System shortcut capture on macOS: an event tap swallows Command shortcuts the system would act on and sends them
// instead. Needs the Accessibility permission (System Settings > Privacy & Security > Accessibility).
//
// The tap's callback runs on the main run loop, which is the UI thread, so `send` is called there.

#include "SystemKeys.h"

#include <MarconiCore/Keysyms.h>

#include <ApplicationServices/ApplicationServices.h>

#include <map>

namespace MarconiUI
{
    namespace
    {
        namespace Keysym = Marconi::Keysym;

        // macOS virtual key codes (the same on every keyboard layout) for the shortcuts worth catching.
        constexpr CGKeyCode kKeyTab = 48;
        constexpr CGKeyCode kKeySpace = 49;
        constexpr CGKeyCode kKeyGrave = 50;
        constexpr CGKeyCode kKeyQ = 12;
        constexpr CGKeyCode kKeyH = 4;
        constexpr CGKeyCode kKeyM = 46;

        class MacSystemKeyCapture : public SystemKeyCapture
        {
        public:
            explicit MacSystemKeyCapture(SendKey send) : m_send(std::move(send)) {}

            ~MacSystemKeyCapture() override { Stop(); }

            bool Start(std::string& problem) override
            {
                if (m_tap != nullptr)
                {
                    return true;
                }
                // Asks for the permission with the system's own prompt the first time, and explains if refused.
                const void* keys[] = {kAXTrustedCheckOptionPrompt};
                const void* values[] = {kCFBooleanTrue};
                CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                                             &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
                bool trusted = AXIsProcessTrustedWithOptions(options);
                CFRelease(options);
                if (!trusted)
                {
                    problem = "macOS needs permission first: allow Marconi under System Settings > Privacy & "
                              "Security > Accessibility, then turn this on again";
                    return false;
                }
                CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
                m_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, mask,
                                         &MacSystemKeyCapture::Tap, this);
                if (m_tap == nullptr)
                {
                    problem = "macOS refused to let Marconi watch the keyboard";
                    return false;
                }
                m_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_tap, 0);
                CFRunLoopAddSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
                CGEventTapEnable(m_tap, true);
                return true;
            }

            void Stop() override
            {
                if (m_tap == nullptr)
                {
                    return;
                }
                CGEventTapEnable(m_tap, false);
                CFRunLoopRemoveSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
                CFRelease(m_source);
                CFRelease(m_tap);
                m_source = nullptr;
                m_tap = nullptr;
                for (const auto& held : m_held)
                {
                    m_send(held.second, false);
                }
                m_held.clear();
            }

            bool IsActive() const override { return m_tap != nullptr; }

        private:
            // The keysym to send for a Command shortcut the system would otherwise act on, or 0 to let it through.
            static uint32_t CapturedKeysym(CGEventRef event)
            {
                CGEventFlags flags = CGEventGetFlags(event);
                if ((flags & kCGEventFlagMaskCommand) == 0)
                {
                    return 0;
                }
                switch (static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode)))
                {
                case kKeyTab: return Keysym::Tab;    // Cmd+Tab: the app switcher
                case kKeySpace: return ' ';          // Cmd+Space: Spotlight
                case kKeyGrave: return '`';          // Cmd+`: next window
                case kKeyQ: return 'q';              // Cmd+Q would quit Marconi
                case kKeyH: return 'h';              // Cmd+H would hide it
                case kKeyM: return 'm';              // Cmd+M would minimise it
                default: return 0;
                }
            }

            static CGEventRef Tap(CGEventTapProxy, CGEventType type, CGEventRef event, void* context)
            {
                MacSystemKeyCapture& self = *static_cast<MacSystemKeyCapture*>(context);
                if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
                {
                    CGEventTapEnable(self.m_tap, true); // the system switches a slow tap off; switch it back on
                    return event;
                }
                if (type != kCGEventKeyDown && type != kCGEventKeyUp)
                {
                    return event;
                }
                bool down = type == kCGEventKeyDown;
                CGKeyCode keyCode = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
                uint32_t keysym = CapturedKeysym(event);
                bool held = self.m_held.count(keyCode) != 0;
                if (keysym == 0 && !(held && !down))
                {
                    return event;
                }
                if (down)
                {
                    if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0)
                    {
                        return nullptr; // swallow repeats of a captured key without sending them again
                    }
                    self.m_held[keyCode] = keysym;
                }
                else
                {
                    keysym = held ? self.m_held[keyCode] : keysym;
                    self.m_held.erase(keyCode);
                }
                self.m_send(keysym, down);
                return nullptr; // swallowed
            }

            SendKey m_send;
            CFMachPortRef m_tap = nullptr;
            CFRunLoopSourceRef m_source = nullptr;
            std::map<CGKeyCode, uint32_t> m_held; // key code -> keysym sent for its press
        };
    } // namespace

    bool SystemKeyCapture::IsSupported()
    {
        return true;
    }

    std::string SystemKeyCapture::GetUnsupportedReason()
    {
        return std::string();
    }

    std::unique_ptr<SystemKeyCapture> SystemKeyCapture::Create(SendKey send)
    {
        return std::make_unique<MacSystemKeyCapture>(std::move(send));
    }
} // namespace MarconiUI
