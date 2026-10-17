#include "SessionFile.h"

namespace pg
{
bool SessionFile::save (const PlaygroundEngine& engine, const juce::File& fileIn, juce::String& error)
{
    const auto file = fileIn.withFileExtension (kExtension);
    auto tree = engine.createSessionTree();
    tree.setProperty ("app", "HEM Rework Playground", nullptr);
    tree.setProperty ("savedAt", juce::Time::getCurrentTime().toISO8601 (true), nullptr);
    for (auto c : tree)
        if (c.hasType ("TRACK") && c["file"].toString().isNotEmpty())
            c.setProperty ("relFile", juce::File (c["file"].toString()).getRelativePathFrom (file.getParentDirectory()).replaceCharacter ('\\', '/'), nullptr);

    auto xml = tree.createXml();
    if (xml == nullptr) { error = "Could not build the session."; return false; }
    if (! file.getParentDirectory().createDirectory()) { error = "Cannot create " + file.getParentDirectory().getFullPathName(); return false; }

    // write to a temp file first so a crash mid-save never destroys the old session
    juce::TemporaryFile temp (file);
    if (! xml->writeTo (temp.getFile())) { error = "Cannot write " + file.getFullPathName(); return false; }
    if (! temp.overwriteTargetFileWithTemporary()) { error = "Cannot replace " + file.getFullPathName(); return false; }
    return true;
}

juce::ValueTree SessionFile::read (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) { error = "Not a readable session file: " + file.getFileName(); return {}; }
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (PlaygroundEngine::kSessionType)) { error = file.getFileName() + " is not a Rework Playground session."; return {}; }

    for (auto c : tree)
        if (c.hasType ("TRACK"))
        {
            const juce::File full (c["file"].toString());
            if (full.existsAsFile()) continue;
            const auto rel = c["relFile"].toString();
            if (rel.isNotEmpty())
            {
                const auto moved = file.getParentDirectory().getChildFile (rel);
                if (moved.existsAsFile()) c.setProperty ("file", moved.getFullPathName(), nullptr);
            }
        }
    return tree;
}
} // namespace pg
