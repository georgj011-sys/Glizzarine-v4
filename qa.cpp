#include "cfg.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <random>
using namespace juce;
#ifdef _WIN32
static void setenv (const char* k, const char* v, int) { _putenv_s (k, v); }
static void unsetenv (const char* k) { _putenv_s (k, ""); }
#endif

struct Sig { float vib = 0; float amp = 0.2f, hz = 220.0f, noise = 0.0f; float amp2 = -1; int switchBlk = 1 << 30; bool silentAfter = false; int silentBlk = 1 << 30; };
struct Run { std::map<String, float> set; Sig sig; int blocks = 150; int bs = 512; double sr = 48000; bool chord = true; float pressure = -1; int vel = 100; bool pedal = false; int releaseBlk = 1 << 30; std::vector<int> pads; int chord2Blk = 1 << 30; };
struct Out { std::vector<float> L, R; int heal = 0; int bad = 0; bool singing = false; float hz = 0; int lat = 0; std::vector<std::pair<int, MidiMessage>> midiOut; };

static Out render (const Run& r)
{
    HarmonizerProcessor p;
    for (auto& kv : r.set) { auto* q = p.apvts.getParameter (kv.first); if (! q) { printf ("NO PARAM %s\n", kv.first.toRawUTF8()); continue; } q->setValueNotifyingHost (q->convertTo0to1 (kv.second)); }
    p.prepareToPlay (r.sr, r.bs);
    AudioBuffer<float> b (2, r.bs);
    std::mt19937 g (7); std::normal_distribution<float> nd (0, 1);
    double ph = 0; Out o;
    for (int blk = 0; blk < r.blocks; ++blk)
    {
        MidiBuffer m;
        if (blk == 1 && r.chord) for (int n : { 57, 60, 64, 67 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) r.vel), 0);
        if (blk == 1 && r.pedal) m.addEvent (MidiMessage::controllerEvent (1, 64, 127), 1);
        if (blk == 2 && r.pressure >= 0) m.addEvent (MidiMessage::channelPressureChange (1, (int) (r.pressure * 127)), 0);
        if (blk == r.releaseBlk) for (int n : { 57, 60, 64, 67, 55, 59, 62, 65 }) m.addEvent (MidiMessage::noteOff (1, n), 0);
        if (blk == 3) for (int pd : r.pads) m.addEvent (MidiMessage::noteOn (10, 36 + pd, (uint8) 100), 0);
        if (blk == r.chord2Blk) for (int n : { 55, 59, 62, 65 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) r.vel), 0);       // next chord, legato
        if (blk == r.chord2Blk + 2) for (int n : { 57, 60, 64, 67 }) m.addEvent (MidiMessage::noteOff (1, n), 0);
        for (int i = 0; i < r.bs; ++i)
        {
            ph += r.sig.hz * (1.0 + r.sig.vib * std::sin (6.2832 * 5.5 * (blk * r.bs + i) / r.sr)) / r.sr;
            float a = (blk >= r.sig.switchBlk && r.sig.amp2 >= 0) ? r.sig.amp2 : r.sig.amp;
            if (blk >= r.sig.silentBlk) a = 0;
            const float s = a * (float) (std::sin (6.2832 * ph) + 0.4 * std::sin (12.566 * ph) + 0.25 * std::sin (18.85 * ph)) + r.sig.noise * nd (g);
            (void) 0;
            b.setSample (0, i, s); b.setSample (1, i, s);
        }
        p.processBlock (b, m);
        for (const auto meta : m) o.midiOut.emplace_back (blk * r.bs + meta.samplePosition, meta.getMessage());
        for (int i = 0; i < r.bs; ++i) { float l = b.getSample (0, i), rr = b.getSample (1, i); if (! std::isfinite (l) || ! std::isfinite (rr)) ++o.bad; o.L.push_back (l); o.R.push_back (rr); }
    }
    o.heal = p.healCount.load(); o.singing = p.singing.load(); o.hz = p.detectedHz.load(); o.lat = p.getLatencySamples();
    return o;
}
static double rms (const std::vector<float>& x, size_t a = 0, size_t e = 0) { if (! e) e = x.size(); double s = 0; for (size_t i = a; i < e; ++i) s += x[i] * x[i]; return std::sqrt (s / std::max<size_t> (1, e - a)); }
static double diff (const Out& a, const Out& b) { double s = 0, n = 0; for (size_t i = 0; i < a.L.size(); ++i) { s += std::pow (a.L[i] - b.L[i], 2) + std::pow (a.R[i] - b.R[i], 2); n += a.L[i] * a.L[i] + a.R[i] * a.R[i]; } return std::sqrt (s / std::max (1e-12, n)); }
static double db (double x) { return 20 * std::log10 (std::max (1e-9, x)); }

int main()
{
    ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto check = [&] (bool ok, const String& what) { printf ("%s  %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8()); if (! ok) ++fails; };

    // 1. parameter sweep: every parameter must change the sound (in a context where it can)
    std::map<String, float> rich = { { "chorus_mix", 0.3f }, { "phaser_mix", 0.3f }, { "wah_mix", 0.4f }, { "delay_mix", 0.3f }, { "reverb", 0.4f }, { "shimmer", 0.3f },
                                     { "trem_on", 1 }, { "lofi_on", 1 }, { "tone", 3000.0f }, { "blend", 0.5f }, { "dry", 0.5f }, { "at_filter_on", 1 }, { "at_vib_on", 1 }, { "at_detune_on", 1 }, { "at_formant_on", 1 },
                                     { "vel_bright_on", 1 }, { "vel_attack_on", 1 }, { "vel_send_on", 1 }, { "suboct", 0.3f } };
    HarmonizerProcessor probe;
    std::set<String> skip = { "pad_latch", "dry_timing", "sustain", "release", "at_response", "vel_curve", "lowest", "glide_time", "bpm", "tempo_src", "gate", "sens",
                              "midi_out", "glide_mode" };
    // need a special signal or situation; each has its own extra check below
    std::set<String> extra = { "follow", "voc_sib", "arp_order", "vel_bright_on", "vel_bright", "consonants", "chord_glide" };
    printf ("--- parameter sweep (difference min vs max) ---\n");
    for (auto* prm : probe.getParameters())
    {
        auto* rp = dynamic_cast<RangedAudioParameter*> (prm); const String id = rp->paramID;
        if (skip.count (id)) continue;
        Run a; a.set = rich; a.pressure = 0.6f; a.blocks = 120;
        if (id.startsWith ("arp_") ) { a.set["arp_mode"] = 2; }
        if (id == "arp_oct" || id == "arp_steps") { a.set["arp_mode"] = 2; }
        if (id == "delay_time") a.set["delay_sync"] = 0;
        Run b = a;
        a.set[id] = rp->convertFrom0to1 (0.0f); b.set[id] = rp->convertFrom0to1 (1.0f);
        if (id == "arp_mode") { a.set[id] = 0; b.set[id] = 2; }
        auto oa = render (a), ob = render (b);
        const double d = diff (oa, ob);
        const bool ok = d > 0.02 && oa.bad == 0 && ob.bad == 0 && oa.heal == 0 && ob.heal == 0;
        const bool known = extra.count (id) && oa.bad == 0 && ob.bad == 0 && oa.heal == 0 && ob.heal == 0;
        if (! ok || d < 0.05) printf ("%s  %-16s diff %.3f bad %d/%d heal %d/%d\n", ok ? "weak" : known ? "(xtr)" : "FAIL", id.toRawUTF8(), d, oa.bad, ob.bad, oa.heal, ob.heal);
        if (! ok && ! known) ++fails;
    }

    printf ("--- gate ---\n");
    {
        Run r; r.sig.amp = 0; r.sig.noise = 0.003f; r.chord = true; r.set = { { "dry", 1.0f }, { "reverb", 0 } };
        r.set["gate"] = -80; auto off = render (r);
        r.set["gate"] = -30; auto on = render (r);
        for (float gdb : { -60.f, -45.f, -40.f, -35.f, -30.f }) { r.set["gate"] = gdb; auto o = render (r); printf ("     noise -38 dB (after +12 gain), gate %.0f: out %.1f dB\n", gdb, db (rms (o.L, 48000))); }
        printf ("     noise only: gate off %.1f dB, gate -30 %.1f dB\n", db (rms (off.L, 48000)), db (rms (on.L, 48000)));
        check (rms (on.L, 48000) < rms (off.L, 48000) * 0.3, "gate pulls room noise down by > 10 dB");
        Run s; s.sig.amp = 0.1f; s.set = { { "dry", 1.0f }, { "reverb", 0 }, { "gate", -80 } }; auto s1 = render (s); s.set["gate"] = -30; auto s2 = render (s);
        printf ("     singing:   gate off %.1f dB, gate -30 %.1f dB\n", db (rms (s1.L, 48000)), db (rms (s2.L, 48000)));
        check (std::abs (db (rms (s2.L, 48000)) - db (rms (s1.L, 48000))) < 1.0, "gate leaves normal singing alone");
    }

    printf ("--- extra checks ---\n");
    { Run r; r.set = { { "wah_mix", 1.0f }, { "dry", 0 }, { "reverb", 0 }, { "chorus_mix", 0 } };
      r.set["wah_sens"] = 0; auto a = render (r); r.set["wah_sens"] = 1; auto b = render (r);
      printf ("     env filter sensitivity diff %.3f (solo voice 0)\n", diff (a, b)); check (diff (a, b) > 0.1, "envelope filter works with solo voice at 0");
      r.set["wah_mix"] = 0; auto c = render (r); check (diff (a, c) > 0.1, "envelope filter audible"); }
    { Run r; r.vel = 40; r.set = { { "reverb", 0 }, { "chorus_mix", 0 } }; auto a = render (r);
      r.set["vel_bright_on"] = 1; r.set["vel_bright"] = 1; auto b = render (r); printf ("     vel brightness diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.05, "velocity brightness (soft hit)");
      Run q = r; q.set = { { "reverb", 0 }, { "chorus_mix", 0 }, { "vel_attack_on", 1 }, { "vel_attack", 1 } }; auto c = render (q);
      const double e1 = rms (a.L, 2 * 512, 12 * 512), e2 = rms (c.L, 2 * 512, 12 * 512); printf ("     first 100 ms: normal %.1f dB, soft attack %.1f dB\n", db (e1), db (e2)); check (e2 < e1 * 0.7, "velocity soft attack"); }
    { Run r; r.set = { { "arp_mode", 2 }, { "arp_order", 0 }, { "reverb", 0 } }; auto a = render (r); r.set["arp_order"] = 1; auto b = render (r); printf ("     arp up vs down diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.1, "arp order"); }
    { Run r; r.set = { { "reverb", 0 } }; auto a = render (r); r.pads = { 11 }; auto b = render (r); printf ("     chaos diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.1, "chaos pad moves held voices"); }
    { Run r; r.sig.vib = 0.03f; r.set = { { "reverb", 0 }, { "chorus_mix", 0 }, { "follow", 0 } }; auto a = render (r); r.set["follow"] = 1; auto b = render (r); printf ("     follow pitch (with vibrato) diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.1, "follow pitch"); }
    { Run r; r.sig.noise = 0.05f; r.set = { { "blend", 1 }, { "reverb", 0 }, { "voc_sib", 0 } }; auto a = render (r); r.set["voc_sib"] = 1; auto b = render (r); printf ("     vocoder sibilance (with noise) diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.05, "vocoder sibilance"); }
    { Run r; r.sig.noise = 0.06f; r.set = { { "reverb", 0 }, { "chorus_mix", 0 }, { "consonants", 0 } }; auto a = render (r); r.set["consonants"] = 1; auto b = render (r);
      printf ("     consonants (with breath noise) diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.05, "consonants"); }
    { Run r; r.chord2Blk = 60; r.set = { { "reverb", 0 }, { "chorus_mix", 0 }, { "chord_glide", 0 } }; auto a = render (r); r.set["chord_glide"] = 0.8f; auto b = render (r);
      printf ("     chord glide on chord change diff %.3f, bad %d heal %d\n", diff (a, b), b.bad, b.heal); check (diff (a, b) > 0.1 && b.bad == 0 && b.heal == 0, "chord glide");
      r.set["glide_mode"] = 0; auto c = render (r); check (c.bad == 0 && c.heal == 0, "chord glide (always)"); }
    { Run r; r.chord2Blk = 60; r.set = { { "reverb", 0 } }; auto a = render (r); r.pads = { 10 }; r.set["glide_time"] = 0.5f; auto b = render (r);
      printf ("     glide pad on chord change diff %.3f\n", diff (a, b)); check (diff (a, b) > 0.05, "glide pad"); }

    printf ("--- MIDI out ---\n");
    { Run r; r.sig.hz = 220.0f; r.chord2Blk = 60; r.blocks = 200; r.releaseBlk = 150; r.sig.silentBlk = 120; r.set = { { "midi_out", 2 } }; auto o = render (r);
      int melOn = -1, offs1 = 0, ons1 = 0; std::set<int> harm; std::map<int, int> open;
      for (auto& e : o.midiOut)
      {
          const auto& mm = e.second; const int key = mm.getChannel() * 128 + mm.getNoteNumber();
          if (mm.isNoteOn()) { ++open[key]; if (mm.getChannel() == 1) { ++ons1; if (melOn < 0) melOn = mm.getNoteNumber(); } else harm.insert (mm.getNoteNumber()); }
          if (mm.isNoteOff()) { --open[key]; if (mm.getChannel() == 1) ++offs1; }
      }
      int hanging = 0; for (auto& kv : open) if (kv.second != 0) ++hanging;
      printf ("     melody note %d (%d on / %d off), harmony notes %d, hanging %d\n", melOn, ons1, offs1, (int) harm.size(), hanging);
      check (melOn == 57, "MIDI out: sung A3 comes out as note 57 on channel 1");
      check (ons1 <= 2, "MIDI out: steady singing gives one note (no flutter)");
      check (harm == std::set<int> { 55, 57, 59, 60, 62, 64, 65, 67 }, "MIDI out: harmony notes on channel 2");
      check (hanging == 0, "MIDI out: no hanging notes after letting go / silence");
      r.set["midi_out"] = 0; auto z = render (r); check (z.midiOut.empty(), "MIDI out off: nothing goes out"); }

    printf ("--- sensitivity ---\n");
    for (float amp : { 0.0005f, 0.002f, 0.01f })
    {
        String line = "     input " + String (db (amp), 0) + " dBFS: ";
        for (float sn : { 0.0f, 50.0f, 100.0f }) { Run r; r.sig.amp = amp; r.set = { { "sens", sn } }; auto o = render (r); line << "sens " << (int) sn << "=" << (o.singing ? "yes " : "no  "); }
        printf ("%s\n", line.toRawUTF8());
    }
    { Run r; r.sig.amp = 0.0005f; r.set = { { "sens", 0 } }; auto a = render (r); r.set["sens"] = 100; auto b = render (r); check (! a.singing && b.singing, "sensitivity changes what counts as singing (-54 dBFS)"); }

    printf ("--- dynamics / leveling ---\n");
    for (float lev : { 0.0f, 0.5f, 1.0f })
    {
        Run r; r.blocks = 400; r.sig.amp = 0.03f; r.sig.amp2 = 0.3f; r.sig.switchBlk = 200; r.set = { { "leveling", lev }, { "reverb", 0 }, { "chorus_mix", 0 } };
        auto o = render (r);
        const double qa = rms (o.L, 120 * 512, 195 * 512), lo = rms (o.L, 320 * 512, 395 * 512);
        printf ("     leveling %.1f: soft %.1f dB, loud %.1f dB, spread %.1f dB (input spread 20 dB)\n", lev, db (qa), db (lo), db (lo) - db (qa));
    }
    { Run r; r.blocks = 400; r.sig.amp = 0.03f; r.sig.amp2 = 0.3f; r.sig.switchBlk = 200; r.set = { { "leveling", 0 }, { "reverb", 0 }, { "chorus_mix", 0 } };
      auto a = render (r); r.set["leveling"] = 1; auto b = render (r);
      const double sa = db (rms (a.L, 320 * 512, 395 * 512)) - db (rms (a.L, 120 * 512, 195 * 512));
      const double sb = db (rms (b.L, 320 * 512, 395 * 512)) - db (rms (b.L, 120 * 512, 195 * 512));
      check (sb < sa - 4, "leveling evens out loud/soft"); check (sa > 12, "without leveling, dynamics come through"); }

    printf ("--- velocity / curve ---\n");
    { Run r; r.set = { { "reverb", 0 }, { "velocity", 1 } }; r.vel = 30; auto a = render (r); r.vel = 127; auto b = render (r);
      printf ("     vel 30 %.1f dB, vel 127 %.1f dB\n", db (rms (a.L, 48000)), db (rms (b.L, 48000)));
      check (rms (b.L, 48000) > rms (a.L, 48000) * 1.5, "velocity changes level");
      r.vel = 60; r.set["vel_curve"] = 0; auto c0 = render (r); r.set["vel_curve"] = 1; auto c1 = render (r);
      check (rms (c0.L, 48000) > rms (c1.L, 48000) * 1.1, "velocity curve works"); }

    printf ("--- aftertouch ---\n");
    { Run r; r.set = { { "reverb", 0 }, { "shimmer", 0 } }; auto a = render (r); r.pressure = 1.0f; auto b = render (r);
      printf ("     no AT %.1f dB, full AT %.1f dB\n", db (rms (a.L, 48000)), db (rms (b.L, 48000)));
      check (rms (b.L, 48000) > rms (a.L, 48000) * 1.2, "aftertouch swell"); 
      r.set["at_on"] = 0; auto c = render (r); check (diff (a, c) < 0.01, "AT master switch off = no effect"); }

    printf ("--- pedal / release ---\n");
    { Run r; r.blocks = 300; r.releaseBlk = 100; r.sig.silentBlk = 100; r.set = { { "reverb", 0 }, { "shimmer", 0 }, { "dry", 0 }, { "delay_mix", 0 }, { "chorus_mix", 0 } };
      auto a = render (r); r.pedal = true; auto b = render (r);
      printf ("     after release: no pedal %.1f dB, pedal %.1f dB\n", db (rms (a.L, 150 * 512, 200 * 512)), db (rms (b.L, 150 * 512, 200 * 512)));
      check (rms (b.L, 150 * 512, 200 * 512) > rms (a.L, 150 * 512, 200 * 512) * 3, "pedal keeps voices ringing"); 
      check (rms (a.L, 250 * 512) < 1e-3, "voices die away without pedal"); }

    printf ("--- pads ---\n");
    { Run base; base.set = { { "reverb", 0.2f } }; auto o0 = render (base);
      for (int pd = 0; pd < PadFx::Count; ++pd) { Run r = base; r.pads = { pd }; auto o = render (r); const double d = diff (o0, o);
        printf ("     %-15s diff %.3f %s\n", PadFx::name (pd), d, o.bad || o.heal ? "BAD" : ""); if ((d < 0.02 && pd != PadFx::Glide) || o.bad || o.heal) ++fails; } }

    printf ("--- robustness: sample rates, block sizes, presets ---\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 }) for (int bs : { 16, 64, 441, 2048, 8192 })
    { Run r; r.sr = sr; r.bs = bs; r.blocks = (int) (sr * 2 / bs); r.set = rich; r.pressure = 0.5f; r.pads = { 0, 7 }; auto o = render (r);
      if (o.bad || o.heal || rms (o.L) < 1e-3) { printf ("FAIL  sr %.0f bs %d bad %d heal %d rms %.4f\n", sr, bs, o.bad, o.heal, rms (o.L)); ++fails; } }
    printf ("     done\n");
    { HarmonizerProcessor p; int bad = 0;
      for (int i = 0; i < p.getNumPrograms(); ++i) { Run r; r.blocks = 300; HarmonizerProcessor q; q.setCurrentProgram (i);
        for (auto* prm : q.getParameters()) { auto* rp = dynamic_cast<RangedAudioParameter*> (prm); r.set[rp->paramID] = rp->convertFrom0to1 (rp->getValue()); }
        auto o = render (r); printf ("     %-20s %6.1f dB\n", p.getProgramName (i).toRawUTF8(), db (rms (o.L, 48000) + rms (o.R, 48000)) - 6.0);
        if (o.bad || o.heal || rms (o.L) < 1e-3 || rms (o.L) > 0.9) { printf ("FAIL  preset %s rms %.3f bad %d heal %d\n", p.getProgramName (i).toRawUTF8(), rms (o.L), o.bad, o.heal); ++bad; } }
      check (bad == 0, "all " + String (p.getNumPrograms()) + " presets play clean"); fails += bad ? 0 : 0; }

    printf ("--- state save/load ---\n");
    { HarmonizerProcessor a; a.apvts.getParameter ("harmony")->setValueNotifyingHost (0.3f); a.startCcLearn ("blend");
      MidiBuffer m; m.addEvent (MidiMessage::controllerEvent (1, 74, 64), 0); AudioBuffer<float> b (2, 256); b.clear(); a.prepareToPlay (48000, 256); a.processBlock (b, m);
      MemoryBlock mb; a.getStateInformation (mb); HarmonizerProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize());
      check (std::abs (c.apvts.getParameter ("harmony")->getValue() - 0.3f) < 0.01f, "parameters restore");
      check (c.ccFor ("blend") == 74, "MIDI learn mapping restores"); }

    printf ("--- own presets ---\n");
    { auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("glzh_qa_presets"); dir.deleteRecursively();
      setenv ("GLZH_PRESET_DIR", dir.getFullPathName().toRawUTF8(), 1);
      HarmonizerProcessor a; a.setCurrentProgram (2);
      a.apvts.getParameter ("harmony")->setValueNotifyingHost (0.2f); a.apvts.getParameter ("input")->setValueNotifyingHost (0.9f);
      check (a.saveUserPreset ("My Test / Choir"), "save own preset (odd characters in the name)");
      check (HarmonizerProcessor::userPresetNames().size() == 1, "it shows up in the list");
      a.setCurrentProgram (0); a.apvts.getParameter ("input")->setValueNotifyingHost (0.1f);
      const String nm = HarmonizerProcessor::userPresetNames()[0];
      check (a.loadUserPreset (nm), "load own preset");
      check (std::abs (a.apvts.getParameter ("harmony")->getValue() - 0.2f) < 0.01f, "sound parameters come back");
      check (std::abs (a.apvts.getParameter ("voicing")->getValue() - a.apvts.getParameter ("voicing")->getValue()) < 0.01f
             && std::abs (a.apvts.getParameter ("input")->getValue() - 0.1f) < 0.01f, "setup (input gain) is not touched by presets");
      a.apvts.getParameter ("harmony")->setValueNotifyingHost (0.7f); check (a.saveUserPreset (nm), "overwrite own preset");
      a.setCurrentProgram (0); a.loadUserPreset (nm); check (std::abs (a.apvts.getParameter ("harmony")->getValue() - 0.7f) < 0.01f, "overwritten values load");
      MemoryBlock mb; a.getStateInformation (mb); HarmonizerProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize());
      check (c.getUserPresetName() == nm, "project remembers which own preset was loaded");
      check (a.deleteUserPreset (nm) && HarmonizerProcessor::userPresetNames().isEmpty(), "delete own preset");
      dir.deleteRecursively(); unsetenv ("GLZH_PRESET_DIR"); }

    printf ("--- latency ---\n");
    { Run r; r.blocks = 5; auto o = render (r); printf ("     reported %d samples = %.1f ms\n", o.lat, o.lat / 48.0); check (o.lat > 0 && o.lat < 4800, "latency reported to host");
      r.set["latency_mode"] = 1; auto l = render (r); printf ("     low latency: %d samples = %.1f ms\n", l.lat, l.lat / 48.0); check (l.lat > 0 && l.lat < o.lat * 0.7, "low latency reported to host");
      r.set["lowest"] = 1; auto l2 = render (r); printf ("     low latency, lowest A2: %.1f ms\n", l2.lat / 48.0); }
    { Run r; r.set = { { "latency_mode", 1 }, { "reverb", 0 } }; auto o = render (r); check (o.bad == 0 && o.heal == 0 && o.singing && rms (o.L, 48000) > 0.01, "low latency plays clean"); }

    printf ("\n%d problem(s)\n", fails);
    return fails ? 1 : 0;
}
