// ============================================================================
//  Common.h  -  Shared types and small helper functions used by every module
//  Project : NSUT File Explorer (Operating Systems Mini Project)
// ============================================================================
#pragma once

// ---- Windows build settings (must come before <windows.h> is included) -----
#ifdef _WIN32
  #ifndef UNICODE
    #define UNICODE
  #endif
  #ifndef _UNICODE
    #define _UNICODE
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0A00   // Windows 10 / 11
  #endif
  #ifndef WINVER
    #define WINVER 0x0A00
  #endif
#endif

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
//  Columns the user can sort by
// ---------------------------------------------------------------------------
enum class SortField { Name, Type, Size, Date };

// ---------------------------------------------------------------------------
//  FileEntry : everything we know about ONE file or folder (its "metadata").
//  The GUI shows a list of these.
// ---------------------------------------------------------------------------
struct FileEntry {
    std::wstring   name;                 // e.g. L"report.docx"
    fs::path       fullPath;             // e.g. C:\Users\Sachin\report.docx
    std::wstring   typeText;             // e.g. L"Microsoft Word Document" (filled lazily)
    std::wstring   extraText;            // used only by the "This PC" view (free space text)
    bool           isDirectory = false;
    bool           isHidden    = false;
    bool           isSystem    = false;
    bool           isReadOnly  = false;
    bool           accessible  = true;   // false if the OS refused to give us information
    bool           hasModified = false;
    std::uintmax_t size        = 0;      // bytes (files only)
    std::time_t    modified    = 0;      // last-write time
};

// ---------------------------------------------------------------------------
//  Small helper functions (implemented in Common.cpp)
// ---------------------------------------------------------------------------
namespace util {

std::wstring toLower(const std::wstring& s);
std::wstring trim(const std::wstring& s);

// Convert a narrow (ANSI/UTF-8) string, e.g. an error message from the C++
// library, to a wide string that Windows controls can display.
std::wstring narrowToWide(const std::string& s);

// 1536 -> "1.50 KB"
std::wstring formatSize(std::uintmax_t bytes);

// time_t -> "02-11-2026 04:35 PM"
std::wstring formatTime(std::time_t t);

// Convert the C++17 file-time type to a normal time_t
std::time_t toTimeT(fs::file_time_type ft);

// Turn an error_code into a message a normal person can understand.
std::wstring friendlyError(const std::error_code& ec, const fs::path& path);

// Clean up a path typed by the user (quotes, spaces, "C:" -> "C:\").
fs::path normalizeUserPath(const std::wstring& typed);

// true if 'child' is the same as, or located inside, 'parent'
bool isInside(const fs::path& child, const fs::path& parent);

} // namespace util
