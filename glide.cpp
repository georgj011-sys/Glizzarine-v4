// V2 engine checks (no JUCE): chord glide (voice leading, legato/always, added notes, pedal, pyramid),
// voicing, humanize, consonants, low latency. Prints OK/FAIL per check.
#define _ALLOW_KEYWORD_MACROS 1   // MSVC: allow the test-only trick below
#define private public
#include "HarmonizerEngine.h"
#include "Sequencer.h"
#include <cstdio>
#include <random>
#include <set>
#include <string>

static int fails = 0;
static void check (bool ok, const std::string& what) { printf ("%s  %s\n", ok ? "OK  " : "FAIL", what.c_str()); if (! ok) ++fails; }

struct Rig
{
    float sr = 48000.0f;
    HarmonizerEngine h;
    Sequencer q { h };
    HarmonizerEngine::Params p;
    double ph = 0.0; int64_t pos = 0; float noise = 0.0f; float amp = 0.15f; float hz = 196.0f;
    std::mt19937 g { 3 }; std::normal_distribution<float> nd { 0.0f, 1.0f };
    std::vector<float> L, R;
    Rig (float s = 48000.0f) : sr (s) { p.dry = 0.0f; p.release = 0.3f; p.attack = 0.01f; h.prepare (sr); h.setParams (p); q.setSampleRate (sr); }
    void set() { h.setParams (p); }
    void run (double seconds)
    {
        const int N = (int) (seconds * sr);
        std::vector<float> in (64), l (64), r (64), d (64), dd (64), s1 (64), s2 (64);
        for (int done = 0; done < N;)
        {
            const int len = std::min (64, std::min (N - done, std::max (1, q.samplesToNextEvent())));
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) (pos + i) / sr;
                ph += hz * (1.0 + 0.003 * std::sin (6.2832 * 5.0 * t)) / sr;
                float s = 0.0f;   // voice-like: many harmonics falling off
                for (int hn = 1; hn <= 12; ++hn) s += (float) std::sin (6.2832 * ph * hn) / (float) hn;
                in[(size_t) i] = amp * 0.6f * s + noise * nd (g);
            }
            h.process (in.data(), l.data(), r.data(), d.data(), dd.data(), s1.data(), s2.data(), len);
            q.advance (len); q.fire();
            for (int i = 0; i < len; ++i) { L.push_back (l[(size_t) i]); R.push_back (r[(size_t) i]); }
            pos += len; done += len;
        }
    }
    std::multiset<int> gatedNotes() const
    {
        std::multiset<int> s;
        for (auto& v : h.voices) if (v.active && ! v.pending && (v.gate || v.sustained)) s.insert (v.note);
        return s;
    }
    int audible() const { int k = 0; for (auto& v : h.voices) if (v.active && v.env.out > 0.05f) ++k; return k; }
    float pitchOf (int note) const { for (auto& v : h.voices) if (v.active && v.note == note && (v.gate || v.sustained)) return HarmonizerEngine::basePitch (v); return -1.0f; }
};

static double clicks (const std::vector<float>& x, size_t a, size_t b)
{
    std::vector<double> d; double mx = 0;
    for (size_t i = a + 2; i < b; ++i) { const double v = std::abs (x[i] - 2 * x[i - 1] + x[i - 2]); d.push_back (v); mx = std::max (mx, v); }
    std::nth_element (d.begin(), d.begin() + (long) (d.size() * 0.99), d.end());
    return mx / (d[(size_t) (d.size() * 0.99)] + 1e-12);
}
static double rms (const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return std::sqrt (s / std::max<size_t> (1, b - a)); }

int main()
{
    printf ("--- chord glide ---\n");
    {   // legato: C E G held, B D G comes before C E G is let go -> C->B, E->D, G stays; no extra voices
        Rig r; r.p.chordGlide = 0.6f; r.p.glideLegato = true; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (1.2);
        for (int k : { 59, 62 }) r.h.noteOn (k, 0.8f);   // G is the common tone: it stays held
        r.run (0.05);   // chord window has passed: voices are moving
        const float pB = r.pitchOf (59), pD = r.pitchOf (62);
        printf ("     50 ms into the glide: voice for B at %.2f, voice for D at %.2f\n", pB, pD);
        check (pB > 59.05f && pB <= 60.0f && pD > 62.05f && pD <= 64.0f, "voices slide from C->B and E->D (nearest note, smallest movement)");
        check (r.gatedNotes().size() <= 6 && r.audible() == 3, "no new voices start, the old ones move over (3 audible)");
        const size_t a = r.L.size();
        for (int k : { 60, 64 }) r.h.noteOff (k);
        r.run (0.8);
        check (std::abs (r.pitchOf (59) - 59.0f) < 0.01f && std::abs (r.pitchOf (62) - 62.0f) < 0.01f, "voices land exactly on the new chord");
        check (r.gatedNotes() == std::multiset<int> { 59, 62, 67 }, "letting go of the old keys does not stop the moved voices");
        const double c = clicks (r.L, a - (size_t) (0.1 * r.sr), r.L.size());
        printf ("     click ratio during glide %.1f\n", c);
        check (c < 8.0, "glide is click-free");
    }
    {   // legato mode: release first, then new chord -> no glide (new voices)
        Rig r; r.p.chordGlide = 0.6f; r.p.glideLegato = true; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (1.0); for (int k : { 60, 64, 67 }) r.h.noteOff (k);
        r.run (0.05); for (int k : { 59, 62, 67 }) r.h.noteOn (k, 0.8f);
        r.run (0.05);
        check (std::abs (r.pitchOf (59) - 59.0f) < 0.01f, "legato mode: detached chord starts fresh (no glide)");
    }
    {   // always mode: release first, then new chord -> glide from the fading voices
        Rig r; r.p.chordGlide = 0.6f; r.p.glideLegato = false; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (1.0); for (int k : { 60, 64, 67 }) r.h.noteOff (k);
        r.run (0.1); for (int k : { 59, 62, 67 }) r.h.noteOn (k, 0.8f);
        r.run (0.06);
        check (r.pitchOf (59) > 59.05f && r.pitchOf (62) > 62.05f, "always mode: detached chord glides from the fading voices");
    }
    {   // adding one note to a held chord must not steal a voice
        Rig r; r.p.chordGlide = 0.6f; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (0.5); r.h.noteOn (71, 0.8f); r.run (0.1);
        check (r.gatedNotes() == std::multiset<int> { 60, 64, 67, 71 }, "adding a 7th to a held chord keeps the chord (4 voices)");
    }
    {   // 4 -> 3 voices and 3 -> 5 voices
        Rig r; r.p.chordGlide = 0.4f; r.set();
        r.run (0.3); for (int k : { 48, 55, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (0.5); for (int k : { 50, 57, 65 }) r.h.noteOn (k, 0.8f);
        r.run (0.05); for (int k : { 48, 55, 64, 67 }) r.h.noteOff (k);
        r.run (0.6);
        check (r.gatedNotes() == std::multiset<int> { 50, 57, 65 }, "4-note chord to 3-note chord: 3 voices move, 1 fades");
        for (int k : { 48, 52, 55, 59, 62 }) r.h.noteOn (k, 0.8f);
        r.run (0.05); for (int k : { 50, 57, 65 }) r.h.noteOff (k);
        r.run (0.6);
        check (r.gatedNotes() == std::multiset<int> { 48, 52, 55, 59, 62 }, "3-note chord to 5-note chord: 3 move, 2 fade in");
    }
    {   // pedal: chord, pedal down, let go, new chord -> glides
        Rig r; r.p.chordGlide = 0.5f; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.h.setSustain (true); r.run (0.4); for (int k : { 60, 64, 67 }) r.h.noteOff (k);
        r.run (0.3); for (int k : { 62, 65, 69 }) r.h.noteOn (k, 0.8f);
        r.run (0.06);
        check (r.pitchOf (62) > 60.0f && r.pitchOf (62) < 61.9f, "with the pedal, the next chord glides from the sustained one");
        r.h.setSustain (false); r.run (1.0);
        check (r.gatedNotes() == std::multiset<int> { 62, 65, 69 }, "pedal up keeps the new (held) chord");
    }
    {   // pyramid: entering notes take over fading voices of the last chord
        Rig r; r.p.chordGlide = 0.4f; r.p.glideLegato = false; r.set();
        r.q.setMode (Sequencer::ModePyramid); r.q.setStepSamples (r.sr * 0.15); r.q.setStyle (Sequencer::Legato, 0.8f, 0.8f);
        r.run (0.3); for (int k : { 60, 64, 67 }) r.q.keyDown (k, 0.8f);
        r.run (0.8); for (int k : { 60, 64, 67 }) r.q.keyUp (k);
        r.run (0.02); for (int k : { 62, 65, 69 }) r.q.keyDown (k, 0.8f);
        r.run (0.05);
        float p = r.pitchOf (62);
        printf ("     pyramid: first new note at %.2f 50 ms after it entered\n", p);
        check (p > 60.0f && p < 64.0f && std::abs (p - 62.0f) > 0.05f, "pyramid: the first new note glides out of the old chord");
        r.run (0.8);
        check (r.gatedNotes() == std::multiset<int> { 62, 65, 69 }, "pyramid stacks the full new chord");
    }
    {   // vocoder follows the glide
        Rig r; r.p.chordGlide = 0.5f; r.p.blend = 1.0f; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (0.6); for (int k : { 59, 62, 67 }) r.h.noteOn (k, 0.8f);
        r.run (0.06);
        bool moving = false; for (auto& v : r.h.vocoder.voices) if (v.env.active() && v.note == 59 && v.gPos > 0.0f && v.gPos < 1.0f) moving = true;
        check (moving, "vocoder voices glide with the harmonizer");
        int active = 0; for (auto& v : r.h.vocoder.voices) if (v.env.active() && v.env.stage != 3) ++active;
        check (active == 3, "vocoder: no extra synth voices");
    }

    printf ("--- voicing ---\n");
    for (int vc = 0; vc < 5; ++vc)
    {
        Rig r; r.p.voicing = vc; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (1.0);
        const bool lo = r.h.aux[1].active && r.h.aux[1].note == 48, hi = r.h.aux[2].active && r.h.aux[2].note == 79;
        int down = 0; for (auto& v : r.h.voices) if (v.active && v.voicingOct == -1) ++down;
        const double e = rms (r.L, (size_t) (0.8 * r.sr), r.L.size()) + rms (r.R, (size_t) (0.8 * r.sr), r.R.size());
        printf ("     voicing %d: bass double %d, top double %d, voices an octave down %d, level %.3f\n", vc, lo, hi, down, e);
        const bool ok = (vc == 0 && ! lo && ! hi && down == 0) || (vc == 1 && lo && ! hi) || (vc == 2 && ! lo && hi)
                     || (vc == 3 && lo && hi) || (vc == 4 && ! lo && ! hi && down == 1);
        check (ok, "voicing " + std::to_string (vc) + " builds the right voices");
    }
    {
        Rig r; r.p.voicing = HarmonizerEngine::BassTop; r.set();
        r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
        r.run (1.0); for (int k : { 60, 64, 67 }) r.h.noteOff (k);
        r.run (1.5);
        check (! r.h.aux[1].active && ! r.h.aux[2].active, "voicing doubles stop when you let go");
    }

    printf ("--- humanize ---\n");
    {
        Rig a; a.run (0.3); for (int k : { 60, 64, 67 }) a.h.noteOn (k, 0.8f); a.run (2.0);
        Rig b; b.p.humanize = 1.0f; b.set(); b.run (0.3); for (int k : { 60, 64, 67 }) b.h.noteOn (k, 0.8f); b.run (2.0);
        double d = 0, e = 0; for (size_t i = 0; i < a.L.size(); ++i) { d += std::pow (a.L[i] - b.L[i], 2); e += a.L[i] * a.L[i]; }
        printf ("     humanize difference %.3f\n", std::sqrt (d / e));
        check (std::sqrt (d / e) > 0.2, "humanize changes the voices");
        bool finite = true; for (float x : b.L) if (! std::isfinite (x)) finite = false;
        check (finite, "humanize output clean");
        // onsets are spread out
        Rig c; c.p.humanize = 1.0f; c.set(); c.run (0.3); for (int k : { 60, 64, 67, 71 }) c.h.noteOn (k, 0.8f);
        int delayed = 0; for (auto& v : c.h.voices) if (v.active && v.hDelay > 0) ++delayed;
        check (delayed >= 2, "humanize: voices come in at slightly different times");
        c.h.noteOff (60); c.h.noteOff (64); c.h.noteOff (67); c.h.noteOff (71); c.run (2.0);
        int act = 0; for (auto& v : c.h.voices) if (v.active) ++act;
        check (act == 0, "humanize: a quick tap does not leave a hanging voice");
    }

    printf ("--- consonants ---\n");
    {
        auto runCons = [] (float cons)
        {
            Rig r; r.p.consonants = cons; r.set();
            r.run (0.3); for (int k : { 60, 64, 67 }) r.h.noteOn (k, 0.8f);
            r.run (0.5);
            // "s" burst while singing goes on, then pure "sss" with no voice
            r.noise = 0.08f; r.run (0.3); r.amp = 0.0f; r.run (0.15); r.amp = 0.15f; r.noise = 0.0f; r.run (0.5);
            return r;
        };
        auto a = runCons (0.0f), b = runCons (1.0f);
        const size_t s0 = (size_t) (0.8 * 48000), s1 = (size_t) (1.25 * 48000);
        double d = 0, e = 0, lr = 0, ll = 0, rr = 0;
        for (size_t i = s0; i < s1; ++i) { const double x = b.L[i] - a.L[i], y = b.R[i] - a.R[i]; d += x * x + y * y; e += a.L[i] * a.L[i] + a.R[i] * a.R[i]; lr += x * y; ll += x * x; rr += y * y; }
        const double corr = lr / std::sqrt (ll * rr + 1e-20);
        printf ("     consonant layer level %.3f of the voices, L/R correlation %.2f\n", std::sqrt (d / e), corr);
        check (std::sqrt (d / e) > 0.05, "consonants knob adds S/T/breath to the voices");
        check (corr < 0.9, "consonants are spread in stereo");
    }

    printf ("--- low latency ---\n");
    for (float lowest : { 78.0f, 104.0f })
    {
        Rig n; n.p.lowestHz = lowest; n.set();
        Rig l; l.p.lowestHz = lowest; l.p.lowLatency = true; l.set();
        const double msN = 1000.0 * n.h.getLatencySamples() / 48000.0, msL = 1000.0 * l.h.getLatencySamples() / 48000.0;
        n.run (0.3); l.run (0.3);
        for (int k : { 57, 60, 64 }) { n.h.noteOn (k, 0.8f); l.h.noteOn (k, 0.8f); }
        n.run (2.0); l.run (2.0);
        const double cN = clicks (n.L, (size_t) (0.5 * 48000), n.L.size()), cL = clicks (l.L, (size_t) (0.5 * 48000), l.L.size());
        const double eN = rms (n.L, 48000, n.L.size()), eL = rms (l.L, 48000, l.L.size());
        printf ("     lowest %.0f Hz: normal %.1f ms (click %.1f, level %.3f), low latency %.1f ms (click %.1f, level %.3f), voiced %d\n",
                lowest, msN, cN, eN, msL, cL, eL, (int) l.h.isVoiced());
        check (msL < msN * 0.7 && cL < 8.0 && eL > eN * 0.6 && l.h.isVoiced(), "low latency at lowest " + std::to_string ((int) lowest) + " Hz: shorter, clean, same level");
    }
    {   // low latency, pitch tracking still exact
        for (float f : { 82.4f, 196.0f, 440.0f })
        {
            Rig l; l.p.lowLatency = true; l.p.lowestHz = 78.0f; l.set(); l.hz = f; l.run (1.0);
            const double cents = 1200.0 * std::log2 (l.h.currentPitchHz() / f);
            check (l.h.isVoiced() && std::abs (cents) < 5.0, "low latency pitch tracking " + std::to_string ((int) f) + " Hz (" + std::to_string (cents).substr (0, 5) + " cents)");
        }
    }

    printf ("\n%d problem(s)\n", fails);
    return fails ? 1 : 0;
}
