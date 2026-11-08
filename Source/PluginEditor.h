#pragma once
#include "PluginProcessor.h"

struct KlippaLook : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
};

class KlippaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KlippaEditor (KlippaProcessor&);
    ~KlippaEditor() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawCurve (juce::Graphics&, juce::Rectangle<float>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float lin, const juce::String&);

    KlippaProcessor& proc;
    KlippaLook look;
    juce::Rectangle<int> scopeR, curveR, meterInR, meterOutR;

    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; };
    Knob drive, curve, asym, ceiling, mix;
    juce::ComboBox osBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osA;

    void setup (Knob&, const juce::String& id, const juce::String& name, const juce::String& suffix, int dp);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlippaEditor)
};
