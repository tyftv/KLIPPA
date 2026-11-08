#include "PluginEditor.h"

static const juce::Colour bg     { 0xff0e0e10 };
static const juce::Colour panel  { 0xff17171b };
static const juce::Colour accent { 0xffff5a1f };
static const juce::Colour ink    { 0xfff2ece4 };
static const juce::Colour dim    { 0xff6d6a66 };

void KlippaLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                   float a0, float a1, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const float ang = a0 + pos * (a1 - a0);

    g.setColour (panel);
    g.fillEllipse (r);
    juce::Path track; track.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f - 3, d * 0.5f - 3, 0, a0, a1, true);
    g.setColour (dim.withAlpha (0.5f));
    g.strokePath (track, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f - 3, d * 0.5f - 3, 0, a0, ang, true);
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -d * 0.5f + 9, 3.f, d * 0.22f, 1.5f);
    g.setColour (ink);
    g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (r.getCentreX(), r.getCentreY()));
}

void KlippaEditor::setup (Knob& k, const juce::String& id, const juce::String& name, const juce::String& suffix, int dp)
{
    k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    k.s.setColour (juce::Slider::textBoxTextColourId, ink);
    k.s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.s.setNumDecimalPlacesToDisplay (dp);
    k.s.setTextValueSuffix (suffix);
    k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    auto* prm = proc.apvts.getParameter (id);
    k.s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (k.s);
    k.l.setText (name, juce::dontSendNotification);
    k.l.setJustificationType (juce::Justification::centred);
    k.l.setColour (juce::Label::textColourId, dim);
    k.l.setFont (juce::Font (juce::FontOptions (13.f, juce::Font::bold)));
    addAndMakeVisible (k.l);
    k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.s);
}

KlippaEditor::KlippaEditor (KlippaProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    setup (drive,   "drive",   "DRIVE",     " dB", 1);
    setup (curve,   "curve",   "HARD / SOFT", "",  2);
    setup (asym,    "asym",    "ASYMMETRY", "",    2);
    setup (ceiling, "ceiling", "CEILING",   " dB", 1);
    setup (mix,     "mix",     "MIX",       "",    2);

    osBox.addItemList (juce::StringArray { "Off (0 latency)", "2x", "4x", "8x" }, 1);
    osBox.setColour (juce::ComboBox::backgroundColourId, panel);
    osBox.setColour (juce::ComboBox::textColourId, ink);
    osBox.setColour (juce::ComboBox::outlineColourId, dim);
    addAndMakeVisible (osBox);
    osA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "os", osBox);

    setSize (720, 470);
    startTimerHz (30);
}

void KlippaEditor::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto head = r.removeFromTop (40);
    osBox.setBounds (head.removeFromRight (150).reduced (0, 6));
    r.removeFromTop (8);

    auto top = r.removeFromTop (190);
    meterInR  = top.removeFromLeft (14);
    top.removeFromLeft (6);
    meterOutR = top.removeFromRight (14);
    top.removeFromRight (6);
    curveR = top.removeFromRight (190);
    top.removeFromRight (10);
    scopeR = top;

    r.removeFromTop (10);
    const int w = r.getWidth() / 5;
    Knob* ks[] = { &drive, &curve, &asym, &ceiling, &mix };
    for (auto* k : ks)
    {
        auto c = r.removeFromLeft (w);
        k->l.setBounds (c.removeFromTop (18));
        k->s.setBounds (c);
    }
}

void KlippaEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float lin, const juce::String& label)
{
    g.setColour (panel); g.fillRoundedRectangle (r, 4.f);
    const float db = juce::Decibels::gainToDecibels (lin, -48.f);
    const float f = juce::jlimit (0.f, 1.f, (db + 48.f) / 54.f);   // -48 .. +6 dB
    auto bar = r.withTop (r.getBottom() - r.getHeight() * f);
    g.setColour (db > 0.f ? juce::Colours::red : accent);
    g.fillRoundedRectangle (bar, 4.f);
    const float zeroY = r.getBottom() - r.getHeight() * (48.f / 54.f);
    g.setColour (ink.withAlpha (0.6f));
    g.drawLine (r.getX(), zeroY, r.getRight(), zeroY, 1.f);
    g.setColour (dim); g.setFont (10.f);
    g.drawText (label, r.withY (r.getBottom() + 1).withHeight (12), juce::Justification::centred);
}

void KlippaEditor::drawScope (juce::Graphics& g, juce::Rectangle<float> s)
{
    g.setColour (panel); g.fillRoundedRectangle (s, 8.f);
    const float ceilLin = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue ("ceiling")->load());
    const float range = 1.6f;
    auto mapY = [&] (float v) { return s.getCentreY() - juce::jlimit (-range, range, v) / range * s.getHeight() * 0.5f; };

    g.setColour (dim.withAlpha (0.35f));
    g.drawLine (s.getX(), s.getCentreY(), s.getRight(), s.getCentreY());
    g.setColour (accent.withAlpha (0.55f));            // ceiling lines
    g.drawLine (s.getX(), mapY (ceilLin),  s.getRight(), mapY (ceilLin),  1.f);
    g.drawLine (s.getX(), mapY (-ceilLin), s.getRight(), mapY (-ceilLin), 1.f);

    const int N = KlippaProcessor::scopeSize;
    const int start = proc.scopePos.load();
    juce::Path pin, pout;
    for (int i = 0; i < N; ++i)
    {
        const int idx = (start + i) % N;
        const float x = s.getX() + s.getWidth() * (float) i / (float) (N - 1);
        const float yi = mapY (proc.scopeIn[(size_t) idx]), yo = mapY (proc.scopeOut[(size_t) idx]);
        if (i == 0) { pin.startNewSubPath (x, yi); pout.startNewSubPath (x, yo); }
        else        { pin.lineTo (x, yi);          pout.lineTo (x, yo); }
    }
    g.setColour (ink.withAlpha (0.35f));
    g.strokePath (pin, juce::PathStrokeType (1.2f));
    g.setColour (accent);
    g.strokePath (pout, juce::PathStrokeType (2.f));
    g.setColour (dim); g.setFont (10.f);
    g.drawText ("IN (after drive)  /  OUT", s.reduced (8, 4), juce::Justification::topLeft);
}

void KlippaEditor::drawCurve (juce::Graphics& g, juce::Rectangle<float> s)
{
    g.setColour (panel); g.fillRoundedRectangle (s, 8.f);
    g.setColour (dim.withAlpha (0.35f));
    g.drawLine (s.getX(), s.getCentreY(), s.getRight(), s.getCentreY());
    g.drawLine (s.getCentreX(), s.getY(), s.getCentreX(), s.getBottom());

    const float p = klippa::curveToP (proc.apvts.getRawParameterValue ("curve")->load());
    const float bias = proc.apvts.getRawParameterValue ("asym")->load() * 0.6f;
    const float off = bias > 0.f ? klippa::clip (bias, p) : 0.f;
    const float ceilLin = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue ("ceiling")->load());
    const float xr = 2.0f;
    auto mapX = [&] (float x) { return s.getCentreX() + x / xr * s.getWidth() * 0.5f; };
    auto mapY = [&] (float y) { return s.getCentreY() - y / xr * s.getHeight() * 0.5f; };

    juce::Path path;
    for (int i = 0; i <= 240; ++i)
    {
        const float x = -xr + 2.f * xr * (float) i / 240.f;
        const float y = klippa::clipAsym (x, p, bias, off) * ceilLin;
        if (i == 0) path.startNewSubPath (mapX (x), mapY (y)); else path.lineTo (mapX (x), mapY (y));
    }
    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (2.5f));

    const float pk = juce::jmin (xr, proc.inPeak.load());
    const float dy = klippa::clipAsym (pk, p, bias, off) * ceilLin;
    g.setColour (ink);
    g.fillEllipse (mapX (pk) - 4.f, mapY (dy) - 4.f, 8.f, 8.f);
}

void KlippaEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (ink);
    g.setFont (juce::Font (juce::FontOptions (30.f, juce::Font::bold)));
    g.drawText ("KLIPPA", 18, 14, 300, 40, juce::Justification::centredLeft);

    drawScope (g, scopeR.toFloat());
    drawCurve (g, curveR.toFloat());
    drawMeter (g, meterInR.toFloat(),  proc.inPeak.load(),  "IN");
    drawMeter (g, meterOutR.toFloat(), proc.outPeak.load(), "OUT");
}
