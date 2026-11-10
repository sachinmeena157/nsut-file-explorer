// ============================================================================
//  FileOperations.h
//  Everything that CHANGES the disk: create, rename, delete, copy, move.
//  Also builds the "Properties" information.
//  Every function reports problems through an OpResult - nothing crashes.
// ============================================================================
#pragma once

#include "Common.h"

#include <string>
#include <utility>
#include <vector>

struct OpResult {
    bool         ok = true;
    std::wstring message;          // filled when ok == false
    static OpResult success()                    { return OpResult{true, L""}; }
    static OpResult failure(const std::wstring& m) { return OpResult{false, m}; }
};

// ---------------------------------------------------------------------------
//  The application's own clipboard for Copy / Cut / Paste
// ---------------------------------------------------------------------------
class FileClipboard {
public:
    void set(const std::vector<fs::path>& items, bool cut) { items_ = items; cut_ = cut; }
    void clear()                                           { items_.clear(); cut_ = false; }
    bool empty() const                                     { return items_.empty(); }
    bool isCut() const                                     { return cut_; }
    const std::vector<fs::path>& items() const             { return items_; }
private:
    std::vector<fs::path> items_;
    bool cut_ = false;
};

// ---------------------------------------------------------------------------
//  Information shown in the Properties window
// ---------------------------------------------------------------------------
struct FileProperties {
    std::wstring name;
    std::wstring type;
    std::wstring extension;
    std::wstring location;
    std::wstring fullPath;
    bool         isDirectory = false;
    std::uintmax_t size = 0;           // for folders: total size of contents
    std::uintmax_t fileCount = 0;      // folders only
    std::uintmax_t folderCount = 0;    // folders only
    bool         sizeIsPartial = false;// true if we stopped counting early
    std::wstring created, modified, accessed;
    std::vector<std::wstring> accessLines;   // attributes, permissions, owner

    std::wstring toText() const;
};

class FileOperations {
public:
    // ---- Names -------------------------------------------------------------
    // Windows does not allow  \ / : * ? " < > |  in file names.
    static bool isValidName(const std::wstring& name, std::wstring& reason);

    // "New folder", "New folder (2)", "New folder (3)" ... first one that is free.
    static fs::path uniquePath(const fs::path& dir, const std::wstring& stem,
                               const std::wstring& ext = L"");

    // ---- Create ------------------------------------------------------------
    static OpResult createFolder(const fs::path& parent, const std::wstring& name,
                                 fs::path* created = nullptr);
    static OpResult createTextFile(const fs::path& parent, const std::wstring& name,
                                   fs::path* created = nullptr);

    // ---- Rename ------------------------------------------------------------
    static OpResult rename(const fs::path& item, const std::wstring& newName,
                           fs::path* newPath = nullptr);

    // ---- Delete ------------------------------------------------------------
    // toRecycleBin = true  -> item can be restored from the Recycle Bin
    // toRecycleBin = false -> permanent deletion
    static OpResult remove(const std::vector<fs::path>& items, bool toRecycleBin);

    // ---- Copy / Cut + Paste -----------------------------------------------
    // Copies (move == false) or moves (move == true) every source into destDir.
    // If a name already exists, a free name such as "file - Copy.txt" is used.
    static OpResult paste(const std::vector<fs::path>& sources, const fs::path& destDir,
                          bool move, std::vector<fs::path>* resultPaths = nullptr);

    // ---- Properties --------------------------------------------------------
    static FileProperties getProperties(const fs::path& p);

    // Counts files/folders/bytes inside a folder (stops after 'maxItems').
    static void measureFolder(const fs::path& dir, std::uintmax_t& bytes, std::uintmax_t& files,
                              std::uintmax_t& folders, bool& partial, std::size_t maxItems = 50000);
};
