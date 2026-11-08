#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout KlippaProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
           NormalisableRange<float> (0.f, 36.f, 0.01f), 6.f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "curve", 1 }, "Curve",
           NormalisableRange<float> (0.f, 1.f, 0.001f), 0.35f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
           NormalisableRange<float> (-24.f, 0.f, 0.01f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
           NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "hq", 1 }, "HQ", false));
    return l;
}

KlippaProcessor::KlippaProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KLIPPA", createLayout())
{
    pDrive = apvts.getRawParameterValue ("drive");
    pCurve = apvts.getRawParameterValue ("curve");
    pCeil  = apvts.getRawParameterValue ("ceiling");
    pMix   = apvts.getRawParameterValue ("mix");
    pHQ    = apvts.getRawParameterValue ("hq");
}

bool KlippaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono())
           && o == l.getMainInputChannelSet();
}

void KlippaProcessor::prepareToPlay (double sr, int block)
{
    maxBlock = block;
    const int ch = juce::jmax (1, getTotalNumInputChannels());
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, (juce::uint32) ch };

    os.initProcessing ((size_t) block);
    dry.prepare (spec);
    wetBuf.setSize (ch, block, false, false, true);
    ramp.setSize (4, block, false, false, true);

    sDrive.reset (sr, 0.02); sCurve.reset (sr, 0.02); sCeil.reset (sr, 0.02); sMix.reset (sr, 0.02);
    sDrive.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDrive->load()));
    sCurve.setCurrentAndTargetValue (pCurve->load());
    sCeil .setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pCeil->load()));
    sMix  .setCurrentAndTargetValue (pMix->load());

    hqActive = pHQ->load() > 0.5f;
    setLatencySamples (hqActive ? (int) std::ceil (os.getLatencyInSamples()) : 0);
}

void KlippaProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int chs = buf.getNumChannels();

    const bool hq = pHQ->load() > 0.5f;
    if (hq != hqActive)
    {
        hqActive = hq;
        os.reset(); dry.reset();
        setLatencySamples (hq ? (int) std::ceil (os.getLatencyInSamples()) : 0);
    }

    sDrive.setTargetValue (juce::Decibels::decibelsToGain (pDrive->load()));
    sCurve.setTargetValue (pCurve->load());
    sCeil .setTargetValue (juce::Decibels::decibelsToGain (pCeil->load()));
    sMix  .setTargetValue (pMix->load());

    // per-sample parameter ramps, shared by all channels
    if (n > ramp.getNumSamples()) ramp.setSize (4, n, false, false, true); // only if host exceeds declared block
    float* drv = ramp.getWritePointer (0); float* crv = ramp.getWritePointer (1);
    float* cel = ramp.getWritePointer (2); float* mx  = ramp.getWritePointer (3);
    for (int i = 0; i < n; ++i)
    { drv[i] = sDrive.getNextValue(); crv[i] = sCurve.getNextValue();
      cel[i] = sCeil.getNextValue();  mx[i]  = sMix.getNextValue(); }

    float peak = 0.f;

    if (! hq)
    {
        for (int c = 0; c < chs; ++c)
        {
            auto* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i] * drv[i];
                peak = juce::jmax (peak, std::abs (x));
                const float y = klippa::clip (x, klippa::curveToP (crv[i])) * cel[i];
                d[i] = d[i] + mx[i] * (y - d[i]);
            }
        }
    }
    else
    {
        // keep dry path time-aligned with the oversampler latency
        dry.setDelay (os.getLatencyInSamples());
        for (int c = 0; c < chs; ++c)
        {
            auto* d = buf.getWritePointer (c);
            auto* w = wetBuf.getWritePointer (c);
            for (int i = 0; i < n; ++i) { w[i] = d[i]; dry.pushSample (c, d[i]); d[i] = dry.popSample (c); }
        }

        if (n > wetBuf.getNumSamples()) wetBuf.setSize (wetBuf.getNumChannels(), n, false, false, true);
        juce::dsp::AudioBlock<float> wetBlock (wetBuf.getArrayOfWritePointers(), (size_t) chs, (size_t) n);
        auto up = os.processSamplesUp (wetBlock);
        const int un = (int) up.getNumSamples();
        const int factor = un / juce::jmax (1, n);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* u = up.getChannelPointer (c);
            for (int i = 0; i < un; ++i)
            {
                const int k = juce::jmin (n - 1, i / factor);
                const float x = u[i] * drv[k];
                peak = juce::jmax (peak, std::abs (x));
                u[i] = klippa::clip (x, klippa::curveToP (crv[k])) * cel[k];
            }
        }
        os.processSamplesDown (wetBlock);

        for (int c = 0; c < chs; ++c)
        {
            auto* d = buf.getWritePointer (c);
            auto* w = wetBuf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                // oversampler filter ringing can overshoot slightly: hard-stop at the ceiling
                const float wc = juce::jlimit (-cel[i], cel[i], w[i]);
                d[i] = d[i] + mx[i] * (wc - d[i]);
            }
        }
    }

    inPeak.store (juce::jmax (peak, inPeak.load() * 0.9f));
}

juce::AudioProcessorEditor* KlippaProcessor::createEditor() { return new KlippaEditor (*this); }

void KlippaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void KlippaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KlippaProcessor(); }
