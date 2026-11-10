#include "NavigationManager.h"

bool NavigationManager::validateDirectory(const fs::path& p, std::wstring& error) {
    std::error_code ec;
    bool exists = fs::exists(p, ec);
    if (ec && !exists) {
        error = util::friendlyError(ec, p);
        return false;
    }
    if (!exists) {
        error = L"The path does not exist:\n" + p.wstring();
        return false;
    }
    if (!fs::is_directory(p, ec)) {
        error = L"This is not a folder:\n" + p.wstring();
        return false;
    }
    // Try to really open it - this is where "Access denied" shows up.
    std::error_code ec2;
    fs::directory_iterator it(p, ec2);
    if (ec2) {
        error = util::friendlyError(ec2, p);
        return false;
    }
    return true;
}

bool NavigationManager::goTo(const fs::path& target, std::wstring& error) {
    error.clear();
    if (!target.empty()) {
        if (!validateDirectory(target, error)) return false;
    }
    if (started_) {
        if (target == current_) return true;       // already here, nothing to remember
        back_.push_back(current_);
    }
    current_ = target;
    started_ = true;
    forward_.clear();
    return true;
}

bool NavigationManager::goBack(std::wstring& error) {
    error.clear();
    while (!back_.empty()) {
        fs::path prev = back_.back();
        if (!prev.empty() && !validateDirectory(prev, error)) {
            back_.pop_back();                      // folder vanished - skip it
            continue;
        }
        back_.pop_back();
        forward_.push_back(current_);
        current_ = prev;
        error.clear();
        return true;
    }
    return false;
}

bool NavigationManager::goForward(std::wstring& error) {
    error.clear();
    while (!forward_.empty()) {
        fs::path next = forward_.back();
        if (!next.empty() && !validateDirectory(next, error)) {
            forward_.pop_back();
            continue;
        }
        forward_.pop_back();
        back_.push_back(current_);
        current_ = next;
        error.clear();
        return true;
    }
    return false;
}

fs::path NavigationManager::parentOfCurrent() const {
    if (current_.empty()) return fs::path();
    fs::path parent = current_.parent_path();
    // A drive root such as C:\ has no parent -> go to "This PC" (empty path).
    if (parent.empty() || parent == current_) return fs::path();
    return parent;
}

bool NavigationManager::canGoUp() const {
    return started_ && !current_.empty();
}

bool NavigationManager::goUp(std::wstring& error) {
    if (!canGoUp()) return false;
    return goTo(parentOfCurrent(), error);
}
