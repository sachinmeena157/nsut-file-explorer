#include "SearchManager.h"
#include "FileSystemManager.h"

// Classic wildcard matching with two pointers (no recursion, so it is fast
// and cannot overflow the stack). Both strings must already be lower case.
bool SearchManager::wildcardMatch(const std::wstring& text, const std::wstring& pattern) {
    size_t t = 0, p = 0;
    size_t starP = std::wstring::npos, starT = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t])) {
            ++t;
            ++p;
        } else if (p < pattern.size() && pattern[p] == L'*') {
            starP = p++;
            starT = t;
        } else if (starP != std::wstring::npos) {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}

bool SearchManager::matches(const std::wstring& name, const std::wstring& query) {
    std::wstring q = util::toLower(util::trim(query));
    if (q.empty()) return false;
    std::wstring n = util::toLower(name);
    if (q.find_first_of(L"*?") != std::wstring::npos) return wildcardMatch(n, q);
    return n.find(q) != std::wstring::npos;
}

std::vector<FileEntry> SearchManager::search(const std::vector<fs::path>& roots,
                                             const std::wstring& query,
                                             bool includeHidden,
                                             std::size_t maxResults,
                                             bool& truncated,
                                             const std::atomic<bool>& cancel) {
    std::vector<FileEntry> results;
    truncated = false;

    // We walk the folder tree with our own stack instead of
    // recursive_directory_iterator. One unreadable folder then never stops
    // the whole search.
    std::vector<fs::path> stack(roots.rbegin(), roots.rend());

    while (!stack.empty() && !cancel.load()) {
        fs::path dir = stack.back();
        stack.pop_back();

        std::error_code ec;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;                           // cannot open this folder - skip it

        fs::directory_iterator end;
        while (it != end && !cancel.load()) {
            const fs::path p = it->path();

            if (!includeHidden) {
                AttributeInfo at = FileSystemManager::getAttributes(p);
                if (at.hidden || at.system) {
                    it.increment(ec);
                    if (ec) break;
                    continue;
                }
            }

            if (matches(p.filename().wstring(), query)) {
                results.push_back(FileSystemManager::makeEntry(p));
                if (results.size() >= maxResults) {
                    truncated = true;
                    return results;
                }
            }

            std::error_code e1;
            if (it->is_directory(e1) && !e1 && !FileSystemManager::isReparsePoint(p)) {
                stack.push_back(p);                 // visit this sub-folder later
            }

            it.increment(ec);
            if (ec) break;
        }
    }
    return results;
}
