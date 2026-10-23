// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: finding the server, connecting, logging in, trusting certificates, and what to do when the connection ends

#include "ViewerFrame.h"

#include "ClipboardWatch.h"
#include "ConnectionStore.h"
#include "CredentialsDialog.h"
#include "FilePanel.h"

#include <MarconiCore/Net/ProtocolChoice.h>
#include <MarconiCore/Net/WakeOnLan.h>
#include <MarconiCore/Rdp/RdpClient.h>
#include <MarconiCore/Text.h>
#include <MarconiCore/Wire/WireClient.h>

#include <Log/Log.h>

#include <wx/display.h>
#include <wx/msgdlg.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>

namespace MarconiUI
{
    namespace
    {
        // "AB:CD:..." in two lines, as browsers show certificate fingerprints.
        wxString FormatFingerprint(const std::vector<uint8_t>& sha256)
        {
            wxString text;
            for (size_t i = 0; i < sha256.size(); ++i)
            {
                if (i > 0)
                {
                    text += i == sha256.size() / 2 ? "\n" : ":";
                }
                text += wxString::Format("%02X", sha256[i]);
            }
            return text;
        }
    } // namespace

    void ViewerFrame::Connect()
    {
        const Marconi::Address& address = m_options.address;
        long rememberedPort = 0;
        if (address.protocol == Marconi::Protocol::Marconi)
        {
            ConnectMarconi(Marconi::EffectivePort(address));
        }
        else if (address.protocol == Marconi::Protocol::Rdp)
        {
            ConnectRdp(Marconi::EffectivePort(address));
        }
        else if (m_options.sshTunnel)
        {
            // The VNC port may not be reachable from here at all; the SSH server connects to it on our behalf.
            ConnectVnc(Marconi::EffectivePort(address));
        }
        else if (address.protocol == Marconi::Protocol::Vnc || m_options.skipProbe)
        {
            ConnectVnc(Marconi::EffectivePort(address));
        }
        else if (address.protocol == Marconi::Protocol::Auto && m_options.detected.StartsWith("marconi ") &&
                 m_options.detected.Mid(8).ToLong(&rememberedPort) && rememberedPort > 0 && rememberedPort < 65536)
        {
            m_usingRemembered = true;
            ConnectMarconi(static_cast<uint16_t>(rememberedPort));
        }
        else if (address.protocol == Marconi::Protocol::Auto && m_options.detected.StartsWith("vnc ") &&
                 m_options.detected.Mid(4).ToLong(&rememberedPort) && rememberedPort > 0 && rememberedPort < 65536)
        {
            m_usingRemembered = true;
            ConnectVnc(static_cast<uint16_t>(rememberedPort));
        }
        else if (address.protocol == Marconi::Protocol::Auto && m_options.detected.StartsWith("rdp ") &&
                 m_options.detected.Mid(4).ToLong(&rememberedPort) && rememberedPort > 0 && rememberedPort < 65536)
        {
            m_usingRemembered = true;
            ConnectRdp(static_cast<uint16_t>(rememberedPort));
        }
        else
        {
            StartProbe();
        }
    }

    void ViewerFrame::ConnectVnc(uint16_t port)
    {
        m_port = port;
        m_reconnect = [this]() { ConnectVnc(m_port); };
        m_clipboard.Reset();
        Marconi::Address shown = m_options.address;
        shown.port = port;
        shown.protocol = Marconi::Protocol::Vnc;
        SetStatusText("Connecting to " + wxString::FromUTF8(Marconi::FormatAddress(shown)) + "...");
        Marconi::Rfb::ConnectionOptions connection;
        connection.host = m_options.address.host;
        connection.port = port;
        connection.credentials = m_options.credentials;
        connection.trustedCertificateSha256 = m_options.trustedCertificateSha256;
        connection.encodings = m_options.encodings;
        connection.exactEncodings = !m_options.encodings.empty();
        connection.resendPixelFormatOnResize = m_options.resendPixelFormatOnResize;
        connection.allowTight = m_allowTight;
        connection.allowEncryption = m_allowEncryption;
        connection.sshTunnel = m_options.sshTunnel;
        connection.trustedSshHostKey = m_options.sshHostKey.utf8_string();
        Marconi::QualitySettings quality = Marconi::SettingsFor(m_quality);
        connection.compressLevel = quality.compressLevel;
        connection.qualityLevel = quality.qualityLevel;
        EnsureRfbClient().Connect(connection);
    }

    Marconi::Rfb::Client& ViewerFrame::EnsureRfbClient()
    {
        if (m_rfb == nullptr)
        {
            // The upcast to the (private) listener base happens here, in a member, where it is accessible.
            Marconi::SessionListener& listener = *this;
            std::unique_ptr<Marconi::Rfb::Client> client = std::make_unique<Marconi::Rfb::Client>(listener);
            m_rfb = client.get();
            m_wire = nullptr; // a fallback switches protocols: the previous client goes with m_session
            m_rdp = nullptr;
            m_session = std::move(client);
            m_canvas->SetSession(m_session.get());
            m_clipboard.SetSession(m_session.get());
        }
        return *m_rfb;
    }

    Marconi::Wire::Client& ViewerFrame::EnsureWireClient()
    {
        if (m_wire == nullptr)
        {
            Marconi::SessionListener& listener = *this;
            std::unique_ptr<Marconi::Wire::Client> client = std::make_unique<Marconi::Wire::Client>(listener);
            m_wire = client.get();
            m_rfb = nullptr;
            m_rdp = nullptr;
            m_session = std::move(client);
            m_canvas->SetSession(m_session.get());
            m_clipboard.SetSession(m_session.get());
        }
        return *m_wire;
    }

    Marconi::Rdp::Client& ViewerFrame::EnsureRdpClient()
    {
        if (m_rdp == nullptr)
        {
            Marconi::SessionListener& listener = *this;
            std::unique_ptr<Marconi::Rdp::Client> client = std::make_unique<Marconi::Rdp::Client>(listener);
            m_rdp = client.get();
            m_rfb = nullptr;
            m_wire = nullptr;
            m_session = std::move(client);
            m_canvas->SetSession(m_session.get());
            m_clipboard.SetSession(m_session.get());
        }
        return *m_rdp;
    }

    wxSize ViewerFrame::DesktopSizeForRdp() const
    {
        // A Remote Desktop server makes its desktop the size the client asks for, so ask for the display this
        // window is on (the Fit scale mode then shows it whole, however the window is sized). RDP wants a width
        // that is a multiple of 4.
        wxSize size(1024, 768);
        if (!m_options.headless)
        {
            int index = wxDisplay::GetFromWindow(this);
            wxDisplay display(index == wxNOT_FOUND ? 0u : static_cast<unsigned>(index));
            wxRect area = display.GetClientArea();
            if (area.width >= 640 && area.height >= 480)
            {
                size = area.GetSize();
            }
        }
        size.x = std::min(size.x & ~3, 8192);
        size.y = std::min(size.y, 8192);
        return size;
    }

    void ViewerFrame::ConnectRdp(uint16_t port)
    {
        m_port = port;
        m_reconnect = [this]() { ConnectRdp(m_port); };
        m_clipboard.Reset();
        Marconi::Address shown = m_options.address;
        shown.port = port;
        shown.protocol = Marconi::Protocol::Rdp;
        SetStatusText("Connecting to " + wxString::FromUTF8(Marconi::FormatAddress(shown)) + "...");
        Marconi::Rdp::ClientOptions options;
        options.host = m_options.address.host;
        options.port = port;
        options.credentials = m_options.credentials;
        options.trustedCertificateSha256 = m_options.trustedCertificateSha256;
        wxSize size = DesktopSizeForRdp();
        options.desktopWidth = static_cast<uint16_t>(size.x);
        options.desktopHeight = static_cast<uint16_t>(size.y);
        EnsureRdpClient().Connect(options);
    }

    void ViewerFrame::ConnectMarconi(uint16_t port)
    {
        m_port = port;
        m_reconnect = [this]() { ConnectMarconi(m_port); };
        m_clipboard.Reset();
        Marconi::Address shown = m_options.address;
        shown.port = port;
        shown.protocol = Marconi::Protocol::Marconi;
        SetStatusText("Connecting to " + wxString::FromUTF8(Marconi::FormatAddress(shown)) + "...");
        Marconi::Wire::ClientOptions options;
        options.host = m_options.address.host;
        options.port = port;
        options.password = m_options.credentials.password;
        options.clientName = "Marconi";
        options.trustedCertificateSha256 = m_options.trustedCertificateSha256;
        EnsureWireClient().Connect(options);
    }

    void ViewerFrame::StartProbe()
    {
        const wxString host = wxString::FromUTF8(m_options.address.host);
        SetStatusText("Looking for a remote desktop on " + host + wxString::FromUTF8("\xE2\x80\xA6")); // an ellipsis
        StopProbe();
        m_probeCancel = false;
        Marconi::Address address = m_options.address;
        m_probeThread = std::thread([this, address]() {
            std::vector<Marconi::Net::ProbeResult> results =
                Marconi::Net::Probe(address.host, Marconi::Net::DefaultProbeTargets(address), 1500, &m_probeCancel);
            if (!m_probeCancel)
            {
                CallAfter([this, results]() { HandleProbe(results); });
            }
        });
    }

    void ViewerFrame::StopProbe()
    {
        m_probeCancel = true;
        if (m_probeThread.joinable())
        {
            m_probeThread.join();
        }
    }

    void ViewerFrame::HandleProbe(const std::vector<Marconi::Net::ProbeResult>& results)
    {
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            if (m_closing)
            {
                return;
            }
        }
        for (const Marconi::Net::ProbeResult& result : results)
        {
            std::string detail = result.detail.empty() ? "" : " (" + result.detail + ")";
            LOG_INFO("Viewer", "Probe %s port %u: %s%s", m_options.address.host.c_str(), result.port,
                     result.reachable ? Marconi::ProtocolName(result.protocol) : "no answer", detail.c_str());
        }
        Marconi::Net::Choice choice = Marconi::Net::ChooseProtocol(m_options.address, results);
        if (choice.kind == Marconi::Net::Choice::Kind::Connect)
        {
            m_fallbacks = choice.fallbacks;
            Marconi::Net::ProbeResult chosen;
            chosen.protocol = choice.protocol;
            chosen.port = choice.port;
            ConnectFound(chosen);
            return;
        }
        wxString message = wxString::FromUTF8(Marconi::Net::DescribeChoice(choice, m_options.address));
        LOG_WARNING("Viewer", "%s", message.utf8_string().c_str());
        SetStatusText(message);
        if (!m_options.headless)
        {
            wxMessageBox(message, "Marconi", wxOK | wxICON_INFORMATION, this);
        }
        Close(true);
    }

    void ViewerFrame::ConnectFound(const Marconi::Net::ProbeResult& found)
    {
        const char* name = found.protocol == Marconi::Protocol::Marconi ? "marconi %u"
                           : found.protocol == Marconi::Protocol::Rdp   ? "rdp %u"
                                                                         : "vnc %u";
        if (m_options.address.protocol == Marconi::Protocol::Auto && m_options.address.port == 0)
        {
            RememberDetected(wxString::Format(name, static_cast<unsigned>(found.port)));
        }
        if (found.protocol == Marconi::Protocol::Marconi)
        {
            ConnectMarconi(found.port);
        }
        else if (found.protocol == Marconi::Protocol::Rdp)
        {
            ConnectRdp(found.port);
        }
        else
        {
            ConnectVnc(found.port);
        }
    }

    void ViewerFrame::RememberDetected(const wxString& detected)
    {
        // Unlike the window settings, this is a fact about the machine, so headless runs keep it too.
        if (m_options.savedId.empty() || m_options.detected == detected)
        {
            return;
        }
        m_options.detected = detected;
        ConnectionStore store;
        SavedConnection saved;
        if (store.Load(m_options.savedId, saved))
        {
            saved.detected = detected;
            store.Save(saved);
        }
    }

    bool ViewerFrame::AskUser(const std::function<bool()>& question)
    {
        std::unique_lock<std::mutex> lock(m_questionMutex);
        if (m_closing)
        {
            return false;
        }
        m_question = question;
        m_questionAnswered = false;
        m_questionAccepted = false;
        CallAfter([this]() { RunPendingQuestion(); });
        m_questionChanged.wait(lock, [this]() { return m_questionAnswered || m_closing; });
        return !m_closing && m_questionAccepted;
    }

    void ViewerFrame::RunPendingQuestion()
    {
        std::function<bool()> question;
        {
            // The window may have started closing after this was queued; nobody is waiting any more.
            std::lock_guard<std::mutex> lock(m_questionMutex);
            if (m_closing || !m_question)
            {
                return;
            }
            question = std::move(m_question);
            m_question = nullptr;
        }
        bool accepted = question();
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            m_questionAccepted = accepted;
            m_questionAnswered = true;
        }
        m_questionChanged.notify_all();
    }

    bool ViewerFrame::OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials)
    {
        // Connection thread: ask the UI thread and wait for the answer (or for the window to close).
        if (m_options.headless)
        {
            LOG_WARNING("Viewer", "Headless: the server wants credentials that aren't saved for this connection");
            return false;
        }
        bool accepted = AskUser([this, kind]() {
            SetStatusText("Waiting for credentials...");
            bool offerRemember = !m_options.savedId.empty() && ConnectionStore::IsKeychainAvailable();
            CredentialsDialog dialog(this, GetServerDisplayName(), kind,
                                     wxString::FromUTF8(m_options.credentials.username), offerRemember);
            if (dialog.ShowModal() != wxID_OK)
            {
                m_rememberPassword = false;
                return false;
            }
            m_rememberPassword = dialog.GetRemember();
            m_credentialsAnswer.username = dialog.GetUsername().utf8_string();
            m_credentialsAnswer.password = dialog.GetPassword().utf8_string();
            // Keep them for the rest of this window's life so reconnects don't ask again.
            m_options.credentials = m_credentialsAnswer;
            SetStatusText("Authenticating...");
            return true;
        });
        if (!accepted)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_questionMutex);
        credentials = m_credentialsAnswer;
        return true;
    }

    bool ViewerFrame::OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate)
    {
        // Connection thread.
        if (m_options.headless)
        {
            LOG_WARNING("Viewer", "Headless: the server's certificate isn't trusted (%s); not connecting",
                        certificate.problem.c_str());
            return false;
        }
        LOG_INFO("Viewer", "The server's certificate isn't trusted (%s); asking", certificate.problem.c_str());
        Marconi::Net::TlsCertificate copy = certificate; // the UI thread gets its own copy
        return AskUser([this, copy]() { return ConfirmCertificate(copy); });
    }

    bool ViewerFrame::ConfirmCertificate(const Marconi::Net::TlsCertificate& certificate)
    {
        SetStatusText("Waiting for you to check the server's certificate...");
        wxString message = wxString::Format(
            "%s identifies itself with a security certificate this computer doesn't trust (%s).\n\n"
            "Issued to: %s\nIssued by: %s\nValid until: %s\n\nFingerprint (SHA-256):\n%s\n\n"
            "If you set up that computer yourself, this is normal: connect anyway, and Marconi will remember this "
            "certificate for this connection. If you weren't expecting it, someone may be listening in: cancel and "
            "check the other computer.",
            wxString::FromUTF8(m_options.address.host), wxString::FromUTF8(certificate.problem),
            wxString::FromUTF8(certificate.subject), wxString::FromUTF8(certificate.issuer),
            wxString::FromUTF8(certificate.validUntil), FormatFingerprint(certificate.sha256));
        wxMessageDialog dialog(this, message, "Connection to " + GetServerDisplayName(),
                               wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        dialog.SetYesNoLabels("Connect Anyway", "Cancel");
        if (dialog.ShowModal() != wxID_YES)
        {
            return false;
        }
        // Reconnects in this window don't ask again, and neither does the saved connection next time.
        m_options.trustedCertificateSha256 = certificate.sha256;
        if (!m_options.savedId.empty())
        {
            ConnectionStore store;
            SavedConnection saved;
            if (store.Load(m_options.savedId, saved))
            {
                saved.certificate = wxString::FromUTF8(Marconi::BytesToHex(certificate.sha256));
                store.Save(saved);
            }
        }
        SetStatusText("Connecting securely...");
        return true;
    }

    bool ViewerFrame::OnUntrustedSshHostKey(const Marconi::Net::SshHostKeyQuestion& question)
    {
        // Connection thread.
        if (m_options.headless)
        {
            LOG_WARNING("Viewer", "Headless: the SSH host key (%s) isn't the trusted one; not connecting",
                        question.fingerprint.c_str());
            return false;
        }
        Marconi::Net::SshHostKeyQuestion copy = question; // the UI thread gets its own copy
        return AskUser([this, copy]() { return ConfirmSshHostKey(copy); });
    }

    bool ViewerFrame::ConfirmSshHostKey(const Marconi::Net::SshHostKeyQuestion& question)
    {
        // The same questions the file panel asks before SFTP, since it's the same key.
        SetStatusText("Waiting for you to check the computer's SSH key...");
        wxString fingerprint = wxString::FromUTF8(question.fingerprint);
        wxString name = GetServerDisplayName();
        if (!question.previous.empty())
        {
            wxMessageDialog changed(this,
                                    name + " identified itself differently from last time.\n\nThis happens when the "
                                    "computer has been reinstalled, but it can also mean another computer is pretending "
                                    "to be it. Only trust it if you know why it changed.\n\nWas: " +
                                        wxString::FromUTF8(question.previous) + "\nNow: " + fingerprint,
                                    "This computer has changed", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
            changed.SetYesNoLabels("Trust the New One", "Cancel");
            if (changed.ShowModal() != wxID_YES)
            {
                return false;
            }
        }
        else
        {
            wxMessageDialog trust(this,
                                  "Is this " + name + "?\n\nThis is the first time Marconi connects to it through SSH. "
                                  "Its security fingerprint is:\n\n" + fingerprint +
                                      "\n\nMarconi will remember it, and warn you if it ever changes.",
                                  "Trust this computer?", wxYES_NO | wxICON_QUESTION);
            trust.SetYesNoLabels("Trust", "Cancel");
            if (trust.ShowModal() != wxID_YES)
            {
                return false;
            }
        }
        // Reconnects in this window, the file panel and the saved connection all trust it from now on.
        m_options.sshHostKey = fingerprint;
        if (m_files != nullptr)
        {
            m_files->SetTrustedHostKey(fingerprint);
        }
        if (!m_options.savedId.empty())
        {
            ConnectionStore store;
            SavedConnection saved;
            if (store.Load(m_options.savedId, saved))
            {
                saved.sshHostKey = fingerprint;
                store.Save(saved);
            }
        }
        SetStatusText("Logging in over SSH...");
        return true;
    }

    void ViewerFrame::HandleConnected(const Marconi::Rfb::ServerInit& serverInit)
    {
        m_desktopName = wxString::FromUTF8(serverInit.name);
        SetTitle(GetServerDisplayName() + " - Marconi");
        m_connected = true;
        m_fallbacks.clear(); // this protocol works: reconnects stay with it
        if (m_rfb != nullptr && m_rfb->HasFileTransfer())
        {
            // TightVNC: files travel on this connection, with no second login.
            if (!m_tightFiles)
            {
                m_tightFiles = std::make_unique<Marconi::Files::TightFiles>(
                    [this](const Marconi::Rfb::Bytes& request) { return m_rfb->SendFileTransferRequest(request); });
            }
            m_tightFiles->Reopen();
            Marconi::Files::TightFiles* files = m_tightFiles.get();
            m_rfb->SetFileTransferHandler([files](const Marconi::Rfb::FileTransfer::Reply& reply) {
                files->OnReply(reply);
            });
            m_files->SetTightFiles(files, GetServerDisplayName());
            if (m_options.filesCheck)
            {
                // Diagnostics: a real round trip over TightVNC file transfer, on a worker like the panel's copies.
                m_saveThreads.emplace_back([this, files]() {
                    const std::string folder = "/C:/Users/Public/Marconi files check";
                    const std::string file = folder + "/check.txt";
                    const std::string content = "Marconi TightVNC files check " + std::to_string(std::rand());
                    std::string error;
                    std::vector<Marconi::Files::FileEntry> drives;
                    bool listed = files->List("/", drives, error);
                    LOG_INFO("Viewer", "Files check: listed / (%zu entries): %s", drives.size(),
                             listed ? "ok" : error.c_str());
                    bool ok = listed && files->MakeFolder(folder, error);
                    if (ok)
                    {
                        std::unique_ptr<Marconi::Files::FileWriter> writer = files->OpenWrite(file, false, error);
                        ok = writer && writer->Write(content.data(), content.size(), error) && writer->Finish(error);
                        LOG_INFO("Viewer", "Files check: upload %s %s", ok ? "ok" : "failed:", error.c_str());
                    }
                    std::string readBack;
                    if (ok)
                    {
                        std::unique_ptr<Marconi::Files::FileReader> reader = files->OpenRead(file, error);
                        char buffer[256];
                        ptrdiff_t got = 0;
                        while (reader && (got = reader->Read(buffer, sizeof(buffer), error)) > 0)
                        {
                            readBack.append(buffer, static_cast<size_t>(got));
                        }
                        ok = reader && got == 0 && readBack == content;
                        LOG_INFO("Viewer", "Files check: download %s %s", ok ? "ok" : "failed:", error.c_str());
                    }
                    std::string ignored;
                    files->Remove(file, ignored);
                    files->RemoveFolder(folder, ignored);
                    Marconi::Files::FileEntry leftover;
                    bool tidy = !files->Stat(folder, leftover, ignored);
                    LOG_INFO("Viewer", "Files check: %s (tidied up: %s)", ok ? "passed" : "failed", tidy ? "yes" : "no");
                    CallAfter([this]() { Close(true); });
                });
            }
        }
        if (m_rememberPassword)
        {
            // Only now: the credentials are known to work.
            m_rememberPassword = false;
            ConnectionStore store;
            SavedConnection saved;
            if (store.Load(m_options.savedId, saved))
            {
                saved.username = wxString::FromUTF8(m_options.credentials.username);
                saved.rememberPassword = true;
                store.Save(saved);
                store.SavePassword(saved.id, saved.username, wxString::FromUTF8(m_options.credentials.password));
            }
        }
        m_canvas->SetDesktopSize(serverInit.width, serverInit.height);
        if (m_reconnecting)
        {
            LeaveLostState();
        }
        else if (!m_windowRestored)
        {
            FitToDesktop();
        }
        if (m_options.fullScreen && !IsFullScreen())
        {
            ToggleFullScreen();
        }
        UpdateControls();
        UpdateSystemKeyCapture();
        UpdateStatus();
        m_canvas->SetFocus();
        SyncClipboardToServer();
        WaitForPicture();
        if (m_session)
        {
            m_session->TakeTrafficStats(); // start counting from here
        }
        m_statsTimer.Start(10000);
        m_healthTicks = 0;
        m_healthTimer.Start(1000);
        m_clipboardChangeCount = GetClipboardChangeCount();
        m_clipboardTimer.Start(500);
    }

    void ViewerFrame::HandleDisconnected(const Marconi::Rfb::DisconnectInfo& info)
    {
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            if (m_closing)
            {
                return;
            }
        }
        LOG_INFO("Viewer", "Disconnected: %s", info.error.empty() ? "(no error)" : info.error.c_str());
        if (m_tightFiles)
        {
            if (m_rfb != nullptr)
            {
                m_rfb->SetFileTransferHandler(nullptr);
            }
            m_tightFiles->Close();
            m_files->SetTightFiles(nullptr, GetServerDisplayName());
        }
        if (info.encryptionUnusable && m_allowEncryption)
        {
            // Typically a server offering only anonymous TLS, which this computer's TLS library can't make: connect
            // again without asking for encryption. The status bar then says "not encrypted".
            LOG_INFO("Viewer", "%s; connecting again without encryption", info.error.c_str());
            m_allowEncryption = false;
            ConnectVnc(m_port);
            return;
        }
        if (info.tightFailed && m_allowTight)
        {
            // Some servers offer TightVNC's own login but don't handle it well: try once more without it.
            LOG_INFO("Viewer", "The TightVNC login failed; trying the plain VNC login");
            m_allowTight = false;
            ConnectVnc(m_port);
            return;
        }
        if (m_autoReconnecting)
        {
            m_autoReconnecting = false;
            m_connected = false;
            StopConnectionTimers();
            m_reconnect();
            return;
        }
        StopConnectionTimers();
        m_canvas->ReleaseInput();
        m_connected = false;
        UpdateSystemKeyCapture();
        UpdateControls();
        if (!m_fallbacks.empty() && !info.error.empty() && !info.wasConnected && !info.authenticationFailed &&
            !m_reconnecting)
        {
            // The machine offers more than one way in and the best one failed (e.g. a Remote Desktop login Marconi
            // can't do): try the next.
            Marconi::Net::ProbeResult next = m_fallbacks.front();
            m_fallbacks.erase(m_fallbacks.begin());
            LOG_INFO("Viewer", "%s; trying %s on port %u instead", info.error.c_str(),
                     Marconi::ProtocolName(next.protocol), next.port);
            ConnectFound(next);
            return;
        }
        if (m_reconnecting && !info.authenticationFailed && !info.error.empty())
        {
            // An attempt failed: wait and try again, unless the time is up.
            m_retryUnderWay = false;
            ScheduleRetry(info.error);
            return;
        }
        if (m_reconnecting)
        {
            LeaveLostState(); // a rejected password or a deliberate close: the dialogs below take over
        }
        if (!info.error.empty() && m_usingRemembered && !m_reprobed && !info.wasConnected &&
            !info.authenticationFailed)
        {
            // The machine may have changed (e.g. VNC turned off, RDP turned on): look again once.
            LOG_INFO("Viewer", "The remembered connection didn't answer; probing %s again",
                     m_options.address.host.c_str());
            m_reprobed = true;
            m_usingRemembered = false;
            RememberDetected(wxEmptyString);
            StartProbe();
            return;
        }
        if (info.error.empty() || m_options.headless)
        {
            Close(true);
            return;
        }

        wxString caption = "Connection to " + GetServerDisplayName();
        if (info.authenticationFailed)
        {
            SetStatusText("Authentication failed");
            wxMessageDialog retry(this, wxString::FromUTF8(info.error) + "\n\nWould you like to try again?", caption,
                                  wxYES_NO | wxICON_WARNING);
            retry.SetYesNoLabels("Try Again", "Close");
            if (retry.ShowModal() == wxID_YES)
            {
                m_options.credentials.password.clear(); // rejected: ask again
                m_reconnect();
                return;
            }
            Close(true);
            return;
        }

        if (info.wasConnected)
        {
            // Lost after it was up (a network blip, a server restart): keep the window and retry on a schedule.
            EnterLostState(info.error);
            return;
        }
        SetStatusText("Disconnected: " + wxString::FromUTF8(info.error));
        wxString message = "Could not connect:\n\n" + wxString::FromUTF8(info.error);
        if (!m_options.macAddress.empty())
        {
            // The computer may be asleep: offer to wake it, then keep trying while it starts.
            wxMessageDialog retry(this, message + "\n\nIt may be asleep. Marconi can send it a wake-up call and keep trying.",
                                  caption, wxYES_NO | wxCANCEL | wxICON_ERROR);
            retry.SetYesNoCancelLabels("Wake Up and Try Again", "Try Again", "Close");
            int answer = retry.ShowModal();
            if (answer == wxID_YES)
            {
                std::string error;
                if (Marconi::Net::SendWakeOnLan(m_options.macAddress.utf8_string(), error))
                {
                    EnterWakeState();
                    return;
                }
                wxMessageBox(wxString::FromUTF8(error), caption, wxOK | wxICON_WARNING, this);
            }
            if (answer == wxID_YES || answer == wxID_NO)
            {
                m_reconnect();
                return;
            }
            Close(true);
            return;
        }
        wxMessageDialog retry(this, message, caption, wxYES_NO | wxICON_ERROR);
        retry.SetYesNoLabels("Try Again", "Close");
        if (retry.ShowModal() == wxID_YES)
        {
            m_reconnect();
            return;
        }
        Close(true);
    }
} // namespace MarconiUI
