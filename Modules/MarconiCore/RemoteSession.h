// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the viewer needs from a connection to another computer, whatever protocol carries it: the picture, input,
// the clipboard, displays and a few facts about the connection. Rfb::Client is one implementation.

#pragma once

#include <MarconiCore/Net/SshTunnel.h>
#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Rfb/ClipboardChannel.h>
#include <MarconiCore/Rfb/Framebuffer.h>
#include <MarconiCore/Rfb/Handshake.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    // Cursor image for local rendering: ARGB pixels (alpha 0 or 255), hotspot in pixels. Empty = hidden.
    struct CursorShape
    {
        int width = 0;
        int height = 0;
        int hotspotX = 0;
        int hotspotY = 0;
        std::vector<uint32_t> argb;
    };

    struct DisconnectInfo
    {
        std::string error;                 // empty for a clean, locally requested or cancelled disconnect
        bool authenticationFailed = false; // the server rejected the credentials; retrying may help
        bool wasConnected = false;         // got as far as the server describing its desktop
        bool tightFailed = false;          // VNC: the TightVNC login went wrong (not the password): retry without it
        bool encryptionUnusable = false;   // VNC: the server only offers encryption this computer can't make: retry
                                           // without asking for encryption
        bool serverUpdating = false;       // Marconi Server said it was starting an update, then went: reconnect
    };

    // Counts since the last TakeTrafficStats().
    struct TrafficStats
    {
        int updates = 0;
        int rectangles = 0;
        int requests = 0;
        int pointerEvents = 0;
        int keyEvents = 0;
    };

    // Callbacks arrive on the connection thread. UI implementations should marshal to the UI thread.
    class SessionListener
    {
    public:
        virtual ~SessionListener() = default;

        virtual void OnConnected(const Rfb::ServerInit& /*serverInit*/) {}
        // A complete picture update has been applied; `rects` are the areas that changed.
        virtual void OnFramebufferUpdated(const std::vector<Rfb::Rect>& /*rects*/) {}
        virtual void OnDesktopResized(int /*width*/, int /*height*/) {}
        // The server described its displays, after any OnDesktopResized for the same change. Servers that can't
        // never call this.
        virtual void OnScreenLayout(const std::vector<Rfb::Screen>& /*screens*/) {}
        // The server turned down RequestDesktopSize (status 1 forbidden, 2 out of resources, 3 bad layout).
        virtual void OnDesktopResizeRefused(int /*status*/) {}
        // The server sent its cursor shape: draw it locally (the framebuffer no longer contains it).
        virtual void OnCursorShape(const CursorShape& /*cursor*/) {}
        // The server says where its pointer is (desktop coordinates). Only servers that track it call this.
        virtual void OnCursorPosition(int /*x*/, int /*y*/) {}
        // Text from a server that only carries Western European characters (plain VNC).
        virtual void OnServerCutText(const std::string& /*latin1Text*/) {}
        // The server's clipboard: UTF-8 text and/or an image.
        virtual void OnServerClipboard(const Rfb::ClipboardContent& /*content*/) {}
        virtual void OnBell() {}
        // Exactly once per Connect().
        virtual void OnDisconnected(const DisconnectInfo& /*info*/) {}
        // Blocking request for credentials. Return false to cancel the connection.
        virtual bool OnCredentialsRequired(Rfb::CredentialsKind /*kind*/, Rfb::Credentials& /*inOutCredentials*/)
        {
            return false;
        }
        // Blocking: the server's certificate isn't trusted by this computer. Return true to connect anyway.
        virtual bool OnUntrustedCertificate(const Net::TlsCertificate& /*certificate*/) { return false; }
        // Blocking: connecting through SSH, and the SSH server's key isn't the one trusted before (or none was).
        // Return true to trust it. Only VNC-over-SSH asks this; other connections never call it.
        virtual bool OnUntrustedSshHostKey(const Net::SshHostKeyQuestion& /*question*/) { return false; }
    };

    // A connection the viewer drives. Connecting is protocol-specific and lives on the implementation; everything
    // the window, canvas and clipboard need afterwards is here. Every method is safe to call from any thread, and
    // the input and clipboard methods are silently ignored while not connected.
    class RemoteSession
    {
    public:
        virtual ~RemoteSession() = default;

        // Closes the connection and waits for the connection thread to finish (see Rfb::Client::Disconnect for the
        // one case where it can't wait).
        virtual void Disconnect() = 0;
        virtual bool IsConnected() const = 0;

        // Whether the connection is encrypted, and with what (e.g. "TLS 1.3"). Meaningful once connected.
        virtual bool IsEncrypted() const = 0;
        virtual std::string GetEncryptionName() const = 0;

        // The picture, written by the connection thread under its mutex.
        virtual Rfb::Framebuffer& GetFramebuffer() = 0;

        // Input, in desktop coordinates.
        virtual void SendPointerEvent(int x, int y, uint8_t buttonMask) = 0;
        virtual void SendKeyEvent(uint32_t keysym, bool down) = 0;

        // The clipboard. OfferClipboard tells the server what we have; false means the server takes none of the
        // content's formats this way, so text should go through SendClientCutText instead (plain VNC servers).
        // RequestServerClipboard asks for the server's clipboard, delivered through OnServerClipboard.
        virtual void SendClientCutText(const std::string& latin1Text) = 0;
        virtual bool HasExtendedClipboard() const = 0;
        virtual bool ServerAcceptsClipboardImages() const = 0;
        virtual bool OfferClipboard(const Rfb::ClipboardContent& content) = 0;
        virtual bool RequestServerClipboard() = 0;

        // Asks for the whole picture again.
        virtual void RequestFullUpdate() = 0;

        // The server's displays, once it has described them; empty otherwise.
        virtual std::vector<Rfb::Screen> GetScreens() const = 0;
        // Whether the server can be asked to change its screen size, and the request. The answer comes as
        // OnDesktopResized and OnScreenLayout, or OnDesktopResizeRefused.
        virtual bool SupportsDesktopResize() const = 0;
        virtual void RequestDesktopSize(int width, int height) = 0;

        // Asks a server that can stream one display at a time to send just that display (0 = the whole desktop).
        // Coordinates stay desktop coordinates, so the viewer still shows the display through its viewport. Servers
        // that always send everything ignore it.
        virtual void SelectDisplay(uint32_t /*displayId*/) {}

        // Picture quality on a live connection (see QualitySettings). Ignored until connected.
        virtual void SetQuality(int compressLevel, int qualityLevel) = 0;

        // How long since the last key or pointer event was sent (or attempted).
        virtual std::chrono::steady_clock::duration GetTimeSinceInput() const = 0;

        // Returns the counts so far and starts again from zero.
        virtual TrafficStats TakeTrafficStats() = 0;

        // Asks the other computer to lock its screen, in the connection's own words (the Marconi protocol's Lock
        // message). False when this kind of connection has no such message, so the caller sends the lock keys
        // instead (Win+L). True means the request was sent, not that the lock happened.
        virtual bool SendLock() { return false; }

        // How the connection's datagram channel is doing, for the status bar: "UDP", "UDP, 3% loss", "over TCP"
        // (Wire::DatagramStatusText). Empty when the connection has no datagram channel (everything but a Marconi
        // server that offered one). GetRoundTripMs is the channel's measured round trip, -1 when unknown.
        virtual std::string GetTransportStatus() const { return std::string(); }
        virtual int GetRoundTripMs() const { return -1; }
    };

    namespace Rfb
    {
        // The names the VNC code and its tests grew up with.
        using ClientListener = Marconi::SessionListener;
        using CursorShape = Marconi::CursorShape;
        using DisconnectInfo = Marconi::DisconnectInfo;
        using TrafficStats = Marconi::TrafficStats;
    } // namespace Rfb
} // namespace Marconi
