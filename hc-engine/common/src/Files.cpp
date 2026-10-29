#include "hem/pg/Files.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shlobj.h>
 #include <knownfolders.h>
#endif

namespace hem::pg::files
{
fs::path toPath (const std::string& utf8) { return fs::path (std::u8string (utf8.begin(), utf8.end())); }

std::string toText (const fs::path& p)
{
    const auto u8 = p.u8string();
    return std::string (u8.begin(), u8.end());
}

fs::path documentsFolder()
{
#if defined(_WIN32)
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Documents, 0, nullptr, &raw)) && raw != nullptr) result = fs::path (raw);
    CoTaskMemFree (raw);
    if (! result.empty()) return result;
    if (const wchar_t* profile = _wgetenv (L"USERPROFILE")) return fs::path (profile) / L"Documents";
    return fs::temp_directory_path();
#else
    if (const char* home = std::getenv ("HOME")) return fs::path (home) / "Documents";
    return fs::temp_directory_path();
#endif
}

fs::path executableFolder()
{
#if defined(_WIN32)
    std::wstring buf (32768, L'\0');
    const DWORD n = GetModuleFileNameW (nullptr, buf.data(), (DWORD) buf.size());
    buf.resize (n);
    return fs::path (buf).parent_path();
#else
    std::error_code ec;
    auto p = fs::read_symlink ("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
}

fs::path playgroundDocuments() { return documentsFolder() / "Hybrid Elite Mafia" / "Rework Playground"; }

std::optional<std::string> readText (const fs::path& p)
{
    std::ifstream in (p, std::ios::binary);
    if (! in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeTextAtomic (const fs::path& p, const std::string& text)
{
    std::error_code ec;
    if (! p.parent_path().empty()) fs::create_directories (p.parent_path(), ec);
    auto temp = p;
    temp += ".saving";
    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        out.write (text.data(), (std::streamsize) text.size());
        out.close();
        if (! out) { fs::remove (temp, ec); return false; }
    }
    fs::rename (temp, p, ec);
    if (ec) { fs::remove (temp, ec); return false; }
    return true;
}

std::string legalFileName (const std::string& name)
{
    std::string s;
    for (char c : name)
        if (std::string_view ("\"#@,;:<>*^|?\\/").find (c) == std::string_view::npos) s += c;
    if (s.size() > 128)
    {
        const auto dot = s.find_last_of ('.');
        if (dot != std::string::npos && (int) dot > std::max (0, (int) s.size() - 12))
            s = s.substr (0, 128 - (s.size() - dot)) + s.substr (dot);
        else
            s = s.substr (0, 128);
    }
    return s;
}

fs::path nonexistentSibling (const fs::path& file)
{
    std::error_code ec;
    if (! fs::exists (file, ec)) return file;
    const auto stem = toText (file.stem());
    const auto ext = toText (file.extension());
    const bool endsInDigit = ! stem.empty() && stem.back() >= '0' && stem.back() <= '9';
    for (int n = 2;; ++n)
    {
        const auto candidate = file.parent_path() / toPath (stem + (endsInDigit ? "_" : "") + std::to_string (n) + ext);
        if (! fs::exists (candidate, ec)) return candidate;
    }
}

std::string newId()
{
    static thread_local std::mt19937_64 rng (std::random_device {}() ^ (std::uint64_t) std::chrono::high_resolution_clock::now().time_since_epoch().count());
    unsigned char b[16];
    for (int i = 0; i < 16; i += 8)
    {
        const auto v = rng();
        for (int k = 0; k < 8; ++k) b[i + k] = (unsigned char) (v >> (8 * k));
    }
    b[6] = (unsigned char) ((b[6] & 0x0f) | 0x40);   // version 4
    b[8] = (unsigned char) ((b[8] & 0x3f) | 0x80);
    char out[40];
    std::snprintf (out, sizeof out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}

std::int64_t nowMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace hem::pg::files
