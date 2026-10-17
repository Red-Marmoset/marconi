// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The small bar that slides in at the top of the screen in full screen mode, so the controls are a mouse move away

#pragma once

#include <wx/panel.h>
#include <wx/timer.h>

#include <vector>

class wxStaticText;
class wxToggleButton;

namespace MarconiUI
{
    class FullScreenBar : public wxPanel
    {
    public:
        struct Item
        {
            wxString label;
            int commandId = 0; // sent to the parent as a menu command when clicked
            wxString tooltip;
        };

        // The bar is a child of `parent` (the viewer window), hidden until Begin().
        FullScreenBar(wxWindow* parent, const wxString& title, const std::vector<Item>& items);

        // Full screen started: watch the pointer, show the bar when it rests at the top edge.
        void Begin();
        // Full screen ended: hide the bar and stop watching.
        void End();

        bool IsPinned() const;

    private:
        void OnTick();
        void Reveal();
        void Conceal();
        void PlaceAtTop();

        wxStaticText* m_title = nullptr;
        wxToggleButton* m_pin = nullptr;
        wxTimer m_timer;
        int m_ticksAway = 0;   // how long the pointer has been away from the bar
        int m_ticksAtTop = 0;  // how long the pointer has rested at the top edge
        bool m_watching = false;
    };
} // namespace MarconiUI
