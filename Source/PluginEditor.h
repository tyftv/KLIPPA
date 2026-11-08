#pragma once
#include "PluginProcessor.h"

struct KlippaLook : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

class KlippaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KlippaEditor (KlippaProcessor&);
    ~KlippaEditor() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint (scope); }

    KlippaProcessor& proc;
    KlippaLook look;
    juce::Rectangle<int> scope;

    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; };
    Knob drive, curve, ceiling, mix;
    juce::ToggleButton hq { "HQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqA;

    void setup (Knob&, const juce::String& id, const juce::String& name, const juce::String& suffix, int dp);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlippaEditor)
};
