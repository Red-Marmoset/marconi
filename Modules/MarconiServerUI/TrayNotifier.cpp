// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray's connection notices

#include "TrayNotifier.h"

#include <Log/Log.h>

#include <wx/app.h>
#include <wx/notifmsg.h>

namespace Marconi
{
    namespace ServerUI
    {
        void TrayNotifier::Notify(const Server::ConnectionNotice& notice)
        {
            std::string text = Server::DescribeNotice(notice);
            LOG_INFO("Server", "%s", text.c_str()); // the log keeps a record whether or not it is shown
            if (!m_enabled || wxTheApp == nullptr)
            {
                return;
            }
            wxString message = wxString::FromUTF8(text.c_str());
            // Called from the server's threads: hand the work to the thread that owns the windows.
            wxTheApp->CallAfter([message]() {
                wxNotificationMessage notification("Marconi Server", message, nullptr, wxICON_INFORMATION);
                notification.Show(wxNotificationMessage::Timeout_Auto);
            });
        }
    } // namespace ServerUI
} // namespace Marconi
