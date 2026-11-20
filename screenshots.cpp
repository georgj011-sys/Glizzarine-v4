#include "cfg.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto* p = new HarmonizerProcessor();
    p->prepareToPlay (48000, 512);
    juce::AudioBuffer<float> b (2, 512);
    double ph = 0;
    for (int blk = 0; blk < 300; ++blk)
    {
        juce::MidiBuffer m;
        if (blk == 2) for (int n : { 57, 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (blk == 5) m.addEvent (juce::MidiMessage::channelPressureChange (1, 70), 0);
        for (int i = 0; i < 512; ++i) { ph += 220.0 / 48000.0; const float s = 0.2f * (float) (std::sin (6.2832 * ph) + 0.4 * std::sin (12.566 * ph) + 0.2 * std::sin (18.85 * ph)); b.setSample (0, i, s); b.setSample (1, i, s); }
        p->processBlock (b, m);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setVisible (true);
    auto* he = dynamic_cast<HarmonizerEditor*> (ed.get());
    // click test: a real mouse click on every tab (through hit testing, like in the DAW) must switch the page
    int clickFails = 0;
    {
        juce::StringArray tabNames { "MAIN", "VOICE", "VOCODER", "ARP", "EFFECTS", "PADS", "AT & VEL" };
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        int x = 30;
        for (int i = 0; i < tabNames.size(); ++i)
        {
            auto f = juce::FontOptions (13.0f);
            const int w = (int) juce::GlyphArrangement::getStringWidth (juce::Font (f).withExtraKerningFactor (0.1f), tabNames[i]) + 4;
            const juce::Point<float> pt ((float) (x + w / 2), 88.0f);
            auto* target = ed->getComponentAt (pt.toInt());
            if (target != nullptr)
            {
                juce::MouseEvent e (src, target->getLocalPoint (ed.get(), pt), juce::ModifierKeys::leftButtonModifier, 1.0f, 0, 0, 0, 0,
                                    target, target, juce::Time::getCurrentTime(), target->getLocalPoint (ed.get(), pt), juce::Time::getCurrentTime(), 1, false);
                target->mouseDown (e);
            }
            int visible = -1, k = 0;
            for (auto* c : ed->getChildren()) if (dynamic_cast<Page*> (c) != nullptr) { if (c->isVisible()) visible = k; ++k; }
            printf ("click tab %-8s -> page %d (hit: %s)\n", tabNames[i].toRawUTF8(), visible, target == ed.get() ? "editor" : "other");
            if (visible != i) ++clickFails;
            x += w + 26;
        }
        printf ("tab clicks: %s\n", clickFails == 0 ? "OK" : "FAILED");
    }
    const char* names[] = { "main", "voice", "vocoder", "arp", "effects", "pads", "atvel" };
    const char* bump[] = { "harmony", "sens", "voc_sib", "arp_spread", "chorus_rate", "", "" };
    for (int t = 0; t < 7; ++t)
    {
        he->showTab (t);
        if (t == 5) p->triggerPadFromUi (0, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        if (juce::String (bump[t]).isNotEmpty()) { auto* prm = p->apvts.getParameter (bump[t]); prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, prm->getValue() + 0.25f)); }
        
        juce::MessageManager::getInstance()->runDispatchLoopUntil (220);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String ("tab_") + names[t] + ".png"));
        f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
    }
    // preset save dialog
    he->showTab (0);
    if (auto* b = dynamic_cast<juce::Button*> (ed->findChildWithID ("save"))) b->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile ("dialog_save.png"));
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
    }
    ed = nullptr; delete p;
    return clickFails ? 1 : 0;
}
