#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const float lowestNoteHz[] = { 78.0f, 104.0f, 140.0f, 185.0f };   // a little below E2, A2, D3, G3
    const double arpRateBeats[] = { 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
    const double delaySyncBeats[] = { 0.0, 1.0, 0.5, 0.75, 0.25, 2.0 / 3.0 };
    const double tremRateBeats[] = { 1.0, 0.5, 0.25, 1.0 / 3.0 };

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

// Replaces anything broken (NaN, infinity, absurd peaks) with silence.
// Returns true if something was broken, so the stage that caused it can be reset.
static bool sanitize (float* x, int n)
{
    bool bad = false;
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (x[i]) || std::abs (x[i]) > 1000.0f) { x[i] = 0.0f; bad = true; }
    return bad;
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add = [&p] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit = "")
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                            AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto unit = [] { return NormalisableRange<float> (0.0f, 1.0f); };
    auto toggle = [&p] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto choice = [&p] (const char* id, const char* name, StringArray items, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    // MAIN
    add ("blend",       "Harmonizer / Vocoder", unit(), 0.0f);
    add ("dry",         "Solo Voice",    unit(), 0.0f);
    add ("harmony",     "Harmonies",     NormalisableRange<float> (0.0f, 1.5f), 0.9f);
    add ("tone",        "Filter",        skewed (200.0f, 20000.0f, 2500.0f), 20000.0f, "Hz");
    toggle ("tone_on",  "Filter On", true);
    add ("tone_res",    "Filter Resonance", unit(), 0.25f);

    // VOICE
    add ("input",       "Input Gain",    NormalisableRange<float> (-12.0f, 36.0f), 12.0f, "dB");
    add ("sens",        "Sensitivity",   NormalisableRange<float> (0.0f, 100.0f), 75.0f, "%");
    add ("gate",        "Noise Gate",    NormalisableRange<float> (-80.0f, -20.0f), -80.0f, "dB");
    add ("leveling",    "Leveling",      unit(), 0.3f);
    add ("formant",     "Formant",       NormalisableRange<float> (-1.0f, 1.0f), 0.0f);
    add ("consonants",  "Consonants",    unit(), 0.45f);
    add ("follow",      "Follow Pitch",  unit(), 0.6f);
    add ("warmth",      "Warmth",        unit(), 0.15f);
    add ("suboct",      "Octave Down",   unit(), 0.0f);
    add ("attack",      "Attack",        skewed (5.0f, 800.0f, 40.0f), 15.0f, "ms");
    add ("release",     "Release",       skewed (0.03f, 6.0f, 0.8f), 0.6f, "s");
    add ("sustain",     "Sustain",       skewed (0.0f, 10.0f, 3.0f), 4.0f, "s");
    add ("width",       "Width",         unit(), 0.8f);
    choice ("lowest",   "Lowest Sung Note", StringArray { "E2 (bass)", "A2", "D3", "G3 (high voice)" }, 0);
    choice ("dry_timing", "Solo Voice Timing", StringArray { "Direct (live)", "Aligned" }, 0);

    // V2: choir
    add ("chord_glide", "Chord Glide",   skewed (0.0f, 2.0f, 0.35f), 0.0f, "s");
    choice ("glide_mode", "Glide Mode",  StringArray { "Always", "Legato" }, 1);
    choice ("voicing",  "Voicing",       StringArray { "Close", "Bass Down", "Top Up", "Bass + Top", "Spread" }, 0);
    add ("humanize",    "Humanize",      unit(), 0.0f);
    // V2: setup
    choice ("latency_mode", "Latency",   StringArray { "Normal", "Low Latency" }, 0);
    choice ("midi_out", "MIDI Out",      StringArray { "Off", "Melody", "Melody + Harmonies" }, 0);

    // VOCODER
    choice ("voc_type", "Vocoder Sound", StringArray { "Saw Pad", "Strings", "Glass", "Hollow Pulse", "Noise Choir" }, 0);
    add ("voc_shift", "Vocoder Formant", NormalisableRange<float> (-1.0f, 1.0f), 0.0f);
    add ("voc_sib",   "Vocoder Sibilance", unit(), 0.4f);
    add ("voc_resp",  "Vocoder Smoothness", unit(), 0.3f);
    add ("voc_width", "Vocoder Width", unit(), 0.7f);

    // PYRAMID / ARP
    choice ("arp_mode",  "Arp Mode",     StringArray { "Off", "Pyramid", "Arp" }, 0);
    choice ("arp_order", "Arp Order",    StringArray { "Up", "Down", "Up-Down", "Down-Up", "Crawl", "Outside-In", "Inside-Out", "Random", "As played" }, 0);
    choice ("arp_rate",  "Arp Rate",     StringArray { "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/16 T" }, 1);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "arp_steps", 1 }, "Arp Steps",
                 NormalisableRange<float> (0.0f, 16.0f, 1.0f), 0.0f,
                 AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("All") : String ((int) v); })
                                                .withValueFromStringFunction ([] (const String& t) { return t.trim().equalsIgnoreCase ("all") ? 0.0f : t.getFloatValue(); })));
    add ("arp_gate",     "Arp Gate",     NormalisableRange<float> (0.05f, 1.0f), 0.6f);
    add ("arp_oct",      "Arp Octaves",  NormalisableRange<float> (1.0f, 3.0f, 1.0f), 1.0f);
    choice ("arp_style", "Arp Sound",    StringArray { "Pluck", "Legato", "Swell" }, 0);
    add ("arp_release",  "Arp Release",  skewed (0.03f, 3.0f, 0.3f), 0.2f, "s");
    add ("arp_spread",   "Arp Spread",   unit(), 0.7f);
    choice ("tempo_src", "Tempo Source", StringArray { "Host", "Own" }, 0);
    add ("bpm",          "Own Tempo",    NormalisableRange<float> (40.0f, 240.0f, 0.1f), 100.0f, "BPM");

    // EFFECTS (on/off switches are bypasses; the mix knobs at 0 also mean off)
    toggle ("chorus_on", "Chorus On", true);   add ("chorus_mix", "Chorus Mix", unit(), 0.2f);
    add ("chorus_rate", "Chorus Rate", skewed (0.05f, 5.0f, 0.6f), 0.6f, "Hz"); add ("chorus_depth", "Chorus Depth", unit(), 0.35f);
    toggle ("phaser_on", "Phaser On", true);   add ("phaser_mix", "Phaser Mix", unit(), 0.0f);
    add ("phaser_rate", "Phaser Rate", skewed (0.02f, 5.0f, 0.4f), 0.2f, "Hz"); add ("phaser_depth", "Phaser Depth", unit(), 0.6f);
    toggle ("wah_on", "Envelope Filter On", true); add ("wah_mix", "Envelope Filter", unit(), 0.0f);
    add ("wah_sens", "Envelope Sensitivity", unit(), 0.5f); add ("wah_range", "Envelope Range", unit(), 0.6f);
    toggle ("trem_on", "Tremolo On", false);   add ("trem_depth", "Tremolo Depth", unit(), 0.5f);
    choice ("trem_rate", "Tremolo Rate", StringArray { "1/4", "1/8", "1/16", "1/8 T" }, 1);
    add ("trem_shape", "Tremolo Shape", unit(), 0.0f);
    toggle ("lofi_on", "Lo-Fi On", false);     add ("lofi_mix", "Lo-Fi Mix", unit(), 0.5f);
    add ("lofi_amount", "Lo-Fi Amount", unit(), 0.4f);
    add ("lofi_bw", "Lo-Fi Bandwidth", unit(), 0.55f);
    toggle ("delay_on", "Delay On", true);     add ("delay_mix", "Delay Mix", unit(), 0.0f);
    choice ("delay_sync", "Delay Sync", StringArray { "Free", "1/4", "1/8", "1/8 dotted", "1/16", "1/4 T" }, 3);
    add ("delay_time", "Delay Time", skewed (40.0f, 1500.0f, 375.0f), 375.0f, "ms");
    add ("feedback", "Delay Feedback", NormalisableRange<float> (0.0f, 0.9f), 0.4f);
    toggle ("reverb_on", "Reverb On", true);   add ("reverb", "Reverb Mix", unit(), 0.3f);
    add ("size", "Room Size", unit(), 0.8f);
    add ("rev_damp", "Reverb Damping", unit(), 0.4f);
    add ("rev_pre", "Reverb Pre-delay", NormalisableRange<float> (0.0f, 100.0f), 25.0f, "ms");
    toggle ("shimmer_on", "Shimmer On", true); add ("shimmer", "Shimmer", unit(), 0.0f);
    choice ("shim_int", "Shimmer Interval", StringArray { "+12", "+7", "+19", "+24", "-12" }, 0);
    add ("shim_decay", "Shimmer Decay", unit(), 0.5f);
    add ("shim_tone", "Shimmer Tone", unit(), 0.5f);
    add ("output", "Output", NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB");

    // PADS
    toggle ("pad_latch", "Pads Latch", false);
    add ("glide_time", "Glide Time", skewed (0.02f, 1.5f, 0.2f), 0.25f, "s");

    // AFTERTOUCH
    toggle ("at_on", "Aftertouch On", true);
    add ("at_response", "Aftertouch Response", skewed (0.005f, 1.0f, 0.1f), 0.08f, "s");
    toggle ("at_swell_on", "AT Swell On", true);      add ("at_swell", "AT Swell", unit(), 0.6f);
    toggle ("at_shimmer_on", "AT Shimmer On", true);  add ("at_shimmer", "AT Shimmer", unit(), 0.5f);
    toggle ("at_reverb_on", "AT Reverb On", true);    add ("at_reverb", "AT Reverb", unit(), 0.4f);
    toggle ("at_filter_on", "AT Filter On", false);   add ("at_filter", "AT Filter", unit(), 0.6f);
    toggle ("at_vib_on", "AT Vibrato On", false);     add ("at_vib", "AT Vibrato", unit(), 0.4f);
    toggle ("at_detune_on", "AT Detune On", false);   add ("at_detune", "AT Detune", unit(), 0.4f);
    toggle ("at_formant_on", "AT Formant On", false); add ("at_formant", "AT Formant", NormalisableRange<float> (-1.0f, 1.0f), 0.5f);

    // VELOCITY
    add ("velocity", "Velocity To Level", unit(), 0.6f);
    add ("vel_curve", "Velocity Curve", unit(), 0.5f);
    toggle ("vel_bright_on", "Velocity Brightness On", false);  add ("vel_bright", "Velocity Brightness", unit(), 0.3f);
    toggle ("vel_attack_on", "Velocity Soft Attack On", false); add ("vel_attack", "Velocity Soft Attack", unit(), 0.5f);
    toggle ("vel_send_on", "Velocity Echo Send On", false);   add ("vel_send", "Velocity Echo Send", unit(), 0.5f);

    return { p.begin(), p.end() };
}

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Voice", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& c : ccMap) c = -1;
    startTimerHz (10);
    taps.reserve (8);
    for (int i = 0; i < PadFx::Count; ++i)
    {
        padMap[(size_t) i] = 10 * 128 + 36 + i;   // default: pads on MIDI channel 10, notes C1 upwards
        padOn[(size_t) i] = false;
        uiPadRequest[(size_t) i] = 0;
    }
}

bool HarmonizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void HarmonizerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    {
        HarmonizerEngine::Params ep;
        ep.lowestHz = lowestNoteHz[juce::jlimit (0, 3, (int) val ("lowest"))];
        ep.lowLatency = (int) val ("latency_mode") == 1;
        engine.setParams (ep);
    }
    engine.prepare (sampleRate);
    seq.setSampleRate (sampleRate);
    seq.allOff();
    lastLowest = -1.0f; lastWarmth = -1.0f;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxChunk, 2 };
    chorus.prepare (spec); chorus.setCentreDelay (9.0f); chorus.setFeedback (0.0f);
    phaser.prepare (spec); phaser.setCentreFrequency (900.0f); phaser.setFeedback (0.45f);
    atFilter.prepare (spec); atFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass); atFilter.setResonance (0.9f);
    toneFilter.prepare (spec); toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass); toneFilter.setResonance (0.75f);
    juce::dsp::ProcessSpec mspec { sampleRate, (juce::uint32) maxChunk, 1 };
    warmL.prepare (mspec); warmR.prepare (mspec);
    wah.prepare (sampleRate, maxChunk);
    lofi.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate, maxChunk);
    weird.prepare (sampleRate);
    lastLowLat = engine.getParams().lowLatency;
    limiter.prepare (sampleRate, lastLowLat ? 0.0008 : 0.0015);
    lastLowest = engine.getParams().lowestHz;
    latencySamples = engine.getLatencySamples() + limiter.latency();
    setLatencySamples (latencySamples.load());

    for (auto* v : { &mono, &hL, &hR, &dryV, &dryD, &sL, &sR }) v->assign ((size_t) maxChunk, 0.0f);
    events.reserve (1024);
    outGain.reset (sampleRate, 0.05);
    inGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val ("output")));
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val ("input")));
    aftertouch = atSmooth = 0.0f;
    tremPhase = 0.0; toneSm = val ("tone");
    for (auto& p : padOn) p = false;
    midiOut.ensureSize (4096);
    midiFlush = true;
}

//==============================================================================
// MIDI Out: the sung melody (channel 1) and the harmony notes (channel 2)
void HarmonizerProcessor::flushMidiOut (int samplePos)
{
    if (melodyNote >= 0) midiOut.addEvent (juce::MidiMessage::noteOff (1, melodyNote), samplePos);
    for (int k = 0; k < 128; ++k) if (harmOn[(size_t) k]) midiOut.addEvent (juce::MidiMessage::noteOff (2, k), samplePos);
    harmOn.reset();
    melodyNote = -1; melodyCand = -1; melodyCandSamples = melodyQuietSamples = 0;
}

void HarmonizerProcessor::updateMidiOut (int pos, int offset)
{
    const int elapsed = juce::jmax (0, pos - midiOutLast);
    midiOutLast = pos;
    const int at = offset + juce::jmax (0, pos - 1);
    const int mode = juce::jlimit (0, 2, (int) val ("midi_out"));
    if (mode != lastMidiOutMode) { flushMidiOut (at); lastMidiOutMode = mode; }
    if (mode == 0) return;

    // melody: the detected note, with hysteresis (no flutter between neighbours) and a short hold over gaps
    if (engine.isVoiced())
    {
        melodyQuietSamples = 0;
        const float semis = 69.0f + 12.0f * std::log2 (juce::jmax (20.0f, engine.currentPitchHz()) / 440.0f);
        if (melodyNote >= 0 && std::abs (semis - (float) melodyNote) < 0.8f) melodyCand = -1;
        else
        {
            const int cand = juce::jlimit (0, 127, (int) std::lround (semis));
            if (cand == melodyCand) melodyCandSamples += elapsed; else { melodyCand = cand; melodyCandSamples = elapsed; }
            if (melodyCandSamples >= (int) (0.035 * currentSampleRate))
            {
                if (melodyNote >= 0) midiOut.addEvent (juce::MidiMessage::noteOff (1, melodyNote), at);
                const float lvl = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (inputLevel.load(), -60.0f) + 48.0f) / 48.0f);
                midiOut.addEvent (juce::MidiMessage::noteOn (1, cand, (juce::uint8) juce::jlimit (20, 127, (int) (30 + 97 * lvl))), at);
                melodyNote = cand; melodyCand = -1;
            }
        }
    }
    else if (melodyNote >= 0)
    {
        melodyQuietSamples += elapsed;
        if (melodyQuietSamples >= (int) (0.08 * currentSampleRate)) { midiOut.addEvent (juce::MidiMessage::noteOff (1, melodyNote), at); melodyNote = -1; }
    }

    // harmonies: whatever the voices play (keys, pedal, arp, pyramid, chord glide)
    for (int k = 0; k < 128; ++k)
    {
        const bool held = mode == 2 && engine.noteHeld (k);
        if (held != harmOn[(size_t) k])
        {
            harmOn[(size_t) k] = held;
            midiOut.addEvent (held ? juce::MidiMessage::noteOn (2, k, (juce::uint8) 100) : juce::MidiMessage::noteOff (2, k), at);
        }
    }
}

void HarmonizerProcessor::timerCallback()
{
    // message thread: hosts must not be told about latency changes from inside processBlock
    const int lat = latencySamples.load();
    if (lat > 0 && lat != getLatencySamples()) setLatencySamples (lat);
}

double HarmonizerProcessor::currentBpm()
{
    if ((int) val ("tempo_src") == 0)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    if (*b > 20.0) return *b;
    return (double) val ("bpm");
}

//==============================================================================
void HarmonizerProcessor::startCcLearn (const juce::String& paramID)
{
    const auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]))
            if (rp->paramID == paramID) { ccLearn = i; return; }
}

void HarmonizerProcessor::forgetCc (const juce::String& paramID)
{
    const auto& params = getParameters();
    for (auto& c : ccMap)
    {
        const int idx = c.load();
        if (idx >= 0 && idx < params.size())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[idx]))
                if (rp->paramID == paramID) c = -1;
    }
}

int HarmonizerProcessor::ccFor (const juce::String& paramID) const
{
    const auto& params = getParameters();
    for (int cc = 0; cc < 128; ++cc)
    {
        const int idx = ccMap[(size_t) cc].load();
        if (idx >= 0 && idx < params.size())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[idx]))
                if (rp->paramID == paramID) return cc;
    }
    return -1;
}

juce::String HarmonizerProcessor::padLabel (int fx) const
{
    const int code = padMap[(size_t) fx].load();
    if (code < 0) return "-";
    return juce::MidiMessage::getMidiNoteName (code % 128, true, true, 3) + "  ch " + juce::String (code / 128);
}

void HarmonizerProcessor::setPad (int fx, bool onState)
{
    padOn[(size_t) fx] = onState;
    switch (fx)
    {
        case PadFx::Freeze:    weird.set (WeirdFX::Freeze, onState); break;
        case PadFx::Stutter8:  weird.set (WeirdFX::Stutter8, onState); break;
        case PadFx::Stutter16: weird.set (WeirdFX::Stutter16, onState); break;
        case PadFx::Reverse:   weird.set (WeirdFX::Reverse, onState); break;
        case PadFx::TapeStop:  weird.set (WeirdFX::TapeStop, onState); break;
        case PadFx::Cloud:     weird.set (WeirdFX::Cloud, onState); break;
        case PadFx::Robot:     weird.set (WeirdFX::Robot, onState); break;
        case PadFx::Riser:     weird.set (WeirdFX::Riser, onState); break;
        default: break;   // octaves, glide, chaos are read by the harmonizer each block
    }
}

void HarmonizerProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 64) { seq.setPedal (m.getControllerValue() >= 64); return; }
        if (cc == 120 || cc == 123) { seq.allOff(); return; }
        const int learning = ccLearn.exchange (-1);
        if (learning >= 0)
        {
            for (auto& c : ccMap) if (c.load() == learning) c = -1;
            ccMap[(size_t) cc] = learning;
        }
        const int idx = ccMap[(size_t) cc].load();
        const auto& params = getParameters();
        if (idx >= 0 && idx < params.size())
            params[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        return;
    }
    if (m.isChannelPressure()) { aftertouch = (float) m.getChannelPressureValue() / 127.0f; return; }
    if (m.isAftertouch()) { aftertouch = juce::jmax (aftertouch * 0.98f, (float) m.getAfterTouchValue() / 127.0f); return; }

    if (m.isNoteOn() || m.isNoteOff())
    {
        const int code = m.getChannel() * 128 + m.getNoteNumber();
        // pad learn: the next pad/key you press is assigned and not played
        if (m.isNoteOn())
        {
            const int learning = padLearn.exchange (-1);
            if (learning >= 0)
            {
                for (auto& p : padMap) if (p.load() == code) p = -1;
                padMap[(size_t) learning] = code;
                return;
            }
        }
        for (int fx = 0; fx < PadFx::Count; ++fx)
            if (padMap[(size_t) fx].load() == code)
            {
                if (on ("pad_latch")) { if (m.isNoteOn()) setPad (fx, ! padOn[(size_t) fx].load()); }
                else setPad (fx, m.isNoteOn());
                return;
            }
        if (m.isNoteOn())
        {
            // curve: left = soft hits count more, middle = linear, right = you have to hit harder
            const float c = val ("vel_curve");
            const float v = std::pow (juce::jlimit (0.0f, 1.0f, m.getFloatVelocity()), std::pow (4.0f, c - 0.5f));
            lastVelocity = m.getFloatVelocity();
            seq.keyDown (m.getNoteNumber(), v);
        }
        else seq.keyUp (m.getNoteNumber());
    }
}

//==============================================================================
void HarmonizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = juce::jmin (2, getTotalNumInputChannels());
    if (n <= 0 || buffer.getNumChannels() < 2) return;

    // tap tempo
    while (tapRequests.load() > 0)
    {
        --tapRequests;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastTapMs > 2000.0) taps.clear();
        if (lastTapMs > 0.0 && now - lastTapMs <= 2000.0) taps.push_back (now - lastTapMs);
        lastTapMs = now;
        if (taps.size() > 4) taps.erase (taps.begin());
        if (! taps.empty())
        {
            double avg = 0.0; for (auto t : taps) avg += t; avg /= (double) taps.size();
            const float b = (float) juce::jlimit (40.0, 240.0, 60000.0 / avg);
            if (auto* prm = apvts.getParameter ("bpm")) prm->setValueNotifyingHost (prm->convertTo0to1 (b));
            if (auto* src = apvts.getParameter ("tempo_src")) src->setValueNotifyingHost (1.0f);
        }
    }
    for (int i = 0; i < PadFx::Count; ++i)
    {
        const int r = uiPadRequest[(size_t) i].exchange (0);
        if (r == 1) setPad (i, true); else if (r == 2) setPad (i, false);
    }

    midiOut.clear();
    if (midiFlush.exchange (false)) flushMidiOut (0);
    bpmNow = currentBpm();
    bpmShown = (float) bpmNow;

    // copy the input pointers first: in a stereo track the outputs are the same buffers
    const float* inputs[2] = { buffer.getReadPointer (0), inCh > 1 ? buffer.getReadPointer (1) : buffer.getReadPointer (0) };
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int start = 0; start < n; start += maxChunk)
    {
        const int len = juce::jmin (maxChunk, n - start);
        const float* in2[2] = { inputs[0] + start, inputs[1] + start };
        processChunk (L + start, R + start, in2, juce::jmax (1, inCh), len, midi, start);
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    midi.swapWith (midiOut);   // what goes out: only our own notes (MIDI Out), never the keyboard
    midiOut.clear();

    detectedHz = engine.isVoiced() ? engine.currentPitchHz() : 0.0f;
    singing = engine.isVoiced();
    heldVoices = engine.activeVoices();
    {
        int k = 0;
        for (int i = 0; i < HarmonizerEngine::maxVoices && k < 8; ++i)
            if (const int nn = engine.voiceNote (i); nn >= 0) voiceNotes[(size_t) k++] = nn;
        for (; k < 8; ++k) voiceNotes[(size_t) k] = -1;
        for (int b = 0; b < 20; ++b) bandLevels[(size_t) b] = engine.bandLevel (b);
    }
}

void HarmonizerProcessor::processChunk (float* L, float* R, const float* const* inputs, int inCh, int n,
                                        const juce::MidiBuffer& midi, int offset)
{
    const bool atOn = on ("at_on");
    const float atCoeff = 1.0f - std::exp (-(float) n / (val ("at_response") * (float) currentSampleRate));
    const float at = atOn ? atSmooth : 0.0f;

    // ----- engine parameters -----
    HarmonizerEngine::Params hp;
    hp.dry = val ("dry");
    hp.harmony = val ("harmony");
    hp.blend = val ("blend");
    hp.leveling = val ("leveling");
    hp.formant = val ("formant");
    hp.consonants = val ("consonants");
    hp.follow = val ("follow");
    hp.attack = val ("attack") * 0.001f;
    hp.release = val ("release");
    hp.sustain = val ("sustain");
    hp.width = val ("width");
    hp.subOctave = val ("suboct");
    hp.lowestHz = lowestNoteHz[juce::jlimit (0, 3, (int) val ("lowest"))];
    hp.gate = juce::Decibels::decibelsToGain (val ("gate"));
    hp.sensitivity = val ("sens") * 0.01f;
    hp.velBright = on ("vel_bright_on"); hp.velBrightAmt = val ("vel_bright");
    hp.velAttack = on ("vel_attack_on"); hp.velAttackAmt = val ("vel_attack");
    hp.velSend = on ("vel_send_on");     hp.velSendAmt = val ("vel_send");
    hp.harmonyBoost = 1.0f + (on ("at_swell_on") ? at * val ("at_swell") * 1.8f : 0.0f);
    hp.vibratoCents = on ("at_vib_on") ? at * val ("at_vib") * 45.0f : 0.0f;
    hp.detuneCents = on ("at_detune_on") ? at * val ("at_detune") * 30.0f : 0.0f;
    hp.formantMod = on ("at_formant_on") ? at * val ("at_formant") : 0.0f;
    hp.glide = padOn[PadFx::Glide].load() ? val ("glide_time") : 0.0f;
    hp.chaos = padOn[PadFx::Chaos].load();
    hp.pitchShift = (padOn[PadFx::OctUp].load() ? 12.0f : 0.0f) + (padOn[PadFx::OctDown].load() ? -12.0f : 0.0f)
                  + weird.riserAmount() * 7.0f;
    hp.chordGlide = val ("chord_glide");
    hp.glideLegato = (int) val ("glide_mode") == 1;
    hp.voicing = juce::jlimit (0, 4, (int) val ("voicing"));
    hp.humanize = val ("humanize");
    hp.lowLatency = (int) val ("latency_mode") == 1;
    hp.vocoder.carrier = (int) val ("voc_type");
    hp.vocoder.shift = val ("voc_shift");
    hp.vocoder.sibilance = val ("voc_sib");
    hp.vocoder.response = val ("voc_resp");
    hp.vocoder.width = val ("voc_width");
    engine.setParams (hp);
    engine.setVelocitySensitivity (val ("velocity"));
    if (hp.lowLatency != lastLowLat)
    {
        lastLowLat = hp.lowLatency;
        limiter.prepare (currentSampleRate, hp.lowLatency ? 0.0008 : 0.0015);
        lastLowest = -1.0f;
    }
    if (hp.lowestHz != lastLowest)
    {
        lastLowest = hp.lowestHz;
        latencySamples = engine.getLatencySamples() + limiter.latency();   // the timer passes it on to the host
    }

    seq.setMode ((int) val ("arp_mode"));
    seq.setOrder ((int) val ("arp_order"));
    seq.setSteps ((int) val ("arp_steps"));
    seq.setGate (val ("arp_gate"));
    seq.setOctaves ((int) val ("arp_oct"));
    seq.setStepSamples (arpRateBeats[juce::jlimit (0, 5, (int) val ("arp_rate"))] * 60.0 / bpmNow * currentSampleRate);
    seq.setStyle ((int) val ("arp_style"), val ("arp_release"), val ("arp_spread"));
    weird.setTempo (bpmNow);

    inGain.setTargetValue (juce::Decibels::decibelsToGain (val ("input")));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (val ("output")));

    // ----- mono voice input -----
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float s = inputs[0][i];
        if (inCh > 1) s = 0.5f * (s + inputs[1][i]);
        s *= inGain.getNextValue();
        if (! std::isfinite (s)) s = 0.0f;
        mono[(size_t) i] = juce::jlimit (-4.0f, 4.0f, s);
        peak = juce::jmax (peak, std::abs (s));
    }
    inputLevel = juce::jmax (peak, inputLevel.load() * 0.9f);

    // ----- render, split at MIDI events and sequencer steps -----
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end > pos)
        {
            const int len = end - pos;
            engine.process (mono.data() + pos, hL.data() + pos, hR.data() + pos, dryV.data() + pos,
                            dryD.data() + pos, sL.data() + pos, sR.data() + pos, len);
            seq.advance (len);
        }
        pos = end;
    };

    events.clear();
    for (const auto meta : midi)
        if (meta.samplePosition >= offset && meta.samplePosition < offset + n)
            events.emplace_back (meta.samplePosition - offset, meta.getMessage());

    size_t ev = 0;
    int guard = 0;
    midiOutLast = 0;
    while (pos < n && guard++ < 4096)
    {
        const int nextMidi = ev < events.size() ? juce::jlimit (pos, n, events[ev].first) : n;
        const int nextSeq = juce::jlimit (pos, n, pos + seq.samplesToNextEvent());
        renderTo (juce::jmin (nextMidi, nextSeq));
        while (ev < events.size() && events[ev].first <= pos) handleMidi (events[ev++].second);
        seq.fire();
        updateMidiOut (pos, offset);
    }
    if (pos < n) renderTo (n);
    while (ev < events.size()) handleMidi (events[ev++].second);
    updateMidiOut (n, offset);

    // self-healing: a single broken sample must never kill the sound for good
    {
        bool bad = false;
        for (auto* v : { &hL, &hR, &dryV, &dryD, &sL, &sR }) bad |= sanitize (v->data(), n);
        if (bad) { engine.prepare (currentSampleRate); seq.allOff(); ++healCount; ++healStage[0]; }
    }

    atSmooth += (aftertouch - atSmooth) * atCoeff;
    aftertouchShown = atOn ? atSmooth : 0.0f;

    // ----- harmonies: warmth, envelope filter, filters -----
    const float nyq = juce::jmin (20000.0f, 0.45f * (float) currentSampleRate);   // filters above Nyquist blow up (22 kHz projects)
    const float warmth = val ("warmth");
    if (std::abs (warmth - lastWarmth) > 0.001f)
    {
        lastWarmth = warmth;
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowShelf (currentSampleRate, 180.0, 0.7f,
                                                                    juce::Decibels::decibelsToGain (warmth * 9.0f));
        warmL.coefficients = c; warmR.coefficients = c;
    }
    if (warmth > 0.001f)
        for (int i = 0; i < n; ++i) { hL[(size_t) i] = warmL.processSample (hL[(size_t) i]); hR[(size_t) i] = warmR.processSample (hR[(size_t) i]); }

    if (on ("wah_on") && val ("wah_mix") > 0.001f)
        wah.process (hL.data(), hR.data(), mono.data(), n, val ("wah_mix"), val ("wah_sens"), val ("wah_range"));

    {
        const float target = on ("tone_on") ? val ("tone") : 20000.0f;
        toneFilter.setResonance (0.6f + val ("tone_res") * 3.4f);
        if (target < 19500.0f || toneSm < 19500.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                if ((i & 15) == 0) { toneSm += (target - toneSm) * 0.05f; toneFilter.setCutoffFrequency (juce::jlimit (60.0f, nyq, toneSm)); }
                hL[(size_t) i] = toneFilter.processSample (0, hL[(size_t) i]);
                hR[(size_t) i] = toneFilter.processSample (1, hR[(size_t) i]);
            }
        }
    }

    if (on ("at_filter_on") && atOn)
    {
        const float amt = val ("at_filter");
        const float cutAt = 500.0f * std::exp2 (5.3f * at);
        const float cut = std::exp ((1.0f - amt) * std::log (19000.0f) + amt * std::log (cutAt));
        atFilter.setCutoffFrequency (juce::jlimit (60.0f, nyq, cut));
        for (int i = 0; i < n; ++i) { hL[(size_t) i] = atFilter.processSample (0, hL[(size_t) i]); hR[(size_t) i] = atFilter.processSample (1, hR[(size_t) i]); }
    }

    // ----- harmonies only: motion effects (your voice stays clear) -----
    {
        float* chans[2] = { hL.data(), hR.data() };
        juce::dsp::AudioBlock<float> hblock (chans, 2, (size_t) n);
        if (on ("chorus_on") && val ("chorus_mix") > 0.001f)
        {
            chorus.setRate (val ("chorus_rate")); chorus.setDepth (val ("chorus_depth")); chorus.setMix (val ("chorus_mix") * 0.5f);
            chorus.process (juce::dsp::ProcessContextReplacing<float> (hblock));
        }
        if (on ("phaser_on") && val ("phaser_mix") > 0.001f)
        {
            phaser.setRate (val ("phaser_rate")); phaser.setDepth (val ("phaser_depth")); phaser.setMix (val ("phaser_mix"));
            phaser.process (juce::dsp::ProcessContextReplacing<float> (hblock));
        }
        if (on ("trem_on"))
        {
            const double hz = bpmNow / 60.0 / tremRateBeats[juce::jlimit (0, 3, (int) val ("trem_rate"))];
            const float depth = val ("trem_depth");
            const float shape = val ("trem_shape");
            for (int i = 0; i < n; ++i)
            {
                tremPhase += hz / currentSampleRate; if (tremPhase >= 1.0) tremPhase -= 1.0;
                const float sm = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * tremPhase);
                const float k = 1.0f + shape * 14.0f;   // shape: 0 = soft sine, 1 = almost square (still click-free)
                const float sq = 0.5f + 0.5f * std::tanh ((sm - 0.5f) * k) / std::tanh (0.5f * k);
                const float g = 1.0f - depth * sq;
                hL[(size_t) i] *= g; hR[(size_t) i] *= g;
            }
        }
        if (on ("lofi_on")) lofi.process (hL.data(), hR.data(), n, val ("lofi_mix"), val ("lofi_amount"), val ("lofi_bw"));
    }

    if (sanitize (hL.data(), n) | sanitize (hR.data(), n))
    {
        chorus.reset(); phaser.reset(); toneFilter.reset(); atFilter.reset(); warmL.reset(); warmR.reset();
        wah.prepare (currentSampleRate, maxChunk); lofi.prepare (currentSampleRate); ++healCount; ++healStage[1];
    }

    // ----- mix: harmonies + your voice -----
    const float* solo = (int) val ("dry_timing") == 0 ? dryD.data() : dryV.data();
    for (int i = 0; i < n; ++i) { L[i] = hL[(size_t) i] + solo[i]; R[i] = hR[(size_t) i] + solo[i]; }

    // ----- pads: weird effects on the whole mix -----
    if (weird.anyActive())
    {
        weird.process (L, R, n);
        if (sanitize (L, n) | sanitize (R, n)) { weird.prepare (currentSampleRate); ++healCount; ++healStage[2]; }
    }

    // ----- space -----
    {
        const int ds = juce::jlimit (0, 5, (int) val ("delay_sync"));
        const float timeMs = ds == 0 ? val ("delay_time") : (float) (delaySyncBeats[ds] * 60000.0 / bpmNow);
        const float mix = on ("delay_on") ? val ("delay_mix") * 0.7f : 0.0f;
        if (mix > 0.0f || hp.velSend)
        {
            delay.process (L, R, sL.data(), sR.data(), n, mix, timeMs, val ("feedback"));
            if (sanitize (L, n) | sanitize (R, n)) { delay.clear(); ++healCount; ++healStage[3]; }
        }
    }
    {
        float rvMix = on ("reverb_on") ? val ("reverb") : 0.0f;
        float shim = on ("shimmer_on") ? val ("shimmer") : 0.0f;
        if (atOn && on ("at_reverb_on")) rvMix = juce::jmin (1.0f, rvMix + at * val ("at_reverb"));
        if (atOn && on ("at_shimmer_on")) shim = juce::jmin (1.0f, shim + at * val ("at_shimmer"));
        static const float ratios[] = { 2.0f, 1.4983f, 2.9966f, 4.0f, 0.5f };
        reverb.process (L, R, n, rvMix * 0.8f, val ("size"), shim, val ("rev_damp"), val ("rev_pre"),
                        ratios[juce::jlimit (0, 4, (int) val ("shim_int"))], val ("shim_decay"), val ("shim_tone"));
        if (sanitize (L, n) | sanitize (R, n)) { reverb.prepare (currentSampleRate, maxChunk); ++healCount; ++healStage[4]; }
    }

    // ----- output: gain, clean look-ahead limiter -----
    for (int i = 0; i < n; ++i) { const float g = outGain.getNextValue(); L[i] *= g; R[i] *= g; }
    if (sanitize (L, n) | sanitize (R, n)) { limiter.prepare (currentSampleRate, lastLowLat ? 0.0008 : 0.0015); ++healCount; ++healStage[5]; }
    limiter.process (L, R, n, 0.85f);
    for (int i = 0; i < n; ++i)
    {
        L[i] = std::isfinite (L[i]) ? juce::jlimit (-1.0f, 1.0f, L[i]) : 0.0f;
        R[i] = std::isfinite (R[i]) ? juce::jlimit (-1.0f, 1.0f, R[i]) : 0.0f;
    }
}

//==============================================================================
void HarmonizerProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) harmPresets().size())) return;
    currentPreset = index;
    userPresetName = {};
    const auto& pr = harmPresets()[(size_t) index];
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (isSetupParam (rp->paramID)) continue;
            float plain = rp->convertFrom0to1 (rp->getDefaultValue());
            for (auto& kv : pr.values) if (rp->paramID == kv.first) plain = kv.second;
            rp->setValueNotifyingHost (rp->convertTo0to1 (plain));
        }
}

//==============================================================================
// Your own presets: Documents/Glizzarine Presets/<name>.glzpreset (XML, sound parameters only)
juce::File HarmonizerProcessor::userPresetFolder()
{
    const auto testDir = juce::SystemStats::getEnvironmentVariable ("GLZH_PRESET_DIR", {});   // the tests use their own folder
    if (testDir.isNotEmpty()) return juce::File (testDir);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Glizzarine Presets");
}

juce::File HarmonizerProcessor::userPresetFile (const juce::String& name)
{
    return userPresetFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".glzpreset");
}

juce::StringArray HarmonizerProcessor::userPresetNames()
{
    juce::StringArray names;
    for (const auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.glzpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool HarmonizerProcessor::saveUserPreset (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    juce::XmlElement xml ("GlizzarinePreset");
    xml.setAttribute ("name", name.trim());
    xml.setAttribute ("version", GLZH_VERSION);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (isSetupParam (rp->paramID)) continue;
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->paramID);
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }
    const auto file = userPresetFile (name);
    if (! userPresetFolder().createDirectory()) return false;
    if (! xml.writeTo (file)) return false;
    userPresetName = file.getFileNameWithoutExtension();
    return true;
}

bool HarmonizerProcessor::loadUserPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (userPresetFile (name));
    if (xml == nullptr || ! xml->hasTagName ("GlizzarinePreset")) return false;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (isSetupParam (rp->paramID)) continue;
            float plain = rp->convertFrom0to1 (rp->getDefaultValue());   // missing in the file (older version) = default
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                if (e->getStringAttribute ("id") == rp->paramID) plain = (float) e->getDoubleAttribute ("value", plain);
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->getNormalisableRange().snapToLegalValue (plain)));
        }
    userPresetName = name;
    return true;
}

bool HarmonizerProcessor::deleteUserPreset (const juce::String& name)
{
    const auto f = userPresetFile (name);
    if (! f.existsAsFile() || ! f.deleteFile()) return false;
    if (userPresetName == name) userPresetName = {};
    return true;
}

void HarmonizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::String cc, pads;
    for (int i = 0; i < 128; ++i) if (ccMap[(size_t) i].load() >= 0) cc << i << ":" << ccMap[(size_t) i].load() << ",";
    for (int i = 0; i < PadFx::Count; ++i) pads << padMap[(size_t) i].load() << ",";
    state.setProperty ("ccmap", cc, nullptr);
    state.setProperty ("preset", userPresetName.isNotEmpty() ? userPresetName : juce::String (currentPreset), nullptr);
    state.setProperty ("userpreset", userPresetName.isNotEmpty(), nullptr);
    state.setProperty ("padmap", pads, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            for (auto& c : ccMap) c = -1;
            juce::StringArray items; items.addTokens (state.getProperty ("ccmap").toString(), ",", "");
            for (auto& it : items)
                if (it.containsChar (':'))
                {
                    const int cc = it.upToFirstOccurrenceOf (":", false, false).getIntValue();
                    const int idx = it.fromFirstOccurrenceOf (":", false, false).getIntValue();
                    if (juce::isPositiveAndBelow (cc, 128)) ccMap[(size_t) cc] = idx;
                }
            juce::StringArray pads; pads.addTokens (state.getProperty ("padmap").toString(), ",", "");
            for (int i = 0; i < juce::jmin ((int) PadFx::Count, pads.size()); ++i)
                if (pads[i].isNotEmpty()) padMap[(size_t) i] = pads[i].getIntValue();
            apvts.replaceState (state);
            if ((bool) state.getProperty ("userpreset", false)) userPresetName = state.getProperty ("preset").toString();
            else
            {
                userPresetName = {};
                const int idx = state.getProperty ("preset", -1);
                if (juce::isPositiveAndBelow (idx, (int) harmPresets().size())) currentPreset = idx;
            }
        }
}

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerProcessor();
}
