// ============================================================================
//  MainWindow.cpp  (part 1 of 2)
//  Window creation, controls, layout, drawing the file list, notifications.
//  The file actions (open, create, delete, paste, search ...) are in
//  MainWindowActions.cpp
// ============================================================================
#include "MainWindow.h"

#include "FileSystemManager.h"
#include "SearchJob.h"
#include "UiIds.h"

#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstring>

namespace {
const wchar_t* const kClassName = L"NSUTFileExplorerMainWindow";
const COLORREF kBackground = RGB(243, 243, 243);
}

// ===========================================================================
//  Construction / destruction
// ===========================================================================
MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    cancelSearch();
    if (accel_)      DestroyAcceleratorTable(accel_);
    if (fontUi_)     DeleteObject(fontUi_);
    if (fontIcons_)  DeleteObject(fontIcons_);
    if (bgBrush_)    DeleteObject(bgBrush_);
    if (whiteBrush_) DeleteObject(whiteBrush_);
}

int MainWindow::S(int v) const { return MulDiv(v, dpi_, 96); }

bool MainWindow::create(HINSTANCE hInstance, int nCmdShow) {
    hInst_ = hInstance;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(nullptr);
    dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    if (dpi_ < 96) dpi_ = 96;

    bgBrush_    = CreateSolidBrush(kBackground);
    whiteBrush_ = CreateSolidBrush(RGB(255, 255, 255));

    HICON icon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));   // icon from app.rc
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance     = hInstance;
    wc.hIcon         = icon;
    wc.hIconSm       = icon;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = bgBrush_;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    hwnd_ = CreateWindowExW(0, kClassName, L"NSUT File Explorer",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, S(1120), S(700),
                            nullptr, nullptr, hInstance, this);
    if (!hwnd_) return false;

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    // First location: the user's home folder (or "This PC" if that fails).
    std::wstring err;
    auto quick = FileSystemManager::getQuickAccessFolders();
    fs::path start = quick.empty() ? fs::path() : quick.front().second;
    if (!nav_.goTo(start, err)) nav_.goTo(fs::path(), err);
    loadCurrent();
    SetFocus(hList_);
    return true;
}

int MainWindow::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(hwnd_, accel_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

// ===========================================================================
//  Window procedure
// ===========================================================================
LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            onCreate();
            return 0;

        case WM_SIZE:
            onSize();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = S(780);
            mm->ptMinTrackSize.y = S(440);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_ADDRESS || id == ID_SEARCH) return 0;   // edit-box notifications
            bool fromButton = (lp != 0);
            onCommand(id);
            // give the keyboard back to the list (unless a rename box is open)
            if (fromButton && hList_ && ListView_GetEditControl(hList_) == nullptr &&
                id != ID_HIDDEN)
                SetFocus(hList_);
            return 0;
        }

        case WM_NOTIFY:
            return onNotify(reinterpret_cast<NMHDR*>(lp));

        case WM_CONTEXTMENU:
            onContextMenu(reinterpret_cast<HWND>(wp), lp);
            return 0;

        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lp) == hEmpty_) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, RGB(100, 100, 100));
                SetBkColor(dc, RGB(255, 255, 255));
                return reinterpret_cast<LRESULT>(whiteBrush_);
            }
            break;

        case WM_SETFOCUS:
            if (hList_) SetFocus(hList_);
            return 0;

        case WM_DEVICECHANGE:
            // 0x8000 = DBT_DEVICEARRIVAL, 0x8004 = DBT_DEVICEREMOVECOMPLETE
            if (wp == 0x8000 || wp == 0x8004) PostMessageW(hwnd_, WM_APP_DRIVES_CHANGED, 0, 0);
            return TRUE;

        case WM_APP_SEARCH_DONE:
            onSearchDone(static_cast<unsigned>(wp));
            return 0;

        case WM_APP_RELOAD:
            refreshView();
            if (!pendingSelect_.empty()) {
                selectPaths({pendingSelect_});
                pendingSelect_.clear();
            }
            return 0;

        case WM_APP_DRIVES_CHANGED:
            populateSidebar();
            syncSidebar();
            if (thisPc_) refreshView();
            return 0;

        case WM_DESTROY:
            cancelSearch();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ===========================================================================
//  Building the interface
// ===========================================================================
void MainWindow::onCreate() {
    fontUi_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    fontIcons_ = CreateFontW(-MulDiv(11, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

    createControls();
    populateSidebar();

    // Keyboard shortcuts that work everywhere in the window
    std::vector<ACCEL> keys;
    auto add = [&keys](BYTE flags, WORD key, WORD cmd) {
        ACCEL a;
        a.fVirt = flags;
        a.key   = key;
        a.cmd   = cmd;
        keys.push_back(a);
    };
    add(FVIRTKEY, VK_F5, ID_REFRESH);
    add(FVIRTKEY | FCONTROL, 'L', ID_FOCUS_ADDRESS);
    add(FVIRTKEY | FCONTROL, 'F', ID_FOCUS_SEARCH);
    add(FVIRTKEY | FALT, VK_LEFT, ID_BACK);
    add(FVIRTKEY | FALT, VK_RIGHT, ID_FORWARD);
    add(FVIRTKEY | FALT, VK_UP, ID_UP);
    add(FVIRTKEY | FCONTROL | FSHIFT, 'N', ID_NEWFOLDER);
    accel_ = CreateAcceleratorTableW(keys.data(), static_cast<int>(keys.size()));
}

HWND MainWindow::makeButton(const wchar_t* text, int id, HFONT font, DWORD extraStyle) {
    HWND h = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extraStyle,
                             0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             hInst_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return h;
}

void MainWindow::createControls() {
    // ---- row 1: navigation buttons, address bar, search box ---------------
    hBack_    = makeButton(L"\uE72B", ID_BACK, fontIcons_);
    hFwd_     = makeButton(L"\uE72A", ID_FORWARD, fontIcons_);
    hUp_      = makeButton(L"\uE74A", ID_UP, fontIcons_);
    hRefresh_ = makeButton(L"\uE72C", ID_REFRESH, fontIcons_);

    hAddress_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 10, 10, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ADDRESS)), hInst_, nullptr);
    hSearch_  = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 10, 10, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SEARCH)), hInst_, nullptr);
    SendMessageW(hAddress_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
    SendMessageW(hSearch_,  WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
    SendMessageW(hSearch_, EM_SETCUEBANNER, FALSE,
                 reinterpret_cast<LPARAM>(L"Search files and folders (Enter)"));
    SetWindowSubclass(hAddress_, EditSubclassProc, 1, 0);
    SetWindowSubclass(hSearch_,  EditSubclassProc, 2, 0);

    // ---- row 2: command buttons --------------------------------------------
    hNewFolder_ = makeButton(L"New folder", ID_NEWFOLDER, fontUi_);
    hNewFile_   = makeButton(L"New file",   ID_NEWFILE,   fontUi_);
    hCut_       = makeButton(L"Cut",        ID_CUT,       fontUi_);
    hCopy_      = makeButton(L"Copy",       ID_COPY,      fontUi_);
    hPaste_     = makeButton(L"Paste",      ID_PASTE,     fontUi_);
    hRename_    = makeButton(L"Rename",     ID_RENAME,    fontUi_);
    hDelete_    = makeButton(L"Delete",     ID_DELETE,    fontUi_);
    hProps_     = makeButton(L"Properties", ID_PROPS,     fontUi_);
    hHidden_    = makeButton(L"Show hidden files", ID_HIDDEN, fontUi_, BS_AUTOCHECKBOX);

    // ---- tool tips for the icon-only buttons -------------------------------
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               hwnd_, nullptr, hInst_, nullptr);
    auto addTip = [&](HWND target, const wchar_t* text) {
        TOOLINFOW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize   = sizeof(ti);
        ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd     = hwnd_;
        ti.uId      = reinterpret_cast<UINT_PTR>(target);
        ti.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    };
    addTip(hBack_,    L"Back (Alt+Left)");
    addTip(hFwd_,     L"Forward (Alt+Right)");
    addTip(hUp_,      L"Up one level (Alt+Up / Backspace)");
    addTip(hRefresh_, L"Refresh (F5)");
    addTip(hAddress_, L"Type a path and press Enter (Ctrl+L)");

    // ---- sidebar (tree view) -----------------------------------------------
    hTree_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_SHOWSELALWAYS |
                                 TVS_FULLROWSELECT | TVS_TRACKSELECT | TVS_NOHSCROLL,
                             0, 0, 10, 10, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TREE)), hInst_, nullptr);
    SendMessageW(hTree_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
    SetWindowTheme(hTree_, L"Explorer", nullptr);
    TreeView_SetExtendedStyle(hTree_, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    TreeView_SetItemHeight(hTree_, S(24));

    // ---- file list (virtual list view: only visible rows are requested) ----
    hList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
                                 LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_SHAREIMAGELISTS,
                             0, 0, 10, 10, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)), hInst_, nullptr);
    SendMessageW(hList_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);
    SetWindowTheme(hList_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(hList_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                  LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    struct Col { int sub; const wchar_t* title; int width; int fmt; };
    const Col cols[] = {
        {COL_NAME,     L"Name",          S(340), LVCFMT_LEFT},
        {COL_DATE,     L"Date modified", S(160), LVCFMT_LEFT},
        {COL_TYPE,     L"Type",          S(170), LVCFMT_LEFT},
        {COL_SIZE,     L"Size",          S(100), LVCFMT_RIGHT},
        {COL_LOCATION, L"Location",      0,      LVCFMT_LEFT},
    };
    for (const Col& c : cols) {
        LVCOLUMNW col;
        ZeroMemory(&col, sizeof(col));
        col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        col.iSubItem = c.sub;
        col.pszText = const_cast<wchar_t*>(c.title);
        col.cx      = c.width;
        col.fmt     = c.fmt;
        SendMessageW(hList_, LVM_INSERTCOLUMNW, static_cast<WPARAM>(c.sub), reinterpret_cast<LPARAM>(&col));
    }

    // ---- status bar and the "folder is empty" message ----------------------
    hStatus_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                               0, 0, 0, 0, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), hInst_, nullptr);
    SendMessageW(hStatus_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);

    hEmpty_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_CENTER | SS_NOTIFY,
                              0, 0, 10, 10, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EMPTYMSG)), hInst_, nullptr);
    SendMessageW(hEmpty_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_), TRUE);

    // ---- system icons (the same icons Windows Explorer uses) ----------------
    SHFILEINFOW sfi;
    std::memset(&sfi, 0, sizeof(sfi));
    sysImages_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
        L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
    iconFolder_ = sfi.iIcon;
    if (sysImages_) {
        ListView_SetImageList(hList_, sysImages_, LVSIL_SMALL);
        TreeView_SetImageList(hTree_, sysImages_, TVSIL_NORMAL);
    }
    // icon of "This PC"
    iconThisPC_ = iconFolder_;
    LPITEMIDLIST pidl = nullptr;
    if (SUCCEEDED(SHGetSpecialFolderLocation(nullptr, CSIDL_DRIVES, &pidl)) && pidl) {
        std::memset(&sfi, 0, sizeof(sfi));
        if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &sfi, sizeof(sfi),
                           SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
            iconThisPC_ = sfi.iIcon;
        ILFree(pidl);
    }
}

// ---------------------------------------------------------------------------
//  Sidebar: Quick access + This PC (with all drives)
// ---------------------------------------------------------------------------
void MainWindow::populateSidebar() {
    TreeView_DeleteAllItems(hTree_);
    sidebarPaths_.clear();
    sidebarItems_.clear();

    auto iconOf = [](const fs::path& p) -> int {
        SHFILEINFOW sfi;
        std::memset(&sfi, 0, sizeof(sfi));
        if (SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
            return sfi.iIcon;
        return 0;
    };

    // navigable == false  -> header only (lParam = -1)
    auto addItem = [&](HTREEITEM parent, const std::wstring& text, const fs::path& path,
                       bool navigable, int icon, bool bold) -> HTREEITEM {
        TVINSERTSTRUCTW tvi;
        ZeroMemory(&tvi, sizeof(tvi));
        tvi.hParent      = parent;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask    = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_STATE;
        tvi.item.pszText = const_cast<wchar_t*>(text.c_str());
        tvi.item.iImage = tvi.item.iSelectedImage = icon;
        tvi.item.stateMask = TVIS_BOLD;
        tvi.item.state     = bold ? TVIS_BOLD : 0;
        if (navigable) {
            tvi.item.lParam = static_cast<LPARAM>(sidebarPaths_.size());
            sidebarPaths_.push_back(path);
        } else {
            tvi.item.lParam = -1;
        }
        HTREEITEM item = TreeView_InsertItem(hTree_, &tvi);
        if (navigable) sidebarItems_.push_back(item);
        return item;
    };

    HTREEITEM quick = addItem(TVI_ROOT, L"Quick access", fs::path(), false, iconFolder_, true);
    for (const auto& q : FileSystemManager::getQuickAccessFolders())
        addItem(quick, q.first, q.second, true, iconOf(q.second), false);
    TreeView_Expand(hTree_, quick, TVE_EXPAND);

    HTREEITEM pc = addItem(TVI_ROOT, L"This PC", fs::path(), true, iconThisPC_, true);
    for (const DriveInfo& d : FileSystemManager::getDrives())
        addItem(pc, d.displayName(), d.root, true, iconOf(d.root), false);
    TreeView_Expand(hTree_, pc, TVE_EXPAND);
}

void MainWindow::syncSidebar() {
    std::wstring cur = util::toLower(nav_.current().wstring());
    HTREEITEM match = nullptr;
    for (size_t i = 0; i < sidebarPaths_.size(); ++i) {
        if (util::toLower(sidebarPaths_[i].wstring()) == cur) {
            match = sidebarItems_[i];
            break;
        }
    }
    TreeView_SelectItem(hTree_, match);   // nullptr clears the selection
}

// ===========================================================================
//  Layout (called whenever the window size changes)
// ===========================================================================
void MainWindow::onSize() {
    if (!hList_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;

    SendMessageW(hStatus_, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(hStatus_, &sr);
    const int statusH = sr.bottom - sr.top;
    int parts[3] = {S(200), S(380), -1};
    SendMessageW(hStatus_, SB_SETPARTS, 3, reinterpret_cast<LPARAM>(parts));

    const int m = S(8);

    // row 1
    const int y1 = S(8), h1 = S(30), bw = S(38);
    int x = m;
    HWND navButtons[4] = {hBack_, hFwd_, hUp_, hRefresh_};
    for (HWND b : navButtons) {
        MoveWindow(b, x, y1, bw, h1, TRUE);
        x += bw + S(4);
    }
    x += S(6);
    int searchW = std::min(S(300), W / 3);
    int addrW   = std::max(S(80), W - x - m - searchW - S(8));
    MoveWindow(hAddress_, x, y1, addrW, h1, TRUE);
    MoveWindow(hSearch_, W - m - searchW, y1, searchW, h1, TRUE);

    // row 2
    const int y2 = y1 + h1 + S(8), h2 = S(28);
    struct Item { HWND h; int w; };
    const Item items[] = {{hNewFolder_, S(94)}, {hNewFile_, S(80)}, {hCut_, S(56)},
                          {hCopy_, S(60)},      {hPaste_, S(60)},   {hRename_, S(72)},
                          {hDelete_, S(66)},    {hProps_, S(90)}};
    x = m;
    for (const Item& it : items) {
        MoveWindow(it.h, x, y2, it.w, h2, TRUE);
        x += it.w + S(6);
    }
    MoveWindow(hHidden_, x + S(12), y2, S(170), h2, TRUE);

    // content area
    const int top = y2 + h2 + S(8);
    const int contentH = std::max(S(50), H - statusH - top - S(6));
    const int treeW = S(220);
    MoveWindow(hTree_, m, top, treeW, contentH, TRUE);
    const int listX = m + treeW + m;
    const int listW = std::max(S(100), W - listX - m);
    MoveWindow(hList_, listX, top, listW, contentH, TRUE);
    MoveWindow(hEmpty_, listX + S(6), top + S(46), listW - S(12), S(30), TRUE);
}

// ===========================================================================
//  Notifications from the list and the tree
// ===========================================================================
LRESULT MainWindow::onNotify(NMHDR* nm) {
    if (!nm || !hList_) return 0;
    if (nm->hwndFrom == hList_) return onListNotify(nm);
    if (nm->hwndFrom == hTree_) return onTreeNotify(nm);
    return 0;
}

LRESULT MainWindow::onTreeNotify(NMHDR* nm) {
    if (nm->code == TVN_SELCHANGEDW) {
        NMTREEVIEWW* t = reinterpret_cast<NMTREEVIEWW*>(nm);
        if (t->action == TVC_UNKNOWN) return 0;      // we changed it from code: ignore
        int idx = static_cast<int>(t->itemNew.lParam);
        if (idx >= 0 && idx < static_cast<int>(sidebarPaths_.size())) navigateTo(sidebarPaths_[idx]);
    }
    return 0;
}

LRESULT MainWindow::onListNotify(NMHDR* nm) {
    switch (nm->code) {
        // ---- the list asks: "what do I show in row N?" ----------------------
        case LVN_GETDISPINFOW: {
            NMLVDISPINFOW* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            int i = di->item.iItem;
            if (i < 0 || i >= static_cast<int>(entries_.size())) return 0;
            FileEntry& e = entries_[static_cast<size_t>(i)];
            if (di->item.mask & LVIF_TEXT) {
                std::wstring text;
                switch (di->item.iSubItem) {
                    case COL_NAME: text = e.name; break;
                    case COL_DATE:
                        if (thisPc_) text = e.extraText;
                        else if (e.hasModified) text = util::formatTime(e.modified);
                        break;
                    case COL_TYPE:
                        FileSystemManager::ensureTypeName(e);
                        text = e.typeText;
                        break;
                    case COL_SIZE:
                        if (thisPc_ || !e.isDirectory) text = util::formatSize(e.size);
                        break;
                    case COL_LOCATION: text = e.fullPath.parent_path().wstring(); break;
                }
                lstrcpynW(di->item.pszText, text.c_str(), di->item.cchTextMax);
            }
            if (di->item.mask & LVIF_IMAGE) di->item.iImage = iconIndexFor(e);
            return 0;
        }

        // ---- grey text for hidden / system / cut items ------------------------
        case NM_CUSTOMDRAW: {
            NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(nm);
            switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    size_t i = static_cast<size_t>(cd->nmcd.dwItemSpec);
                    if (i < entries_.size()) {
                        const FileEntry& e = entries_[i];
                        if (e.isHidden || e.isSystem || !e.accessible || isCutItem(e.fullPath))
                            cd->clrText = RGB(140, 140, 140);
                    }
                    return CDRF_NEWFONT;
                }
            }
            return CDRF_DODEFAULT;
        }

        case LVN_COLUMNCLICK:
            onColumnClick(reinterpret_cast<NMLISTVIEW*>(nm)->iSubItem);
            return 0;

        case NM_DBLCLK: {
            NMITEMACTIVATE* a = reinterpret_cast<NMITEMACTIVATE*>(nm);
            if (a->iItem >= 0) openEntry(a->iItem);
            return 0;
        }

        case LVN_KEYDOWN:
            onListKey(reinterpret_cast<NMLVKEYDOWN*>(nm)->wVKey);
            return 0;

        case LVN_ITEMCHANGED: {
            NMLISTVIEW* l = reinterpret_cast<NMLISTVIEW*>(nm);
            if (l->uChanged & LVIF_STATE) updateSelectionStatus();
            return 0;
        }

        // ---- renaming inside the list (F2 or slow double click) ---------------
        case LVN_BEGINLABELEDIT: {
            NMLVDISPINFOW* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            if (thisPc_) return TRUE;                 // TRUE = cancel (cannot rename drives)
            int i = di->item.iItem;
            if (i >= 0 && i < static_cast<int>(entries_.size()) && !entries_[static_cast<size_t>(i)].isDirectory) {
                // select only the file name, not the extension (like Windows)
                size_t stemLen = entries_[static_cast<size_t>(i)].fullPath.stem().wstring().size();
                HWND edit = ListView_GetEditControl(hList_);
                if (edit) PostMessageW(edit, EM_SETSEL, 0, static_cast<LPARAM>(stemLen));
            }
            return FALSE;
        }
        case LVN_ENDLABELEDITW: {
            NMLVDISPINFOW* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            if (di->item.pszText) finishRename(di->item.iItem, di->item.pszText);
            return FALSE;
        }

        // ---- type-ahead: typing "abc" jumps to the first name starting "abc" --
        case LVN_ODFINDITEMW: {
            NMLVFINDITEMW* f = reinterpret_cast<NMLVFINDITEMW*>(nm);
            const int n = static_cast<int>(entries_.size());
            if (n == 0 || !(f->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) || !f->lvfi.psz) return -1;
            std::wstring q = util::toLower(f->lvfi.psz);
            for (int k = 0; k < n; ++k) {
                int i = (f->iStart + k) % n;
                std::wstring nameLower = util::toLower(entries_[static_cast<size_t>(i)].name);
                if (nameLower.compare(0, q.size(), q) == 0) return i;
            }
            return -1;
        }
    }
    return 0;
}

void MainWindow::onListKey(WORD vk) {
    const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (vk) {
        case VK_RETURN: onCommand(ID_OPEN); break;
        case VK_DELETE: onCommand(shift ? ID_DELETE_PERMANENT : ID_DELETE); break;
        case VK_F2:     onCommand(ID_RENAME); break;
        case VK_BACK:   onCommand(ID_UP); break;
        case 'C': if (ctrl) onCommand(ID_COPY); break;
        case 'X': if (ctrl) onCommand(ID_CUT); break;
        case 'V': if (ctrl) onCommand(ID_PASTE); break;
        case 'A': if (ctrl) onCommand(ID_SELECTALL); break;
    }
}

void MainWindow::onColumnClick(int sub) {
    if (thisPc_) return;
    SortField f;
    switch (sub) {
        case COL_NAME: f = SortField::Name; break;
        case COL_DATE: f = SortField::Date; break;
        case COL_TYPE: f = SortField::Type; break;
        case COL_SIZE: f = SortField::Size; break;
        default: return;
    }
    if (f == sortField_) sortAsc_ = !sortAsc_;
    else { sortField_ = f; sortAsc_ = true; }

    std::vector<fs::path> keep = selectedPaths();
    applySort();
    refreshListView();
    selectPaths(keep);
    applyColumnHeaders();
}

// ===========================================================================
//  Context menu (right click)
// ===========================================================================
void MainWindow::onContextMenu(HWND source, LPARAM lp) {
    if (source != hList_ && source != hEmpty_) return;

    POINT pt;
    pt.x = GET_X_LPARAM(lp);
    pt.y = GET_Y_LPARAM(lp);
    bool onItem = false;

    if (lp == static_cast<LPARAM>(-1)) {          // opened with the keyboard (Menu key)
        int sel = ListView_GetNextItem(hList_, -1, LVNI_SELECTED);
        RECT r;
        if (sel >= 0 && ListView_GetItemRect(hList_, sel, &r, LVIR_BOUNDS)) {
            pt.x = r.left + S(24);
            pt.y = r.bottom;
            onItem = true;
        } else {
            pt.x = S(24);
            pt.y = S(60);
        }
        ClientToScreen(hList_, &pt);
    } else if (source == hList_) {
        LVHITTESTINFO hti;
        ZeroMemory(&hti, sizeof(hti));
        hti.pt = pt;
        ScreenToClient(hList_, &hti.pt);
        int idx = ListView_HitTest(hList_, &hti);
        if (idx >= 0) {
            if (!(ListView_GetItemState(hList_, idx, LVIS_SELECTED) & LVIS_SELECTED)) {
                ListView_SetItemState(hList_, -1, 0, LVIS_SELECTED);
                ListView_SetItemState(hList_, idx, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            }
            onItem = true;
        } else {
            ListView_SetItemState(hList_, -1, 0, LVIS_SELECTED);
        }
    } else {
        ListView_SetItemState(hList_, -1, 0, LVIS_SELECTED);
    }

    const bool folderView = !thisPc_ && !inSearch_;
    const int  count = static_cast<int>(ListView_GetSelectedCount(hList_));
    auto flag = [](bool enabled) { return static_cast<UINT>(MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED)); };

    HMENU menu = CreatePopupMenu();
    if (onItem && count > 0) {
        AppendMenuW(menu, flag(true), ID_OPEN, L"Open\tEnter");
        if (inSearch_) AppendMenuW(menu, flag(count == 1), ID_OPENLOCATION, L"Open file location");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, flag(!thisPc_), ID_CUT, L"Cut\tCtrl+X");
        AppendMenuW(menu, flag(!thisPc_), ID_COPY, L"Copy\tCtrl+C");
        AppendMenuW(menu, flag(folderView && !clipboard_.empty()), ID_PASTE, L"Paste\tCtrl+V");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, flag(!thisPc_ && count == 1), ID_RENAME, L"Rename\tF2");
        AppendMenuW(menu, flag(!thisPc_), ID_DELETE, L"Delete\tDel");
        AppendMenuW(menu, flag(!thisPc_), ID_DELETE_PERMANENT, L"Delete permanently\tShift+Del");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, flag(true), ID_PROPS, L"Properties");
    } else {
        AppendMenuW(menu, flag(folderView), ID_NEWFOLDER, L"New folder\tCtrl+Shift+N");
        AppendMenuW(menu, flag(folderView), ID_NEWFILE, L"New text file");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, flag(folderView && !clipboard_.empty()), ID_PASTE, L"Paste\tCtrl+V");
        AppendMenuW(menu, flag(true), ID_SELECTALL, L"Select all\tCtrl+A");
        AppendMenuW(menu, flag(true), ID_REFRESH, L"Refresh\tF5");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, flag(!thisPc_), ID_PROPS, L"Properties");
    }
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

// ===========================================================================
//  Edit boxes: Enter = go / search, Esc = cancel
// ===========================================================================
LRESULT CALLBACK MainWindow::EditSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR id, DWORD_PTR) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                PostMessageW(GetParent(h), WM_COMMAND,
                             MAKEWPARAM(id == 1 ? ID_GO_ADDRESS : ID_DO_SEARCH, 0), 0);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                PostMessageW(GetParent(h), WM_COMMAND,
                             MAKEWPARAM(id == 1 ? ID_ESC_ADDRESS : ID_ESC_SEARCH, 0), 0);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;     // stop the "ding" sound
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, EditSubclassProc, id);
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ===========================================================================
//  Icons
// ===========================================================================
int MainWindow::iconIndexFor(const FileEntry& e) {
    SHFILEINFOW sfi;
    std::memset(&sfi, 0, sizeof(sfi));

    // Drives: ask for the real icon of that drive.
    if (thisPc_) {
        std::wstring key = e.fullPath.wstring();
        auto it = pathIconCache_.find(key);
        if (it != pathIconCache_.end()) return it->second;
        int idx = iconFolder_;
        if (SHGetFileInfoW(e.fullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
            idx = sfi.iIcon;
        pathIconCache_[key] = idx;
        return idx;
    }

    if (e.isDirectory) return iconFolder_;

    std::wstring ext = util::toLower(e.fullPath.extension().wstring());
    const bool ownIcon = (ext == L".exe" || ext == L".ico" || ext == L".lnk" || ext == L".msi" ||
                          ext == L".cur" || ext == L".scr");
    if (ownIcon) {                                // these files carry their own icon
        std::wstring key = e.fullPath.wstring();
        auto it = pathIconCache_.find(key);
        if (it != pathIconCache_.end()) return it->second;
        int idx = 0;
        if (SHGetFileInfoW(e.fullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
            idx = sfi.iIcon;
        pathIconCache_[key] = idx;
        return idx;
    }

    auto it = extIconCache_.find(ext);
    if (it != extIconCache_.end()) return it->second;
    std::wstring dummy = L"file" + ext;
    int idx = 0;
    if (SHGetFileInfoW(dummy.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
        idx = sfi.iIcon;
    extIconCache_[ext] = idx;
    return idx;
}

// ===========================================================================
//  Showing the current folder
// ===========================================================================
void MainWindow::navigateTo(fs::path target) {
    // 'target' is a copy on purpose: callers often pass a reference into
    // entries_, which is replaced while the folder is being reloaded.
    std::wstring err;
    if (!nav_.goTo(target, err)) {
        showError(err);
        updateAddressBar();
        return;
    }
    afterNavigation();
}

void MainWindow::afterNavigation() {
    exitSearchMode(false);
    loadCurrent();
}

void MainWindow::loadCurrent() {
    cancelSearch();
    inSearch_ = false;
    pathIconCache_.clear();

    std::wstring err;
    thisPc_ = nav_.current().empty();
    if (thisPc_) {
        entries_ = FileSystemManager::getDriveEntries();
    } else if (!FileSystemManager::listDirectory(nav_.current(), showHidden_, entries_, err)) {
        entries_.clear();
        showError(err);
    }
    applySort();
    applyColumnHeaders();
    refreshListView();
    updateAddressBar();
    updateTitle();
    updateStatusForView();
    syncSidebar();
}

void MainWindow::refreshView() {
    if (inSearch_) startSearch();
    else loadCurrent();
}

void MainWindow::applySort() {
    if (thisPc_) return;
    FileSystemManager::sortEntries(entries_, sortField_, sortAsc_);
}

void MainWindow::applyColumnHeaders() {
    auto setTitle = [this](int col, const wchar_t* t) {
        LVCOLUMNW c;
        ZeroMemory(&c, sizeof(c));
        c.mask    = LVCF_TEXT;
        c.pszText = const_cast<wchar_t*>(t);
        SendMessageW(hList_, LVM_SETCOLUMNW, static_cast<WPARAM>(col), reinterpret_cast<LPARAM>(&c));
    };
    setTitle(COL_DATE, thisPc_ ? L"Free space" : L"Date modified");
    setTitle(COL_SIZE, thisPc_ ? L"Total size" : L"Size");

    int sortCol = COL_NAME;
    switch (sortField_) {
        case SortField::Name: sortCol = COL_NAME; break;
        case SortField::Date: sortCol = COL_DATE; break;
        case SortField::Type: sortCol = COL_TYPE; break;
        case SortField::Size: sortCol = COL_SIZE; break;
    }
    HWND header = ListView_GetHeader(hList_);
    for (int i = 0; i <= COL_SIZE; ++i) {
        HDITEMW hd;
        ZeroMemory(&hd, sizeof(hd));
        hd.mask = HDI_FORMAT;
        if (SendMessageW(header, HDM_GETITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&hd))) {
            hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if (!thisPc_ && i == sortCol) hd.fmt |= (sortAsc_ ? HDF_SORTUP : HDF_SORTDOWN);
            SendMessageW(header, HDM_SETITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&hd));
        }
    }
    ListView_SetColumnWidth(hList_, COL_LOCATION, inSearch_ ? S(320) : 0);
}

void MainWindow::refreshListView() {
    const int count = static_cast<int>(entries_.size());
    ListView_SetItemState(hList_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(hList_, count, 0);
    if (count > 0) {
        ListView_RedrawItems(hList_, 0, count - 1);
        ListView_EnsureVisible(hList_, 0, FALSE);
    }
    InvalidateRect(hList_, nullptr, TRUE);

    // "This folder is empty" message
    if (count == 0 && !(inSearch_ && searchThread_ != nullptr)) {
        std::wstring msg = inSearch_ ? L"No items match your search."
                                     : (thisPc_ ? L"No drives found." : L"This folder is empty.");
        SetWindowTextW(hEmpty_, msg.c_str());
        ShowWindow(hEmpty_, SW_SHOW);
        SetWindowPos(hEmpty_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ShowWindow(hEmpty_, SW_HIDE);
    }
}

// ===========================================================================
//  Small UI updates
// ===========================================================================
void MainWindow::showError(const std::wstring& message) {
    setStatus(2, L"  " + message.substr(0, message.find(L'\n')));
    MessageBoxW(hwnd_, message.c_str(), L"File Explorer", MB_OK | MB_ICONWARNING);
}

void MainWindow::setStatus(int part, const std::wstring& text) {
    SendMessageW(hStatus_, SB_SETTEXTW, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str()));
}

void MainWindow::updateAddressBar() {
    SetWindowTextW(hAddress_, thisPc_ ? L"This PC" : nav_.current().c_str());
}

void MainWindow::updateTitle() {
    std::wstring name = L"This PC";
    if (!thisPc_) {
        name = nav_.current().filename().wstring();
        if (name.empty()) name = nav_.current().wstring();
    }
    SetWindowTextW(hwnd_, (name + L" - NSUT File Explorer").c_str());
}

void MainWindow::updateButtons() {
    const int count = static_cast<int>(ListView_GetSelectedCount(hList_));
    const bool folderView = !thisPc_ && !inSearch_;
    EnableWindow(hBack_, nav_.canGoBack());
    EnableWindow(hFwd_, nav_.canGoForward());
    EnableWindow(hUp_, nav_.canGoUp());
    EnableWindow(hNewFolder_, folderView);
    EnableWindow(hNewFile_, folderView);
    EnableWindow(hCut_, !thisPc_ && count > 0);
    EnableWindow(hCopy_, !thisPc_ && count > 0);
    EnableWindow(hPaste_, folderView && !clipboard_.empty());
    EnableWindow(hRename_, !thisPc_ && count == 1);
    EnableWindow(hDelete_, !thisPc_ && count > 0);
    EnableWindow(hProps_, true);
}

void MainWindow::updateSelectionStatus() {
    const int count = static_cast<int>(ListView_GetSelectedCount(hList_));
    const int total = static_cast<int>(entries_.size());
    std::wstring left;
    if (count == 0) {
        left = std::to_wstring(total) + (thisPc_ ? (total == 1 ? L" drive" : L" drives")
                                                 : (total == 1 ? L" item" : L" items"));
    } else {
        left = std::to_wstring(count) + (count == 1 ? L" item selected" : L" items selected");
    }
    setStatus(0, L"  " + left);

    std::wstring sizeText;
    if (count > 0 && count <= 500 && !thisPc_) {
        std::uintmax_t sum = 0;
        bool anyFile = false;
        for (int i : selectedIndices()) {
            const FileEntry& e = entries_[static_cast<size_t>(i)];
            if (!e.isDirectory) {
                sum += e.size;
                anyFile = true;
            }
        }
        if (anyFile) sizeText = util::formatSize(sum);
    }
    setStatus(1, L"  " + sizeText);
    updateButtons();
}

void MainWindow::updateStatusForView() {
    updateSelectionStatus();
    if (inSearch_) return;
    std::wstring right;
    if (!thisPc_) {
        std::uintmax_t total = 0, freeBytes = 0;
        if (FileSystemManager::getSpace(nav_.current(), total, freeBytes))
            right = L"  Free space: " + util::formatSize(freeBytes) + L" of " + util::formatSize(total);
    }
    setStatus(2, right);
}
