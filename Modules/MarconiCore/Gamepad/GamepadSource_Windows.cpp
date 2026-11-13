// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Game controllers on Windows through XInput: up to four Xbox-style controllers (a PlayStation or other controller
// shows up here only if a program such as DS4Windows or Steam makes it look like an Xbox one). NOT YET BUILT OR RUN
// ON WINDOWS: written from XInput's documented interface, never compiled.
//
// XInput is loaded at run time (XInput1_4.dll, then XInput1_3.dll) so the viewer starts on a computer without it.
// What to check on Windows:
//  - The Guide (Xbox) button isn't reported: XInputGetState hides it. Reading it needs the undocumented XInputGetStateEx
//    (export number 100). The battery level isn't read either (XInputGetBatteryInformation).
//  - Only four controllers are possible with XInput. GameInput (Windows 11's newer interface) has no such limit and
//    sees PlayStation controllers directly; it would be a second source behind the same interface.

#include <MarconiCore/Gamepad/GamepadSource.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

#include <chrono>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Gamepad
    {
        namespace
        {
            using GetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
            using SetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

            class XInputSource : public GamepadSource
            {
            public:
                ~XInputSource() override
                {
                    if (m_module != nullptr)
                    {
                        FreeLibrary(m_module);
                    }
                }

                bool Load()
                {
                    for (const wchar_t* name : {L"XInput1_4.dll", L"XInput1_3.dll", L"XInput9_1_0.dll"})
                    {
                        m_module = LoadLibraryW(name);
                        if (m_module != nullptr)
                        {
                            break;
                        }
                    }
                    if (m_module == nullptr)
                    {
                        return false;
                    }
                    m_getState = reinterpret_cast<GetStateFunction>(GetProcAddress(m_module, "XInputGetState"));
                    m_setState = reinterpret_cast<SetStateFunction>(GetProcAddress(m_module, "XInputSetState"));
                    return m_getState != nullptr && m_setState != nullptr;
                }

                int GetSlotCount() const override { return XUSER_MAX_COUNT < Wire::kMaxGamepads ? XUSER_MAX_COUNT : Wire::kMaxGamepads; }

                bool Read(int slot, PadInfo& info, Wire::GamepadState& state) override
                {
                    if (slot < 0 || slot >= GetSlotCount())
                    {
                        return false;
                    }
                    std::lock_guard<std::mutex> lock(m_mutex);
                    Slot& s = m_slots[slot];
                    // Asking about an empty slot is slow (several milliseconds), so an empty one is only looked at
                    // once a second; a controller plugged in is seen within that.
                    const auto now = std::chrono::steady_clock::now();
                    if (!s.present && s.checked && now - s.lastCheck < std::chrono::seconds(1))
                    {
                        return false;
                    }
                    XINPUT_STATE xstate = {};
                    const DWORD result = m_getState(static_cast<DWORD>(slot), &xstate);
                    s.checked = true;
                    s.lastCheck = now;
                    s.present = result == ERROR_SUCCESS;
                    if (!s.present)
                    {
                        return false;
                    }
                    info.type = Wire::GamepadType::Xbox;
                    info.name = "Xbox controller " + std::to_string(slot + 1);
                    // XInput's button bits are the protocol's.
                    state.buttons = static_cast<uint16_t>(xstate.Gamepad.wButtons & Wire::GamepadButton::All);
                    state.leftTrigger = xstate.Gamepad.bLeftTrigger;
                    state.rightTrigger = xstate.Gamepad.bRightTrigger;
                    state.leftX = xstate.Gamepad.sThumbLX;
                    state.leftY = xstate.Gamepad.sThumbLY;
                    state.rightX = xstate.Gamepad.sThumbRX;
                    state.rightY = xstate.Gamepad.sThumbRY;
                    state.battery = Wire::kBatteryUnknown;
                    return true;
                }

                void SetRumble(int slot, uint16_t strong, uint16_t weak) override
                {
                    if (slot < 0 || slot >= GetSlotCount())
                    {
                        return;
                    }
                    std::lock_guard<std::mutex> lock(m_mutex);
                    XINPUT_VIBRATION vibration = {};
                    vibration.wLeftMotorSpeed = strong;
                    vibration.wRightMotorSpeed = weak;
                    m_setState(static_cast<DWORD>(slot), &vibration);
                }

            private:
                struct Slot
                {
                    bool present = false;
                    bool checked = false;
                    std::chrono::steady_clock::time_point lastCheck;
                };

                HMODULE m_module = nullptr;
                GetStateFunction m_getState = nullptr;
                SetStateFunction m_setState = nullptr;
                std::mutex m_mutex;
                Slot m_slots[Wire::kMaxGamepads];
            };
        } // namespace

        std::unique_ptr<GamepadSource> CreateSystemGamepadSource(std::string& reason)
        {
            auto source = std::make_unique<XInputSource>();
            if (!source->Load())
            {
                reason = "This computer has no game controller support (XInput isn't installed).";
                return nullptr;
            }
            return source;
        }
    } // namespace Gamepad
} // namespace Marconi
