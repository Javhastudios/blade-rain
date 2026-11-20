#include "Theme.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
Typeface::Ptr load (const char* data, int size)
{
    return Typeface::createSystemTypefaceFor (data, (size_t) size);
}
Typeface::Ptr oswaldTf() { static auto t = load (BinaryData::Oswald_ttf, BinaryData::Oswald_ttfSize); return t; }
Typeface::Ptr monoTf() { static auto t = load (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize); return t; }
Typeface::Ptr monoSemiTf() { static auto t = load (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize); return t; }
Typeface::Ptr tr909Tf() { static auto t = load (BinaryData::TR909_ttf, BinaryData::TR909_ttfSize); return t; }
} // namespace

Font theme::oswald (float size, bool) { return Font (FontOptions (oswaldTf()).withHeight (size)); }
Font theme::mono (float size, bool semibold) { return Font (FontOptions (semibold ? monoSemiTf() : monoTf()).withHeight (size)); }
Font theme::tr909 (float size) { return Font (FontOptions (tr909Tf()).withHeight (size)); }

BRLookAndFeel::BRLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, theme::bg);
    setColour (ScrollBar::thumbColourId, Colour (0xff4c453c));
    setColour (TooltipWindow::backgroundColourId, Colour (0xff1c1712));
    setColour (TooltipWindow::textColourId, theme::cream);
    setColour (TooltipWindow::outlineColourId, Colour (0x22ffffff));
}

Typeface::Ptr BRLookAndFeel::getTypefaceForFont (const Font& f)
{
    if (f.getTypefaceName() == Font::getDefaultSansSerifFontName()) return oswaldTf();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void BRLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      Slider::SliderStyle style, Slider&)
{
    // ranura oscura y "cabeza" de fader color crema, como input.vslider / input.hslider
    if (style == Slider::LinearVertical)
    {
        const float cx = x + w * 0.5f;
        auto groove = Rectangle<float> (cx - 3, (float) y, 6, (float) h);
        g.setGradientFill (ColourGradient (Colour (0xff0c0a08), cx, (float) y, Colour (0xff1c1712), cx, (float) (y + h), false));
        g.fillRoundedRectangle (groove, 3);
        g.setColour (Colour (0x0fffffff));
        g.drawRoundedRectangle (groove, 3, 1);
        auto thumb = Rectangle<float> (22, 13).withCentre ({ cx, pos });
        g.setColour (Colour (0x99000000));
        g.fillRoundedRectangle (thumb.translated (0, 1), 2);
        g.setGradientFill (ColourGradient (Colour (0xffefe6d3), thumb.getX(), thumb.getY(), Colour (0xffb9ac8e), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, 2);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (thumb, 2, 1);
        return;
    }
    const float cy = y + h * 0.5f;
    auto groove = Rectangle<float> ((float) x, cy - 3, (float) w, 6);
    g.setGradientFill (ColourGradient (Colour (0xff0c0a08), (float) x, cy, Colour (0xff1c1712), (float) (x + w), cy, false));
    g.fillRoundedRectangle (groove, 3);
    g.setColour (Colour (0x0fffffff));
    g.drawRoundedRectangle (groove, 3, 1);
    auto thumb = Rectangle<float> (13, 22).withCentre ({ pos, cy });
    g.setGradientFill (ColourGradient (Colour (0xffefe6d3), thumb.getX(), thumb.getY(), Colour (0xffb9ac8e), thumb.getRight(), thumb.getY(), false));
    g.fillRoundedRectangle (thumb, 2);
    g.setColour (Colours::black);
    g.drawRoundedRectangle (thumb, 2, 1);
}
