// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: the private D-Bus daemon and the fake desktop portal of the Wayland tests. See WaylandFakes.h.

#include "WaylandFakes.h"

#if defined(__linux__)

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace MarconiServerTest
{
    using namespace Marconi::Server;

    PrivateBus::~PrivateBus()
    {
        if (m_pid > 0)
        {
            kill(m_pid, SIGTERM);
            int status = 0;
            waitpid(m_pid, &status, 0);
        }
        if (!m_configPath.empty())
        {
            std::remove(m_configPath.c_str());
        }
    }

    bool PrivateBus::Start()
    {
        // A bus that starts nothing by itself: the default session bus would start the computer's real desktop
        // portal when asked for one.
        m_configPath = "/tmp/marconi-test-bus-" + std::to_string(getpid()) + ".conf";
        {
            std::ofstream config(m_configPath);
            config << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" "
                      "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                      "<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>"
                      "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                      "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n";
        }
        int fds[2];
        if (pipe(fds) != 0)
        {
            return false;
        }
        m_pid = fork();
        if (m_pid == 0)
        {
            dup2(fds[1], STDOUT_FILENO);
            close(fds[0]);
            close(fds[1]);
            std::string configArgument = "--config-file=" + m_configPath;
            execlp("dbus-daemon", "dbus-daemon", configArgument.c_str(), "--nofork", "--nopidfile", "--print-address",
                   nullptr);
            _exit(127);
        }
        close(fds[1]);
        if (m_pid < 0)
        {
            close(fds[0]);
            m_pid = 0;
            return false;
        }
        char buffer[512] = {};
        size_t used = 0;
        while (used < sizeof(buffer) - 1)
        {
            ssize_t got = read(fds[0], buffer + used, 1);
            if (got <= 0 || buffer[used] == '\n')
            {
                break;
            }
            ++used;
        }
        close(fds[0]);
        m_address.assign(buffer, used);
        if (m_address.empty())
        {
            int status = 0;
            waitpid(m_pid, &status, 0); // dbus-daemon isn't installed
            m_pid = 0;
            return false;
        }
        return true;
    }

    FakePortal::~FakePortal() { Stop(); }

    bool FakePortal::Start(const std::string& busAddress)
    {
        std::string error;
        m_link = DBusLink::Connect(busAddress, error);
        if (m_link == nullptr || !m_link->RequestName("org.freedesktop.portal.Desktop", error))
        {
            return false;
        }
        m_thread = std::thread([this]() { Run(); });
        return true;
    }

    void FakePortal::Stop()
    {
        m_stop.store(true);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        m_link.reset();
    }

    void FakePortal::Configure(const FakePortalConfig& config)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = config;
    }

    std::vector<std::string> FakePortal::Calls()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_calls;
    }

    std::vector<std::string> FakePortal::Input()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_input;
    }

    std::string FakePortal::LastRestoreTokenSeen()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_restoreSeen;
    }

    std::string FakePortal::LastCursorModeSeen()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cursorSeen;
    }

    bool FakePortal::SessionClosed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    void FakePortal::EndSession()
    {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            path = m_sessionPath;
        }
        m_link->EmitSignal(path, "org.freedesktop.portal.Session", "Closed", {DBusValue::Dict()});
    }

    void FakePortal::Run()
    {
        while (!m_stop.load())
        {
            DBusMessageIn message;
            if (m_link->Receive(message, 100) && message.type == DBusMessageIn::Type::Call)
            {
                Handle(message);
            }
        }
    }

    namespace
    {
        std::string Sanitize(std::string sender)
        {
            if (!sender.empty() && sender[0] == ':')
            {
                sender.erase(0, 1);
            }
            for (char& c : sender)
            {
                if (c == '.')
                {
                    c = '_';
                }
            }
            return sender;
        }

        std::string Short(const std::string& interfaceName)
        {
            size_t dot = interfaceName.rfind('.');
            return dot == std::string::npos ? interfaceName : interfaceName.substr(dot + 1);
        }
    } // namespace

    void FakePortal::Handle(const DBusMessageIn& call)
    {
        FakePortalConfig config;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            config = m_config;
        }
        const std::string name = Short(call.interfaceName) + "." + call.member;
        const bool remote = call.interfaceName == "org.freedesktop.portal.RemoteDesktop";
        const bool cast = call.interfaceName == "org.freedesktop.portal.ScreenCast";

        if (call.interfaceName == "org.freedesktop.DBus.Properties" && call.member == "Get" && call.args.size() == 2)
        {
            const std::string target = call.args[0].text;
            const std::string property = call.args[1].text;
            if (target == "org.freedesktop.portal.RemoteDesktop" && !config.offerRemoteDesktop)
            {
                m_link->ReplyError(call, "org.freedesktop.DBus.Error.UnknownInterface", "no such interface");
                return;
            }
            uint32_t value = 0;
            if (property == "version")
            {
                value = target == "org.freedesktop.portal.RemoteDesktop" ? 2 : 4;
            }
            else if (property == "AvailableCursorModes")
            {
                value = config.cursorModes;
            }
            else if (property == "AvailableDeviceTypes")
            {
                value = 3;
            }
            else if (property == "AvailableSourceTypes")
            {
                value = 1;
            }
            m_link->Reply(call, {DBusValue::Variant(DBusValue::Uint(value))});
            return;
        }

        if ((remote && !config.offerRemoteDesktop) || (!remote && !cast && call.interfaceName != "org.freedesktop.portal.Session"))
        {
            m_link->ReplyError(call, "org.freedesktop.DBus.Error.UnknownMethod", "no such method");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (remote || cast)
            {
                m_calls.push_back(name);
            }
        }

        const DBusValue* options = call.args.empty() ? nullptr : &call.args.back();
        auto optionText = [&](const char* key) {
            const DBusValue* v = options != nullptr ? options->Find(key) : nullptr;
            return v != nullptr ? v->text : std::string();
        };

        // Input calls: record them.
        if (remote && call.member.rfind("Notify", 0) == 0)
        {
            char line[160];
            const auto& a = call.args;
            if (call.member == "NotifyPointerMotionAbsolute" && a.size() == 5)
            {
                std::snprintf(line, sizeof(line), "motion %.1f %.1f node %u", a[3].real, a[4].real, a[2].AsUint());
            }
            else if (call.member == "NotifyPointerButton" && a.size() == 4)
            {
                std::snprintf(line, sizeof(line), "button %d %u", static_cast<int>(a[2].integer), a[3].AsUint());
            }
            else if (call.member == "NotifyPointerAxisDiscrete" && a.size() == 4)
            {
                std::snprintf(line, sizeof(line), "axis %u %d", a[2].AsUint(), static_cast<int>(a[3].integer));
            }
            else if (call.member == "NotifyKeyboardKeysym" && a.size() == 4)
            {
                std::snprintf(line, sizeof(line), "key %d %u", static_cast<int>(a[2].integer), a[3].AsUint());
            }
            else
            {
                std::snprintf(line, sizeof(line), "%s ?", call.member.c_str());
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_input.push_back(line);
            return;
        }

        if (call.interfaceName == "org.freedesktop.portal.Session" && call.member == "Close")
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_closed = true;
            }
            m_link->Reply(call, {});
            return;
        }

        const std::string sender = Sanitize(call.sender);
        if (call.member == "OpenPipeWireRemote")
        {
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            bool connected = false;
            if (!config.pipewireSocket.empty() && fd >= 0)
            {
                sockaddr_un address = {};
                address.sun_family = AF_UNIX;
                std::strncpy(address.sun_path, config.pipewireSocket.c_str(), sizeof(address.sun_path) - 1);
                connected = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
            }
            (void)connected;
            m_link->Reply(call, {DBusValue::Fd(fd)});
            close(fd);
            return;
        }

        // The calls that answer through a Response signal.
        const std::string token = optionText("handle_token");
        const std::string requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
        DBusValue results = DBusValue::Dict();
        uint32_t code = 0;
        if (call.member == "CreateSession")
        {
            std::string sessionPath = "/org/freedesktop/portal/desktop/session/" + sender + "/" + optionText("session_handle_token");
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_sessionPath = sessionPath;
            }
            results.Add("session_handle", DBusValue::String(sessionPath));
        }
        else if (call.member == "SelectDevices" || call.member == "SelectSources")
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!optionText("restore_token").empty())
            {
                m_restoreSeen = optionText("restore_token");
            }
            if (call.member == "SelectSources")
            {
                const DBusValue* mode = options != nullptr ? options->Find("cursor_mode") : nullptr;
                m_cursorSeen = mode != nullptr ? std::to_string(mode->AsUint()) : std::string();
            }
        }
        else if (call.member == "Start")
        {
            if (config.answerDelayMs > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.answerDelayMs));
            }
            if (!config.approve)
            {
                code = 1;
            }
            else
            {
                results.Add("devices", DBusValue::Uint(remote && config.grantControl ? 3 : 0));
                DBusValue properties = DBusValue::Dict();
                if (config.streamWidth > 0)
                {
                    properties.Add("size", DBusValue::StructOf({DBusValue::Int(config.streamWidth),
                                                                 DBusValue::Int(config.streamHeight)}));
                    properties.Add("position", DBusValue::StructOf({DBusValue::Int(0), DBusValue::Int(0)}));
                }
                DBusValue streams;
                streams.kind = DBusValue::Kind::Array;
                streams.elementType = "(ua{sv})";
                streams.items.push_back(DBusValue::StructOf({DBusValue::Uint(config.node), properties}));
                results.Add("streams", streams);
                if (config.giveRestoreToken)
                {
                    results.Add("restore_token", DBusValue::String("fake-token-" + std::to_string(++m_tokenCounter)));
                }
            }
        }
        m_link->Reply(call, {DBusValue::Path(requestPath)});
        m_link->EmitSignal(requestPath, "org.freedesktop.portal.Request", "Response", {DBusValue::Uint(code), results});
    }
} // namespace MarconiServerTest

#endif
