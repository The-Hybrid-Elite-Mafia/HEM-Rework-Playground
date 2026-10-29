// HEM Rework Playground — application entry (Windows, The Hybrid Core, no JUCE).
#include "App.hpp"
#include "platform/Platform.hpp"

#include "hybrid/core/RealtimeThread.hpp"
#include "hybrid/io/DeviceProbe.hpp"
#include "hybrid/ui/GlWindow.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cwchar>

namespace
{
    std::filesystem::path exeFolder()
    {
        wchar_t buf[MAX_PATH * 4] = {};
        const DWORD n = ::GetModuleFileNameW (nullptr, buf, (DWORD) std::size (buf));
        return std::filesystem::path (std::wstring (buf, n)).parent_path();
    }

    hem::app::StartOptions parseOptions()
    {
        hem::app::StartOptions o;
        int argc = 0;
        wchar_t** argv = ::CommandLineToArgvW (::GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i)
        {
            const std::wstring k = argv[i];
            if (k == L"--exit-after" && i + 1 < argc) o.exitAfter = std::wcstod (argv[++i], nullptr);
            else if (k == L"--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
            else if (k == L"--tab" && i + 1 < argc) o.startTab = (int) std::wcstol (argv[++i], nullptr, 10);
            else if (k == L"--offline") o.offline = true;
            else if (k == L"--plugin" && i + 1 < argc) o.pluginWindow = hem::app::platform::utf8 (argv[++i]);
            else if (k == L"--size" && i + 2 < argc) { o.width = (int) std::wcstol (argv[++i], nullptr, 10); o.height = (int) std::wcstol (argv[++i], nullptr, 10); }
            else if (k == L"--stems" && i + 1 < argc)
            {
                std::error_code ec;
                for (const auto& f : std::filesystem::directory_iterator (argv[++i], ec))
                    if (f.is_regular_file (ec)) o.stems.push_back (f.path());
                std::sort (o.stems.begin(), o.stems.end());
            }
            else if (! k.empty() && k[0] != L'-') o.session = k;
        }
        if (argv) ::LocalFree (argv);
        return o;
    }

    HWND findRunningPlayground()
    {
        struct Find { HWND hit = nullptr; } f;
        ::EnumWindows ([] (HWND h, LPARAM lp) -> BOOL
        {
            wchar_t cls[64] = {}, title[128] = {};
            ::GetClassNameW (h, cls, 64);
            ::GetWindowTextW (h, title, 128);
            if (std::wstring (cls) == hc::ui::GlWindow::className() && ::IsWindowVisible (h) && std::wcsncmp (title, L"HEM Rework Playground", 21) == 0)
            {
                reinterpret_cast<Find*> (lp)->hit = h;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM> (&f));
        return f.hit;
    }
}

int WINAPI wWinMain (HINSTANCE, HINSTANCE, PWSTR, int)
{
    ::SetProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto opts = parseOptions();

    // One Playground at a time (two would fight over the sound card). A second start (a double-clicked
    // .hemplay) hands its session to the running one and brings it forward. Test runs are exempt.
    HANDLE instance = ::CreateMutexW (nullptr, FALSE, L"Local\\HEM.ReworkPlayground");
    if (instance && ::GetLastError() == ERROR_ALREADY_EXISTS && opts.exitAfter <= 0.0)
    {
        if (HWND other = findRunningPlayground())
        {
            if (! opts.session.empty())
            {
                const std::wstring p = std::filesystem::absolute (opts.session).wstring();
                COPYDATASTRUCT cd { hc::ui::GlWindow::kOpenFileTag, (DWORD) ((p.size() + 1) * sizeof (wchar_t)), const_cast<wchar_t*> (p.c_str()) };
                DWORD_PTR result = 0;
                ::SendMessageTimeoutW (other, WM_COPYDATA, 0, reinterpret_cast<LPARAM> (&cd), SMTO_ABORTIFHUNG, 5000, &result);
            }
            if (::IsIconic (other)) ::ShowWindow (other, SW_RESTORE);
            ::SetForegroundWindow (other);
        }
        return 0;
    }

    hc::rt::configureProcessForAudio();
    hc::io::ComApartment com (hc::io::Apartment::SingleThreaded);   // ASIO drivers and the file dialogs want an STA with a message pump

    const auto cpu = hc::io::probeCpu();
    if (! cpu.avx2 || ! cpu.fma)
    {
        hem::app::platform::fatalMessage ("HEM Rework Playground", "This computer's processor has no AVX2 / FMA. The Playground is built for the i7-14700HX.");
        return 2;
    }

    int rc = 0;
    {
        hem::app::App app;
        app.assetDir = exeFolder() / "assets";
        std::string error;
        if (! app.start (opts, error))
        {
            hem::app::platform::fatalMessage ("HEM Rework Playground",
                                              "The Playground could not start: " + error + "\n\nUpdate your graphics driver (OpenGL 3.3 is needed), or reinstall the Playground.");
            return 3;
        }
        rc = app.run();
    }
    if (instance) ::CloseHandle (instance);
    return rc;
}
