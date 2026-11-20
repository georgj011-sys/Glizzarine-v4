#include "cfg.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <random>
using namespace juce;
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const int seconds = argc > 1 ? atoi (argv[1]) : 120;
    std::mt19937 g (argc > 2 ? atoi (argv[2]) : 1);
    auto rnd = [&] (float a, float b) { return std::uniform_real_distribution<float> (a, b) (g); };
    auto ri = [&] (int a, int b) { return std::uniform_int_distribution<int> (a, b) (g); };
    HarmonizerProcessor p;
    auto params = p.getParameters();
    double sr = 48000; int maxBs = 512;
    p.prepareToPlay (sr, maxBs);
    double t = 0, ph = 0; long blocks = 0; int bad = 0; float hz = 220.0f;
    while (t < seconds)
    {
        if (ri (0, 4000) == 0)
        {
            const double srs[] = { 44100, 48000, 88200, 96000, 22050, 176400, 192000 };
            sr = srs[ri (0, 6)]; maxBs = ri (16, 4096);
            p.setNonRealtime (ri (0, 1) == 1);   // offline bounce
            p.releaseResources(); p.prepareToPlay (sr, maxBs);
        }
        if (ri (0, 6000) == 0)   // everything to extremes at once
            for (auto* prm : params) prm->setValueNotifyingHost (ri (0, 2) == 0 ? rnd (0, 1) : (float) ri (0, 1));
        if (ri (0, 300) == 0) hz = ri (0, 3) == 0 ? rnd (40, 2500) : rnd (80, 700);
        int bs = ri (0, 20) == 0 ? ri (0, 3) : ri (1, ri (0, 10) == 0 ? 9000 : maxBs);
        AudioBuffer<float> b (2, std::max (1, bs));
        if (bs == 0) b.setSize (2, 0);
        MidiBuffer m;
        const int nev = ri (0, 5) == 0 ? ri (0, 40) : ri (0, 2);
        for (int e = 0; e < nev; ++e)
        {
            const int pos = bs > 0 ? ri (0, bs - 1) : 0;
            const int ch = ri (1, 16);
            switch (ri (0, 9))
            {
                case 0: case 1: m.addEvent (MidiMessage::noteOn (ch, ri (0, 127), (uint8) ri (1, 127)), pos); break;
                case 2: case 3: m.addEvent (MidiMessage::noteOff (ch, ri (0, 127)), pos); break;
                case 4: m.addEvent (MidiMessage::controllerEvent (ch, ri (0, 127), ri (0, 127)), pos); break;
                case 5: m.addEvent (MidiMessage::controllerEvent (ch, 64, ri (0, 1) * 127), pos); break;
                case 6: m.addEvent (MidiMessage::channelPressureChange (ch, ri (0, 127)), pos); break;
                case 7: m.addEvent (MidiMessage::aftertouchChange (ch, ri (0, 127), ri (0, 127)), pos); break;
                case 8: m.addEvent (MidiMessage::pitchWheel (ch, ri (0, 16383)), pos); break;
                default: m.addEvent (MidiMessage::noteOn (10, 36 + ri (0, 11), (uint8) 100), pos); break;
            }
        }
        if (ri (0, 3) == 0) { auto* prm = params[ri (0, params.size() - 1)]; prm->setValueNotifyingHost (ri (0, 3) == 0 ? (float) ri (0, 1) : rnd (0, 1)); }
        if (ri (0, 500) == 0) p.setCurrentProgram (ri (0, p.getNumPrograms() - 1));
        if (ri (0, 3000) == 0) p.startCcLearn (params[ri (0, params.size() - 1)]->getName (64));
        if (ri (0, 3000) == 0) p.startPadLearn (ri (-1, 11));
        if (ri (0, 2000) == 0) p.tapTempo();
        if (ri (0, 1000) == 0) p.triggerPadFromUi (ri (0, 11), ri (0, 1));
        if (ri (0, 5000) == 0) { MemoryBlock mb; p.getStateInformation (mb); p.setStateInformation (mb.getData(), (int) mb.getSize()); }
        const int mode = ri (0, 200);
        for (int i = 0; i < bs; ++i)
        {
            ph += hz / sr;
            float s = 0.3f * (float) std::sin (6.2832 * ph);
            if (mode == 0) s = 1.0e-30f;
            else if (mode == 1) s = rnd (-4, 4);
            else if (mode == 2 && i == 0) s = std::numeric_limits<float>::quiet_NaN();
            else if (mode == 3 && i == 0) s = std::numeric_limits<float>::infinity();
            else if (mode == 4) s = rnd (-0.3f, 0.3f);          // breath / consonant noise
            else if (mode == 5) s = (i & 1) ? 1.0f : -1.0f;      // Nyquist square
            else if (mode < 30) s = 0.0f;
            b.setSample (0, i, s); b.setSample (1, i, s * rnd (0.5f, 1.0f));
        }
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float y = b.getSample (c, i); if (! std::isfinite (y) || std::abs (y) > 1.0f) ++bad; }
        t += bs / sr; ++blocks;
    }
    printf ("fuzz %d s, %ld blocks, bad samples %d, heals %d (engine %d, colour %d, pads %d, delay %d, reverb %d, out %d)\n", seconds, blocks, bad, p.healCount.load(),
            p.healStage[0].load(), p.healStage[1].load(), p.healStage[2].load(), p.healStage[3].load(), p.healStage[4].load(), p.healStage[5].load());
    return (bad > 0 || p.healCount.load() > 0) ? 1 : 0;
}
