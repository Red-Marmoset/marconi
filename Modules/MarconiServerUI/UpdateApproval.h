// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Asks the person at this computer whether a newer Marconi may update Marconi Server: a window that stays in front
// and needs a click, and closes itself (counting as no) when nobody answers in time

#pragma once

#include <MarconiServerCore/UpdateHost.h>

#include <functional>
#include <mutex>

namespace Marconi
{
    namespace ServerUI
    {
        class UpdateApprovalDialog;

        // The server asks from its own threads; the window is made on the main (window) thread.
        class TrayUpdateApprover : public Server::UpdateApprover
        {
        public:
            TrayUpdateApprover() = default;
            ~TrayUpdateApprover() override;

            void Ask(const Server::UpdateQuestion& question, std::function<void(bool allowed)> answer) override;
            void Withdraw() override;
            // Main thread, before the program ends: closes an open question (counting as no).
            void CloseNow();

        private:
            friend class UpdateApprovalDialog;
            void Show(const Server::UpdateQuestion& question, std::function<void(bool)> answer); // main thread
            void Closed(UpdateApprovalDialog* dialog);                                          // main thread

            UpdateApprovalDialog* m_dialog = nullptr; // main thread only
        };
    } // namespace ServerUI
} // namespace Marconi
