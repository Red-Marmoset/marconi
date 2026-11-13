// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux input injection on a Wayland desktop through the desktop portal's RemoteDesktop interface (see
// WaylandPortal.h): the viewer's mouse and keyboard applied to this computer. Keys go by meaning (X keysym), so
// they follow the keyboard layout; the physical-key path (for games) would need the portal's keycode call.

#include "WaylandSupport.h"

#include "SystemInput.h"
#include "SystemLock.h"

#include <MarconiCore/Keysyms.h>

#include <mutex>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // Linux input event codes for the mouse buttons the wire carries (bit 0 left, 1 middle, 2 right).
            constexpr int32_t kButtonLeft = 0x110;
            constexpr int32_t kButtonRight = 0x111;
            constexpr int32_t kButtonMiddle = 0x112;

            class WaylandInput : public InputSink
            {
            public:
                explicit WaylandInput(std::shared_ptr<WaylandPortalSession> session) : m_session(std::move(session)) {}

                void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    int32_t width = 0;
                    int32_t height = 0;
                    m_session->GetCapture().GetSize(width, height);
                    m_session->PointerMotionAbsolute(x, y, width, height);

                    static const struct
                    {
                        uint8_t bit;
                        int32_t code;
                    } kButtons[] = {{0x01, kButtonLeft}, {0x02, kButtonMiddle}, {0x04, kButtonRight}};
                    for (const auto& button : kButtons)
                    {
                        bool now = (buttons & button.bit) != 0;
                        bool was = (m_buttons & button.bit) != 0;
                        if (now != was)
                        {
                            m_session->PointerButton(button.code, now);
                        }
                    }
                    m_buttons = buttons;
                    // The wire's wheel is positive up and right; the portal counts steps positive down and right.
                    Scroll(false, -wheel);
                    Scroll(true, wheelX);
                }

                void SendKey(uint32_t keysym, uint32_t /*scancode*/, bool down) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_session->Key(keysym, down);
                }

                void TypeText(const std::string& utf8) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    size_t i = 0;
                    while (i < utf8.size())
                    {
                        unsigned char lead = static_cast<unsigned char>(utf8[i]);
                        int length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
                        uint32_t codepoint = length == 1 ? lead : lead & (0xFF >> (length + 1));
                        for (int k = 1; k < length && i + k < utf8.size(); ++k)
                        {
                            codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
                        }
                        i += length;
                        uint32_t keysym = Marconi::Keysym::FromUnicode(codepoint);
                        if (keysym != 0)
                        {
                            m_session->Key(keysym, true);
                            m_session->Key(keysym, false);
                        }
                    }
                }

                bool LockWorkstation() override { return LockThisComputer(); }

            private:
                void Scroll(bool horizontal, int delta)
                {
                    if (delta == 0)
                    {
                        return;
                    }
                    int notches = (delta < 0 ? -delta : delta) / 120;
                    if (notches == 0)
                    {
                        notches = 1;
                    }
                    m_session->PointerAxisSteps(horizontal, delta < 0 ? -notches : notches);
                }

                std::shared_ptr<WaylandPortalSession> m_session;
                std::mutex m_mutex;
                uint8_t m_buttons = 0;
            };
        } // namespace

        std::unique_ptr<InputSink> CreateWaylandInput(std::shared_ptr<WaylandPortalSession> session)
        {
            return std::make_unique<WaylandInput>(std::move(session));
        }

        std::unique_ptr<InputSink> CreateWaylandSystemInput(std::string& error)
        {
            std::string busError;
            if (!DBusLink::IsAvailable(busError))
            {
                error = "This is a Wayland desktop, and controlling it needs the D-Bus library. " + busError;
                return nullptr;
            }
            // Joins the session the screen started (one question to the person covers both). With no session, the
            // screen wasn't made, and the reason has been given with it, so there is nothing more to say.
            std::shared_ptr<WaylandPortalSession> session = FindWaylandPortal();
            if (session == nullptr)
            {
                return nullptr;
            }
            return CreateWaylandInput(std::move(session));
        }
    } // namespace Server
} // namespace Marconi
