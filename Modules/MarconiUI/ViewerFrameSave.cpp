// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: after a successful login to a computer that isn't saved, offering to save it, so it's in Saved
// connections (and AI tools can use it by name) with what worked: its name, address, kind and port, user name,
// trusted certificate and, if "remember" was ticked at the login, its password

#include "ViewerFrame.h"

#include "ServerUpdateBar.h"

#include <MarconiConnections/ConnectionStore.h>
#include <MarconiCore/Address.h>
#include <MarconiCore/Quality.h>
#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <wx/aui/aui.h>

namespace MarconiUI
{
    void ViewerFrame::OfferToSave()
    {
        if (!m_options.savedId.empty() || m_options.headless || m_saveOffered)
        {
            return;
        }
        m_saveOffered = true;
        if (m_saveBar == nullptr)
        {
            m_saveBar = new ServerUpdateBar(this);
            m_aui.AddPane(m_saveBar, wxAuiPaneInfo()
                                         .Name("SaveConnection")
                                         .Top()
                                         .Layer(3)
                                         .Row(2)
                                         .CaptionVisible(false)
                                         .CloseButton(false)
                                         .Floatable(false)
                                         .Movable(false)
                                         .Fixed()
                                         .Hide());
        }
        // Named as it will be saved: the name it announced, or else its address.
        const wxString name = m_options.suggestedName.empty()
                                  ? wxString::FromUTF8(Marconi::FormatAddress(m_options.address))
                                  : m_options.suggestedName;
        m_saveBar->SetContent("Save " + name + " to your connections?" +
                                  (m_rememberForSave ? " Its password is saved with it." : wxString()),
                              {{"Save", [this]() { SaveThisComputer(); }}, {"Not now", [this]() { HideSaveBar(); }}});
        m_aui.GetPane("SaveConnection").BestSize(m_saveBar->GetBestSize()).Show();
        m_aui.Update();
    }

    void ViewerFrame::HideSaveBar()
    {
        if (m_saveBar != nullptr && m_aui.GetPane("SaveConnection").IsShown())
        {
            m_aui.GetPane("SaveConnection").Hide();
            m_aui.Update();
        }
    }

    void ViewerFrame::SaveThisComputer()
    {
        ConnectionStore store;
        SavedConnection saved;
        // A computer found on the network is saved by its Bonjour name; anything else by the address it was
        // reached at, as typed.
        saved.address = m_options.saveAddress.empty() ? wxString::FromUTF8(Marconi::FormatAddress(m_options.address))
                                                      : m_options.saveAddress;
        saved.name = m_options.suggestedName; // empty: the list shows the address
        saved.detected = m_options.detected;
        saved.username = wxString::FromUTF8(m_options.credentials.username);
        saved.sshTunnel = m_options.sshTunnel;
        saved.viewOnly = m_options.viewOnly;
        saved.clipboard = m_options.clipboard;
        saved.systemKeys = m_options.systemKeys;
        saved.quality = Marconi::QualityName(m_options.quality);
        saved.certificate = wxString::FromUTF8(Marconi::BytesToHex(m_options.trustedCertificateSha256));
        saved.macAddress = m_options.macAddress;
        saved.lastUsed = wxDateTime::Now();
        saved.rememberPassword = m_rememberForSave && !m_options.credentials.password.empty();
        store.Save(saved);
        if (saved.rememberPassword)
        {
            store.SavePassword(saved.id, saved.username, wxString::FromUTF8(m_options.credentials.password));
        }
        // This window is now that saved connection: later changes (detected port, window place) go to it.
        m_options.savedId = saved.id;
        m_rememberForSave = false;
        LOG_INFO("Viewer", "Saved %s (%s) to the connections", saved.GetDisplayName().utf8_string().c_str(),
                 saved.address.utf8_string().c_str());
        HideSaveBar();
        SetStatusText("Saved " + saved.GetDisplayName() + " to your connections.");
    }
} // namespace MarconiUI
