// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Virtual game controllers on Windows through ViGEmBus, the driver Sunshine, Parsec and DS4Windows use. NOT YET
// BUILT OR RUN ON WINDOWS: written from ViGEmClient's documented interface, never compiled.
//
// ViGEmClient is a small library that talks to the driver; this file loads it at run time (ViGEmClient.dll, from
// the server's own folder or the system's search path) and looks up the handful of functions it needs, so the server
// starts and works without the driver or the library and only says so, in plain words, when a viewer sends a
// controller. All the ViGEm-specific code is in this one file behind VirtualGamepadBackend, so the driver can be
// replaced (a fork of ViGEmBus, or one of our own) by replacing this file.
//
// What to check on Windows:
//  - ViGEmClient normally ships as a static library or source, not a DLL: a ViGEmClient.dll has to be built from its
//    source (MIT licence) and put next to MarconiServer.exe, or this file changed to link it instead.
//  - The function names, the XUSB_REPORT layout (12 bytes, packed) and the notification callback signature below.
//  - Rumble: the driver gives each motor as 0 to 255, scaled here to 0 to 65535.

#include "VirtualGamepad.h"

#include <Log/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <type_traits>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // ViGEmClient's types, spelled out so no header from it is needed.
            using VigemClient = void*;
            using VigemTarget = void*;
            using VigemError = unsigned long; // VIGEM_ERROR; success is 0x20000000
            constexpr VigemError kVigemSuccess = 0x20000000;
            constexpr VigemError kVigemBusNotFound = 0xE0000001;
            constexpr VigemError kVigemNoFreeSlot = 0xE0000002;

#pragma pack(push, 1)
            struct XusbReport // XUSB_REPORT
            {
                USHORT buttons;
                BYTE leftTrigger;
                BYTE rightTrigger;
                SHORT thumbLX;
                SHORT thumbLY;
                SHORT thumbRX;
                SHORT thumbRY;
            };
#pragma pack(pop)
            static_assert(sizeof(XusbReport) == 12, "XUSB_REPORT is 12 bytes");

            // PFN_VIGEM_X360_NOTIFICATION
            using NotificationFunction = void(CALLBACK*)(VigemClient client, VigemTarget target, UCHAR largeMotor,
                                                         UCHAR smallMotor, UCHAR ledNumber, LPVOID userData);

            struct VigemApi
            {
                HMODULE module = nullptr;
                VigemClient(CALLBACK* alloc)() = nullptr;
                void(CALLBACK* free)(VigemClient) = nullptr;
                VigemError(CALLBACK* connect)(VigemClient) = nullptr;
                void(CALLBACK* disconnect)(VigemClient) = nullptr;
                VigemTarget(CALLBACK* x360Alloc)() = nullptr;
                void(CALLBACK* targetFree)(VigemTarget) = nullptr;
                VigemError(CALLBACK* targetAdd)(VigemClient, VigemTarget) = nullptr;
                VigemError(CALLBACK* targetRemove)(VigemClient, VigemTarget) = nullptr;
                VigemError(CALLBACK* x360Update)(VigemClient, VigemTarget, XusbReport) = nullptr;
                VigemError(CALLBACK* x360Register)(VigemClient, VigemTarget, NotificationFunction, LPVOID) = nullptr;
                void(CALLBACK* x360Unregister)(VigemTarget) = nullptr;
            };

            // Loads ViGEmClient.dll once. False (with `error` in plain words) if it isn't there.
            bool LoadVigem(VigemApi& api, std::string& error)
            {
                api.module = LoadLibraryW(L"ViGEmClient.dll");
                if (api.module == nullptr)
                {
                    error = "Game controllers need the ViGEmBus driver, which isn't installed on this computer "
                            "(Marconi Server also couldn't find ViGEmClient.dll). Install ViGEmBus and restart "
                            "Marconi Server.";
                    return false;
                }
                auto find = [&api](auto& target, const char* name) {
                    target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(
                        GetProcAddress(api.module, name));
                    return target != nullptr;
                };
                const bool ok = find(api.alloc, "vigem_alloc") && find(api.free, "vigem_free") &&
                                find(api.connect, "vigem_connect") && find(api.disconnect, "vigem_disconnect") &&
                                find(api.x360Alloc, "vigem_target_x360_alloc") &&
                                find(api.targetFree, "vigem_target_free") && find(api.targetAdd, "vigem_target_add") &&
                                find(api.targetRemove, "vigem_target_remove") &&
                                find(api.x360Update, "vigem_target_x360_update") &&
                                find(api.x360Register, "vigem_target_x360_register_notification") &&
                                find(api.x360Unregister, "vigem_target_x360_unregister_notification");
                if (!ok)
                {
                    FreeLibrary(api.module);
                    api = VigemApi();
                    error = "Game controllers need the ViGEmBus driver, but the ViGEmClient.dll found next to Marconi "
                            "Server isn't one this version understands.";
                    return false;
                }
                return true;
            }

            class VigemPad : public VirtualGamepad
            {
            public:
                VigemPad(const VigemApi& api, RumbleHandler onRumble) : m_api(api), m_onRumble(std::move(onRumble)) {}

                ~VigemPad() override
                {
                    if (m_target != nullptr)
                    {
                        // Stops the notifications (and waits for one that is running) before anything is freed.
                        m_api.x360Unregister(m_target);
                        m_api.targetRemove(m_client, m_target);
                        m_api.targetFree(m_target);
                    }
                    if (m_client != nullptr)
                    {
                        m_api.disconnect(m_client);
                        m_api.free(m_client);
                    }
                }

                // Connects to the driver and plugs in an Xbox 360 controller. False with the reason in plain words.
                bool Open(Wire::GamepadResult::Status& status, std::string& message)
                {
                    m_client = m_api.alloc();
                    if (m_client == nullptr)
                    {
                        status = Wire::GamepadResult::Status::Failed;
                        message = "Windows ran out of memory making a game controller.";
                        return false;
                    }
                    VigemError error = m_api.connect(m_client);
                    if (error != kVigemSuccess)
                    {
                        status = Wire::GamepadResult::Status::NoDriver;
                        message = error == kVigemBusNotFound
                                      ? "Game controllers need the ViGEmBus driver, which isn't installed on this "
                                        "computer. Install ViGEmBus and try again."
                                      : "Marconi Server couldn't reach the ViGEmBus driver (error " +
                                            std::to_string(error) + ").";
                        return false;
                    }
                    m_target = m_api.x360Alloc();
                    if (m_target == nullptr)
                    {
                        status = Wire::GamepadResult::Status::Failed;
                        message = "Windows ran out of memory making a game controller.";
                        return false;
                    }
                    error = m_api.targetAdd(m_client, m_target);
                    if (error != kVigemSuccess)
                    {
                        m_api.targetFree(m_target);
                        m_target = nullptr;
                        status = error == kVigemNoFreeSlot ? Wire::GamepadResult::Status::TooMany
                                                           : Wire::GamepadResult::Status::Failed;
                        message = error == kVigemNoFreeSlot
                                      ? "Windows has no room for another game controller."
                                      : "The ViGEmBus driver wouldn't add a game controller (error " +
                                            std::to_string(error) + ").";
                        return false;
                    }
                    m_api.x360Register(m_client, m_target, &VigemPad::OnNotification, this);
                    return true;
                }

                bool Update(const Wire::GamepadState& state) override
                {
                    // The protocol's buttons are XInput's bits and its sticks have up positive, as the driver wants.
                    XusbReport report;
                    report.buttons = state.buttons;
                    report.leftTrigger = state.leftTrigger;
                    report.rightTrigger = state.rightTrigger;
                    report.thumbLX = state.leftX;
                    report.thumbLY = state.leftY;
                    report.thumbRX = state.rightX;
                    report.thumbRY = state.rightY;
                    return m_api.x360Update(m_client, m_target, report) == kVigemSuccess;
                }

            private:
                static void CALLBACK OnNotification(VigemClient, VigemTarget, UCHAR largeMotor, UCHAR smallMotor,
                                                    UCHAR, LPVOID userData)
                {
                    VigemPad* self = static_cast<VigemPad*>(userData);
                    if (self->m_onRumble)
                    {
                        self->m_onRumble(static_cast<uint16_t>(largeMotor * 257), static_cast<uint16_t>(smallMotor * 257));
                    }
                }

                const VigemApi& m_api;
                RumbleHandler m_onRumble;
                VigemClient m_client = nullptr;
                VigemTarget m_target = nullptr;
            };

            class VigemBackend : public VirtualGamepadBackend
            {
            public:
                ~VigemBackend() override
                {
                    if (m_api.module != nullptr)
                    {
                        FreeLibrary(m_api.module);
                    }
                }

                std::string GetName() const override { return "ViGEmBus"; }

                std::unique_ptr<VirtualGamepad> Create(Wire::GamepadType, RumbleHandler onRumble,
                                                       Wire::GamepadResult::Status& status,
                                                       std::string& message) override
                {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (m_api.module == nullptr && !LoadVigem(m_api, message))
                        {
                            status = Wire::GamepadResult::Status::NoDriver;
                            return nullptr;
                        }
                    }
                    // Every kind of controller is made as an Xbox 360 one, which is what games expect.
                    auto pad = std::make_unique<VigemPad>(m_api, std::move(onRumble));
                    if (!pad->Open(status, message))
                    {
                        return nullptr;
                    }
                    return pad;
                }

            private:
                std::mutex m_mutex;
                VigemApi m_api;
            };
        } // namespace

        std::unique_ptr<VirtualGamepadBackend> CreateSystemGamepadBackend()
        {
            return std::make_unique<VigemBackend>();
        }
    } // namespace Server
} // namespace Marconi
