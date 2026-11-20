// UI click test: clicks, drags, double clicks and mouse wheel over every point of every tab, through real hit testing
// (like a mouse in the DAW), while an audio thread keeps playing. Checks that every tab can be reached, that every
// control on every page actually takes clicks, and (run under ASan/UBSan) that nothing crashes.
//   ./uiclick
#include "cfg.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>
#include <random>
#include <set>
using namespace juce;

static int fails = 0;
static void check (bool ok, const String& what) { printf ("%s  %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8()); if (! ok) ++fails; }

static MouseEvent makeEvent (Component* target, Component* root, Point<float> pt, ModifierKeys mods, int clicks = 1)
{
    auto src = Desktop::getInstance().getMainMouseSource();
    const auto local = target->getLocalPoint (root, pt);
    return MouseEvent (src, local, mods, 1.0f, 0, 0, 0, 0, target, target, Time::getCurrentTime(), local, Time::getCurrentTime(), clicks, false);
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    auto* p = new HarmonizerProcessor();
    p->prepareToPlay (48000, 256);
    std::atomic<bool> stop { false };
    std::atomic<int> bad { 0 };
    std::thread audio ([&]
    {
        AudioBuffer<float> b (2, 256);
        double ph = 0; int blk = 0;
        while (! stop)
        {
            MidiBuffer m;
            if (blk % 200 == 0) for (int n : { 57, 60, 64 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
            if (blk % 200 == 150) for (int n : { 57, 60, 64 }) m.addEvent (MidiMessage::noteOff (1, n), 0);
            for (int i = 0; i < 256; ++i) { ph += 220.0 / 48000.0; const float s = 0.2f * (float) std::sin (6.2832 * ph); b.setSample (0, i, s); b.setSample (1, i, s); }
            p->processBlock (b, m);
            for (int i = 0; i < 256; ++i) if (! std::isfinite (b.getSample (0, i)) || ! std::isfinite (b.getSample (1, i))) ++bad;
            ++blk;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    });

    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    ed->setVisible (true);
    auto* root = ed.get();
    const StringArray tabNames { "MAIN", "VOICE", "VOCODER", "ARP", "EFFECTS", "PADS", "AT & VEL" };

    auto visiblePage = [&]
    {
        int k = 0;
        for (auto* c : root->getChildren()) if (dynamic_cast<Page*> (c) != nullptr) { if (c->isVisible()) return k; ++k; }
        return -1;
    };
    auto clickTab = [&] (int i)
    {
        int x = 30;
        for (int t = 0; t < tabNames.size(); ++t)
        {
            const int w = (int) GlyphArrangement::getStringWidth (Font (FontOptions (13.0f)).withExtraKerningFactor (0.1f), tabNames[t]) + 4;
            if (t == i)
            {
                const Point<float> pt ((float) (x + w / 2), 88.0f);
                if (auto* c = root->getComponentAt (pt.toInt())) { auto e = makeEvent (c, root, pt, ModifierKeys::leftButtonModifier); c->mouseDown (e); c->mouseUp (e); }
                return;
            }
            x += w + 26;
        }
    };

    // 1) every tab reachable by a click
    for (int i = 0; i < tabNames.size(); ++i)
    {
        clickTab (i);
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        check (visiblePage() == i, "tab " + tabNames[i] + " opens by click");
    }

    // 2) on every tab: every control below the tab bar is hit by a click (nothing covers it)
    std::mt19937 g (3);
    for (int i = 0; i < tabNames.size(); ++i)
    {
        clickTab (i);
        MessageManager::getInstance()->runDispatchLoopUntil (30);
        Component* page = nullptr;
        { int k = 0; for (auto* c : root->getChildren()) if (dynamic_cast<Page*> (c) != nullptr) { if (k == i) page = c; ++k; } }
        int controls = 0, covered = 0;
        for (auto* c : page->getChildren())
        {
            if (! c->isVisible() || c->getWidth() < 4 || c->getHeight() < 4) continue;
            ++controls;
            const auto centre = root->getLocalPoint (c, c->getLocalBounds().getCentre());
            auto* hit = root->getComponentAt (centre);
            bool ok = false;
            for (auto* h = hit; h != nullptr; h = h->getParentComponent()) if (h == c) ok = true;
            if (! ok) { ++covered; printf ("     covered on %s: control at %d,%d (%dx%d)\n", tabNames[i].toRawUTF8(), centre.x, centre.y, c->getWidth(), c->getHeight()); }
        }
        check (covered == 0, tabNames[i] + ": all " + String (controls) + " controls take clicks");

        // 3) click / drag / double click / wheel over a grid of the whole page (not the header with preset menu)
        for (int y = 112; y < root->getHeight(); y += 9)
            for (int x = 0; x < root->getWidth(); x += 11)
            {
                const Point<float> pt ((float) x, (float) y);
                auto* c = root->getComponentAt (pt.toInt());
                if (c == nullptr || c == root) continue;
                Component::SafePointer<Component> safe (c);
                auto e = makeEvent (c, root, pt, ModifierKeys::leftButtonModifier);
                c->mouseDown (e);
                if (safe == nullptr) continue;
                const Point<float> pt2 (pt.x + (float) (int) (g() % 61) - 30.0f, pt.y + (float) (int) (g() % 61) - 30.0f);
                c->mouseDrag (makeEvent (c, root, pt2, ModifierKeys::leftButtonModifier));
                if (safe == nullptr) continue;
                c->mouseUp (makeEvent (c, root, pt2, ModifierKeys()));
                if (safe == nullptr) continue;
                if ((x + y) % 7 == 0) { c->mouseDoubleClick (makeEvent (c, root, pt, ModifierKeys::leftButtonModifier, 2)); if (safe == nullptr) continue; }
                MouseWheelDetails w; w.deltaX = 0; w.deltaY = (g() & 1) ? 0.3f : -0.3f; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
                c->mouseWheelMove (makeEvent (c, root, pt, ModifierKeys()), w);
            }
        MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    // 4) header: preset arrows, and opening/cancelling the save dialog
    for (auto* c : root->getChildren())
        if (auto* b = dynamic_cast<TextButton*> (c))
            if (b->getButtonText() == "<" || b->getButtonText() == ">") for (int k = 0; k < 8; ++k) { b->triggerClick(); MessageManager::getInstance()->runDispatchLoopUntil (20); }
    if (auto* b = dynamic_cast<Button*> (root->findChildWithID ("save"))) b->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    bool dialogUp = false;
    for (auto* c : root->getChildren()) if (dynamic_cast<PresetDialog*> (c) != nullptr && c->isVisible()) dialogUp = true;
    check (dialogUp, "SAVE opens the name dialog");
    for (auto* c : root->getChildren())
        if (auto* d = dynamic_cast<PresetDialog*> (c))
            for (auto* k : d->getChildren()) if (auto* b = dynamic_cast<TextButton*> (k)) if (b->getButtonText() == "Cancel") b->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    bool dialogGone = true;
    for (auto* c : root->getChildren()) if (dynamic_cast<PresetDialog*> (c) != nullptr && c->isVisible()) dialogGone = false;
    check (dialogGone, "Cancel closes the dialog");
    clickTab (1); MessageManager::getInstance()->runDispatchLoopUntil (30);
    check (visiblePage() == 1, "tabs still work after the dialog");

    stop = true; audio.join();
    check (bad.load() == 0, "audio stayed clean while clicking (" + String (bad.load()) + " bad samples)");
    check (p->healCount.load() == 0, "no repairs while clicking (" + String (p->healCount.load()) + ")");
    ed = nullptr; delete p;
    printf ("\n%d problem(s)\n", fails);
    return fails ? 1 : 0;
}
