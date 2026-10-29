#include "Platform.hpp"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>

using Microsoft::WRL::ComPtr;

namespace hem::app::platform
{
std::wstring wide (const std::string& s)
{
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w ((std::size_t) n, L'\0');
    ::MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}

std::string utf8 (const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte (CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s ((std::size_t) n, '\0');
    ::WideCharToMultiByte (CP_UTF8, 0, w.data(), (int) w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

namespace
{
    struct Specs
    {
        std::vector<std::wstring> names, patterns;
        std::vector<COMDLG_FILTERSPEC> specs;
        explicit Specs (const std::vector<FileType>& types)
        {
            for (auto& t : types) { names.push_back (wide (t.name)); patterns.push_back (wide (t.patterns)); }
            for (std::size_t i = 0; i < types.size(); ++i) specs.push_back ({ names[i].c_str(), patterns[i].c_str() });
        }
    };

    void setFolder (IFileDialog* d, const fs::path& folder)
    {
        std::error_code ec;
        if (folder.empty() || ! fs::is_directory (folder, ec)) return;
        ComPtr<IShellItem> item;
        if (SUCCEEDED (::SHCreateItemFromParsingName (folder.c_str(), nullptr, IID_PPV_ARGS (&item)))) d->SetFolder (item.Get());
    }

    fs::path pathOf (IShellItem* item)
    {
        PWSTR p = nullptr;
        fs::path out;
        if (SUCCEEDED (item->GetDisplayName (SIGDN_FILESYSPATH, &p)) && p != nullptr) { out = p; ::CoTaskMemFree (p); }
        return out;
    }
}

std::vector<fs::path> openFiles (void* owner, const std::string& title, const fs::path& startFolder, const std::vector<FileType>& types, bool multiple)
{
    std::vector<fs::path> out;
    ComPtr<IFileOpenDialog> d;
    if (FAILED (::CoCreateInstance (CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&d)))) return out;
    DWORD opts = 0;
    d->GetOptions (&opts);
    d->SetOptions (opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | (multiple ? FOS_ALLOWMULTISELECT : 0));
    const std::wstring t = wide (title);
    d->SetTitle (t.c_str());
    Specs specs (types);
    if (! specs.specs.empty()) d->SetFileTypes ((UINT) specs.specs.size(), specs.specs.data());
    setFolder (d.Get(), startFolder);
    if (FAILED (d->Show ((HWND) owner))) return out;
    ComPtr<IShellItemArray> items;
    if (FAILED (d->GetResults (&items))) return out;
    DWORD n = 0;
    items->GetCount (&n);
    for (DWORD i = 0; i < n; ++i)
    {
        ComPtr<IShellItem> item;
        if (SUCCEEDED (items->GetItemAt (i, &item)))
            if (auto p = pathOf (item.Get()); ! p.empty()) out.push_back (p);
    }
    std::sort (out.begin(), out.end());   // "01 Drums", "02 Bass"...: the order the files are numbered in
    return out;
}

std::optional<fs::path> saveFile (void* owner, const std::string& title, const fs::path& startFile, const std::vector<FileType>& types,
                                  const std::string& defaultExt)
{
    ComPtr<IFileSaveDialog> d;
    if (FAILED (::CoCreateInstance (CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&d)))) return std::nullopt;
    DWORD opts = 0;
    d->GetOptions (&opts);
    d->SetOptions (opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    const std::wstring t = wide (title), ext = wide (defaultExt), name = startFile.filename().wstring();
    d->SetTitle (t.c_str());
    Specs specs (types);
    if (! specs.specs.empty()) d->SetFileTypes ((UINT) specs.specs.size(), specs.specs.data());
    if (! ext.empty()) d->SetDefaultExtension (ext.c_str());
    if (! name.empty()) d->SetFileName (name.c_str());
    setFolder (d.Get(), startFile.parent_path());
    if (FAILED (d->Show ((HWND) owner))) return std::nullopt;
    ComPtr<IShellItem> item;
    if (FAILED (d->GetResult (&item))) return std::nullopt;
    auto p = pathOf (item.Get());
    if (p.empty()) return std::nullopt;
    return p;
}

std::optional<fs::path> pickFolder (void* owner, const std::string& title, const fs::path& start)
{
    ComPtr<IFileOpenDialog> d;
    if (FAILED (::CoCreateInstance (CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&d)))) return std::nullopt;
    DWORD opts = 0;
    d->GetOptions (&opts);
    d->SetOptions (opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    const std::wstring t = wide (title);
    d->SetTitle (t.c_str());
    setFolder (d.Get(), start);
    if (FAILED (d->Show ((HWND) owner))) return std::nullopt;
    ComPtr<IShellItem> item;
    if (FAILED (d->GetResult (&item))) return std::nullopt;
    auto p = pathOf (item.Get());
    if (p.empty()) return std::nullopt;
    return p;
}

void reveal (const fs::path& p)
{
    std::error_code ec;
    if (fs::is_directory (p, ec))
    {
        ::ShellExecuteW (nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (PIDLIST_ABSOLUTE pidl = ::ILCreateFromPathW (p.c_str()))
    {
        ::SHOpenFolderAndSelectItems (pidl, 0, nullptr, 0);
        ::ILFree (pidl);
        return;
    }
    ::ShellExecuteW (nullptr, L"open", p.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openUrl (const std::string& url)
{
    if (url.rfind ("https://", 0) != 0 && url.rfind ("http://", 0) != 0) return;   // web pages only
    ::ShellExecuteW (nullptr, L"open", wide (url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool copyText (void* owner, const std::string& text)
{
    if (! ::OpenClipboard ((HWND) owner)) return false;
    ::EmptyClipboard();
    const std::wstring w = wide (text);
    HGLOBAL mem = ::GlobalAlloc (GMEM_MOVEABLE, (w.size() + 1) * sizeof (wchar_t));
    bool ok = false;
    if (mem != nullptr)
    {
        if (auto* dst = static_cast<wchar_t*> (::GlobalLock (mem)))
        {
            std::copy (w.c_str(), w.c_str() + w.size() + 1, dst);
            ::GlobalUnlock (mem);
            ok = ::SetClipboardData (CF_UNICODETEXT, mem) != nullptr;
        }
        if (! ok) ::GlobalFree (mem);
    }
    ::CloseClipboard();
    return ok;
}

bool startInOwnWindow (const fs::path& folder, const std::string& bat, const std::string& title)
{
    // cmd /c start "<title>" /D "<folder>" <bat>
    std::wstring cmd = L"cmd.exe /c start \"" + wide (title) + L"\" /D \"" + folder.wstring() + L"\" \"" + wide (bat) + L"\"";
    STARTUPINFOW si {};
    si.cb = sizeof (si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi {};
    if (! ::CreateProcessW (nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &si, &pi)) return false;
    ::CloseHandle (pi.hThread);
    ::CloseHandle (pi.hProcess);
    return true;
}

void loadPrivateFonts (const fs::path& fontsFolder)
{
    for (const wchar_t* f : { L"Poppins-Medium.ttf", L"Poppins-SemiBold.ttf", L"Poppins-Bold.ttf" })
    {
        const fs::path p = fontsFolder / f;
        std::error_code ec;
        if (fs::exists (p, ec)) ::AddFontResourceExW (p.c_str(), FR_PRIVATE, nullptr);
    }
}

void setOwner (void* window, void* owner)
{
    if (window != nullptr) ::SetWindowLongPtrW ((HWND) window, GWLP_HWNDPARENT, (LONG_PTR) owner);
}

void bringToFront (void* window)
{
    if (window == nullptr) return;
    if (::IsIconic ((HWND) window)) ::ShowWindow ((HWND) window, SW_RESTORE);
    ::SetForegroundWindow ((HWND) window);
}

void setWindowTitle (void* window, const std::string& title)
{
    if (window != nullptr) ::SetWindowTextW ((HWND) window, wide (title).c_str());
}

void fatalMessage (const std::string& title, const std::string& text)
{
    ::MessageBoxW (nullptr, wide (text).c_str(), wide (title).c_str(), MB_OK | MB_ICONERROR);
}

//==============================================================================
namespace
{
    COLORREF ref (Rgba c) { return RGB ((int) std::lround (c.r * 255), (int) std::lround (c.g * 255), (int) std::lround (c.b * 255)); }

    std::wstring toEditText (const std::string& s, bool multiline)
    {
        std::wstring w = wide (s);
        if (! multiline) return w;
        std::wstring out;
        out.reserve (w.size() + 16);
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) out += L'\r';
            out += w[i];
        }
        return out;
    }

    std::string fromEditText (const std::wstring& w)
    {
        std::wstring out;
        out.reserve (w.size());
        for (const wchar_t c : w) if (c != L'\r') out += c;
        return utf8 (out);
    }

    class NativeFields final : public TextFields
    {
    public:
        explicit NativeFields (HWND p) : parent (p)
        {
            ::SetWindowLongPtrW (parent, GWL_STYLE, ::GetWindowLongPtrW (parent, GWL_STYLE) | WS_CLIPCHILDREN);
            ::SetWindowSubclass (parent, &parentProc, 1, reinterpret_cast<DWORD_PTR> (this));
            background = ::CreateSolidBrush (ref (colours::well));
            disabledBg = ::CreateSolidBrush (ref (argb (0xff181b21)));
        }

        ~NativeFields() override
        {
            for (auto& [id, b] : boxes) if (b.hwnd) ::DestroyWindow (b.hwnd);
            ::RemoveWindowSubclass (parent, &parentProc, 1);
            for (auto& [h, f] : fonts) ::DeleteObject (f);
            ::DeleteObject (background);
            ::DeleteObject (disabledBg);
        }

        bool field (Id id, Rect px, std::string& text, const FieldStyle& style, int layer) override
        {
            Box& b = boxes[id];
            if (b.hwnd != nullptr && b.multiline != style.multiline) { ::DestroyWindow (b.hwnd); b = Box {}; }
            if (b.hwnd == nullptr)
            {
                DWORD st = WS_CHILD | WS_CLIPSIBLINGS | ES_LEFT;
                st |= style.multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL) : ES_AUTOHSCROLL;
                if (style.password) st |= ES_PASSWORD;
                b.hwnd = ::CreateWindowExW (0, L"EDIT", L"", st, 0, 0, 10, 10, parent, nullptr, ::GetModuleHandleW (nullptr), nullptr);
                b.multiline = style.multiline;
                ::SetWindowSubclass (b.hwnd, &editProc, 2, reinterpret_cast<DWORD_PTR> (this));
                ::SendMessageW (b.hwnd, EM_SETLIMITTEXT, style.multiline ? 200000 : 1024, 0);
                guard = true;
                ::SetWindowTextW (b.hwnd, toEditText (text, b.multiline).c_str());
                guard = false;
                b.last = text;
                byHwnd[b.hwnd] = id;
            }
            b.used = true;
            b.layer = layer;
            b.px = px;
            const int fontPx = std::max (8, (int) std::lround (style.fontHeightL / 1.4f * style.pixelsPerL));
            if (fontPx != b.fontPx)
            {
                b.fontPx = fontPx;
                ::SendMessageW (b.hwnd, WM_SETFONT, (WPARAM) fontFor (fontPx), TRUE);
                const int m = std::max (2, fontPx / 3);
                ::SendMessageW (b.hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM (m, m));
            }
            if (style.placeholder != b.placeholder && ! style.multiline)
            {
                b.placeholder = style.placeholder;
                ::SendMessageW (b.hwnd, EM_SETCUEBANNER, TRUE, (LPARAM) wide (style.placeholder).c_str());
            }
            if ((::IsWindowEnabled (b.hwnd) != FALSE) != style.enabled) ::EnableWindow (b.hwnd, style.enabled);

            bool changed = false;
            if (b.changed)
            {
                b.changed = false;
                const int n = ::GetWindowTextLengthW (b.hwnd);
                std::wstring w ((std::size_t) n + 1, L'\0');
                ::GetWindowTextW (b.hwnd, w.data(), n + 1);
                w.resize ((std::size_t) n);
                const std::string now = fromEditText (w);
                if (now != text) { text = now; changed = true; }
                b.last = text;
            }
            else if (text != b.last)
            {
                guard = true;
                ::SetWindowTextW (b.hwnd, toEditText (text, b.multiline).c_str());
                guard = false;
                b.last = text;
            }
            return changed;
        }

        void endFrame (const std::vector<std::pair<int, Rect>>& occluders) override
        {
            for (auto& [id, b] : boxes)
            {
                bool show = b.used;
                if (show)
                    for (const auto& [layer, r] : occluders)
                        if (layer > b.layer && r.x < b.px.right() && b.px.x < r.right() && r.y < b.px.bottom() && b.px.y < r.bottom()) { show = false; break; }
                if (show)
                {
                    const int x = (int) b.px.x, y = (int) b.px.y, w = std::max (4, (int) b.px.w), h = std::max (4, (int) b.px.h);
                    if (! b.visible || x != b.shownX || y != b.shownY || w != b.shownW || h != b.shownH)
                    {
                        ::SetWindowPos (b.hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        b.shownX = x; b.shownY = y; b.shownW = w; b.shownH = h;
                        b.visible = true;
                    }
                }
                else if (b.visible)
                {
                    if (::GetFocus() == b.hwnd) ::SetFocus (parent);
                    ::ShowWindow (b.hwnd, SW_HIDE);
                    b.visible = false;
                }
                b.used = false;
            }
        }

        bool anyFocused() const override
        {
            const HWND f = ::GetFocus();
            return f != nullptr && byHwnd.count (f) > 0;
        }

        void unfocus() override { if (anyFocused()) ::SetFocus (parent); }

        void focus (Id id) override
        {
            if (auto it = boxes.find (id); it != boxes.end() && it->second.hwnd != nullptr)
            {
                ::SetFocus (it->second.hwnd);
                ::SendMessageW (it->second.hwnd, EM_SETSEL, 0, -1);
            }
        }

    private:
        struct Box
        {
            HWND hwnd = nullptr;
            bool used = false, visible = false, changed = false, multiline = false;
            int layer = 0, fontPx = 0;
            int shownX = -1, shownY = -1, shownW = -1, shownH = -1;
            Rect px {};
            std::string last, placeholder;
        };

        HFONT fontFor (int px)
        {
            if (auto it = fonts.find (px); it != fonts.end()) return it->second;
            HFONT f = ::CreateFontW (-px, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Poppins Medium");
            if (f == nullptr) f = (HFONT) ::GetStockObject (DEFAULT_GUI_FONT);
            fonts[px] = f;
            return f;
        }

        static LRESULT CALLBACK parentProc (HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
        {
            auto* self = reinterpret_cast<NativeFields*> (data);
            switch (msg)
            {
                case WM_CTLCOLOREDIT:
                case WM_CTLCOLORSTATIC:
                {
                    const HWND ctl = (HWND) lp;
                    if (self->byHwnd.count (ctl) == 0) break;
                    const bool on = ::IsWindowEnabled (ctl) != FALSE;
                    HDC dc = (HDC) wp;
                    ::SetTextColor (dc, ref (on ? colours::paper : colours::paperDim));
                    ::SetBkColor (dc, ref (on ? colours::well : argb (0xff181b21)));
                    return (LRESULT) (on ? self->background : self->disabledBg);
                }
                case WM_COMMAND:
                    if (HIWORD (wp) == EN_CHANGE && ! self->guard)
                        if (auto it = self->byHwnd.find ((HWND) lp); it != self->byHwnd.end())
                            self->boxes[it->second].changed = true;
                    break;
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                    if (self->anyFocused()) ::SetFocus (h);   // a click on the console takes the keyboard back (SPACE = play)
                    break;
                default: break;
            }
            return ::DefSubclassProc (h, msg, wp, lp);
        }

        static LRESULT CALLBACK editProc (HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
        {
            auto* self = reinterpret_cast<NativeFields*> (data);
            const bool multi = (::GetWindowLongPtrW (h, GWL_STYLE) & ES_MULTILINE) != 0;
            if (msg == WM_KEYDOWN)
            {
                const bool ctrl = (::GetKeyState (VK_CONTROL) & 0x8000) != 0;
                if (wp == VK_ESCAPE || (wp == VK_RETURN && ! multi))
                {
                    ::SetFocus (self->parent);
                    // in a dialog, ENTER / ESC also answer it (OK / CANCEL); on a page they only hand the keyboard back
                    if (auto it = self->byHwnd.find (h); it != self->byHwnd.end() && self->boxes[it->second].layer > 0)
                        ::PostMessageW (self->parent, WM_KEYDOWN, wp, lp);
                    return 0;
                }
                if (wp == VK_TAB) return 0;
                // app shortcuts still work while typing (Ctrl+S saves...); editing keys stay with the box
                if (ctrl && wp != 'A' && wp != 'C' && wp != 'V' && wp != 'X' && wp != 'Z' && wp != 'Y' && wp != VK_LEFT && wp != VK_RIGHT
                    && wp != VK_UP && wp != VK_DOWN && wp != VK_HOME && wp != VK_END && wp != VK_BACK && wp != VK_DELETE
                    && wp != VK_CONTROL && wp != VK_SHIFT)
                {
                    ::PostMessageW (self->parent, WM_KEYDOWN, wp, lp);
                    return 0;
                }
            }
            if (msg == WM_CHAR)
            {
                if (wp == 27 || wp == '\t' || (wp == '\r' && ! multi)) return 0;   // no beep
                if (wp == 1) { ::SendMessageW (h, EM_SETSEL, 0, -1); return 0; }      // Ctrl+A selects all
            }
            return ::DefSubclassProc (h, msg, wp, lp);
        }

        HWND parent;
        std::map<Id, Box> boxes;
        std::map<HWND, Id> byHwnd;
        std::map<int, HFONT> fonts;
        HBRUSH background = nullptr, disabledBg = nullptr;
        bool guard = false;
    };
}

std::unique_ptr<TextFields> makeTextFields (void* hwnd) { return std::make_unique<NativeFields> ((HWND) hwnd); }
} // namespace hem::app::platform
