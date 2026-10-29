#pragma once
// =============================================================================
//  Windows services the Playground uses: file dialogs, Explorer, the clipboard,
//  starting another program, and native text boxes over the GL window.
//  UI thread only. Paths are std::filesystem::path (UTF-16 inside), text is UTF-8.
// =============================================================================
#include "../gui/Ui.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hem::app::platform
{
namespace fs = std::filesystem;

struct FileType
{
    std::string name;      // "Audio files"
    std::string patterns;  // "*.wav;*.mp3"
};

/** Open dialog (modal). Empty when cancelled. */
std::vector<fs::path> openFiles (void* owner, const std::string& title, const fs::path& startFolder, const std::vector<FileType>& types,
                                 bool multiple);
/** Save dialog (modal), asks before replacing a file. `defaultExt` without the dot ("wav"). */
std::optional<fs::path> saveFile (void* owner, const std::string& title, const fs::path& startFile, const std::vector<FileType>& types,
                                  const std::string& defaultExt);
std::optional<fs::path> pickFolder (void* owner, const std::string& title, const fs::path& start);

/** Shows the file selected in Explorer (or opens the folder). */
void reveal (const fs::path&);
void openUrl (const std::string& url);
bool copyText (void* owner, const std::string& utf8);
/** Starts `bat` in its own console window titled `title`, in `folder` ("cmd /c start"). */
bool startInOwnWindow (const fs::path& folder, const std::string& bat, const std::string& title);

/** Poppins for the native text boxes (loaded privately from the app's fonts folder). */
void loadPrivateFonts (const fs::path& fontsFolder);

/** Native text boxes for one GL window. */
std::unique_ptr<TextFields> makeTextFields (void* hwnd);

/** Keeps `window` above `owner` (plug-in windows over the main window) and brings it forward. */
void setOwner (void* window, void* owner);
void bringToFront (void* window);
void setWindowTitle (void* window, const std::string& title);

/** Windows' own message box (used only when the GL window cannot start). */
void fatalMessage (const std::string& title, const std::string& text);

std::wstring wide (const std::string& utf8);
std::string utf8 (const std::wstring& w);
} // namespace hem::app::platform
