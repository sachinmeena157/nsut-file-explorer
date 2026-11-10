// ============================================================================
//  FileSystemManager.h
//  Talks to the operating system's file system and returns information
//  (directory listings, metadata, drives, permissions).
//  It never shows any window - it only reads and describes.
// ============================================================================
#pragma once

#include "Common.h"

#include <string>
#include <utility>
#include <vector>

// Information about one disk drive (C:, D:, USB pen drive ...)
struct DriveInfo {
    fs::path       root;          // L"C:\\"
    std::wstring   label;         // volume label, e.g. L"Windows"
    std::wstring   typeName;      // "Local Disk", "Removable Disk" ...
    std::wstring   fileSystem;    // "NTFS", "FAT32", "exFAT" ...
    std::uintmax_t total     = 0; // bytes
    std::uintmax_t freeBytes = 0; // bytes
    bool           ready     = false;

    std::wstring displayName() const;   // e.g. L"Windows (C:)"
};

// File attribute flags (read from the OS)
struct AttributeInfo {
    bool valid        = false;
    bool hidden       = false;
    bool system       = false;
    bool readOnly     = false;
    bool archive      = false;
    bool reparsePoint = false;   // shortcut-like links (junctions / symlinks)
};

class FileSystemManager {
public:
    // ---- Directory listing -------------------------------------------------
    // Reads every item inside 'dir'. Returns false (and fills 'error') if the
    // folder cannot be opened, e.g. "Access denied".
    static bool listDirectory(const fs::path& dir, bool showHidden,
                              std::vector<FileEntry>& out, std::wstring& error);

    // Builds the metadata record (size, date, attributes ...) for one path.
    static FileEntry makeEntry(const fs::path& p);

    // ---- Sorting -----------------------------------------------------------
    // Folders are always kept above files, then the chosen column is used.
    static void sortEntries(std::vector<FileEntry>& v, SortField field, bool ascending);

    // Fills entry.typeText ("Text Document", "File folder" ...) if it is empty.
    // Call from the GUI thread only.
    static void ensureTypeName(FileEntry& e);

    // ---- Attributes / permissions -----------------------------------------
    static AttributeInfo getAttributes(const fs::path& p);
    static bool          isReparsePoint(const fs::path& p);
    static std::wstring  describeAttributes(const AttributeInfo& a);
    static std::wstring  permissionBits(fs::perms p);
    static std::wstring  getOwner(const fs::path& p);
    // Human readable lines about readability, writability, owner, attributes.
    static std::vector<std::wstring> describeAccess(const fs::path& p);

    // ---- Dates (created / modified / accessed) ----------------------------
    static bool getTimes(const fs::path& p, std::wstring& created,
                         std::wstring& modified, std::wstring& accessed);

    // ---- Disks and drives --------------------------------------------------
    static std::vector<DriveInfo> getDrives();
    static std::vector<FileEntry> getDriveEntries();         // drives as list rows
    static bool getSpace(const fs::path& p, std::uintmax_t& total, std::uintmax_t& freeBytes);

    // Desktop, Documents, Downloads ... (only the ones that exist)
    static std::vector<std::pair<std::wstring, fs::path>> getQuickAccessFolders();
};
