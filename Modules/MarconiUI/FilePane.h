// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One side of the file panel, like a file explorer: Back, Forward, Up and the clickable path; a folder tree;
// the folder's contents

#pragma once

#include "BackgroundWork.h"

#include <MarconiCore/Files/FileSource.h>
#include <MarconiCore/Files/FileView.h>

#include <wx/panel.h>
#include <wx/treebase.h>

#include <string>
#include <vector>

class wxBitmapButton;
class wxImageList;
class wxKeyEvent;
class wxListEvent;
class wxListView;
class wxSplitterWindow;
class wxStaticText;
class wxTreeCtrl;

namespace MarconiUI
{
    class FilePanel;

    class FilePane : public wxPanel
    {
    public:
        struct Root
        {
            wxString label;
            std::string path;
        };

        FilePane(wxWindow* parent, FilePanel& owner, bool remote, const wxString& heading);
        ~FilePane() override;

        bool IsRemote() const { return m_remote; }
        const std::string& GetFolder() const { return m_folder; }
        Marconi::Files::FileSource* GetSource() const { return m_source; }

        // Stops background listings; call before the file source goes away.
        void StopWork() { m_work.Stop(); }

        void SetSource(Marconi::Files::FileSource* source, const std::string& folder, const std::vector<Root>& roots);
        void SetHeading(const wxString& heading);
        void Navigate(const std::string& folder, bool remember = true);
        void Reload() { Navigate(m_folder, false); }
        std::vector<std::string> GetSelectedPaths() const;
        // Paths dropped on this pane: from the other pane or from Explorer/Finder.
        void OnDropped(const std::vector<std::string>& paths, bool fromRemote);

    private:
        struct TreeData;
        class DropTarget;

        void ShowFolder(const std::string& folder, std::vector<Marconi::Files::FileEntry> entries);
        void ShowCrumbs();
        void UpdateButtons();
        void GoBack();
        void GoForward();
        void GoUp();
        void SortBy(int column);
        void LoadTreeItem(const wxTreeItemId& item);
        wxTreeItemId FindTreeItem(const wxTreeItemId& parent, const std::string& path);
        void SelectInTree(const std::string& folder);
        void OnActivated(wxListEvent& event);
        void OnKey(wxKeyEvent& event);
        void StartDrag();
        void ShowContextMenu();
        void MakeFolder();
        void RenameSelected();
        void DeleteSelected();
        bool DeleteTree(const std::string& path, std::string& error);

        FilePanel& m_owner;
        bool m_remote = false;
        Marconi::Files::FileSource* m_source = nullptr;
        std::string m_folder;
        std::vector<Marconi::Files::FileEntry> m_entries;
        std::vector<std::string> m_history; // for Back
        std::vector<std::string> m_future;  // for Forward
        int m_generation = 0;               // the latest listing asked for; older answers are dropped
        bool m_syncingTree = false;
        Marconi::Files::SortColumn m_sortColumn = Marconi::Files::SortColumn::Name;
        bool m_sortAscending = true;
        wxStaticText* m_heading = nullptr;
        wxBitmapButton* m_back = nullptr;
        wxBitmapButton* m_forward = nullptr;
        wxBitmapButton* m_up = nullptr;
        wxPanel* m_crumbs = nullptr;
        wxSplitterWindow* m_split = nullptr;
        wxTreeCtrl* m_tree = nullptr;
        wxListView* m_list = nullptr;
        wxImageList* m_icons = nullptr;
        wxTreeItemId m_treeRoot;
        BackgroundWork m_work; // last, so it stops first
    };
} // namespace MarconiUI
