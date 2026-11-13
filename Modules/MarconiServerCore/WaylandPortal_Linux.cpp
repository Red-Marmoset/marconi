// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: the desktop-portal conversation for Wayland. See WaylandPortal.h and SERVER_PLAN.md ("Wayland").
//
// The order of calls (all on org.freedesktop.portal.Desktop, /org/freedesktop/portal/desktop):
//   RemoteDesktop.CreateSession -> SelectDevices (keyboard + pointer, remember the answer) ->
//   ScreenCast.SelectSources (monitor, pointer as metadata when offered) -> RemoteDesktop.Start (the person is asked
//   here, once) -> ScreenCast.OpenPipeWireRemote (a file descriptor to PipeWire) -> PipeWire stream.
// When the desktop has no RemoteDesktop portal (for example wlroots compositors such as Sway), the same is done with
// ScreenCast alone and control is reported as missing.

#include "WaylandPortal.h"

#include "ServiceConfig.h"

#include <Log/Log.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            const char* const kBusName = "org.freedesktop.portal.Desktop";
            const char* const kObjectPath = "/org/freedesktop/portal/desktop";
            const char* const kScreenCast = "org.freedesktop.portal.ScreenCast";
            const char* const kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
            const char* const kRequest = "org.freedesktop.portal.Request";
            const char* const kSession = "org.freedesktop.portal.Session";
            const char* const kProperties = "org.freedesktop.DBus.Properties";

            const char* const kNoPortalText =
                "This desktop doesn't have a screen-sharing service running, so Marconi Server can't share it. "
                "Install the portal for your desktop (xdg-desktop-portal with xdg-desktop-portal-gnome on GNOME, "
                "xdg-desktop-portal-kde on KDE, xdg-desktop-portal-wlr on Sway and similar) and sign in again.";

            std::string ReadToken(const std::string& path)
            {
                std::ifstream file(path);
                std::string token;
                if (file)
                {
                    std::getline(file, token);
                }
                return token;
            }

            void WriteToken(const std::string& path, const std::string& token)
            {
                std::error_code ignored;
                std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ignored);
                std::ofstream file(path, std::ios::trunc);
                if (file)
                {
                    file << token << "\n";
                }
                std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::replace, ignored);
            }
        } // namespace

        std::shared_ptr<WaylandPortalSession> WaylandPortalSession::Start(const WaylandPortalOptions& options)
        {
            std::shared_ptr<WaylandPortalSession> session(new WaylandPortalSession());
            session->m_tokenPath = options.tokenPath.empty() ? GetDefaultDataDirectory() + "/wayland-portal.token"
                                                             : options.tokenPath;
            WaylandPortalSession* raw = session.get();
            session->m_thread = std::thread([raw, options]() { raw->Run(options); });
            return session;
        }

        WaylandPortalSession::~WaylandPortalSession()
        {
            m_stop.store(true);
            if (m_thread.joinable())
            {
                m_thread.join();
            }
            m_capture.Stop();
            if (m_link != nullptr && !m_sessionPath.empty())
            {
                // Tell the desktop we are done, so its "screen is being shared" indicator goes away.
                std::vector<DBusValue> reply;
                std::string errorName;
                std::string error;
                m_link->Call(kBusName, m_sessionPath, kSession, "Close", {}, reply, errorName, error, 2000);
            }
        }

        WaylandPortalSession::State WaylandPortalSession::GetState() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_state;
        }

        std::string WaylandPortalSession::GetStatusText() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_status;
        }

        bool WaylandPortalSession::HasControl() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_control;
        }

        bool WaylandPortalSession::WaitUntilSettled(int timeoutMs) const
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                State state = GetState();
                if (state != State::Starting && state != State::WaitingForApproval)
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        }

        bool WaylandPortalSession::WaitUntilAnsweredOrAsking(int timeoutMs) const
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (GetState() != State::Starting)
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        }

        void WaylandPortalSession::GetStreamGeometry(int32_t& x, int32_t& y, int32_t& width, int32_t& height) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            x = m_streamX;
            y = m_streamY;
            width = m_streamWidth;
            height = m_streamHeight;
        }

        void WaylandPortalSession::SetState(State state, const std::string& text)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = state;
            m_status = text;
        }

        void WaylandPortalSession::Run(WaylandPortalOptions options)
        {
            Negotiate(options);
            if (GetState() == State::Ready)
            {
                Monitor();
            }
        }

        bool WaylandPortalSession::CallWithResponse(const std::string& interfaceName, const std::string& method,
                                                    std::vector<DBusValue> args, DBusValue& options,
                                                    DBusValue& results, uint32_t& code, int timeoutMs,
                                                    std::string& error)
        {
            // The portal names the request after our bus name and a token we choose, so the answer can be matched
            // without a race between the call returning and the answer arriving.
            std::string token = "marconi" + std::to_string(++m_counter);
            options.Add("handle_token", DBusValue::String(token));
            args.push_back(options);
            std::string expected = std::string(kObjectPath) + "/request/" + m_senderToken + "/" + token;

            std::vector<DBusValue> reply;
            std::string errorName;
            if (!m_link->Call(kBusName, kObjectPath, interfaceName, method, args, reply, errorName, error, 30000))
            {
                error = errorName.empty() ? error : errorName + ": " + error;
                return false;
            }
            std::string handle = !reply.empty() && reply[0].kind == DBusValue::Kind::ObjectPath ? reply[0].text : expected;

            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (!m_stop.load())
            {
                DBusMessageIn message;
                if (m_link->Receive(message, 200))
                {
                    if (message.type == DBusMessageIn::Type::Signal && message.interfaceName == kRequest &&
                        message.member == "Response" && (message.path == handle || message.path == expected) &&
                        message.args.size() >= 2)
                    {
                        code = message.args[0].AsUint();
                        results = message.args[1];
                        return true;
                    }
                    if (message.type == DBusMessageIn::Type::Signal && message.interfaceName == kSession &&
                        message.member == "Closed")
                    {
                        error = "The desktop closed the sharing request.";
                        return false;
                    }
                }
                if (std::chrono::steady_clock::now() > deadline)
                {
                    error = "The desktop didn't answer in time.";
                    return false;
                }
            }
            error = "Stopped.";
            return false;
        }

        bool WaylandPortalSession::Negotiate(const WaylandPortalOptions& options)
        {
            std::string error;
            m_link = DBusLink::Connect(options.busAddress, error);
            if (m_link == nullptr)
            {
                LOG_WARNING("Server", "Wayland portal: %s", error.c_str());
                SetState(State::Failed, "Marconi Server can't ask the desktop to share the screen. " + error);
                return false;
            }
            // Our bus name ":1.42" becomes "1_42" in request paths.
            m_senderToken = m_link->GetUniqueName();
            if (!m_senderToken.empty() && m_senderToken[0] == ':')
            {
                m_senderToken.erase(0, 1);
            }
            for (char& c : m_senderToken)
            {
                if (c == '.')
                {
                    c = '_';
                }
            }
            m_link->AddMatch(std::string("type='signal',interface='") + kRequest + "',member='Response'", error);
            m_link->AddMatch(std::string("type='signal',interface='") + kSession + "',member='Closed'", error);

            SetState(State::Starting, "Marconi Server is asking the desktop to share the screen.");

            auto getProperty = [&](const char* interfaceName, const char* name, DBusValue& value) {
                std::vector<DBusValue> reply;
                std::string errorName;
                std::string propertyError;
                if (!m_link->Call(kBusName, kObjectPath, kProperties, "Get",
                                  {DBusValue::String(interfaceName), DBusValue::String(name)}, reply, errorName,
                                  propertyError, 5000) ||
                    reply.empty())
                {
                    return false;
                }
                value = reply[0].kind == DBusValue::Kind::Variant && !reply[0].items.empty() ? reply[0].items[0] : reply[0];
                return true;
            };

            DBusValue castVersion;
            if (!getProperty(kScreenCast, "version", castVersion))
            {
                LOG_WARNING("Server", "Wayland portal: no ScreenCast portal on the bus");
                SetState(State::Failed, kNoPortalText);
                return false;
            }
            DBusValue cursorModes;
            uint32_t modes = getProperty(kScreenCast, "AvailableCursorModes", cursorModes) ? cursorModes.AsUint() : 1;
            uint32_t cursorMode = (modes & 4) != 0 ? 4 : (modes & 2) != 0 ? 2 : 1; // metadata, else painted in, else hidden
            m_cursorSeparate.store(cursorMode == 4);

            bool control = false;
            if (options.wantControl)
            {
                DBusValue deviceTypes;
                control = getProperty(kRemoteDesktop, "AvailableDeviceTypes", deviceTypes) &&
                          (deviceTypes.AsUint() & 3) == 3;
            }
            const char* owner = control ? kRemoteDesktop : kScreenCast;

            std::string token = ReadToken(m_tokenPath);

            // 1. The session.
            DBusValue createOptions = DBusValue::Dict();
            createOptions.Add("session_handle_token", DBusValue::String("marconiSession"));
            DBusValue results;
            uint32_t code = 0;
            if (!CallWithResponse(owner, "CreateSession", {}, createOptions, results, code, 30000, error) || code != 0)
            {
                LOG_WARNING("Server", "Wayland portal CreateSession failed: %s (code %u)", error.c_str(), code);
                SetState(State::Failed, kNoPortalText);
                return false;
            }
            const DBusValue* sessionHandle = results.Find("session_handle");
            if (sessionHandle == nullptr || sessionHandle->text.empty())
            {
                SetState(State::Failed, kNoPortalText);
                return false;
            }
            m_sessionPath = sessionHandle->text;
            DBusValue session = DBusValue::Path(m_sessionPath);

            // 2. What to share and control.
            if (control)
            {
                DBusValue deviceOptions = DBusValue::Dict();
                deviceOptions.Add("types", DBusValue::Uint(3));
                deviceOptions.Add("persist_mode", DBusValue::Uint(2));
                if (!token.empty())
                {
                    deviceOptions.Add("restore_token", DBusValue::String(token));
                }
                if (!CallWithResponse(kRemoteDesktop, "SelectDevices", {session}, deviceOptions, results, code, 30000,
                                      error) ||
                    code != 0)
                {
                    LOG_WARNING("Server", "Wayland portal SelectDevices failed: %s", error.c_str());
                    SetState(State::Failed, "The desktop refused the request to control the keyboard and mouse.");
                    return false;
                }
            }
            DBusValue sourceOptions = DBusValue::Dict();
            sourceOptions.Add("types", DBusValue::Uint(1));
            sourceOptions.Add("multiple", DBusValue::Bool(false));
            sourceOptions.Add("cursor_mode", DBusValue::Uint(cursorMode));
            if (!control)
            {
                sourceOptions.Add("persist_mode", DBusValue::Uint(2));
                if (!token.empty())
                {
                    sourceOptions.Add("restore_token", DBusValue::String(token));
                }
            }
            if (!CallWithResponse(kScreenCast, "SelectSources", {session}, sourceOptions, results, code, 30000, error) ||
                code != 0)
            {
                LOG_WARNING("Server", "Wayland portal SelectSources failed: %s", error.c_str());
                SetState(State::Failed, "The desktop refused the request to share the screen.");
                return false;
            }

            // 3. Start: this is where the desktop asks the person (unless it remembers the earlier answer).
            SetState(State::WaitingForApproval,
                     "Marconi Server is waiting for you to allow screen sharing. Your desktop should be showing a "
                     "window asking which screen to share and whether Marconi Server may use the keyboard and "
                     "mouse: choose the screen and press Share (or Allow). If you can't see it, look for it in the "
                     "desktop's notifications.");
            DBusValue startOptions = DBusValue::Dict();
            if (!CallWithResponse(owner, "Start", {session, DBusValue::String("")}, startOptions, results, code,
                                  options.approvalTimeoutMs, error))
            {
                LOG_WARNING("Server", "Wayland portal Start failed: %s", error.c_str());
                SetState(State::Failed, "The desktop didn't answer the request to share the screen (" + error + "). Choose "
                         "\"Check again\" in the Marconi Server tray menu to ask again.");
                return false;
            }
            if (code != 0)
            {
                LOG_INFO("Server", "Wayland portal: the person declined (code %u)", code);
                SetState(State::Declined,
                         "Screen sharing was not allowed, so viewers see a test picture instead of this screen. To "
                         "share this computer, choose \"Check again\" in the Marconi Server tray menu and press "
                         "Share (or Allow) when the desktop asks. If it doesn't ask again, open the desktop's "
                         "privacy or screen-sharing settings and remove the earlier answer for Marconi Server.");
                return false;
            }

            const DBusValue* newToken = results.Find("restore_token");
            if (newToken != nullptr && !newToken->text.empty())
            {
                token = newToken->text;
                WriteToken(m_tokenPath, token);
            }
            const DBusValue* devices = results.Find("devices");
            const DBusValue* streams = results.Find("streams");
            if (streams == nullptr || streams->items.empty() || streams->items[0].items.empty())
            {
                SetState(State::Failed, "The desktop agreed but didn't share any screen.");
                return false;
            }
            const DBusValue& stream = streams->items[0];
            m_streamNode = stream.items[0].AsUint();
            if (stream.items.size() > 1)
            {
                const DBusValue* position = stream.items[1].Find("position");
                const DBusValue* size = stream.items[1].Find("size");
                std::lock_guard<std::mutex> lock(m_mutex);
                if (position != nullptr && position->items.size() == 2)
                {
                    m_streamX = static_cast<int32_t>(position->items[0].integer);
                    m_streamY = static_cast<int32_t>(position->items[1].integer);
                }
                if (size != nullptr && size->items.size() == 2)
                {
                    m_streamWidth = static_cast<int32_t>(size->items[0].integer);
                    m_streamHeight = static_cast<int32_t>(size->items[1].integer);
                }
            }
            bool granted = control && devices != nullptr && (devices->AsUint() & 3) == 3;

            // 4. The PipeWire connection.
            std::vector<DBusValue> reply;
            std::string errorName;
            DBusValue noOptions = DBusValue::Dict();
            if (!m_link->Call(kBusName, kObjectPath, kScreenCast, "OpenPipeWireRemote", {session, noOptions}, reply,
                              errorName, error, 10000) ||
                reply.empty() || reply[0].kind != DBusValue::Kind::UnixFd)
            {
                LOG_WARNING("Server", "Wayland portal OpenPipeWireRemote failed: %s", error.c_str());
                SetState(State::Failed, "The desktop agreed to share the screen but didn't give access to the video.");
                return false;
            }
            int fd = static_cast<int>(reply[0].integer);
            if (!m_capture.Start(fd, m_streamNode, m_cursorSeparate.load(), error))
            {
                LOG_WARNING("Server", "Wayland portal: video stream: %s", error.c_str());
                SetState(State::Failed, error);
                return false;
            }

            std::string status;
            if (!granted)
            {
                status = options.wantControl
                             ? (control ? "Viewers can see this screen but can't control it: keyboard and mouse "
                                          "control wasn't allowed. Choose \"Check again\" in the tray menu and allow "
                                          "it to control this computer."
                                        : "Viewers can see this screen but can't control it: this desktop's screen-"
                                          "sharing service has no keyboard and mouse control (GNOME and KDE have "
                                          "it; the Sway/wlroots one does not yet).")
                             : std::string();
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_control = granted;
                m_state = State::Ready;
                m_status = status;
            }
            LOG_INFO("Server", "Wayland portal ready: node %u, %dx%d at %d,%d, control %s, pointer %s", m_streamNode,
                     m_streamWidth, m_streamHeight, m_streamX, m_streamY, granted ? "yes" : "no",
                     cursorMode == 4 ? "separate" : cursorMode == 2 ? "painted in" : "hidden");
            return true;
        }

        void WaylandPortalSession::Monitor()
        {
            // Looks for news without blocking and sleeps in between: while a read waits, a control call made from
            // another thread would wait for it, and key presses must not.
            while (!m_stop.load())
            {
                DBusMessageIn message;
                if (m_link->Receive(message, 0))
                {
                    if (message.type == DBusMessageIn::Type::Signal && message.interfaceName == kSession &&
                        message.member == "Closed" && message.path == m_sessionPath)
                    {
                        SetState(State::Ended,
                                 "The desktop stopped sharing the screen (the sharing indicator was closed, or the "
                                 "session ended). Choose \"Check again\" in the Marconi Server tray menu to share "
                                 "again.");
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_control = false;
                        return;
                    }
                }
                else if (m_capture.GetState() == PipeWireCapture::State::Failed)
                {
                    SetState(State::Ended, m_capture.GetFailure() + " Choose \"Check again\" in the Marconi Server "
                                                                    "tray menu to share again.");
                    return;
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
        }

        bool WaylandPortalSession::PointerMotionAbsolute(double x, double y, int32_t pictureWidth, int32_t pictureHeight)
        {
            if (!HasControl() || m_link == nullptr)
            {
                return false;
            }
            int32_t sx = 0;
            int32_t sy = 0;
            int32_t sw = 0;
            int32_t sh = 0;
            GetStreamGeometry(sx, sy, sw, sh);
            // The picture is in the display's own pixels, the desktop counts in its (possibly scaled) own units.
            if (sw > 0 && sh > 0 && pictureWidth > 0 && pictureHeight > 0)
            {
                x = x * sw / pictureWidth;
                y = y * sh / pictureHeight;
            }
            return m_link->CallNoReply(kBusName, kObjectPath, kRemoteDesktop, "NotifyPointerMotionAbsolute",
                                       {DBusValue::Path(m_sessionPath), DBusValue::Dict(),
                                        DBusValue::Uint(m_streamNode), DBusValue::Double(x), DBusValue::Double(y)});
        }

        bool WaylandPortalSession::PointerButton(int32_t evdevButton, bool down)
        {
            if (!HasControl() || m_link == nullptr)
            {
                return false;
            }
            return m_link->CallNoReply(kBusName, kObjectPath, kRemoteDesktop, "NotifyPointerButton",
                                       {DBusValue::Path(m_sessionPath), DBusValue::Dict(), DBusValue::Int(evdevButton),
                                        DBusValue::Uint(down ? 1 : 0)});
        }

        bool WaylandPortalSession::PointerAxisSteps(bool horizontal, int32_t steps)
        {
            if (!HasControl() || m_link == nullptr)
            {
                return false;
            }
            return m_link->CallNoReply(kBusName, kObjectPath, kRemoteDesktop, "NotifyPointerAxisDiscrete",
                                       {DBusValue::Path(m_sessionPath), DBusValue::Dict(),
                                        DBusValue::Uint(horizontal ? 1 : 0), DBusValue::Int(steps)});
        }

        bool WaylandPortalSession::Key(uint32_t keysym, bool down)
        {
            if (!HasControl() || m_link == nullptr)
            {
                return false;
            }
            return m_link->CallNoReply(kBusName, kObjectPath, kRemoteDesktop, "NotifyKeyboardKeysym",
                                       {DBusValue::Path(m_sessionPath), DBusValue::Dict(),
                                        DBusValue::Int(static_cast<int32_t>(keysym)), DBusValue::Uint(down ? 1 : 0)});
        }
    } // namespace Server
} // namespace Marconi
