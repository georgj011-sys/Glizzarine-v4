// Offline-export simulation (Sonar "Fast Bounce"): long render with huge / changing block sizes,
// non-realtime mode, sample-rate changes between bounces. Fails if any 5 s window goes silent,
// if anything is NaN/clipped, or if the self-healing had to step in.
//   ./bounce [seconds=60]
#include "cfg.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <random>
using namespace juce;

struct Case { const char* name; double sr; int prepBs; std::vector<int> blocks; std::map<String, float> set; };

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const double seconds = argc > 1 ? atof (argv[1]) : 60.0;
    const std::map<String, float> fx = { { "delay_mix", 0.4f }, { "delay_sync", 0 }, { "delay_time", 500.0f }, { "feedback", 0.6f },
                                         { "reverb", 0.5f }, { "shimmer", 0.3f }, { "chorus_mix", 0.3f }, { "phaser_mix", 0.3f } };
    std::vector<Case> cases = {
        { "44.1k bs 512 (realtime ref)", 44100, 512,  { 512 }, fx },
        { "48k prep 256, bounce 8192",   48000, 256,  { 8192 }, fx },
        { "48k prep 128, bounce 32768",  48000, 128,  { 32768 }, fx },
        { "96k prep 512, random 1..20000", 96000, 512, { -1 }, fx },
        { "88.2k odd blocks 441/1000/3333", 88200, 64, { 441, 1000, 3333 }, fx },
        { "192k bounce 16384",           192000, 1024, { 16384 }, fx },
        { "96k vocoder + arp, 4096",     96000, 480,  { 4096 }, { { "blend", 0.6f }, { "arp_mode", 2 }, { "delay_mix", 0.3f }, { "reverb", 0.4f } } },
        { "48k pyramid + pedal, 8192",   48000, 512,  { 8192 }, { { "arp_mode", 1 }, { "shimmer", 0.4f }, { "reverb", 0.5f } } },
    };

    int fails = 0;
    HarmonizerProcessor p;   // one instance for all bounces: sample-rate changes like in a real session
    std::mt19937 g (11);
    for (auto& c : cases)
    {
        for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (prm->getDefaultValue());
        for (auto& kv : c.set) { auto* q = p.apvts.getParameter (kv.first); q->setValueNotifyingHost (q->convertTo0to1 (kv.second)); }
        p.setNonRealtime (c.blocks[0] != 512);
        p.prepareToPlay (c.sr, c.prepBs);
        const int64_t total = (int64_t) (seconds * c.sr);
        const int64_t win = (int64_t) (5.0 * c.sr);
        std::vector<double> winRms ((size_t) (total / win + 1), 0.0);
        int64_t pos = 0; int bad = 0; double ph = 0; int chord = -1; size_t bi = 0;
        const int prog[4][4] = { { 57, 60, 64, 67 }, { 53, 57, 60, 65 }, { 55, 59, 62, 67 }, { 52, 55, 59, 64 } };
        while (pos < total)
        {
            int bs = c.blocks[bi++ % c.blocks.size()];
            if (bs < 0) bs = 1 + (int) (g() % 20000);
            bs = (int) std::min<int64_t> (bs, total - pos);
            AudioBuffer<float> b (2, bs);
            MidiBuffer m;
            // new chord every 2 s; the old one is released shortly after (legato overlap), pedal in some sections
            for (int i = 0; i < bs; ++i)
            {
                const int64_t s = pos + i;
                const int ch = (int) (s / (int64_t) (2.0 * c.sr));
                if (ch != chord)
                {
                    if (chord >= 0) for (int nn : prog[chord & 3]) m.addEvent (MidiMessage::noteOff (1, nn), i);
                    chord = ch;
                    for (int nn : prog[chord & 3]) m.addEvent (MidiMessage::noteOn (1, nn, (uint8) (70 + (g() % 50))), i);
                    if ((chord & 7) == 3) m.addEvent (MidiMessage::controllerEvent (1, 64, 127), i);
                    if ((chord & 7) == 5) m.addEvent (MidiMessage::controllerEvent (1, 64, 0), i);
                }
                // singing: phrases of 3.5 s with 0.5 s breaths, slow vibrato, pitch moves between phrases
                const double t = (double) s / c.sr;
                const bool sing = std::fmod (t, 4.0) < 3.5;
                const double hz = 196.0 * std::pow (2.0, (double) ((int) (t / 4.0) % 5) / 12.0) * (1.0 + 0.004 * std::sin (6.2832 * 5.2 * t));
                ph += hz / c.sr;
                const float x = sing ? 0.15f * (float) (std::sin (6.2832 * ph) + 0.5 * std::sin (12.566 * ph) + 0.3 * std::sin (18.85 * ph)) : 0.0f;
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            p.processBlock (b, m);
            for (int i = 0; i < bs; ++i)
            {
                const float l = b.getSample (0, i), r = b.getSample (1, i);
                if (! std::isfinite (l) || ! std::isfinite (r) || std::abs (l) > 1.0f || std::abs (r) > 1.0f) ++bad;
                winRms[(size_t) ((pos + i) / win)] += (double) l * l + (double) r * r;
            }
            pos += bs;
        }
        double minDb = 1000.0;
        const size_t fullWins = (size_t) (total / win);
        for (size_t w = 0; w < fullWins; ++w) minDb = std::min (minDb, 10.0 * std::log10 (std::max (1e-20, winRms[w] / (2.0 * (double) win))));
        const int heals = p.healCount.exchange (0);
        const bool ok = bad == 0 && heals == 0 && minDb > -40.0;
        printf ("%s  %-34s quietest 5 s window %6.1f dB  bad %d  heals %d\n", ok ? "OK  " : "FAIL", c.name, minDb, bad, heals);
        if (! ok) ++fails;
    }
    printf ("\n%d problem(s)\n", fails);
    return fails ? 1 : 0;
}
