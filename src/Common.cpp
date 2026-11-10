#include "Common.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
  #include <windows.h>
#endif

namespace util {

std::wstring toLower(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return r;
}

std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::iswspace(s[a])) ++a;
    while (b > a && std::iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring narrowToWide(const std::string& s) {
    if (s.empty()) return L"";
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

std::wstring formatSize(std::uintmax_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    double value = static_cast<double>(bytes);
    int u = 0;
    while (value >= 1024.0 && u < 5) {
        value /= 1024.0;
        ++u;
    }
    std::wostringstream os;
    if (u == 0) {
        os << bytes << L" B";
    } else {
        os << std::fixed << std::setprecision(value < 10.0 ? 2 : 1) << value << L" " << units[u];
    }
    return os.str();
}

std::wstring formatTime(std::time_t t) {
    if (t == 0) return L"";
    // std::localtime is not thread-safe, but this function is only called
    // from the GUI thread (the search thread never formats dates).
    const std::tm* tmv = std::localtime(&t);
    if (tmv == nullptr) return L"";
    wchar_t buf[64];
    if (std::wcsftime(buf, 64, L"%d-%m-%Y %I:%M %p", tmv) == 0) return L"";
    return buf;
}

std::time_t toTimeT(fs::file_time_type ft) {
    using namespace std::chrono;
    // C++17 has no direct conversion, so we shift the time by the difference
    // between "now" on the file clock and "now" on the system clock.
    auto sctp = time_point_cast<system_clock::duration>(
        ft - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sctp);
}

std::wstring friendlyError(const std::error_code& ec, const fs::path& path) {
    std::wstring p = path.wstring();
    bool denied = (ec == std::errc::permission_denied);
    bool missing = (ec == std::errc::no_such_file_or_directory);
#ifdef _WIN32
    if (ec.value() == 5) denied = true;                       // ERROR_ACCESS_DENIED
    if (ec.value() == 2 || ec.value() == 3) missing = true;   // FILE/PATH_NOT_FOUND
#endif
    if (denied)  return L"Access denied. You do not have permission to open:\n" + p;
    if (missing) return L"The path does not exist:\n" + p;
    if (ec == std::errc::not_a_directory) return L"This is not a folder:\n" + p;
    if (ec == std::errc::file_exists)     return L"An item with this name already exists:\n" + p;
    return narrowToWide(ec.message()) + L"\n" + p;
}

fs::path normalizeUserPath(const std::wstring& typed) {
    std::wstring s = trim(typed);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = trim(s.substr(1, s.size() - 2));
    if (s.empty()) return fs::path();
    // "C:" alone means "current folder on drive C" in Windows - we want the root.
    if (s.size() == 2 && s[1] == L':') s += L'\\';
    fs::path p(s);
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (ec) abs = p;
    abs = abs.lexically_normal();
    // remove a trailing separator, except for roots like "C:\"
    std::wstring w = abs.wstring();
    while (w.size() > 3 && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
    return fs::path(w);
}

bool isInside(const fs::path& child, const fs::path& parent) {
    fs::path c = child.lexically_normal();
    fs::path p = parent.lexically_normal();
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci) {
        if (pi->empty()) break;   // trailing separator in 'parent'
        if (ci == c.end()) return false;
#ifdef _WIN32
        if (toLower(ci->wstring()) != toLower(pi->wstring())) return false;
#else
        if (*ci != *pi) return false;
#endif
    }
    return true;
}

} // namespace util
