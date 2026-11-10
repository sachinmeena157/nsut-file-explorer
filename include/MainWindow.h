// ============================================================================
//  MainWindow.h
//  The graphical user interface (GUI) layer, written with the native Windows
//  API (Win32). It owns the window and all controls and connects user actions
//  (clicks, keys) to the manager classes:
//      FileSystemManager, NavigationManager, FileOperations, SearchManager
// ============================================================================
#pragma once

#include "Common.h"
#include "FileOperations.h"
#include "NavigationManager.h"

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <unordered_map>
#include <vector>

struct SearchJob;   // background search (defined in MainWindow.cpp)

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool create(HINSTANCE hInstance, int nCmdShow);   // build and show the window
    int  run();                                       // message loop

private:
    // ---- window plumbing ---------------------------------------------------
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void onCreate();
    void onSize();
    void onCommand(int id);
    LRESULT onNotify(NMHDR* nm);
    LRESULT onListNotify(NMHDR* nm);
    LRESULT onTreeNotify(NMHDR* nm);
    void onListKey(WORD vk);
    void onContextMenu(HWND source, LPARAM lParam);

    // ---- building the interface -------------------------------------------
    HWND makeButton(const wchar_t* text, int id, HFONT font, DWORD extraStyle = 0);
    void createControls();
    void populateSidebar();
    void syncSidebar();
    int  S(int v) const;                 // scale a pixel value for the screen DPI

    // ---- navigation & loading ---------------------------------------------
    void navigateTo(fs::path target);    // by value on purpose (see .cpp)
    void afterNavigation();
    void loadCurrent();                  // read the current folder and show it
    void refreshView();                  // reload folder, or re-run the search
    void refreshListView();
    void applySort();
    void applyColumnHeaders();
    void onColumnClick(int subItem);

    // ---- selection helpers -------------------------------------------------
    std::vector<int>      selectedIndices() const;
    std::vector<fs::path> selectedPaths() const;
    void selectPaths(const std::vector<fs::path>& paths);
    int  indexOfPath(const fs::path& p) const;
    bool isCutItem(const fs::path& p) const;
    int  iconIndexFor(const FileEntry& e);

    // ---- user actions ------------------------------------------------------
    void openEntry(int index);
    void openSelected();
    void createNewFolder();
    void createNewTextFile();
    void beginRenameOf(const fs::path& p);
    void finishRename(int index, const wchar_t* newName);
    void copyOrCutSelected(bool cut);
    void pasteHere();
    void deleteSelected(bool permanent);
    void showProperties();
    void openFileLocation();
    void goToAddressText();

    // ---- search ------------------------------------------------------------
    void startSearch();
    void cancelSearch();
    void exitSearchMode(bool reload);
    void onSearchDone(unsigned generation);

    // ---- feedback ----------------------------------------------------------
    void showError(const std::wstring& message);
    void setStatus(int part, const std::wstring& text);
    void updateButtons();
    void updateAddressBar();
    void updateTitle();
    void updateStatusForView();
    void updateSelectionStatus();

    // ---- data --------------------------------------------------------------
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND hBack_ = nullptr, hFwd_ = nullptr, hUp_ = nullptr, hRefresh_ = nullptr;
    HWND hAddress_ = nullptr, hSearch_ = nullptr;
    HWND hNewFolder_ = nullptr, hNewFile_ = nullptr, hCut_ = nullptr, hCopy_ = nullptr;
    HWND hPaste_ = nullptr, hRename_ = nullptr, hDelete_ = nullptr, hProps_ = nullptr;
    HWND hHidden_ = nullptr;
    HWND hTree_ = nullptr, hList_ = nullptr, hStatus_ = nullptr, hEmpty_ = nullptr;

    HFONT  fontUi_ = nullptr, fontIcons_ = nullptr;
    HBRUSH bgBrush_ = nullptr, whiteBrush_ = nullptr;
    HACCEL accel_ = nullptr;
    HIMAGELIST sysImages_ = nullptr;
    int dpi_ = 96;
    int iconFolder_ = 0, iconThisPC_ = 0;

    NavigationManager      nav_;
    FileClipboard          clipboard_;
    std::vector<FileEntry> entries_;       // what the list currently shows
    bool thisPc_ = false;                  // showing the drives view
    bool showHidden_ = false;
    SortField sortField_ = SortField::Name;
    bool sortAsc_ = true;

    // sidebar bookkeeping (tree item i  <->  sidebarPaths_[i])
    std::vector<fs::path>  sidebarPaths_;
    std::vector<HTREEITEM> sidebarItems_;

    std::unordered_map<std::wstring, int> extIconCache_;
    std::unordered_map<std::wstring, int> pathIconCache_;

    // search state
    bool         inSearch_ = false;
    std::wstring searchQuery_;
    bool         searchTruncated_ = false;
    SearchJob*   job_ = nullptr;
    HANDLE       searchThread_ = nullptr;
    unsigned     generation_ = 0;

    fs::path pendingSelect_;               // select this item after the next reload
};
