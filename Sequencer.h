#pragma once
// Pyramid and Arp for the harmonizer. Sits between the keyboard and the engine:
// it collects the held keys and decides when each voice starts.
#include <vector>
#include <algorithm>
#include <random>
#include "HarmonizerEngine.h"

class Sequencer
{
public:
    enum Mode { ModeOff = 0, ModePyramid, ModeArp };
    enum Order { Up = 0, Down, UpDown, DownUp, Crawl, OutsideIn, InsideOut, Random, Played };
    enum Style { Pluck = 0, Legato, Swell };

    explicit Sequencer (HarmonizerEngine& e) : eng (e) {}

    void setSampleRate (double s) { sr = s; }
    void setMode (int m)
    {
        if (m == mode) return;
        eng.allNotesOff(); eng.setSustain (false);
        keys.clear(); entered.clear(); pendingOffs.clear();
        running = false; mode = m;
        if (mode == ModeOff && pedal) eng.setSustain (true);
    }
    void setOrder (int o) { order = o; }
    void setSteps (int s) { steps = std::max (0, s); }   // 0 = the whole pattern (all keys, all octaves)
    void setGate (float g) { gate = std::max (0.05f, std::min (1.0f, g)); }
    void setOctaves (int o) { octaves = std::max (1, std::min (3, o)); }
    void setStepSamples (double s) { stepLen = std::max (16.0, s); }
    void setStyle (int s, float releaseSec, float spread) { style = s; arpRelease = std::max (0.03f, releaseSec); arpSpread = spread; }
    int getMode() const { return mode; }

    void keyDown (int note, float vel)
    {
        if (mode == ModeOff) { eng.noteOn (note, vel); return; }
        const bool wasEmpty = keys.empty();
        auto it = std::find_if (keys.begin(), keys.end(), [note] (const Key& k) { return k.note == note; });
        if (it != keys.end()) { it->held = true; it->sustained = false; it->vel = vel; }
        else keys.push_back ({ note, vel, ++pressCounter, true, false });
        if (wasEmpty) { running = true; counter = 0.0; stepIdx = 0; walkPos = 0; entered.clear(); }
    }

    void keyUp (int note)
    {
        if (mode == ModeOff) { eng.noteOff (note); return; }
        for (auto& k : keys) if (k.note == note) { k.held = false; k.sustained = pedal; }
        cleanup();
    }

    void setPedal (bool down)
    {
        pedal = down;
        if (mode == ModeOff) { eng.setSustain (down); return; }
        if (! down) { for (auto& k : keys) k.sustained = false; cleanup(); }
    }

    void allOff() { eng.allNotesOff(); keys.clear(); entered.clear(); pendingOffs.clear(); running = false; }

    int samplesToNextEvent() const
    {
        double next = 1.0e9;
        if (running) next = std::min (next, counter);
        for (auto& p : pendingOffs) next = std::min (next, p.countdown);
        return (int) std::ceil (std::max (0.0, next));
    }

    void advance (int samples)
    {
        if (running) counter -= samples;
        for (auto& p : pendingOffs) p.countdown -= samples;
    }

    void fire()
    {
        for (size_t i = 0; i < pendingOffs.size();)
        {
            if (pendingOffs[i].countdown <= 0.0) { eng.noteOff (pendingOffs[i].note); pendingOffs.erase (pendingOffs.begin() + (long) i); }
            else ++i;
        }
        int guard = 0;
        while (running && counter <= 0.0 && guard++ < 4) { step(); counter += stepLen; }
    }

private:
    struct Key { int note; float vel; uint64_t pressed; bool held, sustained; };
    struct PendingOff { int note; double countdown; };

    EnvShape shapeFor (bool pyramid) const
    {
        const float stepSec = (float) (stepLen / sr);
        EnvShape e;
        e.release = arpRelease;
        switch (style)
        {
            case Pluck:  e.attack = 0.005f; e.decay = std::max (0.06f, stepSec * (pyramid ? 3.0f : 0.7f)); e.sustain = pyramid ? 0.55f : 0.0f; break;
            case Swell:  e.attack = std::max (0.02f, stepSec * 0.8f); e.decay = 1.0f; e.sustain = 1.0f; break;
            case Legato:
            default:     e.attack = 0.02f; e.decay = 1.0f; e.sustain = 1.0f; break;
        }
        return e;
    }

    void cleanup()
    {
        for (size_t i = 0; i < keys.size();)
        {
            if (! keys[i].held && ! keys[i].sustained)
            {
                const int note = keys[i].note;
                if (mode == ModePyramid)
                {
                    auto e = std::find (entered.begin(), entered.end(), note);
                    if (e != entered.end()) { eng.noteOff (note); entered.erase (e); }
                }
                keys.erase (keys.begin() + (long) i);
            }
            else ++i;
        }
        if (keys.empty()) { running = false; entered.clear(); }
    }

    // the order in which notes come, built from the held keys
    std::vector<Key> pattern (bool forArp) const
    {
        std::vector<Key> base = keys;
        if (order == Played) std::sort (base.begin(), base.end(), [] (const Key& a, const Key& b) { return a.pressed < b.pressed; });
        else std::sort (base.begin(), base.end(), [] (const Key& a, const Key& b) { return a.note < b.note; });
        std::vector<Key> up;
        const int oct = forArp ? octaves : 1;
        for (int o = 0; o < oct; ++o) for (auto k : base) { k.note += 12 * o; up.push_back (k); }
        if (order == Played) return up;

        std::vector<Key> out;
        const int len = (int) up.size();
        switch (order)
        {
            case Down: out.assign (up.rbegin(), up.rend()); break;
            case UpDown:
                out = up;
                if (forArp) for (int i = len - 2; i >= 1; --i) out.push_back (up[(size_t) i]);
                break;
            case DownUp:
                out.assign (up.rbegin(), up.rend());
                if (forArp) for (int i = 1; i <= len - 2; ++i) out.push_back (up[(size_t) i]);
                break;
            case OutsideIn:
                for (int lo = 0, hi = len - 1; lo <= hi; ++lo, --hi) { out.push_back (up[(size_t) lo]); if (hi != lo) out.push_back (up[(size_t) hi]); }
                break;
            case InsideOut:
            {
                std::vector<Key> oi;
                for (int lo = 0, hi = len - 1; lo <= hi; ++lo, --hi) { oi.push_back (up[(size_t) lo]); if (hi != lo) oi.push_back (up[(size_t) hi]); }
                out.assign (oi.rbegin(), oi.rend());
                break;
            }
            default: out = up; break;   // Up, Crawl, Random use the rising list
        }
        return out;
    }

    void step()
    {
        if (keys.empty()) { running = false; return; }

        if (mode == ModePyramid)
        {
            std::vector<Key> list = pattern (false), pending;
            for (auto& k : list) if (std::find (entered.begin(), entered.end(), k.note) == entered.end()) pending.push_back (k);
            if (pending.empty()) return;   // the full chord stands until you let go
            size_t pick = 0;
            if (order == Random) pick = (size_t) (rng() % pending.size());
            const Key k = pending[pick];
            const EnvShape e = shapeFor (true);
            eng.noteOn (k.note, k.vel, HarmonizerEngine::panOrder ((int) entered.size()) * arpSpread, &e, true);
            entered.push_back (k.note);
            return;
        }

        // Arp: one note per step, starting over after "steps" steps
        const std::vector<Key> seq = pattern (true);
        const int len = (int) seq.size();
        if (len == 0) return;
        int idx;
        if (stepIdx == 0) walkPos = 0;
        switch (order)
        {
            case Random: idx = (int) (rng() % (unsigned) len); break;
            case Crawl:
                idx = walkPos;
                walkPos = std::max (0, std::min (len - 1, walkPos + ((rng() % 3u) == 0 ? -1 : 1)));
                if (walkPos == len - 1 && len > 1 && (rng() & 1u)) walkPos = len - 2;
                break;
            default: idx = stepIdx % len; break;
        }
        const Key k = seq[(size_t) idx];
        const EnvShape e = shapeFor (false);
        eng.noteOn (k.note, k.vel, HarmonizerEngine::panOrder (stepIdx) * arpSpread, &e, stepIdx == 0, false);
        const double len2 = style == Legato ? stepLen * std::max (gate, 0.9f) : stepLen * gate;
        pendingOffs.push_back ({ k.note, len2 });
        const int loopLen = steps > 0 ? steps : (order == Random || order == Crawl ? len : len);
        stepIdx = (stepIdx + 1) % std::max (1, loopLen);
    }

    HarmonizerEngine& eng;
    double sr = 44100.0;
    int mode = ModeOff, order = Up, steps = 0, octaves = 1, style = Pluck;
    float gate = 0.6f, arpRelease = 0.25f, arpSpread = 0.7f;
    double stepLen = 22050.0, counter = 0.0;
    bool running = false, pedal = false;
    int stepIdx = 0, walkPos = 0;
    uint64_t pressCounter = 0;
    std::vector<Key> keys;
    std::vector<int> entered;
    std::vector<PendingOff> pendingOffs;
    std::mt19937 rng { 12345u };
};
