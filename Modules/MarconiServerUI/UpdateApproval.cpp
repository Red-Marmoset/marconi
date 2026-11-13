// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The "may a newer Marconi update this server?" window

#include "UpdateApproval.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace Marconi
{
    namespace ServerUI
    {
        // Modeless, so the tray keeps working, and in front of everything, so the person at the computer sees it.
        class UpdateApprovalDialog : public wxDialog
        {
        public:
            UpdateApprovalDialog(TrayUpdateApprover& owner, const Server::UpdateQuestion& question,
                                 std::function<void(bool)> answer)
                : wxDialog(nullptr, wxID_ANY, "Update Marconi Server?", wxDefaultPosition, wxDefaultSize,
                           wxCAPTION | wxSTAY_ON_TOP),
                  m_owner(owner), m_answer(std::move(answer)), m_timer(this)
            {
                std::string who = question.viewerName.empty() ? std::string("A viewer") : question.viewerName;
                if (!question.address.empty())
                {
                    who += " (" + question.address + ")";
                }
                const std::string text =
                    who + " is connected with Marconi" +
                    (question.viewerVersion.empty() ? std::string() : " " + question.viewerVersion) +
                    " and wants to update Marconi Server on this computer from version " + question.currentVersion +
                    " to " + question.newVersion +
                    ".\n\nMarconi Server checks that the update is genuine before it runs it, and Windows will ask "
                    "for permission. While this question is open, the viewer's mouse and keyboard don't reach this "
                    "computer, so only someone here can answer.\n\nAllow the update?";
                wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
                wxStaticText* message = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(text));
                message->Wrap(FromDIP(420));
                sizer->Add(message, 0, wxALL, FromDIP(16));
                m_countdown = new wxStaticText(this, wxID_ANY, wxString());
                sizer->Add(m_countdown, 0, wxLEFT | wxRIGHT, FromDIP(16));
                wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
                wxButton* no = new wxButton(this, wxID_NO, "Don't Allow");
                wxButton* yes = new wxButton(this, wxID_YES, "Allow the Update");
                buttons->Add(no, 0, wxRIGHT, FromDIP(8));
                buttons->Add(yes, 0);
                sizer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, FromDIP(16));
                SetSizerAndFit(sizer);
                no->SetDefault(); // Enter says no: allowing takes a deliberate click
                no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Finish(false); });
                yes->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Finish(true); });
                Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Finish(false); });
                Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Tick(); });
                m_secondsLeft = Server::kApprovalSeconds;
                Tick();
                m_timer.Start(1000);
                CentreOnScreen();
            }

            // The question went away (the viewer left, or the server's own time ran out): close without answering.
            void Dismiss()
            {
                m_answer = nullptr;
                Finish(false);
            }

        private:
            void Tick()
            {
                if (m_secondsLeft <= 0)
                {
                    Finish(false);
                    return;
                }
                m_countdown->SetLabel(wxString::Format("If nobody answers, the update is turned down in %d s.",
                                                       m_secondsLeft));
                --m_secondsLeft;
            }

            void Finish(bool allowed)
            {
                if (m_finished)
                {
                    return;
                }
                m_finished = true;
                m_timer.Stop();
                if (m_answer)
                {
                    m_answer(allowed);
                    m_answer = nullptr;
                }
                m_owner.Closed(this);
                Destroy();
            }

            TrayUpdateApprover& m_owner;
            std::function<void(bool)> m_answer;
            wxTimer m_timer;
            wxStaticText* m_countdown = nullptr;
            int m_secondsLeft = 0;
            bool m_finished = false;
        };

        TrayUpdateApprover::~TrayUpdateApprover() = default; // CloseNow has closed any window by then

        void TrayUpdateApprover::CloseNow()
        {
            if (m_dialog != nullptr)
            {
                m_dialog->Dismiss();
            }
        }

        void TrayUpdateApprover::Ask(const Server::UpdateQuestion& question, std::function<void(bool allowed)> answer)
        {
            wxTheApp->CallAfter([this, question, answer]() { Show(question, answer); });
        }

        void TrayUpdateApprover::Withdraw()
        {
            wxTheApp->CallAfter([this]() {
                if (m_dialog != nullptr)
                {
                    m_dialog->Dismiss();
                }
            });
        }

        void TrayUpdateApprover::Show(const Server::UpdateQuestion& question, std::function<void(bool)> answer)
        {
            if (m_dialog != nullptr)
            {
                m_dialog->Dismiss(); // only one question at a time; the server never asks two
            }
            m_dialog = new UpdateApprovalDialog(*this, question, std::move(answer));
            m_dialog->Show();
            m_dialog->Raise();
            m_dialog->RequestUserAttention();
        }

        void TrayUpdateApprover::Closed(UpdateApprovalDialog* dialog)
        {
            if (m_dialog == dialog)
            {
                m_dialog = nullptr;
            }
        }
    } // namespace ServerUI
} // namespace Marconi
