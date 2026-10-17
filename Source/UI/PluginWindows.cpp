#include "PluginWindows.h"
#include "UI/PlaygroundLookAndFeel.h"

namespace pg::ui
{
class PluginWindows::Window final : public juce::DocumentWindow
{
public:
    Window (PluginWindows& o, ElitePlugin& p, const juce::String& where)
        : DocumentWindow (juce::String (p.getInfo().name) + "  -  " + where, colours::navy, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          owner (o), plugin (&p)
    {
        setUsingNativeTitleBar (true);
        if (auto* ed = p.createEditor())
            setContentOwned (ed, true);
        setResizable (false, false);   // the editor has its own resize corner
        setAlwaysOnTop (false);
    }

    void closeButtonPressed() override
    {
        // deleting ourselves from inside our own callback is not allowed: post it
        juce::MessageManager::callAsync ([&o = owner, p = plugin, alive = juce::Component::SafePointer<Window> (this)]
        {
            if (alive != nullptr) o.close (p);
        });
    }

private:
    PluginWindows& owner;
    ElitePlugin* plugin;
};

PluginWindows::PluginWindows() = default;
PluginWindows::~PluginWindows() { closeAll(); }

void PluginWindows::open (ElitePlugin& plugin, const juce::String& where)
{
    if (auto it = windows.find (&plugin); it != windows.end())
    {
        it->second->setVisible (true);
        it->second->setMinimised (false);
        it->second->toFront (true);
        return;
    }
    auto w = std::make_unique<Window> (*this, plugin, where);
    // Fit the screen: a plugin window must never be taller / wider than the work area (laptop
    // screens), or its title bar and preset row end up off-screen. The editors keep their aspect.
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = d->userArea;
        constexpr int kTitleBar = 40;   // native title bar + frame, above the content
        if (auto* content = w->getContentComponent())
        {
            const float sx = (float) area.getWidth() * 0.92f / (float) content->getWidth();
            const float sy = (float) (area.getHeight() - kTitleBar - 24) / (float) content->getHeight();
            const float s = juce::jmin (1.0f, sx, sy);
            if (s < 1.0f)
                content->setSize (juce::roundToInt ((float) content->getWidth() * s), juce::roundToInt ((float) content->getHeight() * s));
        }
        // cascade new windows so several open plugins do not stack exactly on top of each other
        const int step = 32 * (cascade++ % 6);
        const int x = juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight() - w->getWidth()), area.getX() + 80 + step);
        const int y = juce::jlimit (area.getY() + kTitleBar, juce::jmax (area.getY() + kTitleBar, area.getBottom() - w->getHeight() - 8),
                                    area.getY() + kTitleBar + step);
        w->setTopLeftPosition (x, y);
    }
    w->setVisible (true);
    w->toFront (true);
    windows[&plugin] = std::move (w);
}

void PluginWindows::close (ElitePlugin* plugin)
{
    if (auto it = windows.find (plugin); it != windows.end())
    {
        auto w = std::move (it->second);
        windows.erase (it);
        w.reset();   // deletes the editor while the plugin still exists
    }
}

void PluginWindows::closeAll()
{
    while (! windows.empty())
        close (windows.begin()->first);
    cascade = 0;
}
} // namespace pg::ui
