// ============================================================================
//  SearchJob.h  -  everything the background search thread needs
// ============================================================================
#pragma once

#include "Common.h"

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

struct SearchJob {
    HWND                  hwnd = nullptr;        // window to notify when finished
    unsigned              generation = 0;        // identifies this particular search
    std::vector<fs::path> roots;                 // folders to search in
    std::wstring          query;
    bool                  includeHidden = false;

    std::atomic<bool>     cancel{false};         // set to true to stop the thread early

    std::vector<FileEntry> results;              // written by the thread
    bool                   truncated = false;
};
