// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: updating an older Marconi Server on the other computer (the bar on connecting, the F8 menu item, the
// progress, the reconnect while it updates, and the check afterwards). SERVER_PLAN.md, "Self-update".

#include "ViewerFrame.h"

#include "ServerInstall.h"
#include "ServerUpdateBar.h"

#include <MarconiConnections/ConnectionStore.h>
#include <MarconiCore/Reconnect.h>
#include <MarconiCore/ServerUpdate.h>
#include <MarconiCore/Wire/WireClient.h>

#include <Log/Log.h>

#include <wx/msgdlg.h>

namespace MarconiUI
{
    namespace
    {
        const char* kUpdateTitle = "Update Marconi Server on the Other Computer";
    }

    void ViewerFrame::ShowServerUpdateBar(const wxString& message, const std::vector<ServerUpdateBar::Button>& buttons)
    {
        if (m_updateBar == nullptr)
        {
            m_updateBar = new ServerUpdateBar(this);
            m_aui.AddPane(m_updateBar, wxAuiPaneInfo()
                                           .Name("ServerUpdate")
                                           .Top()
                                           .Layer(3)
                                           .Row(1)
                                           .CaptionVisible(false)
                                           .CloseButton(false)
                                           .Floatable(false)
                                           .Movable(false)
                                           .Fixed()
                                           .Hide());
        }
        m_updateBar->SetContent(message, buttons);
        m_aui.GetPane("ServerUpdate").BestSize(m_updateBar->GetBestSize()).Show();
        m_aui.Update();
    }

    void ViewerFrame::HideServerUpdateBar()
    {
        if (m_updateBar != nullptr && m_aui.GetPane("ServerUpdate").IsShown())
        {
            m_aui.GetPane("ServerUpdate").Hide();
            m_aui.Update();
        }
    }

    void ViewerFrame::WatchServerUpdate()
    {
        if (m_wire == nullptr)
        {
            return;
        }
        // The uploader reports from the connection's threads; the window acts on the UI thread.
        m_wire->GetServerUpdate().SetListener([this](const Marconi::Wire::UpdateUploader::Progress& progress) {
            CallAfter([this, progress]() { OnServerUpdateProgress(progress); });
        });
    }

    bool ViewerFrame::DecideServerUpdate(bool fromMenu, std::string& installer, std::string& installerVersion,
                                         Marconi::ServerUpdate::OfferDecision& decision)
    {
        Marconi::ServerUpdate::OfferInputs inputs;
        inputs.serverVersion = m_wire->GetServerVersion();
        inputs.serverKnowsUpdates = m_wire->SupportsServerUpdate();
        inputs.installerPath = FindBundledServerInstaller();
        inputs.dontAsk = m_options.skipServerUpdate;
        inputs.fromMenu = fromMenu;
        decision = Marconi::ServerUpdate::DecideOffer(inputs);
        installer = inputs.installerPath;
        installerVersion = Marconi::ServerUpdate::InstallerVersion(installer);
        return decision == Marconi::ServerUpdate::OfferDecision::Offer;
    }

    void ViewerFrame::OfferServerUpdateOnConnect()
    {
        if (m_wire == nullptr || m_options.headless || m_updateNotNow)
        {
            return;
        }
        std::string installer;
        Marconi::ServerUpdate::OfferDecision decision;
        if (!DecideServerUpdate(false, installer, m_updateVersion, decision))
        {
            if (decision == Marconi::ServerUpdate::OfferDecision::ServerTooOld)
            {
                LOG_INFO("Viewer", "Marconi Server %s predates updating from a viewer",
                         m_wire->GetServerVersion().c_str());
            }
            return;
        }
        // Ask the server first what it would say, so the bar only appears when the update can happen.
        m_updateFromMenu = false;
        m_updateInstaller = installer;
        WatchServerUpdate();
        m_wire->GetServerUpdate().Check(m_updateVersion);
    }

    void ViewerFrame::UpdateServerOnOtherComputer()
    {
        const wxString name = GetServerDisplayName();
        if (!m_connected)
        {
            return;
        }
        if (m_wire == nullptr)
        {
            wxMessageBox("Marconi can update Marconi Server only while it is connected to it (a marconi:// "
                         "connection). This connection is to another kind of server. To put Marconi Server on " +
                             name + ", use Install Marconi Server on the Other Computer.",
                         kUpdateTitle, wxOK | wxICON_INFORMATION, this);
            return;
        }
        std::string installer;
        Marconi::ServerUpdate::OfferDecision decision;
        if (!DecideServerUpdate(true, installer, m_updateVersion, decision))
        {
            wxMessageBox(wxString::FromUTF8(Marconi::ServerUpdate::DescribeDecision(decision, name.utf8_string())),
                         kUpdateTitle, wxOK | wxICON_INFORMATION, this);
            return;
        }
        m_updateFromMenu = true;
        m_updateInstaller = installer;
        WatchServerUpdate();
        if (!m_wire->GetServerUpdate().Check(m_updateVersion))
        {
            wxMessageBox("An update of Marconi Server on " + name + " is already under way.", kUpdateTitle,
                         wxOK | wxICON_INFORMATION, this);
            return;
        }
        ShowServerUpdateBar("Asking Marconi Server on " + name + " whether it can be updated...", {});
    }

    void ViewerFrame::StartServerUpdate()
    {
        if (m_wire == nullptr || !m_connected)
        {
            HideServerUpdateBar();
            return;
        }
        const wxString name = GetServerDisplayName();
        std::string error;
        WatchServerUpdate();
        if (!m_wire->GetServerUpdate().Start(m_updateInstaller, m_updateVersion, error))
        {
            ShowServerUpdateBar(wxString::FromUTF8(error), {{"Close", [this]() { HideServerUpdateBar(); }}});
            return;
        }
        LOG_INFO("Viewer", "Offering Marconi Server %s to %s", m_updateVersion.c_str(), name.utf8_string().c_str());
        ShowServerUpdateBar("Offering the update to Marconi Server on " + name + "...", {});
    }

    void ViewerFrame::DontAskServerUpdateAgain()
    {
        m_options.skipServerUpdate = true;
        ConnectionStore store;
        SavedConnection saved;
        if (store.Load(m_options.savedId, saved))
        {
            saved.skipServerUpdate = true;
            store.Save(saved);
        }
        HideServerUpdateBar();
    }

    void ViewerFrame::OnServerUpdateProgress(const Marconi::Wire::UpdateUploader::Progress& progress)
    {
        using Stage = Marconi::Wire::UpdateUploader::Stage;
        using Answer = Marconi::Wire::UpdateAnswer::Status;
        const std::string name = GetServerDisplayName().utf8_string();
        const auto closeButton = ServerUpdateBar::Button{"Close", [this]() { HideServerUpdateBar(); }};
        switch (progress.stage)
        {
        case Stage::CheckAnswered:
        {
            const bool can = progress.answer == Answer::WouldAccept || progress.answer == Answer::NeedsApproval;
            if (!can)
            {
                if (m_updateFromMenu)
                {
                    ShowServerUpdateBar(wxString::FromUTF8(Marconi::ServerUpdate::DescribeAnswer(progress.answer, name)),
                                        {closeButton});
                }
                else
                {
                    LOG_INFO("Viewer", "Marconi Server on %s won't take an update from here (answer %d)", name.c_str(),
                             static_cast<int>(progress.answer));
                }
                return;
            }
            std::string question =
                Marconi::ServerUpdate::OfferQuestion(name, progress.serverVersion, progress.version);
            if (progress.answer == Answer::NeedsApproval)
            {
                question += " Someone at " + name + " will be asked to allow it.";
            }
            std::vector<ServerUpdateBar::Button> buttons = {{"Update Now", [this]() { StartServerUpdate(); }},
                                                            {"Not Now", [this]() {
                                                                 m_updateNotNow = true;
                                                                 HideServerUpdateBar();
                                                             }}};
            if (!m_options.savedId.empty() && !m_updateFromMenu)
            {
                buttons.push_back({"Don't Ask Again for This Computer", [this]() { DontAskServerUpdateAgain(); }});
            }
            ShowServerUpdateBar(wxString::FromUTF8(question), buttons);
            return;
        }
        case Stage::Offering:
        case Stage::Checking:
            return; // the bar already says so
        case Stage::WaitingForPerson:
            ShowServerUpdateBar(wxString::FromUTF8(Marconi::ServerUpdate::DescribeAnswer(Answer::AskingPerson, name)),
                                {});
            return;
        case Stage::Sending:
        {
            const int percent = progress.total > 0 ? static_cast<int>(progress.sent * 100 / progress.total) : 0;
            ShowServerUpdateBar(wxString::Format("Sending the update to %s: %d%%", wxString::FromUTF8(name), percent),
                                {});
            return;
        }
        case Stage::ServerChecking:
            ShowServerUpdateBar("Marconi Server on " + wxString::FromUTF8(name) + " is checking the update...", {});
            return;
        case Stage::Installing:
            m_updateSentVersion = progress.version;
            ShowServerUpdateBar(wxString::FromUTF8(Marconi::ServerUpdate::DescribeResult(
                                    Marconi::Wire::UpdateResult::Status::Installing, name)),
                                {});
            return;
        case Stage::Refused:
            ShowServerUpdateBar(wxString::FromUTF8(Marconi::ServerUpdate::DescribeAnswer(progress.answer, name)),
                                {closeButton});
            return;
        case Stage::Failed:
            if (!m_updateSentVersion.empty())
            {
                return; // the connection ended after the server said it is installing: expected
            }
            ShowServerUpdateBar(wxString::FromUTF8(progress.error.empty()
                                                       ? Marconi::ServerUpdate::DescribeResult(progress.result, name)
                                                       : progress.error + " Nothing was changed on " + name + "."),
                                {closeButton});
            return;
        case Stage::Idle:
        default:
            return;
        }
    }

    void ViewerFrame::EnterUpdatingState()
    {
        // Like a lost connection, but expected: the headline says what is happening and the picture isn't dimmed.
        LOG_INFO("Viewer", "Marconi Server is updating; reconnecting for up to %d seconds",
                 Marconi::Reconnect::kGiveUpSeconds);
        m_lostHeadline = "Marconi Server on " + GetServerDisplayName() + " is updating, reconnecting in a moment";
        m_reconnecting = true;
        m_retryUnderWay = false;
        m_retryAttempt = 1; // the first try after 4 seconds: the installer takes a little while
        m_lostAt = std::chrono::steady_clock::now();
        m_aui.GetPane("ConnectionLost").Show();
        m_aui.Update();
        ScheduleRetry("");
    }

    void ViewerFrame::CheckServerAfterUpdate()
    {
        if (m_wire == nullptr || m_updateSentVersion.empty())
        {
            return;
        }
        const std::string name = GetServerDisplayName().utf8_string();
        const std::string now = m_wire->GetServerVersion();
        Marconi::ServerUpdate::Outcome outcome = Marconi::ServerUpdate::CheckAfterReconnect(m_updateSentVersion, now);
        LOG_INFO("Viewer", "After the update Marconi Server runs %s (sent %s)", now.c_str(),
                 m_updateSentVersion.c_str());
        ShowServerUpdateBar(
            wxString::FromUTF8(Marconi::ServerUpdate::DescribeOutcome(outcome, name, m_updateSentVersion, now)),
            {{"Close", [this]() { HideServerUpdateBar(); }}});
        m_updateSentVersion.clear();
        m_wire->GetServerUpdate().ClearInstalling();
    }
} // namespace MarconiUI
