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

void KlippaLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : panel);
    g.fillRoundedRectangle (r, 6.f);
    g.setColour (on ? juce::Colours::black : dim);
    g.setFont (juce::Font (juce::FontOptions (15.f, juce::Font::bold)));
    g.drawText ("HQ", r, juce::Justification::centred);
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
    k.s.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
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
    setup (drive,   "drive",   "DRIVE",   " dB", 1);
    setup (curve,   "curve",   "HARD  <  >  SOFT", "", 2);
    setup (ceiling, "ceiling", "CEILING", " dB", 1);
    setup (mix,     "mix",     "MIX",     "", 2);
    addAndMakeVisible (hq);
    hqA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hq", hq);
    setSize (620, 400);
    startTimerHz (30);
}

void KlippaEditor::resized()
{
    auto r = getLocalBounds().reduced (18);
    r.removeFromTop (46);
    scope = r.removeFromTop (160);
    r.removeFromTop (10);
    auto row = r;
    const int w = row.getWidth() / 4;
    Knob* ks[] = { &drive, &curve, &ceiling, &mix };
    for (auto* k : ks)
    {
        auto c = row.removeFromLeft (w);
        k->l.setBounds (c.removeFromTop (18));
        k->s.setBounds (c);
    }
    hq.setBounds (getWidth() - 18 - 56, 16, 56, 28);
}

void KlippaEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (ink);
    g.setFont (juce::Font (juce::FontOptions (30.f, juce::Font::bold)));
    g.drawText ("KLIPPA", 18, 10, 300, 40, juce::Justification::centredLeft);

    // live transfer curve
    auto s = scope.toFloat();
    g.setColour (panel); g.fillRoundedRectangle (s, 8.f);
    g.setColour (dim.withAlpha (0.35f));
    g.drawLine (s.getX(), s.getCentreY(), s.getRight(), s.getCentreY());
    g.drawLine (s.getCentreX(), s.getY(), s.getCentreX(), s.getBottom());

    const float p = klippa::curveToP (*proc.apvts.getRawParameterValue ("curve"));
    const float driveLin = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue ("drive")->load());
    const float ceilLin  = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue ("ceiling")->load());
    const float xr = 2.0f; // input range shown: +/-2 (post-drive)
    auto mapX = [&] (float x) { return s.getCentreX() + x / xr * s.getWidth() * 0.5f; };
    auto mapY = [&] (float y) { return s.getCentreY() - y / xr * s.getHeight() * 0.5f; };

    juce::Path path;
    for (int i = 0; i <= 240; ++i)
    {
        const float x = -xr + 2.f * xr * (float) i / 240.f;
        const float y = klippa::clip (x, p) * ceilLin;
        const float px = mapX (x), py = mapY (y);
        if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
    }
    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (2.5f));

    const float pk = juce::jmin (xr, proc.inPeak.load());
    const float dotY = klippa::clip (pk, p) * ceilLin;
    g.setColour (ink);
    g.fillEllipse (mapX (pk) - 4.f, mapY (dotY) - 4.f, 8.f, 8.f);
    g.fillEllipse (mapX (-pk) - 4.f, mapY (-dotY) - 4.f, 8.f, 8.f);
    juce::ignoreUnused (driveLin);
}
