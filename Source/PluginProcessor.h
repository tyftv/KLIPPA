#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "KlippaDSP.h"

class KlippaProcessor : public juce::AudioProcessor
{
public:
    KlippaProcessor();
    ~KlippaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "KLIPPA"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPeak { 0.f };   // for the UI (linear, post-drive)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::SmoothedValue<float> sDrive, sCurve, sCeil, sMix;
    juce::dsp::Oversampling<float> os { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dry { 256 };
    bool hqActive = false;
    int maxBlock = 512;
    juce::AudioBuffer<float> wetBuf, ramp;

    std::atomic<float>* pDrive; std::atomic<float>* pCurve; std::atomic<float>* pCeil;
    std::atomic<float>* pMix;   std::atomic<float>* pHQ;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlippaProcessor)
};
