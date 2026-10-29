#pragma once
// LIBRARY inside the SONG FORGE tab: Steven's private shelf of his own beats and songs - Band Forge
// parts he took through Suno and brought back, and anything else he makes. Files are copied into
// Documents/.../Rework Playground/Library; tempo, key and loudness are measured on a background
// thread; each one can be named, rated, noted, linked to its Band Forge take, previewed and put in
// the session. Nothing is uploaded anywhere.
#include "../App.hpp"
#include "Library.hpp"

namespace hem::app
{
class LibraryPanel
{
public:
    explicit LibraryPanel (App&);
    ~LibraryPanel();

    void draw (Ui&, Rect area);
    void drawKinds (Ui&, Rect panel);

    /** Copies files in and measures them. Returns how many went in. */
    int addFiles (const std::vector<fs::path>&, const std::string& genre = {}, const fs::path& madeFrom = {}, const std::string& kind = {},
                  const std::string& title = {});
    void refresh();

private:
    void measure (const std::string& id);
    void togglePreview();
    void addToSession();
    void removeSelected();
    pg::library::Entry* current();
    std::string currentId() const;
    void select (const std::string& id);

    App& app;
    pg::library::Library lib;
    std::string kindFilter;
    int sortColumn = 8;
    bool sortForwards = false;
    std::vector<std::string> rows;
    int selectedRow = -1;
    float scroll = 0.0f;
    bool dirty = false;
    int pendingMeasures = 0;
    std::string search;
    std::string previewId, decodingPreview;
    float seekNorm = -1.0f;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::shared_ptr<std::atomic<bool>> stopping = std::make_shared<std::atomic<bool>> (false);
    std::unique_ptr<Worker> worker;
    double lastSave = 0.0;
};
} // namespace hem::app
