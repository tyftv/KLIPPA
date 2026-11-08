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

    // UI readouts (written on the audio thread, read on the message thread)
    std::atomic<float> inPeak  { 0.f };   // linear, after drive
    std::atomic<float> outPeak { 0.f };   // linear, final output
    std::atomic<float> clipAmount { 0.f };// dB of peak reduction this block
    static constexpr int scopeSize = 512, scopeDecimate = 32;
    std::array<float, scopeSize> scopeIn {}, scopeOut {};
    std::atomic<int> scopePos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setupOversampling();

    juce::SmoothedValue<float> sDrive, sCurve, sCeil, sMix, sAsym;
    std::unique_ptr<juce::dsp::Oversampling<float>> osList[3];
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dry { 1024 };
    int osIndex = 0;                 // 0 = off, 1..3 = 2x/4x/8x
    int scopeCount = 0;
    juce::AudioBuffer<float> wetBuf, ramp;

    std::atomic<float>* pDrive; std::atomic<float>* pCurve; std::atomic<float>* pCeil;
    std::atomic<float>* pMix;   std::atomic<float>* pAsym;  std::atomic<float>* pOS;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlippaProcessor)
};
