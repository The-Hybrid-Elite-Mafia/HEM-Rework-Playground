// Renders every Elite plugin window to PNG (headless) so the look can be checked without Windows.
// Usage: FaceplatePreview <output folder>

#include "UI/Editors/EliteEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    pg::ui::EliteEditor::install();

    for (auto* info : pg::PluginRegistry::all())
    {
        auto plugin = pg::PluginRegistry::create (info->id);
        plugin->prepareToPlay (48000.0, 512);
        const auto& presets = pg::Presets::factory (info->id);
        if (! presets.empty()) pg::Presets::apply (*plugin, presets.front());

        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditor());
        // feed a drum-like signal so the meters show something
        juce::Random rnd (3);
        std::vector<float> l (512), r (512);
        for (int block = 0; block < 400; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const int n = block * 512 + i;
                const float env = std::exp (-(float) (n % 24000) / 2500.0f);
                const float x = env * (0.8f * std::sin ((float) n * 0.0131f) + 0.3f * (rnd.nextFloat() * 2.0f - 1.0f)) * 0.9f;
                // a little independent "codec" noise in each side so stereo / swirl displays have something to show
                const float s = 0.06f * (rnd.nextFloat() * 2.0f - 1.0f);
                l[(size_t) i] = x + s; r[(size_t) i] = x - s;
            }
            plugin->processStereo (l.data(), r.data(), 512);
            if (block % 8 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto file = outDir.getChildFile (juce::String (info->id) + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        png.writeImageToStream (img, os);
        std::printf ("%s  %dx%d\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        editor.reset();
    }
    return 0;
}
