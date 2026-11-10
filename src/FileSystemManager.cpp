#include "FileSystemManager.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <unordered_map>

#ifdef _WIN32
  #include <windows.h>
  #include <aclapi.h>
  #include <shellapi.h>
  #include <shlobj.h>
  #include <shlwapi.h>
#else
  #include <wchar.h>
#endif

// ---------------------------------------------------------------------------
//  Local helper: compare two names the way Windows Explorer does
//  (case-insensitive, and "file2" comes before "file10").
// ---------------------------------------------------------------------------
static int compareNames(const std::wstring& a, const std::wstring& b) {
#ifdef _WIN32
    return StrCmpLogicalW(a.c_str(), b.c_str());
#else
    return wcscasecmp(a.c_str(), b.c_str());
#endif
}

// ===========================================================================
//  DriveInfo
// ===========================================================================
std::wstring DriveInfo::displayName() const {
    std::wstring letters = root.wstring();
    while (!letters.empty() && (letters.back() == L'\\' || letters.back() == L'/') && letters.size() > 1)
        letters.pop_back();
    std::wstring first = label.empty() ? typeName : label;
    return first + L" (" + letters + L")";
}

// ===========================================================================
//  Attributes
// ===========================================================================
AttributeInfo FileSystemManager::getAttributes(const fs::path& p) {
    AttributeInfo info;
#ifdef _WIN32
    DWORD a = GetFileAttributesW(p.c_str());          // OS system call wrapper
    if (a == INVALID_FILE_ATTRIBUTES) return info;
    info.valid        = true;
    info.hidden       = (a & FILE_ATTRIBUTE_HIDDEN) != 0;
    info.system       = (a & FILE_ATTRIBUTE_SYSTEM) != 0;
    info.readOnly     = (a & FILE_ATTRIBUTE_READONLY) != 0;
    info.archive      = (a & FILE_ATTRIBUTE_ARCHIVE) != 0;
    info.reparsePoint = (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    info.valid        = true;
    std::wstring n    = p.filename().wstring();
    info.hidden       = !n.empty() && n[0] == L'.';
    info.reparsePoint = fs::is_symlink(p, ec);
#endif
    return info;
}

bool FileSystemManager::isReparsePoint(const fs::path& p) {
    return getAttributes(p).reparsePoint;
}

std::wstring FileSystemManager::describeAttributes(const AttributeInfo& a) {
    std::wstring s;
    auto add = [&s](const wchar_t* t) {
        if (!s.empty()) s += L", ";
        s += t;
    };
    if (a.readOnly) add(L"Read-only");
    if (a.hidden)   add(L"Hidden");
    if (a.system)   add(L"System");
    if (a.archive)  add(L"Archive");
    if (a.reparsePoint) add(L"Link/Junction");
    if (s.empty()) s = L"Normal";
    return s;
}

std::wstring FileSystemManager::permissionBits(fs::perms p) {
    auto bit = [&p](fs::perms f, wchar_t c) { return (p & f) != fs::perms::none ? c : L'-'; };
    std::wstring s;
    s += bit(fs::perms::owner_read,   L'r');
    s += bit(fs::perms::owner_write,  L'w');
    s += bit(fs::perms::owner_exec,   L'x');
    s += bit(fs::perms::group_read,   L'r');
    s += bit(fs::perms::group_write,  L'w');
    s += bit(fs::perms::group_exec,   L'x');
    s += bit(fs::perms::others_read,  L'r');
    s += bit(fs::perms::others_write, L'w');
    s += bit(fs::perms::others_exec,  L'x');
    return s;
}

std::wstring FileSystemManager::getOwner(const fs::path& p) {
#ifdef _WIN32
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    // Ask the OS for the security descriptor (part of the ACL system) of the file.
    DWORD r = GetNamedSecurityInfoW(const_cast<wchar_t*>(p.c_str()), SE_FILE_OBJECT,
                                    OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr,
                                    nullptr, &sd);
    if (r != ERROR_SUCCESS) return L"Unknown (access denied)";
    wchar_t name[256] = {0}, domain[256] = {0};
    DWORD nl = 256, dl = 256;
    SID_NAME_USE use;
    std::wstring result = L"Unknown";
    if (LookupAccountSidW(nullptr, owner, name, &nl, domain, &dl, &use)) {
        result = std::wstring(domain) + L"\\" + name;
    }
    LocalFree(sd);
    return result;
#else
    (void)p;
    return L"N/A";
#endif
}

std::vector<std::wstring> FileSystemManager::describeAccess(const fs::path& p) {
    std::vector<std::wstring> lines;
    AttributeInfo at = getAttributes(p);
    lines.push_back(L"Attributes: " + describeAttributes(at));

    std::error_code ec;
    fs::file_status st = fs::status(p, ec);
    bool readable = false;
    if (fs::is_directory(st)) {
        std::error_code ec2;
        fs::directory_iterator it(p, ec2);          // try to open the folder
        readable = !ec2;
    } else {
        std::ifstream f(p, std::ios::binary);       // try to open the file for reading
        readable = f.is_open();
    }
    lines.push_back(std::wstring(L"Read access: ") + (readable ? L"Yes" : L"No (access denied)"));
    lines.push_back(std::wstring(L"Write access: ") + (at.readOnly ? L"No (read-only attribute)" : L"Yes"));
    if (!ec) lines.push_back(L"Permission bits: " + permissionBits(st.permissions()));
    lines.push_back(L"Owner: " + getOwner(p));
    return lines;
}

// ===========================================================================
//  Dates
// ===========================================================================
#ifdef _WIN32
static std::wstring fileTimeToString(const FILETIME& ft) {
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc)) return L"N/A";
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%02d-%02d-%04d %02d:%02d:%02d", local.wDay, local.wMonth,
                  local.wYear, local.wHour, local.wMinute, local.wSecond);
    return buf;
}
#endif

bool FileSystemManager::getTimes(const fs::path& p, std::wstring& created,
                                 std::wstring& modified, std::wstring& accessed) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) {
        created = modified = accessed = L"N/A";
        return false;
    }
    created  = fileTimeToString(d.ftCreationTime);
    modified = fileTimeToString(d.ftLastWriteTime);
    accessed = fileTimeToString(d.ftLastAccessTime);
    return true;
#else
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    created = accessed = L"N/A";
    modified = ec ? L"N/A" : util::formatTime(util::toTimeT(ft));
    return !ec;
#endif
}

// ===========================================================================
//  Entries and directory listing
// ===========================================================================
FileEntry FileSystemManager::makeEntry(const fs::path& p) {
    FileEntry e;
    e.fullPath = p;
    e.name = p.filename().wstring();
    if (e.name.empty()) e.name = p.wstring();      // roots such as the C drive

    std::error_code ec;
    fs::file_status st = fs::status(p, ec);
    if (ec) {
        e.accessible = false;
    } else {
        e.isDirectory = fs::is_directory(st);
        if (fs::is_regular_file(st)) {
            std::error_code ec2;
            std::uintmax_t sz = fs::file_size(p, ec2);
            if (!ec2) e.size = sz;
        }
    }

    std::error_code ec3;
    auto ft = fs::last_write_time(p, ec3);
    if (!ec3) {
        e.modified = util::toTimeT(ft);
        e.hasModified = true;
    }

    AttributeInfo at = getAttributes(p);
    e.isHidden   = at.hidden;
    e.isSystem   = at.system;
    e.isReadOnly = at.readOnly;
    return e;
}

bool FileSystemManager::listDirectory(const fs::path& dir, bool showHidden,
                                      std::vector<FileEntry>& out, std::wstring& error) {
    out.clear();
    error.clear();

    std::error_code ec;
    fs::directory_iterator it(dir, ec);            // opens the directory (OS call)
    if (ec) {
        error = util::friendlyError(ec, dir);
        return false;
    }

    fs::directory_iterator end;
    while (it != end) {
        FileEntry e = makeEntry(it->path());
        if (showHidden || !(e.isHidden || e.isSystem)) out.push_back(std::move(e));
        it.increment(ec);                          // move to the next item
        if (ec) break;                             // stop quietly on a bad entry
    }
    return true;
}

// ===========================================================================
//  Type names ("Text Document") - uses the Windows registry via the shell
// ===========================================================================
void FileSystemManager::ensureTypeName(FileEntry& e) {
    if (!e.typeText.empty()) return;
    if (e.isDirectory) {
        e.typeText = L"File folder";
        return;
    }
    std::wstring ext = util::toLower(e.fullPath.extension().wstring());

    std::wstring fallback;
    if (ext.size() > 1) {
        fallback = ext.substr(1);
        for (auto& c : fallback) c = static_cast<wchar_t>(towupper(c));
        fallback += L" File";
    } else {
        fallback = L"File";
    }

#ifdef _WIN32
    static std::unordered_map<std::wstring, std::wstring> cache;   // used by GUI thread only
    auto found = cache.find(ext);
    if (found != cache.end()) {
        e.typeText = found->second;
        return;
    }
    std::wstring dummy = L"file" + ext;
    SHFILEINFOW sfi;
    std::memset(&sfi, 0, sizeof(sfi));
    std::wstring text;
    if (SHGetFileInfoW(dummy.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES)) {
        text = sfi.szTypeName;
    }
    if (text.empty()) text = fallback;
    cache[ext] = text;
    e.typeText = text;
#else
    e.typeText = fallback;
#endif
}

// ===========================================================================
//  Sorting
// ===========================================================================
void FileSystemManager::sortEntries(std::vector<FileEntry>& v, SortField field, bool ascending) {
    if (field == SortField::Type) {
        for (auto& e : v) ensureTypeName(e);
    }
    std::stable_sort(v.begin(), v.end(), [&](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;      // folders first
        int c = 0;
        switch (field) {
            case SortField::Name:
                c = compareNames(a.name, b.name);
                break;
            case SortField::Type:
                c = compareNames(a.typeText, b.typeText);
                if (c == 0) c = compareNames(a.name, b.name);
                break;
            case SortField::Size:
                c = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
                if (c == 0) c = compareNames(a.name, b.name);
                break;
            case SortField::Date:
                c = (a.modified < b.modified) ? -1 : (a.modified > b.modified ? 1 : 0);
                if (c == 0) c = compareNames(a.name, b.name);
                break;
        }
        return ascending ? (c < 0) : (c > 0);
    });
}

// ===========================================================================
//  Drives
// ===========================================================================
std::vector<DriveInfo> FileSystemManager::getDrives() {
    std::vector<DriveInfo> drives;
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();               // bit 0 = A:, bit 1 = B: ...
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t letter = static_cast<wchar_t>(L'A' + i);
        std::wstring root;
        root += letter;
        root += L":\\";

        DriveInfo d;
        d.root = root;
        switch (GetDriveTypeW(root.c_str())) {
            case DRIVE_REMOVABLE: d.typeName = L"Removable Disk"; break;
            case DRIVE_FIXED:     d.typeName = L"Local Disk";     break;
            case DRIVE_REMOTE:    d.typeName = L"Network Drive";  break;
            case DRIVE_CDROM:     d.typeName = L"CD/DVD Drive";   break;
            case DRIVE_RAMDISK:   d.typeName = L"RAM Disk";       break;
            case DRIVE_NO_ROOT_DIR: continue;
            default:              d.typeName = L"Drive";          break;
        }

        wchar_t vol[MAX_PATH + 1] = {0};
        wchar_t fsn[MAX_PATH + 1] = {0};
        DWORD serial = 0, maxComp = 0, flags = 0;
        if (GetVolumeInformationW(root.c_str(), vol, MAX_PATH + 1, &serial, &maxComp, &flags,
                                  fsn, MAX_PATH + 1)) {
            d.ready      = true;
            d.label      = vol;
            d.fileSystem = fsn;
            ULARGE_INTEGER freeAvail, total, totalFree;
            if (GetDiskFreeSpaceExW(root.c_str(), &freeAvail, &total, &totalFree)) {
                d.total     = total.QuadPart;
                d.freeBytes = totalFree.QuadPart;
            }
        }
        drives.push_back(d);
    }
#else
    DriveInfo d;
    d.root = fs::path("/");
    d.typeName = L"Local Disk";
    d.ready = true;
    getSpace(d.root, d.total, d.freeBytes);
    drives.push_back(d);
#endif
    return drives;
}

std::vector<FileEntry> FileSystemManager::getDriveEntries() {
    std::vector<FileEntry> v;
    for (const DriveInfo& d : getDrives()) {
        FileEntry e;
        e.name        = d.displayName();
        e.fullPath    = d.root;
        e.isDirectory = true;
        e.typeText    = d.typeName + (d.fileSystem.empty() ? L"" : L" (" + d.fileSystem + L")");
        e.size        = d.total;
        e.extraText   = d.ready ? (util::formatSize(d.freeBytes) + L" free of " + util::formatSize(d.total))
                                : L"Not ready";
        v.push_back(std::move(e));
    }
    return v;
}

bool FileSystemManager::getSpace(const fs::path& p, std::uintmax_t& total, std::uintmax_t& freeBytes) {
    std::error_code ec;
    fs::space_info si = fs::space(p, ec);
    if (ec) {
        total = freeBytes = 0;
        return false;
    }
    total = si.capacity;
    freeBytes = si.free;
    return true;
}

// ===========================================================================
//  Quick access folders
// ===========================================================================
std::vector<std::pair<std::wstring, fs::path>> FileSystemManager::getQuickAccessFolders() {
    std::vector<std::pair<std::wstring, fs::path>> result;
    auto addIfExists = [&result](const std::wstring& label, const fs::path& p) {
        std::error_code ec;
        if (!p.empty() && fs::is_directory(p, ec)) result.emplace_back(label, p);
    };

#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {0};
    auto folder = [&buf](int csidl) -> fs::path {
        buf[0] = 0;
        if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf)))
            return fs::path(buf);
        return fs::path();
    };
    fs::path profile = folder(CSIDL_PROFILE);
    addIfExists(profile.filename().wstring().empty() ? L"Home" : profile.filename().wstring(), profile);
    addIfExists(L"Desktop",   folder(CSIDL_DESKTOPDIRECTORY));
    if (!profile.empty()) addIfExists(L"Downloads", profile / L"Downloads");
    addIfExists(L"Documents", folder(CSIDL_PERSONAL));
    addIfExists(L"Pictures",  folder(CSIDL_MYPICTURES));
    addIfExists(L"Music",     folder(CSIDL_MYMUSIC));
    addIfExists(L"Videos",    folder(CSIDL_MYVIDEO));
#else
    const char* home = std::getenv("HOME");
    if (home) {
        fs::path h(home);
        addIfExists(L"Home", h);
        addIfExists(L"Desktop", h / "Desktop");
        addIfExists(L"Downloads", h / "Downloads");
        addIfExists(L"Documents", h / "Documents");
    }
#endif
    return result;
}
