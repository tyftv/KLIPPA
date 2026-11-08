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
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "asym", 1 }, "Asymmetry",
           NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
           NormalisableRange<float> (-24.f, 0.f, 0.01f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
           NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling",
           StringArray { "Off (0 latency)", "2x", "4x", "8x" }, 0));
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
    pAsym  = apvts.getRawParameterValue ("asym");
    pCeil  = apvts.getRawParameterValue ("ceiling");
    pMix   = apvts.getRawParameterValue ("mix");
    pOS    = apvts.getRawParameterValue ("os");
}

bool KlippaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono())
           && o == l.getMainInputChannelSet();
}

void KlippaProcessor::setupOversampling()
{
    osIndex = juce::jlimit (0, 3, (int) std::lround (pOS->load()));
    setLatencySamples (osIndex > 0 ? (int) std::lround (osList[osIndex - 1]->getLatencyInSamples()) : 0);
    for (auto& o : osList) if (o) o->reset();
    dry.reset();
    if (osIndex > 0) dry.setDelay (osList[osIndex - 1]->getLatencyInSamples());
}

void KlippaProcessor::prepareToPlay (double sr, int block)
{
    const int ch = juce::jmax (1, getTotalNumInputChannels());
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, (juce::uint32) ch };

    for (int i = 0; i < 3; ++i)
    {
        // useIntegerLatency = true so the dry path can be aligned exactly
        osList[i] = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) ch, (size_t) (i + 1),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        osList[i]->initProcessing ((size_t) block);
    }
    dry.prepare (spec);
    wetBuf.setSize (ch, block, false, false, true);
    ramp.setSize (6, block, false, false, true);

    for (auto* s : { &sDrive, &sCurve, &sCeil, &sMix, &sAsym }) s->reset (sr, 0.02);
    sDrive.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pDrive->load()));
    sCurve.setCurrentAndTargetValue (pCurve->load());
    sAsym .setCurrentAndTargetValue (pAsym->load());
    sCeil .setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pCeil->load()));
    sMix  .setCurrentAndTargetValue (pMix->load());
    setupOversampling();
}

void KlippaProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int chs = buf.getNumChannels();

    if (juce::jlimit (0, 3, (int) std::lround (pOS->load())) != osIndex)
        setupOversampling();

    sDrive.setTargetValue (juce::Decibels::decibelsToGain (pDrive->load()));
    sCurve.setTargetValue (pCurve->load());
    sAsym .setTargetValue (pAsym->load() * 0.6f);   // bias amount
    sCeil .setTargetValue (juce::Decibels::decibelsToGain (pCeil->load()));
    sMix  .setTargetValue (pMix->load());

    if (n > ramp.getNumSamples()) ramp.setSize (6, n, false, false, true); // host exceeded declared block
    float* drv = ramp.getWritePointer (0); float* crv = ramp.getWritePointer (1);
    float* cel = ramp.getWritePointer (2); float* mx  = ramp.getWritePointer (3);
    float* bia = ramp.getWritePointer (4);
    for (int i = 0; i < n; ++i)
    { drv[i] = sDrive.getNextValue(); crv[i] = sCurve.getNextValue();
      cel[i] = sCeil.getNextValue();  mx[i]  = sMix.getNextValue(); bia[i] = sAsym.getNextValue(); }

    float* inCopy = ramp.getWritePointer (5);   // pre-processing copy of ch0 for the scope
    juce::FloatVectorOperations::copy (inCopy, buf.getReadPointer (0), n);

    float pkIn = 0.f, pkOut = 0.f, pkDry = 0.f;
    auto shape = [] (float x, float curve, float bias) noexcept
    {
        const float p = klippa::curveToP (curve);
        return klippa::clipAsym (x, p, bias, bias > 0.f ? klippa::clip (bias, p) : 0.f);
    };

    if (osIndex == 0)
    {
        for (int c = 0; c < chs; ++c)
        {
            auto* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i] * drv[i];
                pkIn = juce::jmax (pkIn, std::abs (x));
                pkDry = juce::jmax (pkDry, std::abs (d[i]));
                const float y = shape (x, crv[i], bia[i]) * cel[i];
                d[i] = d[i] + mx[i] * (y - d[i]);
                pkOut = juce::jmax (pkOut, std::abs (d[i]));
            }
        }
    }
    else
    {
        auto& os = *osList[osIndex - 1];
        if (n > wetBuf.getNumSamples()) wetBuf.setSize (wetBuf.getNumChannels(), n, false, false, true);

        // dry path delayed to match oversampler latency; wet path copied for processing
        for (int c = 0; c < chs; ++c)
        {
            auto* d = buf.getWritePointer (c);
            auto* w = wetBuf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                w[i] = d[i];
                pkDry = juce::jmax (pkDry, std::abs (d[i]));
                dry.pushSample (c, d[i]);
                d[i] = dry.popSample (c);
            }
        }

        juce::dsp::AudioBlock<float> wetBlock (wetBuf.getArrayOfWritePointers(), (size_t) chs, (size_t) n);
        auto up = os.processSamplesUp (wetBlock);
        const int un = (int) up.getNumSamples();
        const int factor = juce::jmax (1, un / juce::jmax (1, n));
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* u = up.getChannelPointer (c);
            for (int i = 0; i < un; ++i)
            {
                const int k = juce::jmin (n - 1, i / factor);
                const float x = u[i] * drv[k];
                pkIn = juce::jmax (pkIn, std::abs (x));
                u[i] = shape (x, crv[k], bia[k]) * cel[k];
            }
        }
        os.processSamplesDown (wetBlock);

        for (int c = 0; c < chs; ++c)
        {
            auto* d = buf.getWritePointer (c);
            auto* w = wetBuf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                // downsampling filter can ring slightly past the ceiling: hard-stop it
                const float wc = juce::jlimit (-cel[i], cel[i], w[i]);
                d[i] = d[i] + mx[i] * (wc - d[i]);
                pkOut = juce::jmax (pkOut, std::abs (d[i]));
            }
        }
    }

    // scope capture (channel 0), decimated
    {
        auto* out0 = buf.getReadPointer (0);
        int pos = scopePos.load();
        for (int i = 0; i < n; ++i)
            if (++scopeCount >= scopeDecimate)
            {
                scopeCount = 0;
                scopeIn [(size_t) pos] = inCopy[i] * drv[i];
                scopeOut[(size_t) pos] = out0[i];
                pos = (pos + 1) % scopeSize;
            }
        scopePos.store (pos);
    }

    inPeak .store (juce::jmax (pkIn,  inPeak.load()  * 0.92f));
    outPeak.store (juce::jmax (pkOut, outPeak.load() * 0.92f));
    juce::ignoreUnused (pkDry);
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
