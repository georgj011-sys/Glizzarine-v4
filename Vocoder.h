#pragma once
// Vocoder layer: the keys play a synth, your voice shapes it.
// 20 band-pass filters measure how loud each frequency region of your voice is; the same 20
// bands of the synth are turned up and down to match. Works with any level of singing,
// even whispering, because it needs no pitch. No JUCE dependency.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

struct EnvShape
{
    float attack = 0.015f, decay = 1.0f, sustain = 1.0f, release = 0.6f;
};

// click-free ADSR: linear attack, exponential decay/release, plus a 3 ms smoother on the output
struct SmoothEnv
{
    int stage = 0;           // 0 idle, 1 attack, 2 decay/sustain, 3 release
    float value = 0.0f, out = 0.0f;
    EnvShape shape;

    void start (const EnvShape& s) { shape = s; stage = 1; }
    void release() { if (stage != 0) stage = 3; }
    void kill() { stage = 0; value = out = 0.0f; }
    bool active() const { return stage != 0 || out > 1.0e-5f; }

    float next (float sr)
    {
        switch (stage)
        {
            case 1:
                value += 1.0f / (std::max (0.005f, shape.attack) * sr);
                if (value >= 1.0f) { value = 1.0f; stage = 2; }
                break;
            case 2:
            {
                const float c = 1.0f - std::exp (-4.6f / (std::max (0.01f, shape.decay) * sr));
                value += (shape.sustain - value) * c;
                break;
            }
            case 3:
            {
                const float c = 1.0f - std::exp (-4.6f / (std::max (0.03f, shape.release) * sr));
                value -= value * c;
                if (value < 1.0e-5f) { value = 0.0f; stage = 0; }
                break;
            }
            default: break;
        }
        out += (value - out) * smoothC (sr);
        return out;
    }

    static float smoothC (float sr) { return 1.0f - std::exp (-1.0f / (0.003f * sr)); }
};

class Vocoder
{
public:
    static constexpr int numBands = 20, maxVoices = 16;
    float bandLevel (int b) const { return (b >= 0 && b < numBands) ? envs[b] : 0.0f; }
    enum Carrier { SawPad = 0, Strings, Glass, Hollow, NoiseChoir };

    struct Params
    {
        int carrier = SawPad;
        float shift = 0.0f;       // -1 .. +1, formant shift in bands
        float sibilance = 0.4f;   // S and T from your voice added on top
        float response = 0.3f;    // 0 fast (talky) .. 1 slow (smooth)
        float width = 0.7f;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (int b = 0; b < numBands; ++b)
        {
            const float f = 120.0f * std::pow (7000.0f / 120.0f, (float) b / (float) (numBands - 1));
            design (bandCoef[b], f, 5.0f, sr);
            modState[b] = {}; carL[b] = {}; carR[b] = {};
            envs[b] = 0.0f;
        }
        for (auto& v : voices) v = Voice();
        sibHp = sibLp = 0.0f; rng = 12345u;
        stealCounter = 0;
    }

    void setParams (const Params& p) { params = p; }

    void noteOn (int note, float vel, float pan, const EnvShape& env)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.env.active() && x.note == note) v = &x;
        if (! v) for (auto& x : voices) if (! x.env.active()) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
        if (! v->env.active()) { v->phase[0] = rand01(); v->phase[1] = rand01(); v->phase[2] = rand01(); v->panS = pan; v->velS = vel; }
        pan = std::min (1.0f, std::max (-1.0f, pan));
        if (! v->env.active()) v->panS = pan;
        v->note = note; v->vel = vel; v->pan = pan; v->age = ++stealCounter;
        v->gFrom = v->gTo = (float) note; v->gPos = 1.0f;
        v->env.start (env);
    }

    // chord glide: the voice playing 'from' slides to 'to' (and comes back if it was fading)
    void moveNote (int from, int to, float vel, float pan, const EnvShape& env, float glideSec)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.env.active() && x.note == from) { v = &x; break; }
        if (v == nullptr) { noteOn (to, vel, pan, env); return; }
        const float p = v->gPos, s = p * p * (3.0f - 2.0f * p);
        v->gFrom = v->gFrom + (v->gTo - v->gFrom) * s;
        v->gTo = (float) to; v->gPos = 0.0f;
        v->gStep = 1.0f / (std::max (0.001f, glideSec) * sr);
        v->note = to; v->vel = vel; v->age = ++stealCounter;
        if (v->env.stage == 3 || v->env.stage == 0) v->env.start (env);
    }

    void noteOff (int note) { for (auto& v : voices) if (v.note == note && v.env.stage != 3) v.env.release(); }
    void allOff() { for (auto& v : voices) v.env.release(); }

    // modulator = your voice (mono); adds the vocoder output to outL / outR
    void process (const float* modulator, float* outL, float* outR, int n, float glideCents)
    {
        const float envAtk = 1.0f - std::exp (-1.0f / (0.002f * sr));
        const float envRel = 1.0f - std::exp (-1.0f / ((0.012f + params.response * 0.12f) * sr));
        const float sm = SmoothEnv::smoothC (sr);
        const int shiftBands = (int) std::lround (params.shift * 4.0f);
        const float sibC = 1.0f - std::exp (-2.0f * 3.14159265f * 5000.0f / sr);

        for (int i = 0; i < n; ++i)
        {
            // carrier: all playing synth voices
            float cl = 0.0f, cr = 0.0f;
            for (auto& v : voices)
            {
                if (! v.env.active()) continue;
                const float e = v.env.next (sr);
                v.velS += (v.vel - v.velS) * sm;
                v.panS += (v.pan - v.panS) * sm;
                if (v.gPos < 1.0f) v.gPos = std::min (1.0f, v.gPos + v.gStep);
                const float gp = v.gPos * v.gPos * (3.0f - 2.0f * v.gPos);
                const float pitch = v.gFrom + (v.gTo - v.gFrom) * gp;
                const float hz = 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f + glideCents / 1200.0f);
                const float s = osc (v, hz) * e * (0.35f + 0.65f * v.velS);
                const float p = std::min (1.0f, std::max (-1.0f, v.panS * params.width));
                cl += s * std::sqrt (0.5f * (1.0f - p));
                cr += s * std::sqrt (0.5f * (1.0f + p));
            }

            // analysis of your voice and synthesis
            const float m = modulator[i];
            float yl = 0.0f, yr = 0.0f;
            for (int b = 0; b < numBands; ++b)
            {
                const float mb = filt (bandCoef[b], modState[b], m);
                const float a = std::abs (mb);
                envs[b] += (a - envs[b]) * (a > envs[b] ? envAtk : envRel);
            }
            for (int b = 0; b < numBands; ++b)
            {
                const int src = std::min (numBands - 1, std::max (0, b - shiftBands));
                const float g = envs[src] * 3.5f;
                yl += filt (bandCoef[b], carL[b], cl) * g;
                yr += filt (bandCoef[b], carR[b], cr) * g;
            }

            // sibilance: the very top of your voice (S, T, breath) goes straight through
            sibLp += sibC * (m - sibLp);
            sibGate += ((anyActive ? 1.0f : 0.0f) - sibGate) * sm;
            const float sib = (m - sibLp) * params.sibilance * sibGate;
            outL[i] += yl + sib;
            outR[i] += yr + sib;
        }
        anyActive = false;
        for (auto& v : voices) if (v.env.active()) { anyActive = true; break; }
    }

private:
    struct Coef { float b0, b2, a1, a2; };
    struct State { float z1 = 0.0f, z2 = 0.0f; };
    struct Voice
    {
        int note = 60; float vel = 1.0f, velS = 1.0f, pan = 0.0f, panS = 0.0f; uint64_t age = 0;
        float phase[3] {}; float pw = 0.0f;
        float gFrom = 60.0f, gTo = 60.0f, gPos = 1.0f, gStep = 0.0f;   // chord glide
        SmoothEnv env;
    };

    static void design (Coef& c, float f, float q, float sampleRate)
    {
        // RBJ band-pass, 0 dB peak
        const float w = 2.0f * 3.14159265f * f / sampleRate;
        const float alpha = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        c.b0 = alpha / a0; c.b2 = -alpha / a0;
        c.a1 = -2.0f * std::cos (w) / a0; c.a2 = (1.0f - alpha) / a0;
    }

    float filt (const Coef& c, State& s, float x) const
    {
        const float y = c.b0 * x + s.z1;
        s.z1 = -c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    float rand01() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }

    static float blepSaw (float t, float dt)
    {
        float v = 2.0f * t - 1.0f;
        if (t < dt) { const float x = t / dt; v -= x + x - x * x - 1.0f; }
        else if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; v -= x * x + x + x + 1.0f; }
        return v;
    }

    float osc (Voice& v, float hz)
    {
        const float dt = std::min (0.45f, hz / sr);
        auto adv = [] (float& p, float d) { p += d; if (p >= 1.0f) p -= 1.0f; };
        float s = 0.0f;
        switch (params.carrier)
        {
            case Strings:
            {
                const float d2 = dt * 1.007f, d3 = dt * 0.993f;
                s = (blepSaw (v.phase[0], dt) + blepSaw (v.phase[1], d2) + blepSaw (v.phase[2], d3)) * 0.4f;
                adv (v.phase[0], dt); adv (v.phase[1], d2); adv (v.phase[2], d3);
                break;
            }
            case Glass:
            {
                s = std::sin (6.2831853f * v.phase[0]) * 0.8f + std::sin (6.2831853f * v.phase[1]) * 0.35f
                  + std::sin (6.2831853f * v.phase[2]) * 0.15f;
                adv (v.phase[0], dt); adv (v.phase[1], std::min (0.45f, dt * 2.0f)); adv (v.phase[2], std::min (0.45f, dt * 3.0f));
                // a sine has little to shape in the upper bands, so add a soft buzz
                s += blepSaw (v.phase[0], dt) * 0.15f;
                break;
            }
            case Hollow:
            {
                v.pw += 0.25f / sr; if (v.pw >= 1.0f) v.pw -= 1.0f;
                const float width = 0.3f + 0.15f * std::sin (6.2831853f * v.pw);
                float t2 = v.phase[0] + width; if (t2 >= 1.0f) t2 -= 1.0f;
                s = (blepSaw (v.phase[0], dt) - blepSaw (t2, dt)) * 0.6f;
                adv (v.phase[0], dt);
                break;
            }
            case NoiseChoir:
            {
                const float d2 = dt * 1.004f;
                s = (blepSaw (v.phase[0], dt) + blepSaw (v.phase[1], d2)) * 0.35f + (rand01() * 2.0f - 1.0f) * 0.35f;
                adv (v.phase[0], dt); adv (v.phase[1], d2);
                break;
            }
            case SawPad:
            default:
            {
                const float d2 = dt * 1.004f;
                s = (blepSaw (v.phase[0], dt) + blepSaw (v.phase[1], d2)) * 0.5f;
                adv (v.phase[0], dt); adv (v.phase[1], d2);
                break;
            }
        }
        return s;
    }

private:
    Params params;
    float sr = 44100.0f;
    Coef bandCoef[numBands] {};
    State modState[numBands], carL[numBands], carR[numBands];
    float envs[numBands] {};
    Voice voices[maxVoices];
    float sibHp = 0.0f, sibLp = 0.0f;
    uint32_t rng = 12345u;
    uint64_t stealCounter = 0;
    bool anyActive = false;
    float sibGate = 0.0f;
};
