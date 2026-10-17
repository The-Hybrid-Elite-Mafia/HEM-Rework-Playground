#pragma once

#include "UI/AppContext.h"
#include "Library/Library.h"

namespace pg::ui
{
/**
    LIBRARY inside the SONG FORGE tab: Steven's private shelf of his own beats and songs - Band Forge
    parts he took through Suno and brought back, and anything else he makes.
    Drop files on it (or ADD FILES): they are copied into Documents/.../Rework Playground/Library,
    tempo, key and loudness are measured on a background thread, and each one can be named,
    rated, noted, linked to the Band Forge take it came from, previewed and put in the session.
    Message thread only; decoding / measuring run on a worker thread and report back via callAsync.
*/
class LibraryPanel final : public juce::Component, public juce::FileDragAndDropTarget,
                           private juce::TableListBoxModel, private juce::Timer
{
public:
    explicit LibraryPanel (AppContext&);
    ~LibraryPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Left panel: kinds with counts (the kind filter). */
    juce::Component& getKindsPanel() { return kindsPanel; }

    /** Adds files (copied in, then measured). Returns how many went in. */
    int addFiles (const juce::Array<juce::File>&, const juce::String& genre = {}, const juce::File& madeFrom = {},
                  const juce::String& kind = {}, const juce::String& title = {});
    void select (const juce::String& id);
    void refresh();
    library::Library& getLibrary() noexcept { return lib; }
    /** True while files are still being measured. */
    bool isBusy() const noexcept { return pendingMeasures > 0; }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    class KindsPanel final : public juce::Component
    {
    public:
        explicit KindsPanel (LibraryPanel& o);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        juce::Rectangle<int> rowArea (int i) const;
        LibraryPanel& owner;
        juce::TextButton folderButton { "OPEN LIBRARY FOLDER" };
    };

    // table
    int getNumRows() override { return rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;

    void timerCallback() override;
    void showDetails();
    void detailsEdited();
    void measure (const juce::String& id);
    void togglePreview();
    void addToSession();
    void removeSelected();
    library::Entry* current();
    juce::String currentId() const;
    void saveSoon() { dirty = true; }

    AppContext& ctx;
    library::Library lib;
    juce::String kindFilter;          // "" = all
    int sortColumn = 8;               // added
    bool sortForwards = false;        // newest first
    juce::StringArray rows;
    bool dirty = false, dragOver = false, showingDetails = false;
    int pendingMeasures = 0;

    juce::TextEditor search;
    juce::TextButton addButton { "ADD FILES..." };
    juce::TableListBox table { "Library", this };
    juce::Label status;

    // details of the selected entry
    juce::TextEditor title, notes;
    juce::ComboBox kind, genre, stars, madeFrom;
    juce::Array<juce::File> takeFolders;
    juce::TextButton playButton { "PLAY" }, sessionButton { "ADD TO SESSION" }, showButton { "SHOW FILE" },
                     doubleButton { "BPM x2" }, halveButton { "BPM / 2" }, takeButton { "OPEN TAKE" }, removeButton { "REMOVE" };
    juce::Slider position;
    juce::Rectangle<int> detailsArea, factsArea, headerArea;
    juce::String previewId, decodingPreview;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::shared_ptr<std::atomic<bool>> stopping = std::make_shared<std::atomic<bool>> (false);   // set when the panel goes
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withThreadName ("Library").withNumberOfThreads (1) };
    KindsPanel kindsPanel { *this };
};
} // namespace pg::ui
