// Console tests for the non-GUI modules. Build & run (Windows or Linux):
//   g++ -std=c++17 -Iinclude tests/test_core.cpp src/Common.cpp src/FileSystemManager.cpp
//       src/NavigationManager.cpp src/FileOperations.cpp src/SearchManager.cpp -o test_core
#include "Common.h"
#include "FileOperations.h"
#include "FileSystemManager.h"
#include "NavigationManager.h"
#include "SearchManager.h"

#include <atomic>
#include <fstream>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
#endif

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (cond) { ++g_pass; }                                                        \
        else { ++g_fail; std::cout << "FAILED line " << __LINE__ << ": " #cond "\n"; } \
    } while (0)

static void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    f << text;
}

int main() {
    fs::path root = fs::temp_directory_path() / "fe_test_root";
    fs::remove_all(root);
    fs::create_directories(root / "alpha" / "inner");
    fs::create_directories(root / "beta");
    writeFile(root / "b.txt", "hello world");          // 11 bytes
    writeFile(root / "a10.txt", "x");
    writeFile(root / "a2.txt", "xx");
    writeFile(root / "alpha" / "inner" / "deep_report.txt", "deep");
    writeFile(root / ".hidden_file", "h");
#ifdef _WIN32
    SetFileAttributesW((root / ".hidden_file").c_str(), FILE_ATTRIBUTE_HIDDEN);   // dot-files are not hidden on Windows
#endif

    // ---------- util ----------
    CHECK(util::formatSize(0) == L"0 B");
    CHECK(util::formatSize(1536) == L"1.50 KB");
    CHECK(util::formatSize(1048576) == L"1.00 MB");
    CHECK(util::toLower(L"AbC") == L"abc");
    CHECK(util::trim(L"  x  ") == L"x");
    CHECK(util::isInside(root / "alpha" / "inner", root / "alpha"));
    CHECK(!util::isInside(root / "alpha", root / "alpha" / "inner"));
    CHECK(util::isInside(root / "alpha", root / "alpha"));
    CHECK(!util::isInside(root / "alphabet", root / "alpha"));

    // ---------- FileSystemManager ----------
    std::vector<FileEntry> list;
    std::wstring err;
    CHECK(FileSystemManager::listDirectory(root, false, list, err));
    CHECK(list.size() == 5);                       // alpha beta a10 a2 b  (hidden one excluded)
    CHECK(FileSystemManager::listDirectory(root, true, list, err));
    CHECK(list.size() == 6);
    FileSystemManager::sortEntries(list, SortField::Name, true);
    CHECK(list[0].isDirectory && list[1].isDirectory);          // folders first
    CHECK(list[0].name == L"alpha" && list[1].name == L"beta");
    FileSystemManager::sortEntries(list, SortField::Size, false);
    CHECK(list[2].name == L"b.txt");                            // biggest file after the folders
    FileSystemManager::sortEntries(list, SortField::Type, true);
    CHECK(list[0].isDirectory);
    CHECK(!FileSystemManager::listDirectory(root / "nope", false, list, err));
    CHECK(!err.empty());
    FileEntry fe = FileSystemManager::makeEntry(root / "b.txt");
    CHECK(fe.size == 11 && !fe.isDirectory && fe.hasModified);
    FileSystemManager::ensureTypeName(fe);
    CHECK(!fe.typeText.empty());
    CHECK(!FileSystemManager::getDrives().empty());
    CHECK(!FileSystemManager::getDriveEntries().empty());
    CHECK(!FileSystemManager::describeAccess(root / "b.txt").empty());

    // ---------- NavigationManager ----------
    NavigationManager nav;
    CHECK(nav.goTo(root, err));
    CHECK(!nav.canGoBack() && !nav.canGoForward() && nav.canGoUp());
    CHECK(nav.goTo(root / "alpha", err));
    CHECK(nav.goTo(root / "alpha" / "inner", err));
    CHECK(nav.canGoBack());
    CHECK(nav.goBack(err) && nav.current() == root / "alpha");
    CHECK(nav.canGoForward());
    CHECK(nav.goForward(err) && nav.current() == root / "alpha" / "inner");
    CHECK(nav.goUp(err) && nav.current() == root / "alpha");
    CHECK(!nav.goTo(root / "does_not_exist", err) && !err.empty());
    CHECK(nav.current() == root / "alpha");                      // unchanged after failure
    CHECK(!nav.goTo(root / "b.txt", err));                       // a file is not a folder
    CHECK(nav.goTo(fs::path(), err) && nav.isThisPC());           // "This PC"
    CHECK(!nav.canGoUp());

    // ---------- FileOperations ----------
    std::wstring why;
    CHECK(!FileOperations::isValidName(L"bad:name", why));
    CHECK(!FileOperations::isValidName(L"", why));
    CHECK(!FileOperations::isValidName(L"CON", why));
    CHECK(!FileOperations::isValidName(L"end.", why));
    CHECK(FileOperations::isValidName(L"good name.txt", why));

    fs::path created;
    CHECK(FileOperations::createFolder(root, L"New folder", &created).ok && fs::is_directory(created));
    CHECK(!FileOperations::createFolder(root, L"New folder").ok);                 // duplicate
    CHECK(FileOperations::uniquePath(root, L"New folder").filename() == "New folder (2)");
    CHECK(FileOperations::createTextFile(root, L"notes.txt", &created).ok && fs::is_regular_file(created));
    CHECK(!FileOperations::createTextFile(root, L"notes.txt").ok);

    fs::path renamed;
    CHECK(FileOperations::rename(root / "notes.txt", L"renamed.txt", &renamed).ok);
    CHECK(fs::exists(root / "renamed.txt") && !fs::exists(root / "notes.txt"));
    CHECK(!FileOperations::rename(root / "renamed.txt", L"b.txt").ok);             // name taken
    CHECK(!FileOperations::rename(root / "renamed.txt", L"a/b").ok);               // illegal chars

    // copy file (same folder -> "- Copy" name)
    std::vector<fs::path> results;
    CHECK(FileOperations::paste({root / "b.txt"}, root, false, &results).ok);
    CHECK(fs::exists(root / "b - Copy.txt") && results.size() == 1);
    CHECK(FileOperations::paste({root / "b.txt"}, root, false).ok);
    CHECK(fs::exists(root / "b - Copy (2).txt"));
    // copy folder recursively into another folder
    CHECK(FileOperations::paste({root / "alpha"}, root / "beta", false).ok);
    CHECK(fs::exists(root / "beta" / "alpha" / "inner" / "deep_report.txt"));
    // folder into itself must fail
    CHECK(!FileOperations::paste({root / "alpha"}, root / "alpha" / "inner", false).ok);
    CHECK(!FileOperations::paste({root / "alpha"}, root / "alpha" / "inner", true).ok);
    // move
    CHECK(FileOperations::paste({root / "a2.txt"}, root / "beta", true).ok);
    CHECK(!fs::exists(root / "a2.txt") && fs::exists(root / "beta" / "a2.txt"));
    // move to same folder = no-op
    CHECK(FileOperations::paste({root / "a10.txt"}, root, true).ok && fs::exists(root / "a10.txt"));
    // missing source
    CHECK(!FileOperations::paste({root / "ghost.txt"}, root, false).ok);

    // properties
    FileProperties pr = FileOperations::getProperties(root / "alpha");
    CHECK(pr.isDirectory && pr.fileCount == 1 && pr.folderCount == 1 && pr.size == 4);
    CHECK(!pr.toText().empty());
    FileProperties pf = FileOperations::getProperties(root / "b.txt");
    CHECK(pf.size == 11 && pf.extension == L".txt");

    // ---------- SearchManager ----------
    CHECK(SearchManager::matches(L"Deep_Report.TXT", L"report"));
    CHECK(SearchManager::matches(L"Deep_Report.TXT", L"*.txt"));
    CHECK(SearchManager::matches(L"photo01.jpg", L"photo??.jpg"));
    CHECK(!SearchManager::matches(L"photo1.jpg", L"photo??.jpg"));
    CHECK(!SearchManager::matches(L"abc", L""));
    std::atomic<bool> cancel{false};
    bool trunc = false;
    auto found = SearchManager::search({root}, L"deep_report", true, 100, trunc, cancel);
    CHECK(found.size() == 2 && !trunc);                           // alpha/inner + beta/alpha/inner
    found = SearchManager::search({root}, L".txt", true, 3, trunc, cancel);
    CHECK(found.size() == 3 && trunc);
    found = SearchManager::search({root}, L"hidden", false, 100, trunc, cancel);
    CHECK(found.empty());                                         // hidden files skipped
    found = SearchManager::search({root}, L"hidden", true, 100, trunc, cancel);
    CHECK(found.size() == 1);
    cancel = true;
    found = SearchManager::search({root}, L"txt", true, 100, trunc, cancel);
    CHECK(found.empty());                                         // cancelled immediately

    // ---------- delete ----------
    CHECK(FileOperations::remove({root / "beta"}, false).ok && !fs::exists(root / "beta"));

    fs::remove_all(root);
    std::cout << "Passed: " << g_pass << "   Failed: " << g_fail << "\n";
    return g_fail == 0 ? 0 : 1;
}
