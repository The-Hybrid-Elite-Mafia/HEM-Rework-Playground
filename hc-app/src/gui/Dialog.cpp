#include "Dialog.hpp"

#include <memory>

namespace hem::app
{
namespace
{
    const Rgba kBox = argb (0xff1b1f26);
    std::uint64_t dialogCounter = 0;

    Rect boxFor (const Ui& ui, float w, float h)
    {
        const Rect s = ui.screen();
        w = std::min (w, s.w - 40.0f);
        h = std::min (h, s.h - 40.0f);
        return { s.x + (s.w - w) * 0.5f, s.y + (s.h - h) * 0.5f, w, h };
    }

    void frame (Ui& ui, Rect box, const std::string& title, Rgba accent)
    {
        auto& p = ui.p;
        p.fill (ui.screen(), alpha (colours::navy, 0.62f));
        p.fillRound (expanded (box, 8, 8), 14.0f, alpha (colours::black, 0.45f));
        p.fillRound (box, 8.0f, kBox);
        p.outlineRound (box, 8.0f, 1.5f, alpha (accent, 0.7f), true);
        p.text (title, { box.x + 18, box.y + 10, box.w - 36, 30 }, 18.0f, true, brighter (accent, 0.5f));
    }
}

void showDialog (Ui& ui, DialogSpec spec, DialogDone done)
{
    struct State
    {
        DialogSpec spec;
        DialogDone done;
        std::vector<std::string> values;
        Id base = 0;
        bool focused = false;
    };
    auto s = std::make_shared<State>();
    s->spec = std::move (spec);
    s->done = std::move (done);
    for (auto& f : s->spec.fields) s->values.push_back (f.value);
    s->base = makeId ("dialog", ++dialogCounter);
    if (s->spec.escapeButton < 0) s->spec.escapeButton = (int) s->spec.buttons.size() - 1;

    ui.pushOverlay ([s] (Ui& u) -> bool
    {
        auto& p = u.p;
        const auto& sp = s->spec;
        const float textH = 13.5f;
        const auto lines = p.wrapLines (sp.message, sp.width - 36.0f, textH, false);
        float h = 50.0f + (sp.message.empty() ? 0.0f : p.lineHeight (textH) * (float) lines.size() + 10.0f);
        for (auto& f : sp.fields) h += 20.0f + (f.multiline ? 110.0f : 32.0f) + 8.0f;
        h += 56.0f;
        const Rect box = boxFor (u, sp.width, h);
        frame (u, box, sp.title, sp.accent);
        Rect r = reduced (trimTop (box, 44.0f), 18.0f, 6.0f);
        if (! sp.message.empty())
        {
            const Rect m = takeTop (r, p.lineHeight (textH) * (float) lines.size() + 10.0f);
            p.wrapped (sp.message, m, textH, false, colours::paper, 40);
        }
        for (std::size_t i = 0; i < sp.fields.size(); ++i)
        {
            const auto& f = sp.fields[i];
            p.text (f.label, takeTop (r, 20.0f), 12.0f, true, colours::paperDim);
            FieldStyle fs;
            fs.multiline = f.multiline;
            fs.placeholder = f.placeholder;
            fs.accent = sp.accent;
            fs.fontHeightL = 15.0f;
            const Id fid = subId (s->base, i + 1);
            u.textField (fid, takeTop (r, f.multiline ? 110.0f : 32.0f), s->values[i], fs);
            takeTop (r, 8.0f);
            if (! s->focused && i == 0 && u.fields() != nullptr) { u.fields()->focus (fid); s->focused = true; }
        }
        // buttons, right-aligned, the ENTER one lit
        Rect row = reduced ({ box.x, box.bottom() - 54.0f, box.w, 54.0f }, 18.0f, 10.0f);
        int pressed = -1;
        for (int i = (int) sp.buttons.size() - 1; i >= 0; --i)
        {
            const float bw = std::max (96.0f, p.textWidth (sp.buttons[(std::size_t) i], 13.0f) + 32.0f);
            const Rect b = takeRight (row, bw);
            takeRight (row, 8.0f);
            if (u.button (subId (s->base, 100 + (std::uint64_t) i), b, sp.buttons[(std::size_t) i], ButtonLook::dark, sp.accent, true,
                          i == sp.enterButton, 13.0f))
                pressed = i;
        }
        if (pressed < 0 && u.key (0x0D)) pressed = sp.enterButton;   // ENTER
        if (pressed < 0 && u.key (0x1B)) pressed = sp.escapeButton;  // ESC
        if (pressed < 0) return true;
        if (u.fields() != nullptr) u.fields()->unfocus();
        auto done = s->done;
        auto values = s->values;
        if (done) done (pressed, values);
        return false;
    });
}

void showPanel (Ui& ui, std::string title, float width, float height, Rgba accent, std::function<bool (Ui&, Rect body)> body)
{
    ui.pushOverlay ([title = std::move (title), width, height, accent, body = std::move (body)] (Ui& u) -> bool
    {
        const Rect box = boxFor (u, width, height);
        frame (u, box, title, accent);
        return body (u, reduced (trimTop (box, 44.0f), 18.0f, 8.0f));
    });
}
} // namespace hem::app
