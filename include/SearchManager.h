// ============================================================================
//  SearchManager.h
//  Searches files and folders BY NAME inside a folder and ALL its sub-folders
//  (this is "directory traversal").
//
//  The search can be cancelled at any time through an atomic flag, because the
//  GUI runs it in a separate thread so the window never freezes.
// ============================================================================
#pragma once

#include "Common.h"

#include <atomic>
#include <string>
#include <vector>

class SearchManager {
public:
    // True if 'name' matches 'query'.
    //  - Not case sensitive.
    //  - Plain text  "report"  matches any name CONTAINING "report".
    //  - Wildcards   "*.txt"   "photo??.jpg"  are supported ( * = any text, ? = one character ).
    static bool matches(const std::wstring& name, const std::wstring& query);

    // Searches every folder in 'roots' (and everything below them).
    // Stops when 'maxResults' is reached (truncated = true) or when
    // 'cancel' becomes true.
    static std::vector<FileEntry> search(const std::vector<fs::path>& roots,
                                         const std::wstring& query,
                                         bool includeHidden,
                                         std::size_t maxResults,
                                         bool& truncated,
                                         const std::atomic<bool>& cancel);

private:
    static bool wildcardMatch(const std::wstring& text, const std::wstring& pattern);
};
