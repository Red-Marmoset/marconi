// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: stand-ins for a Wayland desktop, for the tests of the Wayland capture and input: a private D-Bus
// daemon, a fake desktop portal on it (answering like xdg-desktop-portal, recording the keyboard and mouse calls),
// and a fake screen that publishes a PipeWire video stream of a known picture.

#pragma once

#if defined(__linux__)

#include <MarconiServerCore/DBusLite.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

namespace MarconiServerTest
{
    // A dbus-daemon of its own, so the tests never touch the real session bus. Start() is false when dbus-daemon
    // isn't installed.
    class PrivateBus
    {
    public:
        ~PrivateBus();
        bool Start();
        const std::string& Address() const { return m_address; }

    private:
        pid_t m_pid = 0;
        std::string m_address;
        std::string m_configPath;
    };

    struct FakePortalConfig
    {
        bool offerRemoteDesktop = true; // false: ScreenCast only, like a wlroots desktop
        bool approve = true;            // false: the person says no
        uint32_t declineCode = 1;       // the answer when not approved: 1 = the person said no, 2 = it failed
        bool grantControl = true;       // false: share the screen, but no keyboard and mouse
        uint32_t cursorModes = 7;       // hidden | embedded | metadata
        int answerDelayMs = 0;          // how long "the person" takes to answer
        uint32_t node = 0;              // the PipeWire node the stream is on
        int streamWidth = 0;            // the size the portal reports for the stream (0 = leave out)
        int streamHeight = 0;
        std::string pipewireSocket;     // PipeWire's socket to hand out; empty = hand out a useless descriptor
        bool giveRestoreToken = true;
    };

    // Plays the desktop portal's part of the conversation, on its own thread and its own bus connection.
    class FakePortal
    {
    public:
        explicit FakePortal(const FakePortalConfig& config) : m_config(config) {}
        ~FakePortal();
        bool Start(const std::string& busAddress);
        void Stop();

        // Everything the server called, in order: "RemoteDesktop.CreateSession", "ScreenCast.SelectSources", ...
        std::vector<std::string> Calls();
        // The keyboard and mouse calls, one text line each ("motion 120.5 60.5 node 42", "button 272 1",
        // "axis 0 -1", "key 97 1").
        std::vector<std::string> Input();
        std::string LastRestoreTokenSeen();
        std::string LastCursorModeSeen();
        bool SessionClosed();
        // The desktop ends the sharing by itself (the person closes the "sharing" indicator).
        void EndSession();
        void Configure(const FakePortalConfig& config);

    private:
        void Run();
        void Handle(const Marconi::Server::DBusMessageIn& call);

        FakePortalConfig m_config;
        std::unique_ptr<Marconi::Server::DBusLink> m_link;
        std::thread m_thread;
        std::atomic<bool> m_stop{false};
        std::mutex m_mutex;
        std::vector<std::string> m_calls;
        std::vector<std::string> m_input;
        std::string m_restoreSeen;
        std::string m_cursorSeen;
        std::string m_sessionPath;
        bool m_closed = false;
        int m_tokenCounter = 0;
    };

    // A fake screen: a PipeWire video source in this process's PipeWire connection (the daemon must be running,
    // XDG_RUNTIME_DIR pointing at it). The picture is a background colour with rectangles painted on it.
    class FakeScreen
    {
    public:
        FakeScreen();
        ~FakeScreen();
        bool Start(uint32_t width, uint32_t height, uint32_t pixelFormat, std::string& error);
        void Stop();
        uint32_t NodeId() const { return m_node; }

        void Fill(uint32_t rgb);
        void FillRect(int x, int y, int width, int height, uint32_t rgb);
        // Changes the stream's size (a display resolution change); the consumer sees a new format.
        void Resize(uint32_t width, uint32_t height);
        // Pushes a pointer image through the stream's cursor data (shape and position).
        void SetCursor(int x, int y, int hotspotX, int hotspotY, int width, int height, uint32_t argb);
        uint64_t FramesSent() const { return m_frames.load(); }

        struct Impl;

    private:
        std::unique_ptr<Impl> m_impl;
        uint32_t m_node = 0;
        std::atomic<uint64_t> m_frames{0};
    };

    // PipeWire's socket under XDG_RUNTIME_DIR ("pipewire-0"), or empty when no daemon is running there.
    std::string FindPipeWireSocket();
} // namespace MarconiServerTest

#endif
