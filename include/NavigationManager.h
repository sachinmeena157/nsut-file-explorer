// ============================================================================
//  NavigationManager.h
//  Remembers where the user is and where they have been.
//  Uses two STACKS (back stack and forward stack) - exactly like a web browser.
//
//  An EMPTY path means the special "This PC" view (the list of drives).
// ============================================================================
#pragma once

#include "Common.h"

#include <string>
#include <vector>

class NavigationManager {
public:
    // Try to go to 'target'. On success the old location is pushed on the
    // back stack and the forward stack is cleared. On failure 'error' says why
    // (invalid path, access denied ...) and nothing changes.
    bool goTo(const fs::path& target, std::wstring& error);

    bool goBack(std::wstring& error);
    bool goForward(std::wstring& error);
    bool goUp(std::wstring& error);

    bool canGoBack() const    { return !back_.empty(); }
    bool canGoForward() const { return !forward_.empty(); }
    bool canGoUp() const;

    const fs::path& current() const { return current_; }
    bool isThisPC() const           { return started_ && current_.empty(); }

    // Parent folder of the current location (empty path = "This PC").
    fs::path parentOfCurrent() const;

    // Checks that 'p' exists, is a folder and that the OS allows us to open it.
    static bool validateDirectory(const fs::path& p, std::wstring& error);

private:
    fs::path               current_;
    bool                   started_ = false;
    std::vector<fs::path>  back_;
    std::vector<fs::path>  forward_;
};
