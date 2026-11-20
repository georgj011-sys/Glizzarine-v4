#pragma once
// Glizzarine Harmonizer engine v2: real-time pitch-synchronous overlap-add (PSOLA) + vocoder layer.
// The singer's voice is analysed (pitch + one mark per vocal cycle). Every held key gets its own
// voice that rebuilds the singing at the key's pitch from those cycles (timbre stays), and the
// same keys play a vocoder synth shaped by the voice. No JUCE dependency, testable offline.
//
// v2: chord glide (voices slide to the nearest note of the next chord), voicing (extra octave
// voices / open spread), humanize, consonants + breath spread over the voices, low-latency mode.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "Vocoder.h"

class HarmonizerEngine
{
public:
    static constexpr int maxVoices = 16;
    enum Voicing { Close = 0, BassDown, TopUp, BassTop, Spread };

    struct Params
    {
        float dry = 0.8f, harmony = 0.9f, formant = 0.0f, consonants = 0.3f;
        float attack = 0.015f, release = 0.6f;       // chord envelope (seconds)
        float width = 0.7f, lowestHz = 80.0f;
        float gate = 0.0001f, sensitivity = 0.7f;   // gate: level below which noise is pulled down; sensitivity 0..1
        float sustain = 4.0f, leveling = 0.5f, subOctave = 0.0f, follow = 0.5f;
        bool  velBright = false; float velBrightAmt = 0.3f;
        bool  velAttack = false; float velAttackAmt = 0.5f;
        bool  velSend   = false; float velSendAmt = 0.5f;
        float vibratoCents = 0.0f, detuneCents = 0.0f, formantMod = 0.0f, harmonyBoost = 1.0f;
        float glide = 0.0f;       // seconds, voices slide to new notes (pad FX)
        bool  chaos = false;      // voices jump octaves at random (pad FX)
        float pitchShift = 0.0f;  // semitones added to all voices (octave pads, riser)
        float blend = 0.0f;       // 0 = harmonizer only, 1 = vocoder only
        float chordGlide = 0.0f;  // seconds; 0 = off. Voices slide to the nearest note of the next chord
        bool  glideLegato = true; // true: only when the new chord comes before the old one is let go
        int   voicing = Close;
        float humanize = 0.0f;    // 0..1
        bool  lowLatency = false;
        Vocoder::Params vocoder;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        inSize = 1 << 15;
        in.assign ((size_t) inSize, 0.0f);
        lp.assign ((size_t) inSize, 0.0f);
        vflag.assign ((size_t) inSize, 0);
        diff.assign (4096, 0.0f);
        marks.clear(); marks.reserve (600);
        n = 0; lastMark = 0;
        t0 = (float) (sr / 150.0);
        voiced = false;
        lpState = lpState2 = 0.0f;
        lpC = 1.0f - std::exp (-2.0f * 3.14159265f * 900.0f / (float) sr);
        setLowest (params.lowestHz, params.lowLatency);
        for (auto& v : voices) resetVoice (v);
        for (auto& a : aux) resetVoice (a);
        hopCount = 0; uvSmooth = 0.0f;
        gateEnv = 0.0f; gateGain = 1.0f;
        gateRel = std::exp (-1.0f / (0.1f * (float) sr));
        gateMsC = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
        gateOpen = true;
        gateAtkC = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
        gateRelC = 1.0f - std::exp (-1.0f / (0.12f * (float) sr));
        for (auto& c : cap) { c.data.assign ((size_t) (2 * 1200 + 4), 0.0f); c.period = 0.0f; }
        capCount = 0; capW = 0; lastVoicedN = -1000000;
        levelSlow = 0.01f; levelGain = 1.0f; vibPhase = 0.0;
        devCents = devSmooth = 0.0f; countGain = 1.0f;
        hpC = 1.0f - std::exp (-2.0f * 3.14159265f * 120.0f / (float) sr);
        smC = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
        // consonants: high band above ~3 kHz, its energy against the whole signal
        consHpC = 1.0f - std::exp (-2.0f * 3.14159265f * std::min (3000.0f, 0.2f * (float) sr) / (float) sr);
        consEnvC = 1.0f - std::exp (-1.0f / (0.008f * (float) sr));
        consLp1 = consLp2 = consHfE = consAllE = 0.0f;
        consBuf.assign (4096, 0.0f); consW = 0;
        vocoder.prepare (sampleRate);
        vocL.assign (8192, 0.0f); vocR.assign (8192, 0.0f); vocIn.assign (8192, 0.0f);
        rng = 98765u;
        qCount = 0; groupId = 0; pedal = false; lastNoteOn = -1; lastChordOn = -1000000;
    }

    void setParams (const Params& p)
    {
        const bool relatency = p.lowestHz != params.lowestHz || p.lowLatency != params.lowLatency;
        params = p;
        if (relatency) setLowest (p.lowestHz, p.lowLatency);
        vocoder.setParams (p.vocoder);
    }
    const Params& getParams() const { return params; }
    void setVelocitySensitivity (float s) { velSens = std::min (1.0f, std::max (0.0f, s)); }
    int voiceNote (int i) const { return (i >= 0 && i < maxVoices && voices[i].active && voices[i].env.out > 0.02f) ? voices[i].note : -1; }
    float bandLevel (int b) const { return vocoder.bandLevel (b); }
    int activeVoices() const { int k = 0; for (auto& v : voices) if (v.active && (v.gate || v.sustained)) ++k; return k; }
    int getLatencySamples() const { return delay; }
    // keys that currently hold a voice (for MIDI out)
    bool noteHeld (int note) const
    {
        for (auto& v : voices) if (v.active && ! v.pending && v.note == note && (v.gate || v.sustained)) return true;
        for (int i = 0; i < qCount; ++i) if (q[i].note == note) return true;
        return false;
    }

    static float panOrder (int k)
    {
        static const float seq[8] = { 0.0f, -1.0f, 1.0f, -0.6f, 0.6f, -0.3f, 0.3f, -0.85f };
        return seq[k & 7];
    }

    // pan < -1.5 = automatic (centre, hard left, hard right, ...); env = nullptr: chord envelope
    // canGlide: the note may take over a voice of the previous chord (chord glide). The arp passes false.
    void noteOn (int note, float velocity, float pan = -2.0f, const EnvShape* env = nullptr, bool accent = false, bool canGlide = true)
    {
        if (note < 0 || note > 127) return;
        const EnvShape shape = shapeFor (env, velocity);

        if (canGlide && params.chordGlide > 0.001f)
        {
            bool sameKey = false;
            for (auto& x : voices) if (x.active && x.note == note && (x.gate || x.sustained)) sameKey = true;
            if (! sameKey)
            {
                if (env == nullptr)
                {
                    // chord: collect the notes that come together (a few ms), then lead the voices over
                    const int64_t win = (int64_t) ((params.lowLatency ? 0.012 : 0.025) * sr);
                    if (qCount == 0)
                    {
                        const bool sameChord = n - lastChordOn < win;   // a note of the chord that just started
                        lastChordOn = n;
                        if (sameChord) { startNote (note, velocity, pan, shape, accent); return; }
                        ++groupId;
                        if (! anyDonor (true)) { startNote (note, velocity, pan, shape, accent); return; }
                        qDeadline = n + win;
                    }
                    else lastChordOn = n;
                    for (int i = 0; i < qCount; ++i) if (q[i].note == note) { q[i].vel = velocity; return; }
                    if (qCount < maxQueue) { q[qCount++] = { note, velocity, pan, shape, accent }; return; }
                    resolveQueue();
                }
                else
                {
                    // pyramid: each entering note takes over the nearest voice that is fading out
                    ++groupId;
                    QNote one { note, velocity, pan, shape, accent };
                    if (lead (&one, 1, false)) return;
                }
            }
        }
        startNote (note, velocity, pan, shape, accent);
    }

    void noteOff (int note)
    {
        for (int i = 0; i < qCount; ++i) if (q[i].note == note) { resolveQueue(); break; }
        for (auto& v : voices)
        {
            if (v.pending && v.pNote == note) { v.pReleased = ! pedal; }
            if (v.active && v.note == note && v.gate)
            {
                v.gate = false; v.sustained = pedal;
                if (! pedal) { v.env.release(); v.hDelay = 0; }
            }
        }
        if (! pedal) vocoder.noteOff (note);   // the vocoder follows the same keys (with the pedal: on pedal up)
    }

    void setSustain (bool down)
    {
        pedal = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustained) { v.sustained = false; v.env.release(); v.hDelay = 0; if (! v.gate) vocoder.noteOff (v.note); }
    }

    void allNotesOff()
    {
        qCount = 0;
        for (auto& v : voices) { v.gate = false; v.sustained = false; v.pending = false; v.hDelay = 0; v.env.release(); }
        vocoder.allOff();
    }

    // mono in; harmonies stereo (harmonizer + vocoder blend), your voice mono (delayed and direct), velocity send
    void process (const float* input, float* harmL, float* harmR, float* dryOut, float* dryDirect, float* sendL, float* sendR, int num)
    {
        const float fShiftBase = std::exp2 ((params.formant + params.formantMod) * 0.5f);
        const float holdC = params.sustain > 0.01f ? std::exp (std::log (0.001f) / (params.sustain * (float) sr)) : 0.0f;
        const float slowC = 1.0f / (1.0f * (float) sr);
        const float glideC = params.glide > 0.001f ? 1.0f - std::exp (-1.0f / (params.glide * (float) sr)) : 0.0015f;
        const float offC = std::max (glideC, params.chordGlide > 0.001f ? 1.0f - std::exp (-1.0f / (0.5f * params.chordGlide * (float) sr)) : 0.0f);
        updateAux();
        int sounding = 0;
        for (auto& v : voices) if (v.active && (v.gate || v.sustained)) ++sounding;
        const float wantCount = std::pow ((float) std::max (1, sounding), -0.4f);
        const float consNorm = 0.9f / std::sqrt ((float) std::max (1, sounding));
        const float hBlend = std::cos (params.blend * 1.5707963f), vBlend = std::sin (params.blend * 1.5707963f);
        const float fsr = (float) sr;
        const float hum = std::min (1.0f, std::max (0.0f, params.humanize));
        const int hop = params.lowLatency ? 128 : 256;
        if ((int) vocIn.size() < num) { vocIn.resize ((size_t) num, 0.0f); }

        for (int i = 0; i < num; ++i)
        {
            if (qCount > 0 && n >= qDeadline) resolveQueue();

            // soft gate: slow and gentle, so it never chops your singing
            const float ax = std::abs (input[i]);
            // level follower (RMS over ~10 ms, so single noise peaks don't hold the gate open)
            gateEnv += (ax * ax - gateEnv) * gateMsC;
            const float lvl = std::sqrt (gateEnv) * 1.41f;
            // hysteresis: opens at the gate level, closes 3 dB below it
            if (lvl >= params.gate) gateOpen = true;
            else if (lvl < params.gate * 0.7f) gateOpen = false;
            const float gr = lvl / std::max (1.0e-7f, params.gate);
            const float gt = gateOpen ? 1.0f : std::max (0.003f, gr * gr * gr);   // closed: steep expander down to -50 dB
            gateGain += (gt - gateGain) * (gt > gateGain ? gateAtkC : gateRelC);
            const float x = input[i] * gateGain;
            in[(size_t) (n & (inSize - 1))] = x;
            lpState += lpC * (x - lpState);
            lpState2 += lpC * (lpState - lpState2);
            lp[(size_t) (n & (inSize - 1))] = lpState2;
            ++n;

            if (++hopCount >= hop) { hopCount = 0; detectPitch(); }
            vflag[(size_t) (n & (inSize - 1))] = voiced ? 1 : 0;
            if (voiced) lastVoicedN = n;
            placeMarks();

            const int64_t t = n - delay;
            const float tIn = in[(size_t) (t & (inSize - 1))];
            countGain += 0.0005f * (wantCount - countGain);
            // chaos pad: while it is held, every voice jumps to a random octave about 4 times a second
            if (params.chaos && ++chaosCount >= (int) (sr * 0.25))
            {
                chaosCount = 0;
                for (auto& cv : voices) if (cv.active) cv.chaosOct = (int) (rand01() * 3.0f) - 1;
            }
            else if (! params.chaos && chaosCount != 0)
            {
                chaosCount = 0;
                for (auto& cv : voices) cv.chaosOct = 0;
            }
            devSmooth += 0.002f * ((voiced ? devCents : 0.0f) - devSmooth);

            if (vflag[(size_t) (t & (inSize - 1))])
                levelSlow += slowC * (std::abs (tIn) * 1.4f - levelSlow);
            if ((n & 31) == 0) wantGainC = std::min (30.0f, std::max (0.3f, std::pow (0.08f / std::max (1.0e-4f, levelSlow), params.leveling)));
            levelGain += 0.00005f * (wantGainC - levelGain);

            vibPhase += 5.5 / sr; if (vibPhase >= 1.0) vibPhase -= 1.0;
            const float vib = (float) std::sin (6.283185307 * vibPhase) * params.vibratoCents + devSmooth * params.follow;

            // consonants and breath: the band above ~3 kHz, stronger when it dominates (S, T, K, breath)
            const float uv = vflag[(size_t) (t & (inSize - 1))] ? 0.0f : 1.0f;
            uvSmooth += (uv - uvSmooth) * 0.01f;
            consLp1 += consHpC * (tIn - consLp1);
            const float h1 = tIn - consLp1;
            consLp2 += consHpC * (h1 - consLp2);
            const float hf = h1 - consLp2;
            consHfE += consEnvC * (hf * hf - consHfE);
            consAllE += consEnvC * (tIn * tIn - consAllE);
            const float hfShare = std::min (1.0f, std::max (0.0f, (consHfE / (consAllE + 1.0e-9f) - 0.04f) * 4.0f));
            const bool singingNearby = (n - lastVoicedN) < (int64_t) (0.3 * sr);
            const float consAmt = singingNearby ? params.consonants * (0.25f + 0.75f * std::max (uvSmooth, hfShare)) : 0.0f;
            consBuf[(size_t) consW] = hf * consAmt * 1.6f;

            float l = 0.0f, r = 0.0f, sl = 0.0f, srr = 0.0f;
            for (int vi = 0; vi < maxVoices + numAux; ++vi)
            {
                const bool main = vi < maxVoices;
                Voice& v = main ? voices[vi] : aux[vi - maxVoices];
                if (! v.active) continue;

                // steal fade, then the new note starts from silence
                if (v.pending)
                {
                    v.fade -= 1.0f / (0.005f * fsr);
                    if (v.fade <= 0.0f)
                    {
                        const bool rel = v.pReleased;
                        v.pending = false; v.pReleased = false;
                        v.env.kill();
                        v.active = false;
                        begin (v, v.pNote, v.pVel, v.pPan, v.pShape, v.pAccent);
                        if (rel) { v.gate = false; v.env.release(); v.hDelay = 0; }
                    }
                }
                // humanize: a voice may come in a few ms late
                if (v.hDelay > 0 && --v.hDelay == 0) v.env.start (v.hShape);

                const float e = v.env.next (fsr);
                if (! v.env.active() && ! v.pending && v.hDelay == 0) { v.active = false; continue; }

                // smoothed per-voice values: nothing ever jumps
                v.velS += (v.vel - v.velS) * smC;
                v.panS += (v.pan - v.panS) * smC;
                v.accent *= 0.9997f;
                v.accentS += (v.accent - v.accentS) * smC;

                // pitch = glide (chord glide / glide pad) + smoothed octave offsets (chaos, octave pads, spread voicing)
                if (v.gPos < 1.0f) v.gPos = std::min (1.0f, v.gPos + v.gStep);
                const float offT = (float) (v.chaosOct + v.voicingOct) * 12.0f + params.pitchShift;
                v.offS += (offT - v.offS) * offC;
                v.glidePitch = basePitch (v) + v.offS;

                float cents = vib + params.detuneCents * (v.haas * 2.0f - 1.0f);
                float hGain = 1.0f;
                if (hum > 0.001f && main)
                {
                    // humanize: slow pitch drift, own vibrato that grows in, slightly different level
                    if (--v.hDriftCount <= 0) { v.hDriftCount = (int) ((0.25f + 0.5f * rand01()) * fsr); v.hDriftT = (rand01() * 2.0f - 1.0f) * 9.0f; }
                    v.hDrift += (v.hDriftT * hum - v.hDrift) * (2.0f / fsr);
                    v.hVibPh += v.hVibRate / fsr; if (v.hVibPh >= 1.0f) v.hVibPh -= 1.0f;
                    v.hAge = std::min (1.0f, v.hAge + 1.0f / (0.6f * fsr));
                    const float sv = std::sin (6.2831853f * v.hVibPh);
                    cents += v.hDrift + sv * v.hVibDepth * hum * v.hAge;
                    hGain = 1.0f + hum * (v.hGain - 1.0f) + hum * 0.06f * sv;
                }

                if (v.nextMark <= (double) t + (double) maxT0)   // only work out the pitch when a grain is due
                {
                    const float targetPeriod = (float) (sr / (440.0 * std::pow (2.0, (v.glidePitch - 69.0) / 12.0 + cents / 1200.0)));
                    int guard = 0;
                    while (v.nextMark <= (double) t + (double) maxT0 && guard++ < 8)
                        emitGrain (v, targetPeriod, fShiftBase, t);
                }

                const size_t idx = (size_t) (t & (olaSize - 1));
                float raw = v.ola[idx];
                v.ola[idx] = 0.0f;
                if (v.live)
                {
                    v.pIn  += 0.00002f * (tIn * tIn - v.pIn);
                    v.pOut += 0.00002f * (raw * raw - v.pOut);
                    const float want = std::sqrt ((v.pIn + 1.0e-10f) / (v.pOut + 1.0e-10f));
                    v.agc += 0.00003f * (std::min (3.0f, std::max (0.5f, want)) - v.agc);
                }
                // hold: back to full smoothly while singing, slow ring-out after you stop
                if (v.ringing) v.hold *= holdC; else v.hold += (1.0f - v.hold) * smC;

                if (params.velBright)
                {
                    const float cutVel = 1200.0f * std::exp2 (3.9f * v.velocity);
                    const float cut = std::min (0.45f * fsr, std::exp ((1.0f - params.velBrightAmt) * std::log (18000.0f) + params.velBrightAmt * std::log (cutVel)));
                    v.lpf += (1.0f - std::exp (-6.2831853f * cut / fsr)) * (raw - v.lpf);
                    raw = v.lpf;
                }
                if (v.auxKind != AuxSub && v.auxKind != AuxLow) { v.hpLp += hpC * (raw - v.hpLp); raw -= v.hpLp * 0.85f; }

                const float lvlA = main ? 1.0f : v.auxKind == AuxSub ? params.subOctave : 0.5f;
                const float fade = v.pending ? std::max (0.0f, v.fade) : 1.0f;
                const float amp = e * (1.0f - velSens * (1.0f - v.velS)) * lvlA * fade * (1.0f + v.accentS * 0.6f) * hGain;
                const float s = raw * v.agc * v.hold * amp;

                const float pan = std::min (1.0f, std::max (-1.0f, v.panS * params.width * (main ? 1.0f : 1.15f)));
                const float pl = std::sqrt (0.5f * (1.0f - pan));
                const float pr = std::sqrt (0.5f * (1.0f + pan));
                // fractional Haas delay: it can glide without skipping samples (no clicks when the pan moves)
                const float hdf = (0.3f + 0.7f * v.haas) * std::abs (pan) * 0.012f * fsr;
                const int hd = std::min (4000, (int) hdf); const float hfr = hdf - (float) hd;
                v.haasBuf[(size_t) (v.haasW & 4095)] = s;
                const float sd = v.haasBuf[(size_t) ((v.haasW - hd) & 4095)] * (1.0f - hfr)
                               + v.haasBuf[(size_t) ((v.haasW - hd - 1) & 4095)] * hfr;
                v.haasW = (v.haasW + 1) & 0xFFFFF;
                float oL = pan < 0.0f ? s * pl : sd * pl;
                float oR = pan < 0.0f ? sd * pr : s * pr;

                // consonants ride on every sounding voice with its own small time offset and place
                if (main && consAmt > 0.0f)
                {
                    const int cd = 8 + (int) ((0.15f + 0.85f * v.haas) * 0.007f * fsr);
                    const float c = consBuf[(size_t) ((consW - std::min (4000, cd)) & 4095)] * e * fade * consNorm;
                    oL += c * pl; oR += c * pr;
                }
                l += oL; r += oR;
                if (params.velSend) { const float g = params.velSendAmt * v.velocity; sl += oL * g; srr += oR * g; }
            }
            consW = (consW + 1) & 4095;

            const float hg = params.harmony * params.harmonyBoost * levelGain * countGain;
            harmL[i] = l * hg * hBlend;
            harmR[i] = r * hg * hBlend;
            sendL[i] = sl * hg;
            sendR[i] = srr * hg;
            if ((n & 31) == 0) dryLvlC = std::pow (std::max (0.3f, levelGain), 0.75f);
            const float dg = params.dry * dryLvlC;
            dryOut[i] = tIn * dg;
            dryDirect[i] = x * dg;
            vocIn[(size_t) i] = tIn * std::min (8.0f, levelGain);   // vocoder hears the leveled, aligned voice
        }

        // vocoder layer (in chunks, any block size is fine)
        if (vBlend > 0.001f)
        {
            int done = 0;
            while (done < num)
            {
                const int len = std::min (num - done, 4096);
                std::fill (vocL.begin(), vocL.begin() + len, 0.0f);
                std::fill (vocR.begin(), vocR.begin() + len, 0.0f);
                vocoder.process (vocIn.data() + done, vocL.data(), vocR.data(), len, devSmooth * params.follow + params.pitchShift * 100.0f);
                const float g = params.harmony * params.harmonyBoost * countGain * vBlend;
                for (int i = 0; i < len; ++i) { harmL[done + i] += vocL[(size_t) i] * g; harmR[done + i] += vocR[(size_t) i] * g; }
                done += len;
            }
        }
    }

    bool isVoiced() const { return voiced; }
    float currentPitchHz() const { return (float) (sr / t0); }
    float getLevelGain() const { return levelGain; }
    float inputLevelAligned() const { return levelSlow; }

private:
    struct Mark { int64_t pos; float period; bool voiced; };
    enum AuxKind { AuxNone = 0, AuxSub, AuxLow, AuxHigh };
    struct Voice
    {
        bool active = false, gate = false, sustained = false, live = false, ringing = false;
        int note = -1; float vel = 1.0f, velS = 1.0f, velocity = 1.0f, pan = 0.0f, panS = 0.0f, haas = 0.0f;
        double nextMark = 0.0; uint64_t age = 0;
        float pIn = 0.0f, pOut = 0.0f, agc = 1.5f, hold = 1.0f, lpf = 0.0f, hpLp = 0.0f, accent = 0.0f, accentS = 0.0f;
        float glidePitch = 60.0f; int chaosOct = 0, voicingOct = 0;
        float gFrom = 60.0f, gTo = 60.0f, gPos = 1.0f, gStep = 0.0f, offS = 0.0f;   // glide (semitones), smoothed offsets
        int capIdx = 0, haasW = 0, group = 0, auxKind = AuxNone;
        // humanize
        float hDrift = 0.0f, hDriftT = 0.0f, hVibPh = 0.0f, hVibRate = 5.5f, hVibDepth = 0.0f, hGain = 1.0f, hAge = 0.0f;
        int hDriftCount = 0, hDelay = 0; EnvShape hShape;
        SmoothEnv env;
        bool pending = false, pReleased = false, pAccent = false; int pNote = 60; float pVel = 1.0f, pPan = 0.0f; EnvShape pShape; float fade = 1.0f;
        std::vector<float> ola, haasBuf;
    };
    struct QNote { int note; float vel; float pan; EnvShape shape; bool accent; };

    static constexpr int olaSize = 1 << 13;
    static constexpr int numAux = 3;
    static constexpr int maxQueue = 24;

    void resetVoice (Voice& v)
    {
        const int kind = v.auxKind;
        v = Voice();
        v.auxKind = kind;
        v.ola.assign ((size_t) olaSize, 0.0f);
        v.haasBuf.assign (4096, 0.0f);
    }

    EnvShape shapeFor (const EnvShape* env, float velocity) const
    {
        EnvShape shape;
        if (env) return *env;
        shape.attack = std::max (0.005f, params.attack) + (params.velAttack ? params.velAttackAmt * 0.6f * (1.0f - velocity) : 0.0f);   // soft key = up to 0.6 s fade-in
        shape.decay = 1.0f; shape.sustain = 1.0f; shape.release = std::max (0.03f, params.release);
        return shape;
    }

    // ---------- glide ----------
    static float basePitch (const Voice& v)
    {
        const float p = v.gPos, s = p * p * (3.0f - 2.0f * p);   // smooth start and landing
        return v.gFrom + (v.gTo - v.gFrom) * s;
    }
    void startGlide (Voice& v, float from, float to, float seconds)
    {
        if (seconds <= 0.001f || std::abs (to - from) < 0.001f) { v.gFrom = v.gTo = to; v.gPos = 1.0f; v.gStep = 0.0f; return; }
        v.gFrom = from; v.gTo = to; v.gPos = 0.0f;
        v.gStep = (float) (1.0 / (std::max (0.001f, seconds) * sr));
    }

    // a voice of an earlier chord that could slide over: still held (or pedal), or fading out (Always mode)
    bool isHeldDonor (const Voice& v) const { return v.active && ! v.pending && v.hDelay == 0 && v.group != groupId && (v.gate || v.sustained); }
    bool isFadingDonor (const Voice& v) const { return v.active && ! v.pending && v.hDelay == 0 && ! v.gate && ! v.sustained && v.env.out > 0.03f; }
    bool anyDonor (bool chord) const
    {
        for (auto& v : voices)
            if ((chord && isHeldDonor (v)) || ((! chord || ! params.glideLegato) && isFadingDonor (v))) return true;
        return false;
    }

    void resolveQueue()
    {
        const int k = qCount;
        qCount = 0;
        if (k <= 0) return;
        if (! lead (q, k, true))
            for (int i = 0; i < k; ++i) startNote (q[i].note, q[i].vel, q[i].pan, q[i].shape, q[i].accent);
    }

    // Voice leading: each note of the new chord takes over the voice of the old chord that is nearest,
    // with the smallest total movement (order-preserving matching, optimal on a line). Notes left over start
    // fresh, voices left over keep their old key (and fade when it is let go).
    // Returns false if there was no voice to lead (caller starts the notes normally).
    bool lead (QNote* notes, int k, bool chord)
    {
        int heldIdx[maxVoices], fadeIdx[maxVoices]; int nh = 0, nf = 0;
        for (int i = 0; i < maxVoices; ++i)
        {
            if (chord && isHeldDonor (voices[i])) heldIdx[nh++] = i;
            else if ((! chord || ! params.glideLegato) && isFadingDonor (voices[i])) fadeIdx[nf++] = i;
        }
        // adding one note to a chord you hold is not a chord change: it gets a voice of its own
        const bool useHeld = nh > 0 && (k >= 2 || nh == 1);
        int pool[maxVoices]; int m = 0;
        if (useHeld) for (int i = 0; i < nh; ++i) pool[m++] = heldIdx[i];
        for (int i = 0; i < nf; ++i) pool[m++] = fadeIdx[i];
        if (m == 0) return false;

        std::sort (pool, pool + m, [this] (int a, int b) { return basePitch (voices[a]) < basePitch (voices[b]); });
        int order[maxQueue]; for (int i = 0; i < k; ++i) order[i] = i;
        std::sort (order, order + k, [notes] (int a, int b) { return notes[a].note < notes[b].note; });

        // dp[i][j]: best cost using the first i voices for the first j notes; the longer list may skip entries
        static constexpr float inf = 1.0e9f;
        float dp[maxVoices + 1][maxQueue + 1];
        for (int i = 0; i <= m; ++i) for (int j = 0; j <= k; ++j) dp[i][j] = inf;
        dp[0][0] = 0.0f;
        for (int i = 0; i <= m; ++i)
            for (int j = 0; j <= k; ++j)
            {
                if (dp[i][j] >= inf) continue;
                if (i < m && j < k)
                {
                    const float c = dp[i][j] + std::abs (basePitch (voices[pool[i]]) - (float) notes[order[j]].note);
                    dp[i + 1][j + 1] = std::min (dp[i + 1][j + 1], c);
                }
                if (i < m && m > k) dp[i + 1][j] = std::min (dp[i + 1][j], dp[i][j]);   // skip a voice
                if (j < k && k > m) dp[i][j + 1] = std::min (dp[i][j + 1], dp[i][j]);   // skip a note
            }
        // walk back
        bool used[maxQueue] {};
        int vi = m, nj = k;
        int pairV[maxQueue], pairN[maxQueue]; int np = 0;
        while (vi > 0 && nj > 0)
        {
            const float c = std::abs (basePitch (voices[pool[vi - 1]]) - (float) notes[order[nj - 1]].note);
            if (std::abs (dp[vi][nj] - (dp[vi - 1][nj - 1] + c)) < 1.0e-3f) { pairV[np] = pool[vi - 1]; pairN[np] = order[nj - 1]; ++np; --vi; --nj; }
            else if (m > k && std::abs (dp[vi][nj] - dp[vi - 1][nj]) < 1.0e-3f) --vi;
            else --nj;
        }
        if (np == 0) return false;
        for (int p = 0; p < np; ++p) { claim (voices[pairV[p]], notes[pairN[p]]); used[pairN[p]] = true; }
        for (int j = 0; j < k; ++j)
            if (! used[j]) startNote (notes[j].note, notes[j].vel, notes[j].pan, notes[j].shape, notes[j].accent);
        return true;
    }

    // a voice of the old chord slides over to a note of the new one (it keeps its place in the stereo field)
    void claim (Voice& v, const QNote& qn)
    {
        const int oldNote = v.note;
        const float from = basePitch (v);
        v.note = qn.note; v.gate = true; v.sustained = false;
        v.vel = qn.vel; v.velocity = qn.vel;
        v.age = ++ageCounter; v.group = groupId; v.accent = 0.0f;
        startGlide (v, from, (float) qn.note, params.chordGlide);
        if (v.env.stage == 3 || v.env.stage == 0) v.env.start (qn.shape);   // was fading: comes back without a new attack
        lastNoteOn = qn.note;
        vocoder.moveNote (oldNote, qn.note, qn.vel, v.pan, qn.shape, params.chordGlide);   // v.pan: the voice's real place (qn.pan may be "automatic")
    }

    void startNote (int note, float velocity, float pan, const EnvShape& shape, bool accent)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.active && x.note == note) v = &x;            // same key again
        if (! v) for (auto& x : voices) if (! x.active) { v = &x; break; }
        bool steal = false;
        if (! v)
        {
            // steal: prefer a releasing voice, then the oldest; it fades out over 5 ms first
            for (auto& x : voices) if (! x.gate && ! x.sustained && (! v || x.age < v->age)) v = &x;
            if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
            steal = true;
        }

        if (pan < -1.5f)
        {
            int k = 0; for (auto& x : voices) if (x.active && &x != v) ++k;
            pan = panOrder (k);
        }

        if (steal || (v->active && v->note != note))
        {
            v->pending = true; v->pNote = note; v->pVel = velocity; v->pPan = pan; v->pShape = shape; v->pAccent = accent;
            v->fade = 1.0f;
            return;
        }
        begin (*v, note, velocity, pan, shape, accent);
    }

    void startVoice (Voice& v)
    {
        v.pIn = v.pOut = 0.0f; v.agc = 1.5f; v.hold = 1.0f; v.live = false; v.ringing = false; v.capIdx = 0;
        v.lpf = 0.0f; v.hpLp = 0.0f; v.chaosOct = 0; v.hDelay = 0;
        v.nextMark = (double) (n - delay);
        std::fill (v.ola.begin(), v.ola.end(), 0.0f);
        std::fill (v.haasBuf.begin(), v.haasBuf.end(), 0.0f);
    }

    void begin (Voice& v, int note, float velocity, float pan, const EnvShape& shape, bool accent)
    {
        const bool fresh = ! v.active;
        if (fresh) { startVoice (v); v.panS = pan; }
        else if (v.env.out < 0.05f) v.panS = pan;   // almost silent: jump to the new place instead of sliding through the stereo field
        const int prevNote = v.note;
        v.active = true; v.note = note; v.gate = true; v.sustained = false;
        v.velocity = velocity; v.vel = velocity;
        if (fresh) v.velS = velocity;
        v.age = ++ageCounter; v.group = groupId;
        v.pan = pan;
        if (fresh) v.haas = (float) ((v.age * 7919u) % 100u) / 100.0f;
        // glide pad: slide in from the previous note
        if (params.glide > 0.001f && prevNote >= 0 && ! fresh) startGlide (v, basePitch (v), (float) note, params.glide);
        else if (params.glide > 0.001f && lastNoteOn >= 0) startGlide (v, (float) lastNoteOn, (float) note, params.glide);
        else startGlide (v, (float) note, (float) note, 0.0f);
        v.chaosOct = params.chaos ? (int) (rand01() * 3.0f) - 1 : 0;
        if (fresh) v.offS = (float) (v.chaosOct + v.voicingOct) * 12.0f + params.pitchShift;
        v.accent = accent ? 1.0f : 0.0f;
        // humanize: every voice gets its own little character
        v.hGain = 1.0f + 0.3f * (rand01() * 2.0f - 1.0f);
        v.hVibRate = 4.6f + 1.6f * rand01();
        v.hVibDepth = 6.0f + 10.0f * rand01();
        v.hVibPh = rand01(); v.hAge = 0.0f; v.hDriftCount = 0;
        if (fresh) { v.hDrift = 0.0f; v.hDriftT = 0.0f; }
        const int hd = params.humanize > 0.001f && fresh ? (int) (rand01() * params.humanize * 0.03f * sr) : 0;
        if (hd > 0) { v.hDelay = hd; v.hShape = shape; }
        else v.env.start (shape);
        lastNoteOn = note;
        vocoder.noteOn (note, velocity, pan, shape);
    }

    // extra voices: octave down (knob), voicing doubles (bass an octave down, top an octave up) and spread
    void updateAux()
    {
        int lowest = 1000, highest = -1; float velLo = 0.0f, velHi = 0.0f;
        int order[maxVoices]; int cnt = 0;
        for (int i = 0; i < maxVoices; ++i)
        {
            auto& v = voices[i];
            if (! (v.active && ! v.pending && (v.gate || v.sustained))) continue;
            order[cnt++] = i;
            if (v.note < lowest) { lowest = v.note; velLo = v.velocity; }
            if (v.note > highest) { highest = v.note; velHi = v.velocity; }
        }
        // spread (open voicing): every second voice from the top goes down an octave
        std::sort (order, order + cnt, [this] (int a, int b) { return voices[a].note > voices[b].note; });
        for (int j = 0; j < cnt; ++j) voices[order[j]].voicingOct = 0;   // fading voices keep where they were
        if (params.voicing == Spread && cnt >= 2)
            for (int j = 1; j < cnt; j += 2) voices[order[j]].voicingOct = -1;

        const bool any = cnt > 0;
        const float auxGlide = params.chordGlide > 0.001f ? params.chordGlide : 0.03f;
        auto drive = [&] (Voice& a, int kind, bool want, int note, float vel, float pan, float haas)
        {
            a.auxKind = kind;
            if (want)
            {
                note = std::min (127, std::max (0, note));
                if (! a.active)
                {
                    startVoice (a); a.active = true;
                    EnvShape s; s.attack = 0.05f; s.release = std::max (0.05f, params.release); a.env.start (s);
                    startGlide (a, (float) note, (float) note, 0.0f); a.offS = params.pitchShift;
                    a.panS = pan;
                }
                else if (a.env.stage == 3) { EnvShape s = a.env.shape; a.env.start (s); }
                if (a.note != note && a.note >= 0) startGlide (a, basePitch (a), (float) note, auxGlide);
                a.note = note; a.gate = true; a.sustained = false;
                a.velocity = vel; a.vel = vel; a.pan = pan; a.haas = haas;
            }
            else if (a.active && a.gate) { a.gate = false; a.env.release(); }
        };
        drive (aux[0], AuxSub, params.subOctave > 0.001f && any, lowest - 12, velLo, 0.0f, 0.0f);
        drive (aux[1], AuxLow, (params.voicing == BassDown || params.voicing == BassTop) && any, lowest - 12, velLo, -0.85f, 0.55f);
        drive (aux[2], AuxHigh, (params.voicing == TopUp || params.voicing == BassTop) && any, highest + 12, velHi, 0.85f, 0.75f);
    }

    void setLowest (float hz, bool lowLat)
    {
        params.lowestHz = hz;
        params.lowLatency = lowLat;
        maxT0 = (int) std::ceil (sr / std::max (50.0f, hz));
        const int old = delay;
        // normal: 1.6 periods of the lowest note; low latency: one period (grains come from a cycle earlier)
        delay = lowLat ? maxT0 + 24 : (int) (maxT0 * 1.6f) + 32;
        if (old != delay)
            for (int vi = 0; vi < maxVoices + numAux; ++vi)
            {
                Voice& v = vi < maxVoices ? voices[vi] : aux[vi - maxVoices];
                if (! v.ola.empty()) std::fill (v.ola.begin(), v.ola.end(), 0.0f);   // nothing stale pops up after the jump
                v.nextMark = (double) (n - delay);
            }
    }

    float at (int64_t p) const { return in[(size_t) (p & (inSize - 1))]; }
    float atLp (int64_t p) const { return lp[(size_t) (p & (inSize - 1))]; }

    // YIN pitch detection on the newest samples.
    // Coarse pass on every 4th lag (8x less work, no CPU spikes), then a fine search around the result.
    void detectPitch()
    {
        const int W = params.lowLatency ? 512 : 768;
        const int dec = std::max (1, (int) (sr / 12000.0));
        const int tauMin = std::max (2, (int) (sr / 1000.0)), tauMax = maxT0;
        if (n < W + tauMax + 2 * dec + 2) return;
        const int64_t start = n - W - tauMax;
        const int64_t base = start + tauMax;
        double energy = 0.0;
        for (int j = 0; j < W; j += 2) { const float s = atLp (base + j); energy += s * s; }
        const float rms = (float) std::sqrt (energy / (W / 2));

        const int qMax = tauMax / dec;
        if ((int) diff.size() < qMax + 2) diff.resize ((size_t) qMax + 2, 0.0f);
        std::vector<float>& d = diff;
        float running = 0.0f;
        d[0] = 1.0f;
        for (int q = 1; q <= qMax; ++q)
        {
            const int tau = q * dec;
            double sum = 0.0;
            for (int j = 0; j < W; j += dec) { const float a = atLp (base + j) - atLp (base + j - tau); sum += a * a; }
            running += (float) sum;
            d[(size_t) q] = (float) sum * (float) q / std::max (1.0e-12f, running);
        }
        const float s = std::min (1.0f, std::max (0.0f, params.sensitivity));
        const float absThr = 0.1f + 0.15f * s;
        const int qMin = std::max (1, tauMin / dec);
        int best = -1; float bestVal = 1.0f;
        for (int q = qMin; q < qMax; ++q)
            if (d[(size_t) q] < absThr && d[(size_t) q] < d[(size_t) q - 1] && d[(size_t) q] <= d[(size_t) q + 1])
            { best = q; bestVal = d[(size_t) q]; break; }
        if (best < 0)
            for (int q = qMin; q < qMax; ++q)
                if (d[(size_t) q] < bestVal) { bestVal = d[(size_t) q]; best = q; }

        // sensitivity: higher = quieter and breathier singing still counts
        const float enter = 0.3f + 0.35f * s, stay = std::min (0.9f, enter + 0.15f);
        const float loudThr = std::pow (10.0f, (-30.0f - 60.0f * s) / 20.0f);
        const bool periodic = best > 0 && bestVal < (voiced ? stay : enter);
        const bool loud = rms > loudThr;
        unvoicedHops = (periodic && loud) ? 0 : unvoicedHops + 1;
        const int hopsPerVoice = params.lowLatency ? 2 : 1;
        const bool isVoice = (periodic && loud) || (voiced && loud && unvoicedHops < hopsPerVoice * (1 + (int) (s * 4.0f)));
        if (isVoice && periodic)
        {
            // fine search around the coarse lag
            const int centre = best * dec;
            int bt = centre; double bv = 1.0e30, vals[64] {};
            const int lo = std::max (2, centre - dec), hi = std::min ({ tauMax, centre + dec, lo + 62 });
            for (int tau = lo; tau <= hi && tau - lo < 64; ++tau)
            {
                double sum = 0.0;
                for (int j = 0; j < W; j += 2) { const float a = atLp (base + j) - atLp (base + j - tau); sum += a * a; }
                vals[tau - lo] = sum;
                if (sum < bv) { bv = sum; bt = tau; }
            }
            float tau = (float) bt;
            if (bt > lo && bt < hi)
            {
                const double a = vals[bt - 1 - lo], b = vals[bt - lo], c = vals[bt + 1 - lo];
                const double den = a - 2.0 * b + c;
                if (std::abs (den) > 1.0e-12) tau += (float) (0.5 * (a - c) / den);
            }
            tau = std::max (2.0f, tau);
            const bool jump = voiced && std::abs (tau - t0) > 0.2f * t0;
            if (jump && std::abs (tau - pendingTau) > 0.1f * tau) { pendingTau = tau; }
            else { t0 = voiced && ! jump ? 0.6f * t0 + 0.4f * tau : tau; pendingTau = 0.0f; }
            const double semis = 69.0 + 12.0 * std::log2 ((sr / t0) / 440.0);
            devCents = (float) ((semis - std::round (semis)) * 100.0);
        }
        voiced = isVoice;
    }

    // one analysis mark per vocal cycle, placed on the cycle's peak
    void placeMarks()
    {
        const float period = voiced ? t0 : 256.0f;
        const int64_t search = (int64_t) (period * 0.25f);
        if (n < lastMark + (int64_t) period + search + 1) return;
        int64_t m = lastMark + (int64_t) period;
        if (voiced)
        {
            int64_t bestP = m; float bestV = -1.0e9f;
            for (int64_t p = m - search; p <= m + search; ++p)
            { const float v = atLp (p); if (v > bestV) { bestV = v; bestP = p; } }
            m = bestP;
        }
        // keep copies of the last few sung cycles, so held notes can ring on after you stop
        if (! marks.empty() && marks.back().voiced)
        {
            const Mark& pm = marks.back();
            const int half = std::min (1200, (int) pm.period);
            if (pm.pos + half + 2 <= n)
            {
                auto& c = cap[(size_t) (capW++ & 3)];
                for (int k = -half; k <= half + 1; ++k) c.data[(size_t) (k + half)] = at (pm.pos + k);
                c.period = (float) half;
                capCount = std::min (4, capCount + 1);
            }
        }
        marks.push_back ({ m, period, voiced });
        lastMark = m;
        while (marks.size() > 512) marks.erase (marks.begin());
    }

    // t = current output position: grain parts before it are already played and must not be written
    // (they would sit in the ring buffer and pop up as a click one buffer length later)
    void emitGrain (Voice& v, float targetPeriod, float fShift, int64_t t)
    {
        const Mark* src = nullptr; double bestDist = 1.0e18;
        for (auto it = marks.rbegin(); it != marks.rend(); ++it)
        {
            if (it->pos + (int64_t) maxT0 + 2 > n) continue;
            const double dd = std::abs ((double) it->pos - (double) v.nextMark);
            if (dd < bestDist) { bestDist = dd; src = &*it; }
            if ((double) it->pos < v.nextMark - 2.0 * maxT0) break;
        }

        const bool liveOk = src != nullptr && src->voiced && bestDist < (params.lowLatency ? 2.6 : 2.0) * maxT0;
        const int64_t centre = (int64_t) std::llround (v.nextMark);
        v.ringing = false;

        if (liveOk)
        {
            v.live = true;
            const float P = src->period;
            const int half = std::max (2, std::min (maxT0, std::max ((int) (P / fShift), (int) (0.6f * targetPeriod))));
            const float gain = std::min (2.0f, targetPeriod / (float) half);
            for (int k = std::max (-half, (int) (t - centre)); k <= half; ++k)
            {
                const double sp = (double) src->pos + (double) k * fShift;
                const int64_t s0 = (int64_t) std::floor (sp);
                if (s0 + 1 >= n) break;   // never read samples that have not arrived yet
                const float w = 0.5f + 0.5f * std::cos (3.14159265f * (float) k / (float) half);
                const float fr = (float) (sp - (double) s0);
                const float s = at (s0) + fr * (at (s0 + 1) - at (s0));
                v.ola[(size_t) ((centre + k) & (olaSize - 1))] += s * w * gain;
            }
        }
        else
        {
            v.live = false;
            const bool longGap = (n - lastVoicedN) > (int64_t) (0.15 * sr);
            if (longGap && params.sustain > 0.01f && capCount > 0 && v.hold > 1.0e-4f)
            {
                v.ringing = true;
                const auto& c = cap[(size_t) (v.capIdx++ % capCount)];
                const float P = c.period;
                const int half = std::max (2, std::min (maxT0, (int) (P / fShift)));
                const float gain = std::min (2.0f, targetPeriod / (float) half);
                for (int k = std::max (-half, (int) (t - centre)); k <= half; ++k)
                {
                    const float w = 0.5f + 0.5f * std::cos (3.14159265f * (float) k / (float) half);
                    const double sp = (double) P + (double) k * fShift;
                    const int s0 = (int) std::floor (sp); const float fr = (float) (sp - (double) s0);
                    if (s0 < 0 || s0 + 1 >= (int) c.data.size()) continue;
                    const float s = c.data[(size_t) s0] + fr * (c.data[(size_t) s0 + 1] - c.data[(size_t) s0]);
                    v.ola[(size_t) ((centre + k) & (olaSize - 1))] += s * w * gain;
                }
            }
        }
        v.nextMark += std::max (1.0f, targetPeriod);
    }

    float rand01() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }

    Params params;
    double sr = 44100.0;
    std::vector<float> in, lp, diff;
    std::vector<uint8_t> vflag;
    float uvSmooth = 0.0f;
    int inSize = 0, hopCount = 0, maxT0 = 600, delay = 1000;
    float wantGainC = 1.0f, dryLvlC = 1.0f;
    int64_t n = 0, lastMark = 0;
    float t0 = 300.0f, lpState = 0.0f, lpState2 = 0.0f, lpC = 0.12f;
    bool voiced = false, pedal = false;
    int unvoicedHops = 0;
    float pendingTau = 0.0f;
    float gateEnv = 0.0f, gateGain = 1.0f, gateRel = 0.999f, gateAtkC = 0.5f, gateRelC = 0.001f, gateMsC = 0.002f;
    bool gateOpen = true;
    int chaosCount = 0;
    struct Captured { std::vector<float> data; float period = 0.0f; };
    Captured cap[4];
    int capCount = 0; uint32_t capW = 0;
    int64_t lastVoicedN = -1000000;
    float levelSlow = 0.01f, levelGain = 1.0f;
    double vibPhase = 0.0;
    float devCents = 0.0f, devSmooth = 0.0f, countGain = 1.0f, hpC = 0.02f, smC = 0.01f;
    float consHpC = 0.3f, consEnvC = 0.003f, consLp1 = 0.0f, consLp2 = 0.0f, consHfE = 0.0f, consAllE = 0.0f;
    std::vector<float> consBuf; int consW = 0;
    std::vector<Mark> marks;
    Voice voices[maxVoices];
    Voice aux[numAux];
    QNote q[maxQueue]; int qCount = 0; int64_t qDeadline = 0, lastChordOn = -1000000; int groupId = 0;
    uint64_t ageCounter = 0;
    float velSens = 0.6f;
    int lastNoteOn = -1;
    uint32_t rng = 98765u;
    Vocoder vocoder;
    std::vector<float> vocL, vocR, vocIn;
};
