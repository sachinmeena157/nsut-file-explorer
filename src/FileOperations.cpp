#include "FileOperations.h"
#include "FileSystemManager.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
  #include <windows.h>
  #include <shellapi.h>
  #ifndef FOF_WANTNUKEWARNING
    #define FOF_WANTNUKEWARNING 0x4000
  #endif
#endif

// ---------------------------------------------------------------------------
//  Local helpers
// ---------------------------------------------------------------------------
static std::wstring withCommas(std::uintmax_t n) {
    std::wstring s = std::to_wstring(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), L",");
    return s;
}

// ===========================================================================
//  Names
// ===========================================================================
bool FileOperations::isValidName(const std::wstring& name, std::wstring& reason) {
    if (name.empty() || util::trim(name).empty()) {
        reason = L"The name cannot be empty.";
        return false;
    }
    if (name.size() > 255) {
        reason = L"The name is too long (maximum 255 characters).";
        return false;
    }
    const std::wstring bad = L"\\/:*?\"<>|";
    for (wchar_t c : name) {
        if (bad.find(c) != std::wstring::npos || c < 32) {
            reason = L"A name cannot contain any of these characters:\n\\ / : * ? \" < > |";
            return false;
        }
    }
    if (name.back() == L' ' || name.back() == L'.') {
        reason = L"A name cannot end with a space or a dot.";
        return false;
    }
    // Names reserved by Windows (they would refer to devices, not files)
    std::wstring stem = util::toLower(name.substr(0, name.find(L'.')));
    static const wchar_t* reserved[] = {L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3",
                                        L"com4", L"com5", L"com6", L"com7", L"com8", L"com9",
                                        L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6",
                                        L"lpt7", L"lpt8", L"lpt9"};
    for (const wchar_t* r : reserved) {
        if (stem == r) {
            reason = L"\"" + name + L"\" is a reserved system name. Please choose another name.";
            return false;
        }
    }
    return true;
}

fs::path FileOperations::uniquePath(const fs::path& dir, const std::wstring& stem,
                                    const std::wstring& ext) {
    std::error_code ec;
    fs::path candidate = dir / (stem + ext);
    int n = 2;
    while (fs::exists(candidate, ec) && n < 10000) {
        candidate = dir / (stem + L" (" + std::to_wstring(n) + L")" + ext);
        ++n;
    }
    return candidate;
}

// ===========================================================================
//  Create
// ===========================================================================
OpResult FileOperations::createFolder(const fs::path& parent, const std::wstring& name,
                                      fs::path* created) {
    std::wstring reason;
    if (!isValidName(name, reason)) return OpResult::failure(reason);

    fs::path target = parent / name;
    std::error_code ec;
    if (fs::exists(target, ec))
        return OpResult::failure(L"A file or folder named \"" + name + L"\" already exists here.");

    if (!fs::create_directory(target, ec) || ec)
        return OpResult::failure(L"Could not create the folder.\n" + util::friendlyError(ec, target));

    if (created) *created = target;
    return OpResult::success();
}

OpResult FileOperations::createTextFile(const fs::path& parent, const std::wstring& name,
                                        fs::path* created) {
    std::wstring reason;
    if (!isValidName(name, reason)) return OpResult::failure(reason);

    fs::path target = parent / name;
    std::error_code ec;
    if (fs::exists(target, ec))
        return OpResult::failure(L"A file or folder named \"" + name + L"\" already exists here.");

    std::ofstream out(target, std::ios::out | std::ios::binary);   // creates an empty file
    if (!out.is_open())
        return OpResult::failure(L"Could not create the file. You may not have permission to write in this folder:\n" +
                                 parent.wstring());
    out.close();

    if (created) *created = target;
    return OpResult::success();
}

// ===========================================================================
//  Rename
// ===========================================================================
OpResult FileOperations::rename(const fs::path& item, const std::wstring& newName,
                                fs::path* newPath) {
    std::wstring reason;
    if (!isValidName(newName, reason)) return OpResult::failure(reason);

    std::wstring oldName = item.filename().wstring();
    fs::path target = item.parent_path() / newName;
    if (newName == oldName) {
        if (newPath) *newPath = item;
        return OpResult::success();                // nothing to do
    }

    std::error_code ec;
    if (fs::exists(target, ec) && util::toLower(newName) != util::toLower(oldName))
        return OpResult::failure(L"A file or folder named \"" + newName + L"\" already exists here.");

    fs::rename(item, target, ec);                  // OS call: MoveFile
    if (ec) return OpResult::failure(L"Could not rename.\n" + util::friendlyError(ec, item));

    if (newPath) *newPath = target;
    return OpResult::success();
}

// ===========================================================================
//  Delete
// ===========================================================================
OpResult FileOperations::remove(const std::vector<fs::path>& items, bool toRecycleBin) {
    if (items.empty()) return OpResult::success();

#ifdef _WIN32
    // SHFileOperation wants ONE string: path1\0path2\0...\0\0
    std::wstring from;
    for (const fs::path& p : items) {
        from += p.wstring();
        from.push_back(L'\0');
    }
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op;
    std::memset(&op, 0, sizeof(op));
    op.wFunc  = FO_DELETE;
    op.pFrom  = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (toRecycleBin) op.fFlags |= FOF_ALLOWUNDO | FOF_WANTNUKEWARNING;

    int r = SHFileOperationW(&op);
    if (r != 0 || op.fAnyOperationsAborted) {
        return OpResult::failure(L"Some items could not be deleted (error code " + std::to_wstring(r) +
                                 L").\nThey may be in use by another program or protected by Windows.");
    }
    return OpResult::success();
#else
    (void)toRecycleBin;
    for (const fs::path& p : items) {
        std::error_code ec;
        fs::remove_all(p, ec);
        if (ec) return OpResult::failure(L"Could not delete.\n" + util::friendlyError(ec, p));
    }
    return OpResult::success();
#endif
}

// ===========================================================================
//  Paste (copy or move)
// ===========================================================================
OpResult FileOperations::paste(const std::vector<fs::path>& sources, const fs::path& destDir,
                               bool move, std::vector<fs::path>* resultPaths) {
    std::error_code ec;
    if (!fs::is_directory(destDir, ec))
        return OpResult::failure(L"The destination is not a folder:\n" + destDir.wstring());

    int failed = 0;
    std::wstring firstError;
    auto fail = [&](const std::wstring& msg) {
        ++failed;
        if (firstError.empty()) firstError = msg;
    };

    for (const fs::path& src : sources) {
        std::error_code e1;
        if (!fs::exists(src, e1)) {
            fail(L"The item no longer exists:\n" + src.wstring());
            continue;
        }
        const bool srcIsDir = fs::is_directory(src, e1);

        if (srcIsDir && util::isInside(destDir, src)) {
            fail(L"A folder cannot be " + std::wstring(move ? L"moved" : L"copied") +
                 L" into itself:\n" + src.wstring());
            continue;
        }

        // Moving something into the folder it is already in does nothing.
        std::error_code e2;
        if (move && fs::equivalent(src.parent_path(), destDir, e2) && !e2) {
            if (resultPaths) resultPaths->push_back(src);
            continue;
        }

        // Choose a free target name.
        fs::path target = destDir / src.filename();
        if (fs::exists(target, e1)) {
            std::wstring suffix = move ? L"" : L" - Copy";
            if (srcIsDir)
                target = uniquePath(destDir, src.filename().wstring() + suffix, L"");
            else
                target = uniquePath(destDir, src.stem().wstring() + suffix, src.extension().wstring());
        }

        std::error_code e3;
        if (!move) {
            if (srcIsDir)
                fs::copy(src, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, e3);
            else
                fs::copy_file(src, target, fs::copy_options::none, e3);
            if (e3) {
                fail(L"Could not copy:\n" + util::friendlyError(e3, src));
                continue;
            }
        } else {
            fs::rename(src, target, e3);           // fast path: same drive
            if (e3) {
                // Different drive: copy first, then delete the original.
                std::error_code e4;
                if (srcIsDir)
                    fs::copy(src, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, e4);
                else
                    fs::copy_file(src, target, fs::copy_options::none, e4);
                if (e4) {
                    fail(L"Could not move:\n" + util::friendlyError(e4, src));
                    continue;
                }
                std::error_code e5;
                fs::remove_all(src, e5);
                if (e5) {
                    fail(L"The item was copied but the original could not be removed:\n" +
                         util::friendlyError(e5, src));
                    continue;
                }
            }
        }
        if (resultPaths) resultPaths->push_back(target);
    }

    if (failed == 0) return OpResult::success();
    std::wstring msg = firstError;
    if (failed > 1) msg += L"\n\n(" + std::to_wstring(failed) + L" items failed in total.)";
    return OpResult::failure(msg);
}

// ===========================================================================
//  Properties
// ===========================================================================
void FileOperations::measureFolder(const fs::path& dir, std::uintmax_t& bytes, std::uintmax_t& files,
                                   std::uintmax_t& folders, bool& partial, std::size_t maxItems) {
    bytes = files = folders = 0;
    partial = false;
    std::size_t visited = 0;
    std::vector<fs::path> stack;
    stack.push_back(dir);

    while (!stack.empty()) {
        fs::path current = stack.back();
        stack.pop_back();

        std::error_code ec;
        fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        fs::directory_iterator end;
        while (it != end) {
            if (++visited > maxItems) {
                partial = true;
                return;
            }
            std::error_code e1;
            const fs::directory_entry& de = *it;
            if (de.is_directory(e1)) {
                ++folders;
                if (!FileSystemManager::isReparsePoint(de.path())) stack.push_back(de.path());
            } else {
                ++files;
                std::error_code e2;
                std::uintmax_t sz = de.file_size(e2);
                if (!e2) bytes += sz;
            }
            it.increment(ec);
            if (ec) break;
        }
    }
}

FileProperties FileOperations::getProperties(const fs::path& p) {
    FileProperties pr;
    FileEntry e = FileSystemManager::makeEntry(p);
    FileSystemManager::ensureTypeName(e);

    pr.name        = e.name;
    pr.type        = e.typeText;
    pr.isDirectory = e.isDirectory;
    pr.fullPath    = p.wstring();
    pr.location    = p.has_parent_path() && p.parent_path() != p ? p.parent_path().wstring() : p.wstring();
    std::wstring ext = p.extension().wstring();
    pr.extension   = (e.isDirectory || ext.empty()) ? L"(none)" : ext;

    if (e.isDirectory) {
        measureFolder(p, pr.size, pr.fileCount, pr.folderCount, pr.sizeIsPartial);
    } else {
        pr.size = e.size;
    }
    FileSystemManager::getTimes(p, pr.created, pr.modified, pr.accessed);
    pr.accessLines = FileSystemManager::describeAccess(p);
    return pr;
}

std::wstring FileProperties::toText() const {
    std::wostringstream os;
    os << L"Name:        " << name << L"\n";
    os << L"Type:        " << type << L"\n";
    if (!isDirectory) os << L"Extension:   " << extension << L"\n";
    os << L"Location:    " << location << L"\n";
    os << L"Full path:   " << fullPath << L"\n\n";

    os << L"Size:        " << util::formatSize(size) << L"  (" << withCommas(size) << L" bytes)";
    if (isDirectory && sizeIsPartial) os << L"  - at least (folder is very large)";
    os << L"\n";
    if (isDirectory)
        os << L"Contains:    " << withCommas(fileCount) << L" files, " << withCommas(folderCount)
           << L" folders\n";

    os << L"\nCreated:     " << created << L"\n";
    os << L"Modified:    " << modified << L"\n";
    os << L"Accessed:    " << accessed << L"\n\n";
    for (const std::wstring& line : accessLines) os << line << L"\n";
    return os.str();
}
