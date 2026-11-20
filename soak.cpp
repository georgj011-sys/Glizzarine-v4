// Soak test: like a long session in Sonar. An audio thread runs processBlock non-stop with random MIDI and
// parameter moves, while the message thread keeps the editor open, switches tabs, loads presets,
// saves/restores the state (as the host does when saving the project) and starts MIDI learn.
// Run under AddressSanitizer:  ./soak [seconds=120]
#include "cfg.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>
#include <thread>
using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const double seconds = argc > 1 ? atof (argv[1]) : 120.0;
    auto* p = new HarmonizerProcessor();
    p->prepareToPlay (48000, 256);
    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 };
    std::atomic<int> bad { 0 };

    std::thread audio ([&]
    {
        std::mt19937 g (5);
        auto ri = [&] (int a, int b) { return std::uniform_int_distribution<int> (a, b) (g); };
        AudioBuffer<float> b (2, 4096);
        double ph = 0;
        auto& params = p->getParameters();
        while (! stop)
        {
            const int bs = ri (0, 9) == 0 ? ri (1, 4096) : 256;
            b.setSize (2, bs, false, false, true);
            MidiBuffer m;
            if (ri (0, 20) == 0) m.addEvent (MidiMessage::noteOn (1, ri (36, 84), (uint8) ri (20, 127)), 0);
            if (ri (0, 20) == 0) m.addEvent (MidiMessage::noteOff (1, ri (36, 84)), ri (0, bs - 1));
            if (ri (0, 60) == 0) m.addEvent (MidiMessage::controllerEvent (1, ri (1, 90), ri (0, 127)), 0);
            if (ri (0, 80) == 0) m.addEvent (MidiMessage::controllerEvent (1, 64, ri (0, 1) * 127), 0);
            if (ri (0, 40) == 0) m.addEvent (MidiMessage::channelPressureChange (1, ri (0, 127)), 0);
            if (ri (0, 150) == 0) m.addEvent (MidiMessage::noteOn (10, 36 + ri (0, 11), (uint8) 100), 0);
            if (ri (0, 150) == 0) m.addEvent (MidiMessage::noteOff (10, 36 + ri (0, 11)), 0);
            if (ri (0, 30) == 0) params[ri (0, params.size() - 1)]->setValueNotifyingHost ((float) ri (0, 100) / 100.0f);
            for (int i = 0; i < bs; ++i)
            {
                ph += 196.0 / 48000.0;
                const float s = 0.2f * (float) (std::sin (6.2832 * ph) + 0.4 * std::sin (12.566 * ph));
                b.setSample (0, i, s); b.setSample (1, i, s);
            }
            p->processBlock (b, m);
            for (int i = 0; i < bs; ++i) if (! std::isfinite (b.getSample (0, i))) ++bad;
            ++blocks;
        }
    });

    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    ed->setVisible (true);
    auto* he = dynamic_cast<HarmonizerEditor*> (ed.get());
    std::mt19937 g (9);
    const double start = Time::getMillisecondCounterHiRes();
    int step = 0;
    while (Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (40);
        ++step;
        if (step % 5 == 0) he->showTab ((int) (g() % 8));
        if (step % 37 == 0) p->setCurrentProgram ((int) (g() % (unsigned) p->getNumPrograms()));
        if (step % 53 == 0) { MemoryBlock mb; p->getStateInformation (mb); p->setStateInformation (mb.getData(), (int) mb.getSize()); }
        if (step % 71 == 0) p->startCcLearn ("harmony");
        if (step % 89 == 0) p->startPadLearn ((int) (g() % 12));
        if (step % 97 == 0) { ed = nullptr; ed.reset (p->createEditor()); ed->setVisible (true); he = dynamic_cast<HarmonizerEditor*> (ed.get()); }
        if (step % 113 == 0) p->tapTempo();
        if (step % 131 == 0) p->triggerPadFromUi ((int) (g() % 12), (g() & 1) != 0);
    }
    stop = true;
    audio.join();
    ed = nullptr;
    printf ("soak %.0f s: %ld audio blocks, editor steps %d, bad samples %d, heals %d\n", seconds, blocks.load(), step, bad.load(), p->healCount.load());
    delete p;
    return bad.load() ? 1 : 0;
}
