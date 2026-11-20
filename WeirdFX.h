#pragma once
// Weird FX for the pads: each one runs while its pad is held, with smooth 10 ms fades in and out.
// Works on the full stereo mix. No JUCE dependency.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

class WeirdFX
{
public:
    enum Id { Freeze = 0, Stutter8, Stutter16, Reverse, TapeStop, OctUp, OctDown, Cloud, Robot, Riser, NumFx };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        size = 1; while (size < (int) (sr * 4.0f)) size <<= 1;
        histL.assign ((size_t) size, 0.0f); histR.assign ((size_t) size, 0.0f);
        w = 0;
        for (auto& f : fx) f = Slot();
        fadeC = 1.0f - std::exp (-1.0f / (0.01f * sr));
        for (auto& s : shifters) s.prepare (sr);
        for (auto& g : grains) g.active = false;
        rng = 4242u;
        ringPh = 0.0f; combW = 0; combL.assign (4096, 0.0f); combR.assign (4096, 0.0f);
        hp1L = hp1R = hp2L = hp2R = 0.0f;
        freezeL.assign ((size_t) (0.6f * sr), 0.0f); freezeR.assign ((size_t) (0.6f * sr), 0.0f);
        const size_t maxSlice = (size_t) (sr * 1.1f);
        st8L.assign (maxSlice, 0.0f); st8R.assign (maxSlice, 0.0f); st16L.assign (maxSlice, 0.0f); st16R.assign (maxSlice, 0.0f);
    }

    void setTempo (double bpmValue) { bpm = (float) std::max (30.0, std::min (300.0, bpmValue)); }
    void set (int id, bool on)
    {
        if (id < 0 || id >= NumFx) return;
        Slot& f = fx[id];
        if (on && ! f.on)
        {
            f.startPos = w; f.time = 0.0f; f.readPos = (double) w; f.rate = 1.0; f.phase = 0.0;
            if (id == Freeze)
            {
                // copy the last 0.6 s, so the frozen sound stays the same however long you hold
                const int len = (int) freezeL.size();
                for (int k = 0; k < len; ++k) { freezeL[(size_t) k] = read (histL, w - len + k); freezeR[(size_t) k] = read (histR, w - len + k); }
                f.phase = 0.0; f.phase2 = 0.0; f.g0 = 0; f.g1 = len / 3;
            }
            if (id == Stutter8 || id == Stutter16)
            {
                const int slice = sliceLen (id);
                auto& bl = id == Stutter8 ? st8L : st16L; auto& br = id == Stutter8 ? st8R : st16R;
                for (int k = 0; k < slice; ++k) { bl[(size_t) k] = read (histL, w - slice + k); br[(size_t) k] = read (histR, w - slice + k); }
            }
        }
        f.on = on;
    }
    float riserAmount() const { return fx[Riser].on ? std::min (1.0f, fx[Riser].time / 3.0f) : 0.0f; }
    bool isOn (int id) const { return id >= 0 && id < NumFx && fx[id].on; }
    bool anyActive() const { for (auto& f : fx) if (f.on || f.wet > 1.0e-4f) return true; return false; }

    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            histL[(size_t) w] = L[i]; histR[(size_t) w] = R[i];
            float l = L[i], r = R[i];
            const float dl = l, dr = r;

            for (int id = 0; id < NumFx; ++id)
            {
                Slot& f = fx[id];
                f.wet += ((f.on ? 1.0f : 0.0f) - f.wet) * fadeC;
                if (f.wet < 1.0e-4f && ! f.on) continue;
                if (f.on) f.time += 1.0f / sr;
                float yl = 0.0f, yr = 0.0f; bool replace = true; float wetMix = 1.0f;

                switch (id)
                {
                    case Freeze:
                    {
                        // two overlapping grains wander through the frozen 0.6 s
                        const int len = (int) freezeL.size();
                        const int win = std::max (64, (int) (0.2f * sr));
                        for (int g = 0; g < 2; ++g)
                        {
                            double& ph = g == 0 ? f.phase : f.phase2;
                            int& st = g == 0 ? f.g0 : f.g1;
                            const double p = std::fmod (ph + (g ? win * 0.5 : 0.0), (double) win);
                            if (p < 1.0) st = (int) (rand01() * (float) (len - win - 1));
                            const float hw = 0.5f - 0.5f * std::cos (6.2831853f * (float) p / (float) win);
                            const int k = std::min (len - 1, st + (int) p);
                            yl += freezeL[(size_t) k] * hw; yr += freezeR[(size_t) k] * hw;
                            ph += 1.0;
                        }
                        break;
                    }
                    case Stutter8:
                    case Stutter16:
                    {
                        const int slice = sliceLen (id);
                        const auto& bl = id == Stutter8 ? st8L : st16L; const auto& br = id == Stutter8 ? st8R : st16R;
                        const int pos = (int) std::fmod (f.phase, (double) slice);
                        const float edge = std::min (1.0f, std::min ((float) pos, (float) (slice - pos)) / (0.003f * sr));
                        yl = bl[(size_t) pos] * edge; yr = br[(size_t) pos] * edge;
                        f.phase += 1.0;
                        break;
                    }
                    case Reverse:
                    {
                        // classic reverse delay: two readers run backwards through 300 ms chunks, crossfaded
                        const int chunk = (int) (0.3f * sr);
                        for (int g = 0; g < 2; ++g)
                        {
                            const double ph = std::fmod (f.phase + (g ? chunk / 2 : 0), (double) chunk);
                            const int chunkStart = w - (int) ph - chunk;
                            const float hw = 0.5f - 0.5f * std::cos (6.2831853f * (float) ph / (float) chunk);
                            yl += read (histL, chunkStart + chunk - (int) ph * 1) * hw * 0.9f;
                            yr += read (histR, chunkStart + chunk - (int) ph * 1) * hw * 0.9f;
                        }
                        f.phase += 1.0;
                        wetMix = 0.9f;
                        break;
                    }
                    case TapeStop:
                    {
                        f.rate = std::max (0.0, f.rate - 1.0 / (0.8 * sr));
                        f.readPos += f.rate;
                        const float fade = (float) std::min (1.0, f.rate * 4.0);
                        yl = readF (histL, f.readPos) * fade; yr = readF (histR, f.readPos) * fade;
                        break;
                    }
                    case OctUp:
                    case OctDown:
                        // octave jumps are done by the harmonizer itself (clean, voice character kept)
                        replace = false; wetMix = 0.0f;
                        break;
                    case Cloud:
                    {
                        if (rand01() < 45.0f / sr) spawnGrain();
                        for (auto& g : grains)
                        {
                            if (! g.active) continue;
                            const float t = (float) g.age / (float) g.len;
                            const float env = std::sin (3.14159265f * t);
                            const float s = 0.5f * (readF (histL, g.pos) + readF (histR, g.pos)) * env * env;
                            yl += s * g.gl; yr += s * g.gr;
                            g.pos += g.inc;
                            if (++g.age >= g.len) g.active = false;
                        }
                        replace = false; wetMix = 0.8f;
                        break;
                    }
                    case Robot:
                    {
                        ringPh += 110.0f / sr; if (ringPh >= 1.0f) ringPh -= 1.0f;
                        const float rm = std::sin (6.2831853f * ringPh);
                        const int cd = (int) (sr / 147.0f);
                        const float cl = combL[(size_t) ((combW - cd) & 4095)], cr = combR[(size_t) ((combW - cd) & 4095)];
                        const float il = dl * rm, ir = dr * rm;
                        combL[(size_t) (combW & 4095)] = il + cl * 0.6f; combR[(size_t) (combW & 4095)] = ir + cr * 0.6f;
                        combW = (combW + 1) & 4095;
                        yl = (il + cl) * 0.8f; yr = (ir + cr) * 0.8f;
                        wetMix = 0.85f;
                        break;
                    }
                    case Riser:
                    {
                        // filter sweeps up, an octave layer rises in, plus a little noise
                        const float tt = std::min (1.0f, f.time / 3.0f);
                        const float fc = 80.0f * std::exp2 (tt * 6.5f);
                        const float c = 1.0f - std::exp (-6.2831853f * fc / sr);
                        hp1L += c * (dl - hp1L); hp1R += c * (dr - hp1R);
                        const float al = dl - hp1L, ar = dr - hp1R;
                        hp2L += c * (al - hp2L); hp2R += c * (ar - hp2R);
                        const float bl = al - hp2L, br = ar - hp2R;
                        // the voices bend upwards in the harmonizer at the same time
                        const float nz = (rand01() * 2.0f - 1.0f) * 0.05f * tt;
                        yl = bl * (1.0f + tt) + nz;
                        yr = br * (1.0f + tt) + nz;
                        break;
                    }
                    default: break;
                }

                if (replace) { l += (yl * wetMix - l) * f.wet; r += (yr * wetMix - r) * f.wet; }
                else { l += yl * wetMix * f.wet; r += yr * wetMix * f.wet; }
            }

            L[i] = l; R[i] = r;
            w = (w + 1) & (size - 1);
        }
    }

private:
    struct Slot
    {
        bool on = false; float wet = 0.0f, time = 0.0f;
        int startPos = 0, g0 = 0, g1 = 0; double phase = 0.0, phase2 = 0.0, readPos = 0.0, rate = 1.0;
    };

    // pitch shifter with any ratio: four overlapping Hann taps
    struct Shifter
    {
        std::vector<float> buf; int wp = 0; float d = 0.0f; float win = 2048.0f;
        void prepare (float sampleRate) { buf.assign (16384, 0.0f); wp = 0; d = 0.0f; win = std::max (512.0f, 0.045f * sampleRate); }
        float process (float x, float ratio)
        {
            buf[(size_t) wp] = x;
            d += 1.0f - ratio;                       // delay grows/shrinks with the ratio
            if (d < 0.0f) d += win;
            if (d >= win) d -= win;
            float out = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                float dk = d + win * 0.25f * (float) k; if (dk >= win) dk -= win;
                const float hw = std::sin (3.14159265f * dk / win);
                float pos = (float) wp - dk - 2.0f; if (pos < 0.0f) pos += (float) buf.size();
                const int i0 = (int) pos; const float fr = pos - (float) i0;
                const float a = buf[(size_t) (i0 & 16383)], b = buf[(size_t) ((i0 + 1) & 16383)];
                out += (a + fr * (b - a)) * hw * hw;
            }
            wp = (wp + 1) & 16383;
            return out * 0.5f;
        }
    };

    struct Grain { bool active = false; double pos = 0.0, inc = 1.0; int age = 0, len = 1; float gl = 0.7f, gr = 0.7f; };

    void spawnGrain()
    {
        for (auto& g : grains)
        {
            if (g.active) continue;
            g.len = (int) ((0.06f + rand01() * 0.2f) * sr);
            static const float ratios[5] = { 1.0f, 2.0f, 0.5f, 1.5f, 1.0f };
            g.inc = ratios[(int) (rand01() * 4.99f)];
            g.pos = (double) (w - (int) (rand01() * 1.5f * sr) - (int) (g.len * g.inc) - 8);
            const float p = rand01() * 2.0f - 1.0f;
            g.gl = std::sqrt (0.5f * (1.0f - p)); g.gr = std::sqrt (0.5f * (1.0f + p));
            g.age = 0; g.active = true;
            return;
        }
    }

    int sliceLen (int id) const
    {
        const int s = (int) (60.0f / bpm * (id == Stutter8 ? 0.5f : 0.25f) * sr);
        return std::max (64, std::min (s, (int) st8L.size() - 1));
    }

    float read (const std::vector<float>& h, int pos) const { return h[(size_t) (pos & (size - 1))]; }
    float readF (const std::vector<float>& h, double pos) const
    {
        const double fl = std::floor (pos); const int i0 = (int) fl; const float fr = (float) (pos - fl);
        const float a = h[(size_t) (i0 & (size - 1))], b = h[(size_t) ((i0 + 1) & (size - 1))];
        return a + fr * (b - a);
    }
    float rand01() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }

    float sr = 44100.0f, bpm = 120.0f, fadeC = 0.01f;
    int size = 1, w = 0;
    std::vector<float> histL, histR;
    Slot fx[NumFx];
    Shifter shifters[1];
    Grain grains[24];
    uint32_t rng = 4242u;
    float ringPh = 0.0f; int combW = 0; std::vector<float> combL, combR;
    float hp1L = 0.0f, hp1R = 0.0f, hp2L = 0.0f, hp2R = 0.0f;
    std::vector<float> freezeL, freezeR, st8L, st8R, st16L, st16R;
};
