#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// "Dark Clean" look: one dark ground, cards, one accent colour for whatever is active.
namespace Theme
{
    inline const juce::Colour bg      { 0xff09090d };
    inline const juce::Colour card    { 0xff101017 };
    inline const juce::Colour card2   { 0xff14141c };
    inline const juce::Colour line    { 0xff1c1c26 };
    inline const juce::Colour edge    { 0xff2a2a38 };
    inline const juce::Colour track   { 0xff242432 };
    inline const juce::Colour hub     { 0xff17171f };
    inline const juce::Colour text    { 0xffecebf2 };
    inline const juce::Colour soft    { 0xffc9c7d8 };
    inline const juce::Colour dim     { 0xff8f8da3 };
    inline const juce::Colour mute    { 0xff4d4b60 };
    inline const juce::Colour knob    { 0xffbdbacd };
    inline const juce::Colour accent  { 0xffff5fd2 };
    inline const juce::Colour violet  { 0xff8f7bff };
    inline const juce::Colour orange  { 0xffffb547 };
    inline const juce::Colour rec     { 0xffff3b4f };

    juce::Font sans (float size, bool bold = false);
    juce::Font mono (float size, bool bold = false);
    void label (juce::Graphics&, const juce::String& caps, juce::Rectangle<int> r, juce::Colour c = dim,
                juce::Justification j = juce::Justification::centredLeft);
    void panel (juce::Graphics&, juce::Rectangle<int> r, float radius = 20.0f, juce::Colour c = card);
}

class HarmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HarmLookAndFeel();
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::sans (14.0f); }
    juce::Font getPopupMenuFont() override { return Theme::sans (14.0f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
};

//==============================================================================
// Anything that animates gets ticked by the editor's timer.
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick (float dt) = 0;
};

// A knob that glides to its value, leaves a faint trail and lights up when it moves
// (also when you turn the knob on the KeyLab). Right click = MIDI learn.
class AnimKnob : public juce::Slider, public Animated
{
public:
    enum Style { Big, Inline, Ring };
    AnimKnob (HarmonizerProcessor& p, const juce::String& paramId, const juce::String& name, Style s = Big,
              const juce::String& help = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void valueChanged() override;
    void tick (float dt) override;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    void setCaption (const juce::String& c) { caption = c; repaint(); }
    static juce::String valueText (juce::RangedAudioParameter*);

private:
    void drawKnob (juce::Graphics&, juce::Point<float> c, float radius, float thick);
    HarmonizerProcessor& proc;
    juce::String id, name, help, caption;
    Style style;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float disp = 0.0f, velo = 0.0f, trailFrom = 0.0f, trailAlpha = 0.0f, glow = 0.0f, bubble = 0.0f;
    double activeUntil = 0.0;
    bool dimmed = false, first = true;
};

// A row of choices (choice, bool or whole-number parameter). Click = select.
class Segments : public juce::Component, public Animated
{
public:
    Segments (HarmonizerProcessor& p, const juce::String& paramId, juce::StringArray labels, bool joined = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float) override;
    int current() const;
private:
    juce::RangedAudioParameter* param = nullptr;
    juce::StringArray labels;
    bool joined;
    int shown = -1;
};

// The three big MODE cards on the ARP tab
class ModeCards : public juce::Component, public Animated
{
public:
    explicit ModeCards (HarmonizerProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float) override;
    int current() const;
private:
    juce::RangedAudioParameter* param = nullptr;
    int shown = -1;
};

// The vocoder carrier list with little waveforms
class CarrierList : public juce::Component, public Animated
{
public:
    explicit CarrierList (HarmonizerProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float) override;
private:
    juce::RangedAudioParameter* param = nullptr;
    int shown = -1;
};

// A pill on/off switch bound to a bool parameter
class Switch : public juce::Component, public Animated
{
public:
    Switch (HarmonizerProcessor& p, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float dt) override;
    bool isOn() const;
private:
    juce::RangedAudioParameter* param = nullptr;
    float pos = 0.0f;
};

// A thin horizontal amount bar (drag it)
class Bar : public juce::Slider
{
public:
    Bar (HarmonizerProcessor& p, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool dimmed = false;
};

// A number you drag up/down (own BPM)
class NumberBox : public juce::Slider
{
public:
    NumberBox (HarmonizerProcessor& p, const juce::String& paramId, const juce::String& suffix);
    void paint (juce::Graphics&) override;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String suffix;
};

// One KeyLab pad slot
class PadCell : public juce::Component, public Animated
{
public:
    PadCell (HarmonizerProcessor& p, int fxIndex);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick (float dt) override;
private:
    HarmonizerProcessor& proc;
    int fx;
    bool lit = false, learning = false;
    float glow = 0.0f;
    juce::String mapping;
};

//==============================================================================
// Save / overwrite / delete questions for presets, shown inside the plugin window (never behind the DAW)
class PresetDialog : public juce::Component
{
public:
    PresetDialog();
    void askName (const juce::String& suggestion, std::function<void (const juce::String&)> onSave);
    void askConfirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
    bool keyPressed (const juce::KeyPress&) override;
private:
    void close() { setVisible (false); nameFn = nullptr; okFn = nullptr; }
    void ok();
    juce::TextEditor name;
    juce::TextButton okButton, cancelButton { "Cancel" };
    juce::String title, message;
    bool asking = false;
    std::function<void (const juce::String&)> nameFn;
    std::function<void()> okFn;
    juce::Rectangle<int> card;
};

//==============================================================================
// A tab page: owns its controls and ticks them.
class Page : public juce::Component
{
public:
    // the page covers the whole window, so it must let clicks through to the tab bar (only its controls take clicks)
    explicit Page (HarmonizerProcessor& p) : proc (p) { setInterceptsMouseClicks (false, true); }
    virtual void tickPage (float dt) { for (auto* a : animated) a->tick (dt); }
protected:
    template <typename T, typename... Args> T* add (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        if (auto* a = dynamic_cast<Animated*> (c)) animated.push_back (a);
        return c;
    }
    AnimKnob* knob (const juce::String& id, const juce::String& name, AnimKnob::Style s = AnimKnob::Big, const juce::String& help = {})
    { return add<AnimKnob> (proc, id, name, s, help); }
    HarmonizerProcessor& proc;
    juce::OwnedArray<juce::Component> owned;
    std::vector<Animated*> animated;
};

class HarmonizerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HarmonizerEditor (HarmonizerProcessor&);
    ~HarmonizerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void showTab (int i);

private:
    void timerCallback() override;
    void refreshPresetBox();
    int selectedIdForState() const;
    void presetChosen (int id);
    void stepPreset (int delta);
    void savePreset();

    HarmonizerProcessor& proc;
    HarmLookAndFeel lnf;
    juce::OwnedArray<Page> pages;
    juce::StringArray tabNames { "MAIN", "VOICE", "VOCODER", "ARP", "EFFECTS", "PADS", "AT & VEL" };
    int currentTab = 0;
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, saveButton { "SAVE" };
    juce::StringArray userNames;
    PresetDialog dialog;
    int presetScan = 0;
    double lastTick = 0.0;
    juce::String statusText;
    int statusHeal = 0;
    bool statusSinging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerEditor)
};
