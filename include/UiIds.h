// ============================================================================
//  UiIds.h  -  numbers that identify buttons, menu items and custom messages
// ============================================================================
#pragma once

#include "Common.h"
#include <windows.h>

enum UiId : int {
    // child controls
    ID_BACK = 1001, ID_FORWARD, ID_UP, ID_REFRESH,
    ID_ADDRESS, ID_SEARCH,
    ID_NEWFOLDER, ID_NEWFILE, ID_CUT, ID_COPY, ID_PASTE, ID_RENAME, ID_DELETE, ID_PROPS, ID_HIDDEN,
    ID_LIST, ID_TREE, ID_STATUS, ID_EMPTYMSG,

    // extra commands (menus, keyboard, edit boxes)
    ID_OPEN = 1100, ID_OPENLOCATION, ID_DELETE_PERMANENT, ID_SELECTALL,
    ID_GO_ADDRESS, ID_DO_SEARCH, ID_ESC_ADDRESS, ID_ESC_SEARCH,
    ID_FOCUS_ADDRESS, ID_FOCUS_SEARCH
};

// Custom window messages (WM_APP + n is reserved for application use)
constexpr UINT WM_APP_SEARCH_DONE    = WM_APP + 1;   // wParam = search generation
constexpr UINT WM_APP_RELOAD         = WM_APP + 2;   // refresh view, then select pendingSelect_
constexpr UINT WM_APP_DRIVES_CHANGED = WM_APP + 3;   // a USB drive was plugged in / removed

// Sub-item (column) numbers of the file list
constexpr int COL_NAME = 0, COL_DATE = 1, COL_TYPE = 2, COL_SIZE = 3, COL_LOCATION = 4;
