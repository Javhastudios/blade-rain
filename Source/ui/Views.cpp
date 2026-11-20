#include "Views.h"

using namespace juce;

//==============================================================================
int Rocker::idealWidth() const
{
    return jmax (24, (int) std::ceil (GlyphArrangement::getStringWidth (theme::oswald (10.0f), label)) + 4);
}

void Rocker::paint (Graphics& g)
{
    const bool o = isOn();
    lastOn = o;
    const float alpha = dim ? 0.5f : 1.0f;
    auto dot = Rectangle<float> (20, 22).withCentre ({ getWidth() * 0.5f, 11.0f + (down ? 2.0f : 0.0f) });
    if (o)
    {
        g.setColour (theme::orange.withAlpha (0.45f * alpha));
        g.fillRoundedRectangle (dot.expanded (2.5f), 5);
        g.setGradientFill (ColourGradient (theme::orange.withMultipliedAlpha (alpha), dot.getX(), dot.getY(),
                                           theme::orangeDim.withMultipliedAlpha (alpha), dot.getX(), dot.getBottom(), false));
    }
    else
        g.setGradientFill (ColourGradient (Colour (0xff3a352b).withMultipliedAlpha (alpha), dot.getX(), dot.getY(),
                                           Colour (0xff221e18).withMultipliedAlpha (alpha), dot.getX(), dot.getBottom(), false));
    g.fillRoundedRectangle (dot, 3);
    g.setColour (Colour (0xb3000000));
    g.drawRoundedRectangle (dot, 3, 1);

    auto led = Rectangle<float> (6, 6).withCentre ({ dot.getCentreX(), dot.getY() + 7 });
    if (o)
    {
        g.setColour (theme::ledOn.withAlpha (0.5f * alpha));
        g.fillEllipse (led.expanded (3));
        g.setColour (theme::ledOn.withMultipliedAlpha (alpha));
    }
    else g.setColour (theme::ledOff.withMultipliedAlpha (alpha));
    g.fillEllipse (led);

    if (label.isNotEmpty())
    {
        g.setColour ((o ? theme::cream : theme::rockerText).withMultipliedAlpha (alpha));
        g.setFont (theme::oswald (10.0f));
        g.drawText (label, Rectangle<int> (0, 28, getWidth(), 14), Justification::centredTop, false);
    }
}

//==============================================================================
Fader::Fader (BladeRainProcessor& p, const char* paramId, String n, std::function<String (double)> fmt)
    : name (std::move (n)), format (std::move (fmt))
{
    slider.setSliderStyle (Slider::LinearVertical);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setWantsKeyboardFocus (false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, slider);
    if (auto* prm = p.apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    refresh();
}

void Fader::resized() { slider.setBounds ((getWidth() - 26) / 2, 17, 26, 92); }

void Fader::refresh()
{
    const auto t = format (slider.getValue());
    if (t != valueText) { valueText = t; repaint (0, 0, getWidth(), 16); }
}

void Fader::paint (Graphics& g)
{
    g.setFont (theme::mono (9.0f));
    g.setColour (theme::value);
    g.drawText (valueText, Rectangle<int> (-6, 0, getWidth() + 12, 13), Justification::centred, false);
    g.setColour (theme::label);
    g.drawText (name.toUpperCase(), Rectangle<int> (-8, 113, getWidth() + 16, 13), Justification::centred, false);
}

//==============================================================================
SplitSlider::SplitSlider (BladeRainProcessor& p)
{
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setWantsKeyboardFocus (false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, pid::splitPoint, slider);
}

void SplitSlider::resized() { slider.setBounds (0, 13, 154, 24); }

void SplitSlider::paint (Graphics& g)
{
    g.setFont (theme::mono (9.0f));
    g.setColour (theme::label);
    g.drawText (String::fromUTF8 ("PUNTO DE CORTE"), Rectangle<int> (0, 0, getWidth(), 12), Justification::centredLeft, false);
    g.setColour (theme::value);
    const char* names[5] = { "F3", "A3", "C4", "E4", "G4" };
    for (int i = 0; i < 5; ++i)
    {
        const float x = 7 + i * 140.0f / 4;   // el recorrido del fader va de 7 a 147
        g.drawText (names[i], Rectangle<float> (x - 10, 38, 20, 12), Justification::centred, false);
    }
}

//==============================================================================
void VocoderMeter::update()
{
    for (int b = 0; b < br::VP330Engine::kBands; ++b)
        levels[b] = levels[b] * 0.65f + proc.takeBandLevel (b) * 0.35f;   // suavizado visual, como la web
    repaint();
}

void VocoderMeter::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto meter = area.removeFromTop (96);
    g.setGradientFill (ColourGradient (Colour (0xff0d0b09), 0, meter.getY(), Colour (0xff171310), 0, meter.getBottom(), false));
    g.fillRoundedRectangle (meter, 4);
    g.setColour (Colour (0x0dffffff));
    g.drawRoundedRectangle (meter, 4, 1);

    constexpr int segments = 12;
    auto inner = meter.reduced (10, 0).withTrimmedTop (8).withTrimmedBottom (6);
    const float gap = 4, colW = (inner.getWidth() - gap * 9) / 10;
    const float segGap = 2, segH = (inner.getHeight() - segGap * (segments - 1)) / segments;
    for (int b = 0; b < 10; ++b)
    {
        const int litCount = roundToInt (jmin (1.0f, levels[b] * 6) * segments);
        const float x = inner.getX() + b * (colW + gap);
        for (int s = 0; s < segments; ++s)
        {
            auto r = Rectangle<float> (x, inner.getBottom() - (s + 1) * segH - s * segGap, colW, segH);
            const bool on = s < litCount;
            const bool hot = on && s > segments - 3;
            if (on)
            {
                const auto c = hot ? Colour (0xffff5a3c) : theme::ledOn;
                g.setColour (c.withAlpha (0.35f));
                g.fillRoundedRectangle (r.expanded (1.5f), 2);
                g.setColour (c);
            }
            else g.setColour (theme::ledOff);
            g.fillRoundedRectangle (r, 1);
        }
        g.setColour (theme::value);
        g.setFont (theme::mono (8.0f));
        const double f = br::VP330Engine::kBandFreqs[b];
        const String lbl = f >= 1000 ? String (f / 1000, 1) + "k" : String ((int) f);
        g.drawText (lbl, Rectangle<float> (x, meter.getBottom() + 6, colW, 10), Justification::centred, false);
    }
}

//==============================================================================
void Led::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (8, 8);
    if (state) { g.setColour (on.withAlpha (0.5f)); g.fillEllipse (r.expanded (3)); }
    g.setColour (state ? on : off);
    g.fillEllipse (r);
}

//==============================================================================
std::vector<Keyboard::Key> Keyboard::layout() const
{
    std::vector<Key> keys;
    const int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int blackAfter[7] = { 1, 3, -1, 6, 8, 10, -1 };
    const int totalWhite = 7 * kOctaves + 1;
    auto inner = getLocalBounds().toFloat().reduced (6, 0).withTrimmedTop (6);
    const float ww = inner.getWidth() / totalWhite;
    int wi = 0;
    std::vector<Key> blacks;
    for (int oct = 0; oct <= kOctaves; ++oct)
    {
        const int maxI = oct == kOctaves ? 1 : 7;
        for (int i = 0; i < maxI; ++i)
        {
            keys.push_back ({ kStart + oct * 12 + whiteOffsets[i], Rectangle<float> (inner.getX() + wi * ww, inner.getY(), ww - 1, inner.getHeight()), false });
            if (oct < kOctaves && blackAfter[i] >= 0)
            {
                const float bw = inner.getWidth() * 0.055f;
                const float bx = inner.getX() + (wi + 1) * ww - ww * 0.28f;
                blacks.push_back ({ kStart + oct * 12 + blackAfter[i], Rectangle<float> (bx, inner.getY(), bw, inner.getHeight() * 0.62f), true });
            }
            ++wi;
        }
    }
    keys.insert (keys.end(), blacks.begin(), blacks.end());   // las negras encima
    return keys;
}

int Keyboard::noteAt (Point<float> pos) const
{
    auto keys = layout();
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        if (it->r.contains (pos)) return it->midi;
    return -1;
}

void Keyboard::paint (Graphics& g)
{
    g.setColour (Colour (0xff0e0c0a));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4);
    for (auto& k : layout())
    {
        const bool lit = proc.isNoteLit (k.midi) || k.midi == mouseNote;
        Path p;
        p.addRoundedRectangle (k.r.getX(), k.r.getY(), k.r.getWidth(), k.r.getHeight(), k.black ? 3.0f : 4.0f, k.black ? 3.0f : 4.0f, false, false, true, true);
        if (k.black)
        {
            g.setColour (Colour (0x80000000));
            g.fillPath (p, AffineTransform::translation (0, 3));
            g.setGradientFill (lit ? ColourGradient (theme::orangeDim, 0, k.r.getY(), Colour (0xff5c3410), 0, k.r.getBottom(), false)
                                   : ColourGradient (Colour (0xff2a2620), 0, k.r.getY(), Colour (0xff0c0a08), 0, k.r.getBottom(), false));
            g.fillPath (p);
        }
        else
        {
            g.setGradientFill (lit ? ColourGradient (theme::orange, 0, k.r.getY(), Colour (0xffc85f1f), 0, k.r.getBottom(), false)
                                   : ColourGradient (Colour (0xffefe6d3), 0, k.r.getY(), Colour (0xffd8cbac), 0, k.r.getBottom(), false));
            g.fillPath (p);
            g.setColour (Colour (0xff3d372a));
            g.strokePath (p, PathStrokeType (1.0f));
        }
    }
    // marcador del punto de corte (en la primera tecla blanca >= split)
    const int split = BladeRainProcessor::kSplitPoints[jlimit (0, 4, (int) proc.param (pid::splitPoint))];
    for (auto& k : layout())
        if (! k.black && k.midi >= split)
        {
            g.setColour (theme::orange.withAlpha (0.45f));
            g.fillRect (k.r.getX() - 3, 0.0f, 6.0f, (float) getHeight());
            g.setColour (theme::orange);
            g.fillRect (k.r.getX() - 1, 0.0f, 2.0f, (float) getHeight());
            break;
        }
}

void Keyboard::press (int midi)
{
    if (midi == mouseNote) return;
    releasePressed();
    if (midi < 0) return;
    mouseNote = midi;
    proc.keyboardState.noteOn (1, midi, 1.0f);
    repaint();
}

void Keyboard::releasePressed()
{
    if (mouseNote >= 0) proc.keyboardState.noteOff (1, mouseNote, 0.0f);
    mouseNote = -1;
    repaint();
}

void Keyboard::mouseDown (const MouseEvent& e) { press (noteAt (e.position)); }
void Keyboard::mouseDrag (const MouseEvent& e) { press (noteAt (e.position)); }
void Keyboard::mouseUp (const MouseEvent&) { releasePressed(); }

void Keyboard::refresh()
{
    uint64_t a = 0, b = 0;
    for (int m = 0; m < 128; ++m)
        if (proc.isNoteLit (m)) (m < 64 ? a : b) |= uint64_t (1) << (m & 63);
    const int split = (int) proc.param (pid::splitPoint);
    if (a != litCache[0] || b != litCache[1] || split != splitCache)
    {
        litCache[0] = a; litCache[1] = b; splitCache = split;
        repaint();
    }
}
