// ============================================================================
//  MainWindowActions.cpp  (part 2 of 2)
//  What happens when the user clicks a button or presses a key:
//  open, create, rename, copy/cut/paste, delete, properties, search.
// ============================================================================
#include "MainWindow.h"

#include "FileOperations.h"
#include "FileSystemManager.h"
#include "SearchJob.h"
#include "SearchManager.h"
#include "UiIds.h"

#include <windowsx.h>
#include <shellapi.h>

#include <algorithm>
#include <unordered_set>

// ===========================================================================
//  Background search thread  (a real OS thread created with CreateThread)
// ===========================================================================
static DWORD WINAPI SearchThreadProc(LPVOID param) {
    SearchJob* job = static_cast<SearchJob*>(param);
    job->results = SearchManager::search(job->roots, job->query, job->includeHidden,
                                         3000, job->truncated, job->cancel);
    if (!job->cancel.load()) PostMessageW(job->hwnd, WM_APP_SEARCH_DONE, job->generation, 0);
    return 0;
}

// ===========================================================================
//  Command dispatcher: every button, menu item and shortcut ends up here
// ===========================================================================
void MainWindow::onCommand(int id) {
    std::wstring err;
    switch (id) {
        case ID_BACK:
            if (nav_.goBack(err)) afterNavigation();
            else if (!err.empty()) showError(err);
            break;
        case ID_FORWARD:
            if (nav_.goForward(err)) afterNavigation();
            else if (!err.empty()) showError(err);
            break;
        case ID_UP:
            if (nav_.goUp(err)) afterNavigation();
            else if (!err.empty()) showError(err);
            break;
        case ID_REFRESH:        refreshView(); break;

        case ID_NEWFOLDER:      createNewFolder(); break;
        case ID_NEWFILE:        createNewTextFile(); break;
        case ID_CUT:            copyOrCutSelected(true); break;
        case ID_COPY:           copyOrCutSelected(false); break;
        case ID_PASTE:          pasteHere(); break;
        case ID_DELETE:         deleteSelected(false); break;
        case ID_DELETE_PERMANENT: deleteSelected(true); break;
        case ID_PROPS:          showProperties(); break;
        case ID_OPEN:           openSelected(); break;
        case ID_OPENLOCATION:   openFileLocation(); break;

        case ID_RENAME: {
            std::vector<int> sel = selectedIndices();
            if (!thisPc_ && sel.size() == 1) {
                SetFocus(hList_);
                ListView_EditLabel(hList_, sel[0]);
            }
            break;
        }

        case ID_HIDDEN:
            showHidden_ = (Button_GetCheck(hHidden_) == BST_CHECKED);
            refreshView();
            break;

        case ID_SELECTALL:
            ListView_SetItemState(hList_, -1, LVIS_SELECTED, LVIS_SELECTED);
            break;

        case ID_GO_ADDRESS:     goToAddressText(); break;
        case ID_DO_SEARCH:      startSearch(); break;
        case ID_ESC_ADDRESS:
            updateAddressBar();
            SetFocus(hList_);
            break;
        case ID_ESC_SEARCH:
            SetWindowTextW(hSearch_, L"");
            if (inSearch_) exitSearchMode(true);
            SetFocus(hList_);
            break;
        case ID_FOCUS_ADDRESS:
            SetFocus(hAddress_);
            SendMessageW(hAddress_, EM_SETSEL, 0, -1);
            break;
        case ID_FOCUS_SEARCH:
            SetFocus(hSearch_);
            break;
    }
}

// ===========================================================================
//  Selection helpers
// ===========================================================================
std::vector<int> MainWindow::selectedIndices() const {
    std::vector<int> v;
    int i = -1;
    while ((i = ListView_GetNextItem(hList_, i, LVNI_SELECTED)) != -1) v.push_back(i);
    return v;
}

std::vector<fs::path> MainWindow::selectedPaths() const {
    std::vector<fs::path> v;
    for (int i : selectedIndices())
        if (i >= 0 && i < static_cast<int>(entries_.size())) v.push_back(entries_[static_cast<size_t>(i)].fullPath);
    return v;
}

void MainWindow::selectPaths(const std::vector<fs::path>& paths) {
    if (paths.empty()) return;
    std::unordered_set<std::wstring> wanted;
    for (const fs::path& p : paths) wanted.insert(util::toLower(p.wstring()));

    bool first = true;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (wanted.count(util::toLower(entries_[i].fullPath.wstring()))) {
            UINT st = LVIS_SELECTED | (first ? LVIS_FOCUSED : 0);
            ListView_SetItemState(hList_, static_cast<int>(i), st, st);
            if (first) ListView_EnsureVisible(hList_, static_cast<int>(i), FALSE);
            first = false;
        }
    }
}

int MainWindow::indexOfPath(const fs::path& p) const {
    std::wstring key = util::toLower(p.wstring());
    for (size_t i = 0; i < entries_.size(); ++i)
        if (util::toLower(entries_[i].fullPath.wstring()) == key) return static_cast<int>(i);
    return -1;
}

bool MainWindow::isCutItem(const fs::path& p) const {
    if (!clipboard_.isCut()) return false;
    std::wstring key = util::toLower(p.wstring());
    for (const fs::path& c : clipboard_.items())
        if (util::toLower(c.wstring()) == key) return true;
    return false;
}

// ===========================================================================
//  Open
// ===========================================================================
void MainWindow::openEntry(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    const FileEntry e = entries_[static_cast<size_t>(index)];     // copy: list may reload
    if (e.isDirectory) {
        navigateTo(e.fullPath);
        return;
    }
    // Ask Windows to open the file with its default application.
    HINSTANCE r = ShellExecuteW(hwnd_, L"open", e.fullPath.c_str(), nullptr,
                                e.fullPath.parent_path().c_str(), SW_SHOWNORMAL);
    INT_PTR code = reinterpret_cast<INT_PTR>(r);
    if (code <= 32) {
        std::wstring why;
        if (code == SE_ERR_NOASSOC || code == SE_ERR_ASSOCINCOMPLETE)
            why = L"No program is associated with this type of file.";
        else if (code == SE_ERR_ACCESSDENIED)
            why = L"Access denied. You do not have permission to open this file.";
        else if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            why = L"The file no longer exists.";
        else
            why = L"Windows could not open the file (error " + std::to_wstring(code) + L").";
        showError(why + L"\n" + e.fullPath.wstring());
    }
}

void MainWindow::openSelected() {
    std::vector<int> sel = selectedIndices();
    if (sel.empty()) return;
    if (sel.size() == 1) {
        openEntry(sel[0]);
        return;
    }
    int opened = 0;
    for (int i : sel) {
        if (!entries_[static_cast<size_t>(i)].isDirectory && opened < 15) {
            openEntry(i);
            ++opened;
        }
    }
}

void MainWindow::openFileLocation() {
    std::vector<fs::path> sel = selectedPaths();
    if (sel.size() != 1) return;
    fs::path file = sel[0];
    navigateTo(file.parent_path());
    selectPaths({file});
}

void MainWindow::goToAddressText() {
    int len = GetWindowTextLengthW(hAddress_);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hAddress_, &text[0], len + 1);
    text.resize(static_cast<size_t>(len));
    text = util::trim(text);

    if (text.empty()) {
        updateAddressBar();
        return;
    }
    if (util::toLower(text) == L"this pc") {
        navigateTo(fs::path());
        SetFocus(hList_);
        return;
    }
    fs::path p = util::normalizeUserPath(text);
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) {          // typed the path of a file: show it in its folder
        navigateTo(p.parent_path());
        selectPaths({p});
    } else {
        navigateTo(p);
    }
    SetFocus(hList_);
}

// ===========================================================================
//  Create / rename
// ===========================================================================
void MainWindow::beginRenameOf(const fs::path& p) {
    loadCurrent();
    int idx = indexOfPath(p);
    if (idx < 0) return;
    SetFocus(hList_);
    ListView_SetItemState(hList_, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(hList_, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hList_, idx, FALSE);
    ListView_EditLabel(hList_, idx);            // user can type the real name immediately
}

void MainWindow::createNewFolder() {
    if (thisPc_ || inSearch_) return;
    std::wstring name = FileOperations::uniquePath(nav_.current(), L"New folder").filename().wstring();
    fs::path created;
    OpResult r = FileOperations::createFolder(nav_.current(), name, &created);
    if (!r.ok) {
        showError(r.message);
        return;
    }
    beginRenameOf(created);
}

void MainWindow::createNewTextFile() {
    if (thisPc_ || inSearch_) return;
    std::wstring name =
        FileOperations::uniquePath(nav_.current(), L"New Text Document", L".txt").filename().wstring();
    fs::path created;
    OpResult r = FileOperations::createTextFile(nav_.current(), name, &created);
    if (!r.ok) {
        showError(r.message);
        return;
    }
    beginRenameOf(created);
}

void MainWindow::finishRename(int index, const wchar_t* newName) {
    if (index < 0 || index >= static_cast<int>(entries_.size()) || !newName) return;
    fs::path oldPath = entries_[static_cast<size_t>(index)].fullPath;
    fs::path newPath;
    OpResult r = FileOperations::rename(oldPath, newName, &newPath);
    if (!r.ok) {
        showError(r.message);
        return;
    }
    pendingSelect_ = newPath;
    PostMessageW(hwnd_, WM_APP_RELOAD, 0, 0);   // reload after the edit box has closed
}

// ===========================================================================
//  Copy / Cut / Paste
// ===========================================================================
void MainWindow::copyOrCutSelected(bool cut) {
    if (thisPc_) return;
    std::vector<fs::path> sel = selectedPaths();
    if (sel.empty()) return;
    clipboard_.set(sel, cut);
    InvalidateRect(hList_, nullptr, FALSE);     // cut items become grey
    updateButtons();
    std::wstring n = std::to_wstring(sel.size()) + (sel.size() == 1 ? L" item " : L" items ");
    setStatus(2, L"  " + n + (cut ? L"cut - choose a folder and press Paste" : L"copied - choose a folder and press Paste"));
}

void MainWindow::pasteHere() {
    if (clipboard_.empty() || thisPc_ || inSearch_) return;

    const bool move = clipboard_.isCut();
    std::vector<fs::path> results;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    OpResult r = FileOperations::paste(clipboard_.items(), nav_.current(), move, &results);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (move) clipboard_.clear();               // moved items cannot be pasted again
    loadCurrent();
    selectPaths(results);
    if (!r.ok) {
        showError(r.message);
    } else {
        std::wstring n = std::to_wstring(results.size()) + (results.size() == 1 ? L" item " : L" items ");
        setStatus(2, L"  " + n + (move ? L"moved" : L"copied"));
    }
}

// ===========================================================================
//  Delete (with confirmation)
// ===========================================================================
void MainWindow::deleteSelected(bool permanent) {
    if (thisPc_) return;
    std::vector<fs::path> sel = selectedPaths();
    if (sel.empty()) return;

    std::wstring what = (sel.size() == 1) ? (L"\"" + sel[0].filename().wstring() + L"\"")
                                          : (L"these " + std::to_wstring(sel.size()) + L" items");
    std::wstring question;
    if (permanent)
        question = L"Permanently delete " + what + L"?\n\nThis cannot be undone.";
    else
        question = L"Move " + what + L" to the Recycle Bin?";

    UINT flags = MB_YESNO | MB_ICONWARNING | (permanent ? MB_DEFBUTTON2 : MB_DEFBUTTON1);
    if (MessageBoxW(hwnd_, question.c_str(), L"Confirm delete", flags) != IDYES) return;

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    OpResult r = FileOperations::remove(sel, !permanent);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    refreshView();
    if (!r.ok) showError(r.message);
    else setStatus(2, L"  " + std::to_wstring(sel.size()) + (sel.size() == 1 ? L" item deleted" : L" items deleted"));
}

// ===========================================================================
//  Properties
// ===========================================================================
void MainWindow::showProperties() {
    std::vector<int> sel = selectedIndices();
    std::wstring text;

    if (sel.empty()) {
        if (thisPc_) return;
        text = FileOperations::getProperties(nav_.current()).toText();
    } else if (sel.size() == 1) {
        const FileEntry& e = entries_[static_cast<size_t>(sel[0])];
        if (thisPc_) {                                  // a drive
            for (const DriveInfo& d : FileSystemManager::getDrives()) {
                if (d.root != e.fullPath) continue;
                text = L"Drive:         " + d.displayName() + L"\n";
                text += L"Type:          " + d.typeName + L"\n";
                text += L"File system:   " + (d.fileSystem.empty() ? std::wstring(L"unknown") : d.fileSystem) + L"\n\n";
                if (d.ready) {
                    text += L"Capacity:     " + util::formatSize(d.total) + L"\n";
                    text += L"Used space:  " + util::formatSize(d.total - d.freeBytes) + L"\n";
                    text += L"Free space:   " + util::formatSize(d.freeBytes) + L"\n";
                } else {
                    text += L"The drive is not ready.\n";
                }
            }
        } else {
            text = FileOperations::getProperties(e.fullPath).toText();
        }
    } else {
        std::uintmax_t total = 0;
        int files = 0, folders = 0;
        for (int i : sel) {
            const FileEntry& e = entries_[static_cast<size_t>(i)];
            if (e.isDirectory) ++folders;
            else { ++files; total += e.size; }
        }
        text = std::to_wstring(sel.size()) + L" items selected\n\n";
        text += L"Files:     " + std::to_wstring(files) + L"\n";
        text += L"Folders:  " + std::to_wstring(folders) + L"\n";
        text += L"Total size of the files:  " + util::formatSize(total) + L"\n";
    }
    if (!text.empty()) MessageBoxW(hwnd_, text.c_str(), L"Properties", MB_OK | MB_ICONINFORMATION);
}

// ===========================================================================
//  Search (runs in its own thread so the window never freezes)
// ===========================================================================
void MainWindow::startSearch() {
    int len = GetWindowTextLengthW(hSearch_);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hSearch_, &text[0], len + 1);
    text.resize(static_cast<size_t>(len));
    text = util::trim(text);

    if (text.empty()) {                          // empty search box = leave search mode
        if (inSearch_) exitSearchMode(true);
        return;
    }

    cancelSearch();                              // stop an older search first

    std::vector<fs::path> roots;
    if (nav_.current().empty()) {
        for (const DriveInfo& d : FileSystemManager::getDrives())
            if (d.ready) roots.push_back(d.root);
    } else {
        roots.push_back(nav_.current());
    }
    if (roots.empty()) return;

    inSearch_ = true;
    thisPc_ = false;
    searchQuery_ = text;
    ++generation_;

    job_ = new SearchJob;
    job_->hwnd          = hwnd_;
    job_->generation    = generation_;
    job_->roots         = roots;
    job_->query         = text;
    job_->includeHidden = showHidden_;

    searchThread_ = CreateThread(nullptr, 0, SearchThreadProc, job_, 0, nullptr);
    if (!searchThread_) {
        delete job_;
        job_ = nullptr;
        inSearch_ = false;
        showError(L"The search could not be started.");
        return;
    }

    entries_.clear();
    applyColumnHeaders();
    refreshListView();
    setStatus(0, L"  Searching...");
    setStatus(1, L"");
    std::wstring where = nav_.current().empty() ? std::wstring(L"all drives") : nav_.current().wstring();
    setStatus(2, L"  Searching for \"" + text + L"\" in " + where);
    updateButtons();
}

void MainWindow::cancelSearch() {
    if (searchThread_) {
        if (job_) job_->cancel = true;                       // ask the thread to stop
        WaitForSingleObject(searchThread_, INFINITE);        // wait until it has stopped
        CloseHandle(searchThread_);
        searchThread_ = nullptr;
    }
    if (job_) {
        delete job_;
        job_ = nullptr;
    }
}

void MainWindow::exitSearchMode(bool reload) {
    cancelSearch();
    if (inSearch_) {
        inSearch_ = false;
        SetWindowTextW(hSearch_, L"");
    }
    if (reload) loadCurrent();
}

void MainWindow::onSearchDone(unsigned generation) {
    if (!job_ || generation != job_->generation) return;     // an old, cancelled search

    WaitForSingleObject(searchThread_, INFINITE);
    CloseHandle(searchThread_);
    searchThread_ = nullptr;

    entries_ = std::move(job_->results);
    searchTruncated_ = job_->truncated;
    delete job_;
    job_ = nullptr;

    thisPc_ = false;
    applySort();
    applyColumnHeaders();
    refreshListView();
    updateSelectionStatus();

    std::wstring msg = std::to_wstring(entries_.size()) + (entries_.size() == 1 ? L" result" : L" results") +
                       L" for \"" + searchQuery_ + L"\"";
    if (searchTruncated_) msg += L"  (only the first results are shown - try a more specific name)";
    setStatus(2, L"  " + msg);
}
