#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr float kArcStart = -MathConstants<float>::pi * 0.75f;
    constexpr float kArcSpan  =  MathConstants<float>::pi * 1.5f;

    Point<float> onCircle (Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

    void arc (Graphics& g, Point<float> c, float r, float from, float to, float thick, Colour col)
    {
        if (to - from < 0.002f) return;
        Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, kArcStart + from * kArcSpan, kArcStart + to * kArcSpan, true);
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    }

    double nowMs() { return Time::getMillisecondCounterHiRes(); }

    RangedAudioParameter* findParam (HarmonizerProcessor& p, const String& id)
    {
        return p.apvts.getParameter (id);
    }

    void setPlain (RangedAudioParameter* p, float plain)
    {
        if (p == nullptr) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }

    float plainOf (RangedAudioParameter* p) { return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f; }

    String noteName (int n) { return n < 0 ? String ("-") : MidiMessage::getMidiNoteName (n, true, true, 3); }

    void text (Graphics& g, const String& s, Rectangle<int> r, const Font& f, Colour c, Justification j = Justification::centredLeft)
    {
        g.setFont (f); g.setColour (c); g.drawFittedText (s, r, j, 1, 0.85f);
    }

    void para (Graphics& g, const String& s, Rectangle<int> r, const Font& f, Colour c)
    {
        g.setFont (f); g.setColour (c); g.drawFittedText (s, r, Justification::topLeft, 4, 1.0f);
    }
}

//==============================================================================
Font Theme::sans (float size, bool bold) { return Font (FontOptions (size, bold ? Font::bold : Font::plain)); }
Font Theme::mono (float size, bool bold)
{
    return Font (FontOptions (Font::getDefaultMonospacedFontName(), size, bold ? Font::bold : Font::plain));
}
void Theme::label (Graphics& g, const String& caps, Rectangle<int> r, Colour c, Justification j)
{
    auto f = sans (11.0f);
    f.setExtraKerningFactor (0.16f);
    g.setFont (f); g.setColour (c); g.drawText (caps, r, j, true);
}
void Theme::panel (Graphics& g, Rectangle<int> r, float radius, Colour c)
{
    g.setColour (c); g.fillRoundedRectangle (r.toFloat(), radius);
}

HarmLookAndFeel::HarmLookAndFeel()
{
    setColour (ComboBox::backgroundColourId, Theme::card2);
    setColour (ComboBox::textColourId, Theme::text);
    setColour (ComboBox::outlineColourId, Theme::edge);
    setColour (ComboBox::arrowColourId, Theme::dim);
    setColour (PopupMenu::backgroundColourId, Theme::card2);
    setColour (PopupMenu::textColourId, Theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent);
    setColour (PopupMenu::highlightedTextColourId, Theme::bg);
    setColour (TextButton::buttonColourId, Theme::card2);
    setColour (TextButton::textColourOffId, Theme::soft);
    setColour (TextButton::textColourOnId, Theme::bg);
    setColour (TextEditor::backgroundColourId, Theme::hub);
    setColour (TextEditor::textColourId, Theme::text);
    setColour (TextEditor::highlightColourId, Theme::accent.withAlpha (0.35f));
    setColour (TextEditor::outlineColourId, Theme::edge);
    setColour (TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (CaretComponent::caretColourId, Theme::accent);
}

void HarmLookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor&)
{
    g.setColour (Theme::hub); g.fillRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h), 10.0f);
}

void HarmLookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& t)
{
    g.setColour (t.hasKeyboardFocus (true) ? Theme::accent : Theme::edge);
    g.drawRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 10.0f, 1.0f);
}

//==============================================================================
PresetDialog::PresetDialog()
{
    setWantsKeyboardFocus (true);
    name.setFont (Theme::sans (16.0f));
    name.setIndents (12, 10);
    name.setInputRestrictions (60);
    name.onReturnKey = [this] { ok(); };
    name.onEscapeKey = [this] { close(); };
    okButton.setColour (TextButton::buttonColourId, Theme::accent);
    okButton.setColour (TextButton::textColourOffId, Theme::bg);
    okButton.onClick = [this] { ok(); };
    cancelButton.onClick = [this] { close(); };
    addChildComponent (name);
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);
    setVisible (false);
}

void PresetDialog::askName (const String& suggestion, std::function<void (const String&)> onSave)
{
    title = "SAVE PRESET"; message = "Saved in Documents / Glizzarine Presets. Your setup (input, gate, latency, MIDI out) is not part of a preset.";
    asking = true; nameFn = std::move (onSave); okFn = nullptr;
    okButton.setButtonText ("Save");
    name.setVisible (true);
    name.setText (suggestion, false);
    name.selectAll();
    setVisible (true); toFront (true);
    resized();
    name.grabKeyboardFocus();
}

void PresetDialog::askConfirm (const String& t, const String& m, const String& okText, std::function<void()> onOk)
{
    title = t; message = m; asking = false; okFn = std::move (onOk); nameFn = nullptr;
    okButton.setButtonText (okText);
    name.setVisible (false);
    setVisible (true); toFront (true);
    resized();
    grabKeyboardFocus();
}

void PresetDialog::ok()
{
    if (asking)
    {
        const auto n = name.getText().trim();
        if (n.isEmpty()) return;
        auto fn = nameFn; close(); if (fn) fn (n);
    }
    else { auto fn = okFn; close(); if (fn) fn(); }
}

bool PresetDialog::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    if (k == KeyPress::returnKey) { ok(); return true; }
    return false;
}

void PresetDialog::resized()
{
    card = Rectangle<int> (460, asking ? 214 : 176).withCentre (getLocalBounds().getCentre());
    auto r = card.reduced (26, 22);
    r.removeFromTop (16 + 10 + 36);
    if (asking) { name.setBounds (r.removeFromTop (42)); r.removeFromTop (16); }
    auto b = r.removeFromBottom (40);
    okButton.setBounds (b.removeFromRight (120));
    b.removeFromRight (10);
    cancelButton.setBounds (b.removeFromRight (110));
}

void PresetDialog::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.6f));
    Theme::panel (g, card, 20.0f, Theme::card2);
    g.setColour (Theme::edge); g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 20.0f, 1.0f);
    auto r = card.reduced (26, 22);
    Theme::label (g, title, r.removeFromTop (16), Theme::accent);
    r.removeFromTop (10);
    para (g, message, r.removeFromTop (36), Theme::sans (13.0f), Theme::soft);
}

void HarmLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox&)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Theme::card2); g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::edge); g.drawRoundedRectangle (r, 10.0f, 1.0f);
    Path p; const float cx = (float) w - 18.0f, cy = (float) h * 0.5f;
    p.startNewSubPath (cx - 4, cy - 2); p.lineTo (cx, cy + 2); p.lineTo (cx + 4, cy - 2);
    g.setColour (Theme::dim); g.strokePath (p, PathStrokeType (1.6f));
}

void HarmLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.findColour (TextButton::buttonColourId) == Theme::accent)   // the one main action
    {
        g.setColour (down ? Theme::accent.darker (0.2f) : (over ? Theme::accent.brighter (0.15f) : Theme::accent));
        g.fillRoundedRectangle (r, 10.0f);
        return;
    }
    g.setColour (down ? Theme::accent.withAlpha (0.25f) : (over ? Theme::edge : Theme::card2));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::edge); g.drawRoundedRectangle (r, 10.0f, 1.0f);
}

//==============================================================================
AnimKnob::AnimKnob (HarmonizerProcessor& p, const String& paramId, const String& n, Style s, const String& h)
    : proc (p), id (paramId), name (n), help (h), style (s)
{
    param = findParam (p, id);
    setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kArcStart + MathConstants<float>::twoPi, kArcStart + kArcSpan + MathConstants<float>::twoPi, true);
    setMouseDragSensitivity (220);
    setVelocityModeParameters (0.6, 1, 0.08, true, ModifierKeys::shiftModifier);
    if (param != nullptr)
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, *this);
    if (param != nullptr) setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    disp = (float) valueToProportionOfLength (getValue());
    trailFrom = disp;
    first = false;
}

String AnimKnob::valueText (RangedAudioParameter* p)
{
    if (p == nullptr) return {};
    if (auto* f = dynamic_cast<AudioParameterFloat*> (p))
    {
        const float v = f->get();
        const auto& r = f->range;
        const String lab = f->getLabel();
        if (r.interval >= 1.0f) return f->getCurrentValueAsText();
        if (f->paramID == "chord_glide" && v < 0.005f) return "Off";
        if (r.start >= 0.0f && r.end <= 2.0f && lab.isEmpty()) return String (roundToInt (v * 100.0f)) + " %";
        if (r.start == -1.0f && r.end == 1.0f) return (v > 0.005f ? "+" : "") + String (v, 2);
        if (lab == "Hz" && v >= 19500.0f) return "open";
        if (lab == "Hz" && v >= 1000.0f) return String (v / 1000.0f, 1) + " kHz";
        if (std::abs (v) >= 100.0f) return String (roundToInt (v)) + (lab.isEmpty() ? "" : " " + lab);
        return String (v, std::abs (v) < 10.0f ? 2 : 1) + (lab.isEmpty() ? "" : " " + lab);
    }
    if (auto* c = dynamic_cast<AudioParameterChoice*> (p)) return c->getCurrentChoiceName();
    if (auto* b = dynamic_cast<AudioParameterBool*> (p)) return b->get() ? "On" : "Off";
    return p->getCurrentValueAsText();
}

void AnimKnob::valueChanged()
{
    if (first) return;
    if (trailAlpha < 0.03f) trailFrom = disp;
    trailAlpha = 0.28f;
    activeUntil = nowMs() + 1100.0;
}

void AnimKnob::tick (float dt)
{
    const float target = (float) valueToProportionOfLength (getValue());
    bool changed = false;
    if (std::abs (target - disp) > 0.0004f || std::abs (velo) > 0.001f)
    {
        // soft spring with a tiny overshoot: the knob glides instead of jumping
        const float k = 320.0f, c = 23.0f;
        const float steps = jlimit (1.0f, 8.0f, std::ceil (dt / 0.004f));
        const float h = dt / steps;
        for (int i = 0; i < (int) steps; ++i) { velo += (k * (target - disp) - c * velo) * h; disp += velo * h; }
        if (std::abs (target - disp) < 0.0004f && std::abs (velo) < 0.001f) { disp = target; velo = 0.0f; }
        changed = true;
    }
    if (trailAlpha > 0.0f) { trailAlpha = jmax (0.0f, trailAlpha - dt * 0.28f / 0.9f); changed = true; }
    const bool active = nowMs() < activeUntil;
    const float gT = active ? 1.0f : 0.0f;
    if (std::abs (glow - gT) > 0.001f) { glow += (gT - glow) * jmin (1.0f, dt * (active ? 14.0f : 4.0f)); changed = true; }
    const float bT = active ? 1.0f : 0.0f;
    if (std::abs (bubble - bT) > 0.001f) { bubble += (bT - bubble) * jmin (1.0f, dt * (active ? 18.0f : 6.0f)); changed = true; }
    const bool learning = param != nullptr && proc.ccLearningIndex() == param->getParameterIndex();
    if (learning) changed = true;
    if (changed) repaint();
}

void AnimKnob::drawKnob (Graphics& g, Point<float> c, float r, float th)
{
    const bool learning = param != nullptr && proc.ccLearningIndex() == param->getParameterIndex();
    arc (g, c, r, 0.0f, 1.0f, th, Theme::track);
    if (trailAlpha > 0.004f && ! dimmed)
    {
        const float a = jmin (trailFrom, disp), b = jmax (trailFrom, disp);
        arc (g, c, r, a, b, th * 1.9f, Theme::accent.withAlpha (trailAlpha * 0.35f));   // soft halo
        arc (g, c, r, a, b, th, Theme::accent.withAlpha (trailAlpha));
    }
    const Colour valueCol = dimmed ? Theme::mute : Theme::knob.interpolatedWith (Theme::accent, glow);
    if (glow > 0.01f && ! dimmed) arc (g, c, r, 0.0f, disp, th * 2.2f, Theme::accent.withAlpha (0.22f * glow));
    arc (g, c, r, 0.0f, jlimit (0.0f, 1.0f, disp), th, valueCol);
    const float hubR = r - th * 1.6f;
    g.setColour (Theme::hub); g.fillEllipse (c.x - hubR, c.y - hubR, hubR * 2, hubR * 2);
    g.setColour (learning ? Theme::orange : Colour (0xff2c2c3a));
    g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2, hubR * 2, learning ? 2.0f : 1.0f);
    const float a = kArcStart + jlimit (-0.05f, 1.05f, disp) * kArcSpan;
    if (trailAlpha > 0.004f && ! dimmed)
    {
        g.setColour (Theme::text.withAlpha (trailAlpha));
        g.drawLine (Line<float> (c, onCircle (c, hubR * 0.8f, kArcStart + trailFrom * kArcSpan)), th * 0.45f);
    }
    g.setColour (dimmed ? Theme::mute : Theme::text);
    g.drawLine (Line<float> (c, onCircle (c, hubR * 0.8f, a)), th * 0.45f);
}

void AnimKnob::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const String val = valueText (param);
    if (glow > 0.01f && ! dimmed && style != Ring)
    {
        g.setColour (Theme::accent.withAlpha (0.06f * glow)); g.fillRoundedRectangle (b.reduced (1.0f), 16.0f);
        g.setColour (Theme::accent.withAlpha (0.85f * glow)); g.drawRoundedRectangle (b.reduced (1.0f), 16.0f, 1.0f);
    }

    if (style == Big)
    {
        const float top = 20.0f, textH = 38.0f;
        const float d = jmax (24.0f, jmin (b.getWidth() - 12.0f, b.getHeight() - top - textH));
        const Point<float> c (b.getCentreX(), top + d * 0.5f);
        drawKnob (g, c, d * 0.42f, jmax (3.0f, d * 0.075f));
        const int ty = (int) (top + d + 2);
        text (g, name, { 0, ty, getWidth(), 18 }, Theme::sans (14.0f), dimmed ? Theme::mute : Theme::text, Justification::centred);
        text (g, caption.isNotEmpty() ? caption + "  " + val : val, { 0, ty + 18, getWidth(), 16 }, Theme::mono (11.5f),
              Theme::dim, Justification::centred);
        if (bubble > 0.01f && ! dimmed)
        {
            const String s = name + " " + val;
            const auto f = Theme::mono (12.5f, true);
            const float w = GlyphArrangement::getStringWidth (f, s) + 18.0f;
            auto r = Rectangle<float> (w, 22.0f).withCentre ({ b.getCentreX(), 12.0f + (1.0f - bubble) * 6.0f });
            g.setColour (Theme::accent.withAlpha (bubble)); g.fillRoundedRectangle (r, 7.0f);
            g.setFont (f); g.setColour (Theme::bg.withAlpha (bubble)); g.drawText (s, r, Justification::centred);
        }
    }
    else if (style == Inline)
    {
        const float d = jmin (b.getHeight() - 8.0f, 58.0f);
        const Point<float> c (8.0f + d * 0.5f, b.getCentreY());
        drawKnob (g, c, d * 0.42f, jmax (3.0f, d * 0.09f));
        const int tx = (int) (d + 18.0f), tw = getWidth() - tx - 4;
        const int lines = help.isNotEmpty() ? 3 : 2;
        int ty = (int) b.getCentreY() - lines * 9;
        text (g, name, { tx, ty, tw, 18 }, Theme::sans (14.0f), dimmed ? Theme::mute : Theme::text);
        text (g, bubble > 0.01f ? val : val, { tx, ty + 18, tw, 16 }, Theme::mono (12.0f), glow > 0.5f && ! dimmed ? Theme::accent : Theme::dim);
        if (help.isNotEmpty()) text (g, help, { tx, ty + 35, tw, 16 }, Theme::sans (12.0f), Theme::dim);
    }
    else // Ring: the big blend control on the vocoder tab
    {
        const float d = jmin (b.getWidth(), b.getHeight());
        const Point<float> c = b.getCentre();
        const float r = d * 0.5f - 10.0f;
        arc (g, c, r, 0.0f, 1.0f, 10.0f, Colour (0xff24243a));
        if (trailAlpha > 0.004f) arc (g, c, r, jmin (trailFrom, disp), jmax (trailFrom, disp), 10.0f, Theme::accent.withAlpha (trailAlpha));
        arc (g, c, r, 0.0f, disp, 18.0f, Theme::accent.withAlpha (0.18f));
        arc (g, c, r, 0.0f, disp, 10.0f, Theme::accent);
        const float hubR = r - 18.0f;
        g.setColour (Theme::card2); g.fillEllipse (c.x - hubR, c.y - hubR, hubR * 2, hubR * 2);
        g.setColour (Colour (0xff2c2c3a)); g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2, hubR * 2, 1.0f);
        const float v = plainOf (param);
        const int cx = (int) c.x, cy = (int) c.y;
        Theme::label (g, "HARMONIZER " + String (roundToInt ((1.0f - v) * 100.0f)) + " %", { cx - 90, cy - 44, 180, 16 }, Theme::dim, Justification::centred);
        text (g, String (roundToInt (v * 100.0f)) + " %", { cx - 90, cy - 24, 180, 46 }, Theme::sans (40.0f, true), Theme::text, Justification::centred);
        Theme::label (g, "VOCODER", { cx - 90, cy + 26, 180, 16 }, Theme::accent, Justification::centred);
    }
}

void AnimKnob::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) { Slider::mouseDown (e); return; }
    const int cc = proc.ccFor (id);
    PopupMenu m;
    m.addSectionHeader (cc >= 0 ? "MIDI CC " + String (cc) : "No MIDI CC assigned");
    m.addItem (1, "MIDI Learn (turn a knob on your keyboard next)");
    m.addItem (2, "Forget MIDI CC", cc >= 0);
    m.addItem (3, "Reset to default");
    Component::SafePointer<AnimKnob> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->proc.startCcLearn (safe->id);
        if (r == 2) safe->proc.forgetCc (safe->id);
        if (r == 3 && safe->param != nullptr) setPlain (safe->param, safe->param->convertFrom0to1 (safe->param->getDefaultValue()));
    });
}

//==============================================================================
Segments::Segments (HarmonizerProcessor& p, const String& paramId, StringArray l, bool j)
    : param (findParam (p, paramId)), labels (std::move (l)), joined (j) {}

int Segments::current() const
{
    if (param == nullptr) return 0;
    return roundToInt (plainOf (param) - param->getNormalisableRange().start);
}

void Segments::tick (float) { const int c = current(); if (c != shown) { shown = c; repaint(); } }

void Segments::paint (Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;
    const float gap = joined ? 3.0f : 5.0f;
    auto b = getLocalBounds().toFloat();
    if (joined) { g.setColour (Theme::hub); g.fillRoundedRectangle (b, 10.0f); b = b.reduced (3.0f); }
    const float w = (b.getWidth() - gap * (float) (n - 1)) / (float) n;
    const int cur = current();
    for (int i = 0; i < n; ++i)
    {
        auto r = Rectangle<float> (b.getX() + (float) i * (w + gap), b.getY(), w, b.getHeight());
        const bool on = i == cur;
        if (on) { g.setColour (Theme::accent); g.fillRoundedRectangle (r, 9.0f); }
        else if (! joined) { g.setColour (Theme::edge); g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f); }
        g.setFont (Theme::sans (w < 60.0f ? 12.0f : 13.5f));
        g.setColour (on ? Theme::bg : Theme::soft);
        g.drawFittedText (labels[i], r.toNearestInt().reduced (2, 0), Justification::centred, 1, 0.8f);
    }
}

void Segments::mouseDown (const MouseEvent& e)
{
    const int n = labels.size();
    if (n == 0 || param == nullptr) return;
    const int i = jlimit (0, n - 1, (int) ((float) e.x / (float) getWidth() * (float) n));
    setPlain (param, param->getNormalisableRange().start + (float) i);
    repaint();
}

//==============================================================================
ModeCards::ModeCards (HarmonizerProcessor& p) : param (findParam (p, "arp_mode")) {}
int ModeCards::current() const { return roundToInt (plainOf (param)); }
void ModeCards::tick (float) { const int c = current(); if (c != shown) { shown = c; repaint(); } }

void ModeCards::paint (Graphics& g)
{
    static const char* names[] = { "Off", "Pyramid", "Arp" };
    static const char* desc[] = { "All voices play together.", "Voices enter one by one and stay.", "Voices take turns." };
    const float gap = 10.0f, h = ((float) getHeight() - gap * 2.0f) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto r = Rectangle<float> (0.0f, (float) i * (h + gap), (float) getWidth(), h);
        const bool on = i == current();
        g.setColour (on ? Colour (0xff1a1422) : Theme::card); g.fillRoundedRectangle (r, 16.0f);
        g.setColour (on ? Theme::accent : Theme::line); g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
        // icon
        Path p; const float x = r.getX() + 20.0f, y = r.getCentreY() - 13.0f;
        if (i == 0) { for (int k = 0; k < 3; ++k) { p.startNewSubPath (x, y + 4 + k * 9); p.lineTo (x + 34, y + 4 + k * 9); } }
        else if (i == 1) { for (int k = 0; k < 4; ++k) { p.startNewSubPath (x + k * 8.5f, y + 24 - k * 7); p.lineTo (x + k * 8.5f + 8, y + 24 - k * 7); } }
        else { const float ys[] = { 24, 14, 4, 14 }; for (int k = 0; k < 4; ++k) { p.startNewSubPath (x + k * 9.0f, y + ys[k]); p.lineTo (x + k * 9.0f + 5, y + ys[k]); } }
        g.setColour (on ? Theme::accent : Colour (0xff6f6d85));
        g.strokePath (p, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        const int tx = (int) r.getX() + 72;
        text (g, names[i], { tx, (int) r.getCentreY() - 21, (int) r.getWidth() - 80, 22 }, Theme::sans (18.0f, true), Theme::text);
        text (g, desc[i], { tx, (int) r.getCentreY() + 2, (int) r.getWidth() - 80, 18 }, Theme::sans (13.0f), Theme::dim);
    }
}

void ModeCards::mouseDown (const MouseEvent& e)
{
    const int i = jlimit (0, 2, (int) ((float) e.y / (float) getHeight() * 3.0f));
    setPlain (param, (float) i);
    repaint();
}

//==============================================================================
CarrierList::CarrierList (HarmonizerProcessor& p) : param (findParam (p, "voc_type")) {}
void CarrierList::tick (float) { const int c = roundToInt (plainOf (param)); if (c != shown) { shown = c; repaint(); } }

void CarrierList::paint (Graphics& g)
{
    static const char* names[] = { "Saw Pad", "Strings", "Glass", "Hollow Pulse", "Noise Choir" };
    Theme::label (g, "CARRIER SOUND", { 4, 0, getWidth(), 16 });
    const float top = 26.0f, gap = 8.0f, h = jmin (60.0f, ((float) getHeight() - top - gap * 4.0f) / 5.0f);
    const int cur = roundToInt (plainOf (param));
    for (int i = 0; i < 5; ++i)
    {
        auto r = Rectangle<float> (0.0f, top + (float) i * (h + gap), (float) getWidth(), h);
        const bool on = i == cur;
        g.setColour (on ? Colour (0xff1a1422) : Theme::card); g.fillRoundedRectangle (r, 14.0f);
        g.setColour (on ? Theme::accent : Theme::line); g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        Path p;
        const float x0 = r.getX() + 14.0f, w = 54.0f, cy = r.getCentreY();
        for (int k = 0; k <= 40; ++k)
        {
            const float t = (float) k / 40.0f;
            float v = 0.0f;
            switch (i)
            {
                case 0: v = std::fmod (t * 4.0f, 1.0f) * 2.0f - 1.0f; break;
                case 1: v = std::sin (t * 25.0f) * 0.6f + std::sin (t * 50.0f) * 0.3f; break;
                case 2: v = std::sin (t * 25.0f) * std::exp (-t * 2.0f); break;
                case 3: v = (std::sin (t * 25.0f) > 0.3f ? 0.8f : -0.8f); break;
                default: v = std::sin (t * 97.0f) * std::sin (t * 13.0f) * 0.9f; break;
            }
            const float px = x0 + t * w, py = cy - v * 10.0f;
            if (k == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (on ? Theme::accent : Colour (0xff6f6d85));
        g.strokePath (p, PathStrokeType (2.2f));
        text (g, names[i], r.toNearestInt().withTrimmedLeft (82), Theme::sans (14.5f), Theme::text);
    }
}

void CarrierList::mouseDown (const MouseEvent& e)
{
    const float top = 26.0f, gap = 8.0f, h = jmin (60.0f, ((float) getHeight() - top - gap * 4.0f) / 5.0f);
    const int i = (int) (((float) e.y - top) / (h + gap));
    if (i >= 0 && i < 5) { setPlain (param, (float) i); repaint(); }
}

//==============================================================================
Switch::Switch (HarmonizerProcessor& p, const String& paramId) : param (findParam (p, paramId)) { pos = isOn() ? 1.0f : 0.0f; }
bool Switch::isOn() const { return param != nullptr && param->getValue() > 0.5f; }
void Switch::tick (float dt)
{
    const float t = isOn() ? 1.0f : 0.0f;
    if (std::abs (t - pos) > 0.001f) { pos += (t - pos) * jmin (1.0f, dt * 16.0f); if (std::abs (t - pos) < 0.01f) pos = t; repaint(); }
}
void Switch::paint (Graphics& g)
{
    auto r = Rectangle<float> (40.0f, 22.0f).withCentre (getLocalBounds().toFloat().getCentre());
    g.setColour (Theme::edge.interpolatedWith (Theme::accent, pos)); g.fillRoundedRectangle (r, 11.0f);
    g.setColour (Theme::text); g.fillEllipse (r.getX() + 3.0f + pos * 18.0f, r.getY() + 3.0f, 16.0f, 16.0f);
}
void Switch::mouseDown (const MouseEvent&) { if (param != nullptr) setPlain (param, isOn() ? 0.0f : 1.0f); }

//==============================================================================
Bar::Bar (HarmonizerProcessor& p, const String& paramId)
{
    setSliderStyle (Slider::LinearHorizontal);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    if (p.apvts.getParameter (paramId) != nullptr)
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
}
void Bar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 6.0f);
    g.setColour (Colour (0xff1f1f2b)); g.fillRoundedRectangle (r, 3.0f);
    const float p = (float) valueToProportionOfLength (getValue());
    g.setColour (dimmed ? Colour (0xff3a3850) : (isMouseOverOrDragging() ? Theme::accent : Theme::knob));
    g.fillRoundedRectangle (r.withWidth (jmax (3.0f, r.getWidth() * p)), 3.0f);
    if (isMouseOverOrDragging())
    {
        g.setColour (Theme::text);
        g.fillEllipse (r.getX() + r.getWidth() * p - 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
}

NumberBox::NumberBox (HarmonizerProcessor& p, const String& paramId, const String& s) : suffix (s)
{
    setSliderStyle (Slider::LinearBarVertical);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (300);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
}
void NumberBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Theme::hub); g.fillRoundedRectangle (r, 9.0f);
    g.setColour (isMouseOverOrDragging() ? Theme::accent : Theme::edge); g.drawRoundedRectangle (r, 9.0f, 1.0f);
    text (g, String (getValue(), 1) + " " + suffix, getLocalBounds(), Theme::mono (15.0f), isEnabled() ? Theme::text : Theme::mute, Justification::centred);
}

//==============================================================================
PadCell::PadCell (HarmonizerProcessor& p, int fxIndex) : proc (p), fx (fxIndex) {}

void PadCell::tick (float dt)
{
    const bool l = proc.padActive (fx), le = proc.padLearning() == fx;
    const auto m = proc.padLabel (fx);
    const float gT = l ? 1.0f : 0.0f;
    bool changed = (l != lit || le != learning || m != mapping);
    lit = l; learning = le; mapping = m;
    if (std::abs (glow - gT) > 0.001f) { glow += (gT - glow) * jmin (1.0f, dt * (l ? 20.0f : 5.0f)); changed = true; }
    if (changed) repaint();
}

void PadCell::paint (Graphics& g)
{
    static const char* groups[] = { "TIME", "TIME", "TIME", "TIME", "TIME", "PITCH", "PITCH", "TEXTURE", "TEXTURE", "BUILD", "PITCH", "PITCH" };
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (glow > 0.01f) { g.setColour (Theme::accent.withAlpha (0.25f * glow)); g.fillRoundedRectangle (r.expanded (1.0f), 17.0f); }
    g.setColour (Theme::card.interpolatedWith (Theme::accent, glow)); g.fillRoundedRectangle (r.reduced (2.0f), 16.0f);
    g.setColour (learning ? Theme::orange : Theme::line.interpolatedWith (Theme::accent, glow));
    g.drawRoundedRectangle (r.reduced (2.0f), 16.0f, learning ? 2.0f : 1.0f);
    const Colour fg = Theme::text.interpolatedWith (Theme::bg, glow);
    auto in = getLocalBounds().reduced (16, 12);
    text (g, "PAD " + String (fx + 1).paddedLeft ('0', 2), in.withHeight (16), Theme::mono (11.0f), fg.withAlpha (0.75f));
    text (g, groups[fx], in.withHeight (16), Theme::mono (11.0f), fg.withAlpha (0.75f), Justification::centredRight);
    text (g, PadFx::name (fx), in.withSizeKeepingCentre (in.getWidth(), 24), Theme::sans (17.0f, true), fg);
    text (g, learning ? String ("learning... hit a pad") : (lit ? String ("ON") : mapping), in.removeFromBottom (16),
          Theme::mono (11.0f), learning ? Theme::orange : fg.withAlpha (0.75f));
}

void PadCell::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem (1, proc.padLearning() == fx ? "Stop learning" : "Learn (hit a pad on your KeyLab next)");
        Component::SafePointer<PadCell> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe != nullptr && r == 1) safe->proc.startPadLearn (safe->proc.padLearning() == safe->fx ? -1 : safe->fx);
        });
        return;
    }
    const bool latch = proc.apvts.getRawParameterValue ("pad_latch")->load() > 0.5f;
    proc.triggerPadFromUi (fx, ! (latch && proc.padActive (fx)));
}

void PadCell::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (proc.apvts.getRawParameterValue ("pad_latch")->load() < 0.5f) proc.triggerPadFromUi (fx, false);
}

//==============================================================================
// Pages
namespace
{
    const Rectangle<int> kContent { 30, 128, 1140, 492 };   // everything below the tabs

    //--------------------------------------------------------------------------
    class MainPage : public Page
    {
    public:
        explicit MainPage (HarmonizerProcessor& p) : Page (p)
        {
            const char* ids[] = { "blend", "harmony", "wah_mix", "tone", "chorus_mix", "delay_mix", "reverb", "shimmer" };
            const char* names[] = { "Blend", "Harmonies", "Env Filter", "Filter", "Motion", "Delay", "Reverb", "Shimmer" };
            for (int i = 0; i < 8; ++i) { knobs[i] = knob (ids[i], names[i]); knobs[i]->setCaption ("E" + String (i + 1)); }
            glide = knob ("chord_glide", "Chord Glide", AnimKnob::Inline, "slide into the next chord");
            glideMode = add<Segments> (p, "glide_mode", StringArray { "Always", "Legato" }, true);
            voicing = add<Segments> (p, "voicing", StringArray { "Close", "Bass " + String (CharPointer_UTF8 ("\xe2\x86\x93")), "Top " + String (CharPointer_UTF8 ("\xe2\x86\x91")),
                                                                 "Bass + Top", "Spread" });
        }
        void resized() override
        {
            auto r = kContent;
            auto top = r.removeFromTop (96);
            meterRect = top.removeFromRight (240);
            top.removeFromRight (16);
            voiceRect = top;
            r.removeFromTop (20);
            knobRect = r;
            auto k = r.reduced (22, 18);
            k.removeFromTop (24);
            auto strip = k.removeFromBottom (92);
            k = k.withSizeKeepingCentre (k.getWidth(), jmin (k.getHeight(), 190));
            const int w = k.getWidth() / 8;
            for (int i = 0; i < 8; ++i) knobs[i]->setBounds (k.getX() + i * w + 4, k.getY(), w - 8, k.getHeight());

            // CHORD GLIDE | VOICING
            stripRect = strip;
            strip.removeFromTop (14);
            auto left = strip.removeFromLeft (strip.getWidth() / 2 - 20);
            glideLabel = left.removeFromTop (16); left.removeFromTop (4);
            glide->setBounds (left.removeFromLeft (260).withHeight (62));
            glideMode->setBounds (left.withSizeKeepingCentre (left.getWidth() - 20, 40).withY (left.getY() + 11));
            strip.removeFromLeft (40);
            voicingLabel = strip.removeFromTop (16); strip.removeFromTop (4);
            voicing->setBounds (strip.withHeight (40).translated (0, 11));
        }
        void paint (Graphics& g) override
        {
            Theme::panel (g, voiceRect, 16.0f);
            Theme::label (g, "VOICES", voiceRect.withTrimmedLeft (22).withWidth (80));
            auto v = voiceRect.withTrimmedLeft (110).reduced (12, 0);
            const int w = v.getWidth() / 8;
            for (int i = 0; i < 8; ++i)
            {
                const int n = proc.voiceNotes[(size_t) i].load();
                auto s = Rectangle<int> (v.getX() + i * w + 6, v.getCentreY() - 14, w - 12, 6);
                if (n >= 0)
                {
                    g.setColour (Theme::accent.withAlpha (0.25f)); g.fillRoundedRectangle (s.toFloat().expanded (2.0f, 3.0f), 4.0f);
                    g.setColour (Theme::accent);
                }
                else g.setColour (Colour (0xff1f1f2b));
                g.fillRoundedRectangle (s.toFloat(), 3.0f);
                text (g, noteName (n), { s.getX(), s.getBottom() + 8, s.getWidth(), 16 }, Theme::mono (12.0f),
                      n >= 0 ? Theme::text : Theme::mute, Justification::centred);
            }
            Theme::panel (g, meterRect, 16.0f);
            auto m = meterRect.reduced (20, 14);
            Theme::label (g, "INPUT", m.removeFromTop (16));
            const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.inputLevel.load(), -60.0f) + 60.0f) / 60.0f);
            auto b1 = m.removeFromTop (12).withSizeKeepingCentre (m.getWidth(), 8).toFloat();
            g.setColour (Colour (0xff1f1f2b)); g.fillRoundedRectangle (b1, 4.0f);
            g.setColour (lvl > 0.92f ? Theme::orange : Theme::violet); g.fillRoundedRectangle (b1.withWidth (b1.getWidth() * lvl), 4.0f);
            m.removeFromTop (8);
            Theme::label (g, "AFTERTOUCH", m.removeFromTop (16));
            auto b2 = m.removeFromTop (12).withSizeKeepingCentre (m.getWidth(), 8).toFloat();
            g.setColour (Colour (0xff1f1f2b)); g.fillRoundedRectangle (b2, 4.0f);
            g.setColour (Theme::accent); g.fillRoundedRectangle (b2.withWidth (b2.getWidth() * jlimit (0.0f, 1.0f, proc.aftertouchShown.load())), 4.0f);

            Theme::panel (g, knobRect, 20.0f);
            auto h = knobRect.reduced (22, 18).removeFromTop (16);
            Theme::label (g, "KEYLAB KNOBS 1-8", h);
            text (g, "right-click a knob = MIDI learn", h, Theme::sans (12.0f), Theme::dim, Justification::centredRight);
            g.setColour (Theme::line); g.fillRect (stripRect.getX(), stripRect.getY(), stripRect.getWidth(), 1);
            Theme::label (g, "CHORD GLIDE", glideLabel);
            text (g, "Legato = only when the chords overlap", glideLabel, Theme::sans (12.0f), Theme::dim, Justification::centredRight);
            Theme::label (g, "VOICING", voicingLabel);
            text (g, "extra voices: an octave below / above, or open", voicingLabel, Theme::sans (12.0f), Theme::dim, Justification::centredRight);
        }
        void tickPageExtra()
        {
            const bool off = proc.apvts.getRawParameterValue ("chord_glide")->load() < 0.005f;
            if (off != glideOff) { glideOff = off; glideMode->setAlpha (off ? 0.4f : 1.0f); }
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            tickPageExtra();
            if (++frame % 2 == 0) { repaint (voiceRect); repaint (meterRect); }
        }
    private:
        AnimKnob* knobs[8] {};
        AnimKnob* glide = nullptr; Segments* glideMode = nullptr; Segments* voicing = nullptr;
        bool glideOff = false;
        Rectangle<int> voiceRect, meterRect, knobRect, stripRect, glideLabel, voicingLabel;
        int frame = 0;
    };

    //--------------------------------------------------------------------------
    class VoicePage : public Page
    {
    public:
        explicit VoicePage (HarmonizerProcessor& p) : Page (p)
        {
            in[0] = knob ("input", "Input Gain");
            in[1] = knob ("sens", "Sensitivity");
            in[2] = knob ("gate", "Noise Gate");
            lowest = add<Segments> (p, "lowest", StringArray { "E2 bass", "A2", "D3", "G3 high" });
            timing = add<Segments> (p, "dry_timing", StringArray { "Direct (live)", "Aligned" }, true);
            midiOut = add<Segments> (p, "midi_out", StringArray { "Off", "Melody", "+ Harmonies" }, true);
            latency = add<Segments> (p, "latency_mode", StringArray { "Normal", "Low" }, true);
            const char* ids[] = { "formant", "consonants", "follow", "leveling", "warmth", "width", "suboct", "humanize", "attack", "release", "sustain", "dry", "output" };
            const char* names[] = { "Formant", "Consonants", "Follow Pitch", "Leveling", "Warmth", "Width", "Octave Down", "Humanize", "Attack", "Release", "Pedal Sustain", "Solo Voice", "Output" };
            const char* helps[] = { "down = darker, up = brighter", "S, T, K and breath", "copies your vibrato and bends", "evens out loud and soft",
                                    "more low-end body", "spread left / right", "extra voice an octave below", "loose timing, drift, vibrato",
                                    "fade-in of each voice", "fade-out after you let go", "ring-out with the pedal", "your own voice (0 = off)", "master level" };
            for (int i = 0; i < numCh; ++i) ch[i] = knob (ids[i], names[i], AnimKnob::Inline, helps[i]);
        }
        void resized() override
        {
            auto r = kContent;
            leftRect = r.removeFromLeft (420);
            r.removeFromLeft (18);
            rightRect = r;
            auto l = leftRect.reduced (20, 18);
            l.removeFromTop (24);
            meterRect = l.removeFromTop (40);
            l.removeFromTop (28);
            auto kr = l.removeFromTop (126);
            const int w = kr.getWidth() / 3;
            for (int i = 0; i < 3; ++i) in[i]->setBounds (kr.getX() + i * w, kr.getY(), w, kr.getHeight());
            l.removeFromTop (14);
            lowLabel = l.removeFromTop (18); l.removeFromTop (6);
            lowest->setBounds (l.removeFromTop (40));
            l.removeFromTop (14);
            timeLabel = l.removeFromTop (18); l.removeFromTop (6);
            timing->setBounds (l.removeFromTop (40));
            l.removeFromTop (14);
            auto lab = l.removeFromTop (18); l.removeFromTop (6);
            auto row = l.removeFromTop (40);
            midiLabel = lab.removeFromLeft (lab.getWidth() * 3 / 5); latLabel = lab.withTrimmedLeft (12);
            midiOut->setBounds (row.removeFromLeft (row.getWidth() * 3 / 5));
            latency->setBounds (row.withTrimmedLeft (12));

            auto c = rightRect.reduced (20, 18);
            c.removeFromTop (24);
            const int cw = c.getWidth() / 3, chh = c.getHeight() / 5;
            for (int i = 0; i < numCh; ++i)
                ch[i]->setBounds (c.getX() + (i % 3) * cw + 4, c.getY() + (i / 3) * chh + 2, cw - 8, chh - 4);
        }
        void paint (Graphics& g) override
        {
            Theme::panel (g, leftRect);
            auto h = leftRect.reduced (20, 18).removeFromTop (16);
            Theme::label (g, "INPUT", h, Theme::soft);
            text (g, "set once, applies to all presets", h, Theme::sans (12.0f), Theme::dim, Justification::centredRight);
            // LED meter with the gate and detection marks
            g.setColour (Colour (0xff121219)); g.fillRoundedRectangle (meterRect.toFloat(), 10.0f);
            auto seg = meterRect.reduced (8, 9);
            const float lvlDb = Decibels::gainToDecibels (proc.inputLevel.load(), -80.0f);
            const int n = 32; const float sw = ((float) seg.getWidth() - 3.0f * (n - 1)) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const float db = -80.0f + 80.0f * (float) (i + 1) / (float) n;
                const bool litSeg = db <= lvlDb + 2.5f;
                const Colour c = ! litSeg ? Colour (0xff1d1d27) : (db > -12.0f ? Theme::orange : (db < -50.0f ? Theme::violet : Theme::accent));
                g.setColour (c);
                g.fillRoundedRectangle ((float) seg.getX() + (float) i * (sw + 3.0f), (float) seg.getY(), sw, (float) seg.getHeight(), 3.0f);
            }
            auto mark = [&] (float db, Colour c)
            {
                const float x = (float) seg.getX() + (float) seg.getWidth() * jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
                g.setColour (c); g.fillRect (x - 1.0f, (float) meterRect.getY() + 4.0f, 2.0f, (float) meterRect.getHeight() - 8.0f);
            };
            mark (proc.apvts.getRawParameterValue ("gate")->load(), Theme::orange);
            mark (-30.0f - 60.0f * proc.apvts.getRawParameterValue ("sens")->load() * 0.01f, Theme::text);
            auto leg = meterRect.translated (0, 46).withHeight (16);
            text (g, "|  gate cuts below", leg, Theme::sans (12.0f), Theme::orange);
            text (g, "|  singing detected above", leg.withTrimmedLeft (150), Theme::sans (12.0f), Theme::soft);
            Theme::label (g, "LOWEST NOTE YOU SING", lowLabel);
            Theme::label (g, "SOLO VOICE TIMING", timeLabel);
            Theme::label (g, "MIDI OUT", midiLabel);
            Theme::label (g, "LATENCY", latLabel);

            Theme::panel (g, rightRect);
            Theme::label (g, "VOICE CHARACTER", rightRect.reduced (20, 18).removeFromTop (16), Theme::soft);
        }
        void tickPage (float dt) override { Page::tickPage (dt); if (++frame % 2 == 0) repaint (meterRect); }
    private:
        static constexpr int numCh = 13;
        AnimKnob* in[3] {}; AnimKnob* ch[numCh] {};
        Segments* lowest = nullptr; Segments* timing = nullptr; Segments* midiOut = nullptr; Segments* latency = nullptr;
        Rectangle<int> leftRect, rightRect, meterRect, lowLabel, timeLabel, midiLabel, latLabel;
        int frame = 0;
    };

    //--------------------------------------------------------------------------
    class VocoderPage : public Page
    {
    public:
        explicit VocoderPage (HarmonizerProcessor& p) : Page (p)
        {
            carriers = add<CarrierList> (p);
            blend = knob ("blend", "Blend", AnimKnob::Ring);
            const char* ids[] = { "voc_shift", "voc_sib", "voc_resp", "voc_width" };
            const char* names[] = { "Formant", "Sibilance", "Smooth", "Width" };
            for (int i = 0; i < 4; ++i) k[i] = knob (ids[i], names[i]);
            for (auto& b : bars) b = 0.0f;
        }
        void resized() override
        {
            auto r = kContent;
            carriers->setBounds (r.removeFromLeft (250));
            r.removeFromLeft (20);
            rightRect = r.removeFromRight (300);
            r.removeFromRight (20);
            centreRect = r;
            blend->setBounds (Rectangle<int> (230, 230).withCentre (centreRect.getCentre().translated (0, -8)));
            auto kr = rightRect.reduced (16, 30);
            const int w = kr.getWidth() / 2, h = kr.getHeight() / 2;
            for (int i = 0; i < 4; ++i) k[i]->setBounds (kr.getX() + (i % 2) * w + 4, kr.getY() + (i / 2) * h + 4, w - 8, h - 8);
        }
        void paint (Graphics& g) override
        {
            Theme::panel (g, centreRect, 24.0f);
            Theme::panel (g, rightRect, 24.0f);
            const auto c = blend->getBounds().getCentre().toFloat();
            const int n = 40;
            for (int i = 0; i < n; ++i)
            {
                const float a = degreesToRadians (-150.0f + 300.0f * (float) i / (float) (n - 1));
                const float len = 10.0f + bars[(size_t) i] * 62.0f;
                const float r0 = 128.0f;
                g.setColour (bars[(size_t) i] > 0.55f ? Theme::accent : Colour (0xff3a3850));
                g.drawLine (Line<float> (onCircle (c, r0, a), onCircle (c, r0 + len, a)), 8.0f);
            }
            text (g, "ring = what your voice is shaping right now", centreRect.removeFromBottom (34), Theme::sans (12.0f), Theme::dim, Justification::centred);
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            for (int i = 0; i < 40; ++i)
            {
                const float pos = (float) i * 19.0f / 39.0f;
                const int b0 = (int) pos; const float fr = pos - (float) b0;
                const float lv = proc.bandLevels[(size_t) b0].load() * (1.0f - fr) + proc.bandLevels[(size_t) jmin (19, b0 + 1)].load() * fr;
                const float t = jlimit (0.0f, 1.0f, std::sqrt (lv) * 2.4f);
                bars[(size_t) i] += (t - bars[(size_t) i]) * jmin (1.0f, dt * (t > bars[(size_t) i] ? 30.0f : 8.0f));
            }
            if (++frame % 2 == 0) repaint (centreRect);
        }
    private:
        CarrierList* carriers = nullptr; AnimKnob* blend = nullptr; AnimKnob* k[4] {};
        Rectangle<int> centreRect, rightRect;
        std::array<float, 40> bars {};
        int frame = 0;
    };

    //--------------------------------------------------------------------------
    class ArpPage : public Page
    {
    public:
        explicit ArpPage (HarmonizerProcessor& p) : Page (p)
        {
            modes = add<ModeCards> (p);
            rows[0] = add<Segments> (p, "arp_rate", StringArray { "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/16 T" });
            rows[1] = add<Segments> (p, "arp_order", StringArray { "Up", "Down", "Up-Down", "Down-Up", "Crawl", "Out-In", "In-Out", "Random", "Played" });
            rows[2] = add<Segments> (p, "arp_style", StringArray { "Pluck", "Legato", "Swell" });
            rows[3] = add<Segments> (p, "arp_oct", StringArray { "1", "2", "3" });
            rows[4] = add<Segments> (p, "tempo_src", StringArray { "Host", "Own" });
            bpm = add<NumberBox> (p, "bpm", "BPM");
            tap = add<TextButton> ("TAP");
            tap->onClick = [&p] { p.tapTempo(); };
            const char* ids[] = { "arp_gate", "arp_release", "arp_spread", "arp_steps" };
            const char* names[] = { "Gate", "Release", "Spread", "Loop" };
            for (int i = 0; i < 4; ++i) k[i] = knob (ids[i], names[i], AnimKnob::Inline);
        }
        void resized() override
        {
            auto r = kContent;
            modes->setBounds (r.removeFromLeft (270).withTrimmedTop (24));
            r.removeFromLeft (20);
            panel = r;
            auto in = r.reduced (24, 6);
            auto kr = in.removeFromBottom (86);
            const int rh = in.getHeight() / 5;
            static const int widths[] = { 470, 650, 300, 220, 160 };
            for (int i = 0; i < 5; ++i)
            {
                auto row = in.removeFromTop (rh);
                rowRects[i] = row;
                auto ctl = row.withTrimmedLeft (160);
                rows[i]->setBounds (ctl.withWidth (jmin (ctl.getWidth(), widths[i])).withSizeKeepingCentre (jmin (ctl.getWidth(), widths[i]), 38));
                if (i == 4)
                {
                    auto rest = ctl.withTrimmedLeft (widths[4] + 16);
                    bpm->setBounds (rest.removeFromLeft (130).withSizeKeepingCentre (130, 38));
                    rest.removeFromLeft (8);
                    tap->setBounds (rest.removeFromLeft (70).withSizeKeepingCentre (70, 38));
                }
            }
            const int w = kr.getWidth() / 4;
            for (int i = 0; i < 4; ++i) k[i]->setBounds (kr.getX() + i * w, kr.getY() + 8, w - 8, kr.getHeight() - 12);
        }
        void paint (Graphics& g) override
        {
            Theme::label (g, "MODE", kContent.withWidth (270).withHeight (16));
            Theme::panel (g, panel, 22.0f);
            static const char* labels[] = { "Rate", "Order", "Sound", "Octaves", "Tempo" };
            static const char* hints[] = { "note speed", "which voice first", "shape of each note", "range upwards", "clock source" };
            const float a = modes->current() == 0 ? 0.35f : 1.0f;
            for (int i = 0; i < 5; ++i)
            {
                auto row = rowRects[i];
                g.setColour (Theme::line); g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);
                text (g, labels[i], { row.getX(), row.getCentreY() - 18, 150, 20 }, Theme::sans (15.0f, true), Theme::text.withAlpha (a));
                text (g, hints[i], { row.getX(), row.getCentreY() + 2, 150, 16 }, Theme::sans (12.0f), Theme::dim.withAlpha (a));
            }
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            const bool off = modes->current() == 0;
            const float a = off ? 0.35f : 1.0f;
            if (a != alpha)
            {
                alpha = a;
                for (auto* s : rows) s->setAlpha (a);
                for (auto* kk : k) kk->setAlpha (a);
                bpm->setAlpha (a); tap->setAlpha (a);
                repaint();
            }
            const bool own = proc.apvts.getRawParameterValue ("tempo_src")->load() > 0.5f;
            bpm->setEnabled (own);
        }
    private:
        ModeCards* modes = nullptr; Segments* rows[5] {}; NumberBox* bpm = nullptr; TextButton* tap = nullptr; AnimKnob* k[4] {};
        Rectangle<int> panel, rowRects[5];
        float alpha = -1.0f;
    };

    //--------------------------------------------------------------------------
    class EffectsPage : public Page
    {
    public:
        explicit EffectsPage (HarmonizerProcessor& p) : Page (p)
        {
            addRow (true,  "Env Filter", "wah_on",     { { "wah_mix", "Mix" }, { "wah_sens", "Sensitivity" }, { "wah_range", "Range" } });
            addRow (true,  "Filter",     "tone_on",    { { "tone", "Cutoff" }, { "tone_res", "Resonance" }, { "warmth", "Warmth" } });
            addRow (true,  "Chorus",     "chorus_on",  { { "chorus_mix", "Mix" }, { "chorus_rate", "Rate" }, { "chorus_depth", "Depth" } });
            addRow (true,  "Phaser",     "phaser_on",  { { "phaser_mix", "Mix" }, { "phaser_rate", "Rate" }, { "phaser_depth", "Depth" } });
            addRow (true,  "Tremolo",    "trem_on",    { { "trem_depth", "Depth" }, { "trem_rate", "Rate" }, { "trem_shape", "Shape" } });
            addRow (true,  "Lo-Fi",      "lofi_on",    { { "lofi_mix", "Mix" }, { "lofi_amount", "Dirt" }, { "lofi_bw", "Bandwidth" } });
            addRow (false, "Delay",      "delay_on",   { { "delay_mix", "Mix" }, { "delay_sync", "Sync" }, { "feedback", "Feedback" }, { "delay_time", "Free time" } });
            addRow (false, "Reverb",     "reverb_on",  { { "reverb", "Mix" }, { "size", "Size" }, { "rev_damp", "Damping" }, { "rev_pre", "Pre-delay" } });
            addRow (false, "Shimmer",    "shimmer_on", { { "shimmer", "Mix" }, { "shim_int", "Interval" }, { "shim_decay", "Decay" }, { "shim_tone", "Tone" } });
        }
        void resized() override
        {
            auto r = kContent;
            const int lw = (int) ((float) r.getWidth() * 0.58f);
            leftCol = r.removeFromLeft (lw);
            r.removeFromLeft (18);
            rightCol = r;
            layoutCol (true, leftCol.withTrimmedTop (24));
            layoutCol (false, rightCol.withTrimmedTop (24));
        }
        void paint (Graphics& g) override
        {
            auto lh = leftCol.withHeight (16).withTrimmedLeft (4);
            Theme::label (g, "COLOUR", lh, Theme::soft);
            text (g, "harmonies only - your solo voice stays clean", lh, Theme::sans (12.0f), Theme::dim, Justification::centredRight);
            auto rh = rightCol.withHeight (16).withTrimmedLeft (4);
            Theme::label (g, "SPACE", rh, Theme::soft);
            text (g, "on everything", rh, Theme::sans (12.0f), Theme::dim, Justification::centredRight);
            for (auto& row : fx)
            {
                Theme::panel (g, row.rect, 14.0f);
                text (g, row.name, row.nameRect, Theme::sans (15.0f, true), row.sw->isOn() ? Theme::text : Colour (0xff5d5b72));
            }
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            for (auto& row : fx)
            {
                const bool on = row.sw->isOn();
                if (on != row.wasOn) { row.wasOn = on; for (auto* kk : row.knobs) kk->setDimmed (! on); repaint (row.nameRect); }
            }
        }
    private:
        struct Row { bool left; String name; Switch* sw; std::vector<AnimKnob*> knobs; Rectangle<int> rect, nameRect; bool wasOn = true; };
        void addRow (bool left, const String& name, const String& onId, std::vector<std::pair<const char*, const char*>> ks)
        {
            Row r; r.left = left; r.name = name; r.sw = add<Switch> (proc, onId);
            for (auto& kv : ks) r.knobs.push_back (knob (kv.first, kv.second, AnimKnob::Inline));
            r.wasOn = ! r.sw->isOn();
            fx.push_back (r);
        }
        void layoutCol (bool left, Rectangle<int> area)
        {
            int count = 0; for (auto& row : fx) if (row.left == left) ++count;
            const int gap = 6, h = (area.getHeight() - gap * (count - 1)) / count;
            int y = area.getY();
            for (auto& row : fx)
            {
                if (row.left != left) continue;
                row.rect = { area.getX(), y, area.getWidth(), h };
                y += h + gap;
                auto in = row.rect.reduced (14, 4);
                row.sw->setBounds (in.removeFromLeft (44));
                in.removeFromLeft (8);
                row.nameRect = in.removeFromLeft (left ? 104 : 80);
                const int n = (int) row.knobs.size();
                const int cols = left ? n : 2, rws = left ? 1 : 2;
                const int kw = in.getWidth() / cols, kh = in.getHeight() / rws;
                for (int i = 0; i < n; ++i)
                    row.knobs[(size_t) i]->setBounds (in.getX() + (i % cols) * kw, in.getY() + (i / cols) * kh, kw - 4, kh);
            }
        }
        std::vector<Row> fx;
        Rectangle<int> leftCol, rightCol;
    };

    //--------------------------------------------------------------------------
    class PadsPage : public Page
    {
    public:
        explicit PadsPage (HarmonizerProcessor& p) : Page (p)
        {
            for (int i = 0; i < PadFx::Count; ++i) pads[i] = add<PadCell> (p, i);
            latch = add<Segments> (p, "pad_latch", StringArray { "Hold", "Latch" }, true);
            glide = add<Bar> (p, "glide_time");
        }
        void resized() override
        {
            auto r = kContent;
            side = r.removeFromRight (260);
            r.removeFromRight (20);
            const int w = r.getWidth() / 4, h = r.getHeight() / 3;
            for (int i = 0; i < PadFx::Count; ++i) pads[i]->setBounds (r.getX() + (i % 4) * w + 5, r.getY() + (i / 4) * h + 5, w - 10, h - 10);
            auto s = side;
            behRect = s.removeFromTop (150); s.removeFromTop (12);
            glideRect = s.removeFromTop (100); s.removeFromTop (12);
            learnRect = s.removeFromTop (110);
            latch->setBounds (behRect.reduced (16).withTrimmedTop (26).withHeight (40));
            glide->setBounds (glideRect.reduced (16).withTrimmedTop (28).withHeight (20));
        }
        void paint (Graphics& g) override
        {
            Theme::panel (g, behRect, 16.0f); Theme::panel (g, glideRect, 16.0f); Theme::panel (g, learnRect, 16.0f);
            Theme::label (g, "PAD BEHAVIOUR", behRect.reduced (16).withHeight (16));
            const bool l = latch->current() == 1;
            text (g, l ? "Tap once = on, tap again = off." : "Effect is on while you hold the pad.",
                  behRect.reduced (16).withTrimmedTop (76).withHeight (40), Theme::sans (13.0f), Theme::dim, Justification::topLeft);
            Theme::label (g, "GLIDE TIME", glideRect.reduced (16).withHeight (16));
            text (g, AnimKnob::valueText (proc.apvts.getParameter ("glide_time")), glideRect.reduced (16).withTrimmedTop (54).withHeight (16),
                  Theme::mono (12.0f), Theme::dim);
            Theme::label (g, "LEARN", learnRect.reduced (16).withHeight (16));
            para (g, "Right-click a pad > Learn > hit the pad on your KeyLab. Default: MIDI channel 10, from C1 upwards.",
                  learnRect.reduced (16).withTrimmedTop (24), Theme::sans (13.0f), Theme::soft);
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            const int l = latch->current();
            if (l != shownLatch) { shownLatch = l; repaint (behRect); }
            if (++frame % 6 == 0) repaint (glideRect);
        }
    private:
        PadCell* pads[PadFx::Count] {}; Segments* latch = nullptr; Bar* glide = nullptr;
        Rectangle<int> side, behRect, glideRect, learnRect;
        int shownLatch = -1, frame = 0;
    };

    //--------------------------------------------------------------------------
    class AtVelPage : public Page
    {
    public:
        explicit AtVelPage (HarmonizerProcessor& p) : Page (p)
        {
            atOn = add<Switch> (p, "at_on");
            atResp = add<Bar> (p, "at_response");
            velCurve = add<Bar> (p, "vel_curve");
            addRow (true, "Swell", "at_swell_on", "at_swell", "chord gets louder");
            addRow (true, "Shimmer", "at_shimmer_on", "at_shimmer", "octave sparkle on top");
            addRow (true, "Reverb", "at_reverb_on", "at_reverb", "more space");
            addRow (true, "Filter", "at_filter_on", "at_filter", "sound opens up");
            addRow (true, "Vibrato", "at_vib_on", "at_vib", "voices wobble");
            addRow (true, "Detune", "at_detune_on", "at_detune", "wider, floating");
            addRow (true, "Formant", "at_formant_on", "at_formant", "vowel colour shifts");
            addRow (false, "Volume", "", "velocity", "hard = louder");
            addRow (false, "Brightness", "vel_bright_on", "vel_bright", "hard = brighter");
            addRow (false, "Soft attack", "vel_attack_on", "vel_attack", "soft = fades in");
            addRow (false, "Echo send", "vel_send_on", "vel_send", "hard = into the delay");
        }
        void resized() override
        {
            auto r = kContent;
            const int w = (r.getWidth() - 18) / 2;
            cols[0] = r.removeFromLeft (w); r.removeFromLeft (18); cols[1] = r;
            for (int c = 0; c < 2; ++c)
            {
                auto in = cols[c].reduced (20, 18);
                head[c] = in.removeFromTop (40);
                live[c] = in.removeFromTop (14);
                in.removeFromTop (10);
                extra[c] = in.removeFromTop (22);
                in.removeFromTop (8);
                int count = 0; for (auto& rw : rowsV) if (rw.at == (c == 0)) ++count;
                const int rh = jmin (52, in.getHeight() / jmax (1, count));
                int y = in.getY();
                for (auto& rw : rowsV)
                {
                    if (rw.at != (c == 0)) continue;
                    rw.rect = { in.getX(), y, in.getWidth(), rh }; y += rh;
                    auto rr = rw.rect;
                    if (rw.sw != nullptr) rw.sw->setBounds (rr.removeFromLeft (44)); else rr.removeFromLeft (44);
                    rr.removeFromLeft (10);
                    rw.nameRect = rr.removeFromLeft (100);
                    rw.helpRect = rr.removeFromRight (160);
                    rw.bar->setBounds (rr.reduced (6, 0));
                }
            }
            atOn->setBounds (head[0].withLeft (head[0].getRight() - 60).withHeight (24));
            atResp->setBounds (extra[0].withTrimmedLeft (90).withTrimmedRight (80));
            velCurve->setBounds (extra[1].withTrimmedLeft (90).withTrimmedRight (80));
        }
        void paint (Graphics& g) override
        {
            static const char* titles[] = { "AFTERTOUCH", "VELOCITY" };
            static const char* subs[] = { "hold a key and press harder", "how hard you hit the key" };
            for (int c = 0; c < 2; ++c)
            {
                Theme::panel (g, cols[c]);
                Theme::label (g, titles[c], head[c].withHeight (16), Theme::soft);
                text (g, subs[c], head[c].withTrimmedTop (18).withHeight (18), Theme::sans (13.0f), Theme::dim);
                const float v = c == 0 ? proc.aftertouchShown.load() : proc.lastVelocity.load();
                if (c == 1) text (g, "LAST HIT  " + String (roundToInt (v * 127.0f)), head[1].withHeight (16), Theme::mono (12.0f), Theme::text, Justification::centredRight);
                auto b = live[c].toFloat().withSizeKeepingCentre ((float) live[c].getWidth(), 10.0f);
                g.setColour (Colour (0xff1f1f2b)); g.fillRoundedRectangle (b, 5.0f);
                g.setColour (Theme::accent); g.fillRoundedRectangle (b.withWidth (b.getWidth() * jlimit (0.0f, 1.0f, v)), 5.0f);
                text (g, c == 0 ? "Response" : "Curve", extra[c].withWidth (90), Theme::sans (13.0f), Theme::dim);
            }
            text (g, AnimKnob::valueText (proc.apvts.getParameter ("at_response")), extra[0].withLeft (extra[0].getRight() - 76), Theme::mono (12.0f), Theme::dim, Justification::centredRight);
            const float cv = proc.apvts.getRawParameterValue ("vel_curve")->load();
            text (g, cv < 0.4f ? "soft" : (cv > 0.6f ? "hard" : "linear"), extra[1].withLeft (extra[1].getRight() - 76), Theme::mono (12.0f), Theme::dim, Justification::centredRight);
            const bool atEnabled = atOn->isOn();
            for (auto& rw : rowsV)
            {
                const bool on = (rw.sw == nullptr || rw.sw->isOn()) && (! rw.at || atEnabled);
                text (g, rw.name, rw.nameRect, Theme::sans (14.0f), on ? Theme::text : Theme::mute);
                text (g, rw.help, rw.helpRect, Theme::sans (12.0f), on ? Theme::dim : Theme::mute);
            }
        }
        void tickPage (float dt) override
        {
            Page::tickPage (dt);
            const bool atEnabled = atOn->isOn();
            bool any = false;
            for (auto& rw : rowsV)
            {
                const bool on = (rw.sw == nullptr || rw.sw->isOn()) && (! rw.at || atEnabled);
                if (on != rw.wasOn) { rw.wasOn = on; rw.bar->setDimmed (! on); any = true; }
            }
            if (any) repaint();
            if (++frame % 2 == 0) { repaint (live[0]); repaint (live[1]); repaint (head[1]); repaint (extra[0]); repaint (extra[1]); }
        }
    private:
        struct Row { bool at; String name, help; Switch* sw = nullptr; Bar* bar = nullptr; Rectangle<int> rect, nameRect, helpRect; bool wasOn = true; };
        void addRow (bool at, const String& name, const String& swId, const String& amtId, const String& help)
        {
            Row r; r.at = at; r.name = name; r.help = help;
            if (swId.isNotEmpty()) r.sw = add<Switch> (proc, swId);
            r.bar = add<Bar> (proc, amtId);
            r.wasOn = ! ((r.sw == nullptr || r.sw->isOn()));
            rowsV.push_back (r);
        }
        Switch* atOn = nullptr; Bar* atResp = nullptr; Bar* velCurve = nullptr;
        std::vector<Row> rowsV;
        Rectangle<int> cols[2], head[2], live[2], extra[2];
        int frame = 0;
    };
}

//==============================================================================
HarmonizerEditor::HarmonizerEditor (HarmonizerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    pages.add (new MainPage (p));
    pages.add (new VoicePage (p));
    pages.add (new VocoderPage (p));
    pages.add (new ArpPage (p));
    pages.add (new EffectsPage (p));
    pages.add (new PadsPage (p));
    pages.add (new AtVelPage (p));
    for (auto* pg : pages) addChildComponent (pg);

    refreshPresetBox();
    presetBox.onChange = [this] { presetChosen (presetBox.getSelectedId()); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { savePreset(); };
    saveButton.setComponentID ("save");
    addAndMakeVisible (presetBox); addAndMakeVisible (prevPreset); addAndMakeVisible (nextPreset); addAndMakeVisible (saveButton);
    addChildComponent (dialog);

    setSize (1200, 640);
    showTab (0);
    lastTick = nowMs();
    startTimerHz (60);
}

HarmonizerEditor::~HarmonizerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HarmonizerEditor::showTab (int i)
{
    currentTab = jlimit (0, pages.size() - 1, i);
    for (int k = 0; k < pages.size(); ++k) pages[k]->setVisible (k == currentTab);
    repaint (0, 64, getWidth(), 50);
}

void HarmonizerEditor::resized()
{
    for (auto* pg : pages) pg->setBounds (getLocalBounds());
    auto h = Rectangle<int> (0, 0, getWidth(), 64).reduced (30, 12);
    nextPreset.setBounds (h.removeFromRight (40));
    h.removeFromRight (6);
    presetBox.setBounds (h.removeFromRight (230));
    h.removeFromRight (6);
    prevPreset.setBounds (h.removeFromRight (40));
    h.removeFromRight (10);
    saveButton.setBounds (h.removeFromRight (64));
    dialog.setBounds (getLocalBounds());
}

void HarmonizerEditor::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    // header
    text (g, "GLIZZARINE", { 30, 18, 160, 28 }, Theme::sans (22.0f, true), Theme::accent);
    text (g, "HARMONIZER", { 176, 18, 160, 28 }, Theme::sans (22.0f), Theme::dim);
    text (g, String (GLZH_VERSION) + "  " + String (__DATE__), { 318, 20, 140, 24 }, Theme::mono (11.0f), Theme::dim);
    auto st = Rectangle<int> (470, 18, 300, 28);
    g.setColour (statusSinging ? Theme::rec : Theme::mute);
    g.fillEllipse ((float) st.getX(), (float) st.getCentreY() - 4.5f, 9.0f, 9.0f);
    text (g, statusText, st.withTrimmedLeft (16), Theme::sans (13.0f), Theme::soft);
    if (statusHeal > 0)
        text (g, "repaired " + String (statusHeal) + "x", { 470, 40, 200, 16 }, Theme::sans (11.0f), Theme::orange);

    // tabs
    int x = 30;
    for (int i = 0; i < tabNames.size(); ++i)
    {
        auto f = Theme::sans (13.0f);
        f.setExtraKerningFactor (0.1f);
        const int w = (int) GlyphArrangement::getStringWidth (f, tabNames[i]) + 4;
        const bool on = i == currentTab;
        g.setFont (f); g.setColour (on ? Theme::text : Theme::dim);
        g.drawText (tabNames[i], x, 70, w, 36, Justification::centred);
        if (on) { g.setColour (Theme::accent); g.fillRect (x, 104, w, 2); }
        x += w + 26;
    }
    g.setColour (Theme::line); g.fillRect (0, 106, getWidth(), 1);
}

void HarmonizerEditor::mouseDown (const MouseEvent& e)
{
    if (e.y < 66 || e.y > 108) return;
    int x = 30;
    for (int i = 0; i < tabNames.size(); ++i)
    {
        auto f = Theme::sans (13.0f);
        f.setExtraKerningFactor (0.1f);
        const int w = (int) GlyphArrangement::getStringWidth (f, tabNames[i]) + 4;
        if (e.x >= x - 13 && e.x < x + w + 13) { showTab (i); return; }
        x += w + 26;
    }
}

void HarmonizerEditor::timerCallback()
{
    const double now = nowMs();
    const float dt = (float) jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    pages[currentTab]->tickPage (dt);

    // status line
    const bool sing = proc.singing.load();
    String s;
    if (sing) s << "singing · " << noteName (roundToInt (69.0 + 12.0 * std::log2 (jmax (20.0f, proc.detectedHz.load()) / 440.0f)));
    else s << "silent";
    s << " · " << proc.heldVoices.load() << " voices · " << String (proc.bpmShown.load(), 1) << " BPM · "
      << roundToInt (1000.0 * proc.latencySamples.load() / jmax (1.0, proc.getCurrentSampleRate())) << " ms latency";
    const int heal = proc.healCount.load();
    if (s != statusText || sing != statusSinging || heal != statusHeal)
    {
        statusText = s; statusSinging = sing; statusHeal = heal;
        repaint (460, 10, 360, 50);
    }
    // other instances may have saved presets: look at the folder now and then
    if (++presetScan >= 180) { presetScan = 0; if (HarmonizerProcessor::userPresetNames() != userNames) refreshPresetBox(); }
    if (! presetBox.isPopupActive() && presetBox.getSelectedId() != selectedIdForState())
        presetBox.setSelectedId (selectedIdForState(), dontSendNotification);
}

//==============================================================================
// Presets: factory 1..N, own presets 1001.., actions 9001..
namespace { enum { kUserBase = 1001, kSaveAs = 9001, kDelete = 9002, kFolder = 9003, kNone = 9000 }; }

int HarmonizerEditor::selectedIdForState() const
{
    const auto u = proc.getUserPresetName();
    if (u.isNotEmpty()) { const int i = userNames.indexOf (u); return i >= 0 ? kUserBase + i : 0; }
    return proc.getCurrentProgram() + 1;
}

void HarmonizerEditor::refreshPresetBox()
{
    userNames = HarmonizerProcessor::userPresetNames();
    presetBox.clear (dontSendNotification);
    presetBox.addSectionHeading ("FACTORY");
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.addSeparator();
    presetBox.addSectionHeading ("MY PRESETS");
    if (userNames.isEmpty()) { presetBox.addItem ("(none saved yet)", kNone); presetBox.setItemEnabled (kNone, false); }
    for (int i = 0; i < userNames.size(); ++i) presetBox.addItem (userNames[i], kUserBase + i);
    presetBox.addSeparator();
    presetBox.addItem ("Save as new preset...", kSaveAs);
    const auto u = proc.getUserPresetName();
    presetBox.addItem (u.isNotEmpty() ? "Delete \"" + u + "\"" : String ("Delete (load one of your presets first)"), kDelete);
    presetBox.setItemEnabled (kDelete, u.isNotEmpty());
    presetBox.addItem ("Open preset folder", kFolder);
    presetBox.setSelectedId (selectedIdForState(), dontSendNotification);
    if (u.isNotEmpty() && userNames.indexOf (u) < 0) presetBox.setText (u, dontSendNotification);
}

void HarmonizerEditor::presetChosen (int id)
{
    if (id >= 1 && id <= proc.getNumPrograms()) proc.setCurrentProgram (id - 1);
    else if (id >= kUserBase && id < kUserBase + userNames.size())
    {
        if (! proc.loadUserPreset (userNames[id - kUserBase]))
            dialog.askConfirm ("PRESET", "This preset could not be read.", "OK", nullptr);
    }
    else if (id == kSaveAs) savePreset();
    else if (id == kDelete)
    {
        const auto u = proc.getUserPresetName();
        if (u.isNotEmpty())
            dialog.askConfirm ("DELETE PRESET", "Delete \"" + u + "\" for good?", "Delete", [this, u]
            {
                proc.deleteUserPreset (u);   // the sound stays as it is, only the file is gone
                refreshPresetBox();
            });
    }
    else if (id == kFolder)
    {
        auto dir = HarmonizerProcessor::userPresetFolder();
        dir.createDirectory();
        dir.startAsProcess();
    }
    refreshPresetBox();
}

void HarmonizerEditor::stepPreset (int delta)
{
    const int nf = proc.getNumPrograms(), total = nf + userNames.size();
    if (total <= 0) return;
    const auto u = proc.getUserPresetName();
    int cur = u.isNotEmpty() ? nf + jmax (0, userNames.indexOf (u)) : proc.getCurrentProgram();
    cur = (cur + delta + total) % total;
    presetChosen (cur < nf ? cur + 1 : kUserBase + cur - nf);
}

void HarmonizerEditor::savePreset()
{
    const auto u = proc.getUserPresetName();
    dialog.askName (u.isNotEmpty() ? u : String ("My ") + proc.getProgramName (proc.getCurrentProgram()), [this] (const String& name)
    {
        auto doSave = [this, name]
        {
            if (! proc.saveUserPreset (name))
                dialog.askConfirm ("SAVE PRESET", "Could not write the file to Documents / Glizzarine Presets.", "OK", nullptr);
            refreshPresetBox();
        };
        if (HarmonizerProcessor::userPresetFile (name).existsAsFile())
            dialog.askConfirm ("OVERWRITE?", "A preset called \"" + name + "\" already exists. Replace it with the current sound?", "Overwrite", doSave);
        else doSave();
    });
}
