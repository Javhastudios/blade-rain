// Colores, fuentes y aspecto copiados de la hoja de estilos del prototipo (variables :root).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
using juce::Colour;
inline const Colour bg { 0xff17140f };
inline const Colour panelA { 0xff4c453c };
inline const Colour panelB { 0xff2b2620 };
inline const Colour woodA { 0xff6b4226 };
inline const Colour woodB { 0xff3d2410 };
inline const Colour orange { 0xffe8752c };
inline const Colour orangeDim { 0xffa3521c };
inline const Colour ledOn { 0xffffb020 };
inline const Colour ledOff { 0xff3a2f22 };
inline const Colour cream { 0xffece3d1 };
inline const Colour ink { 0xff201c17 };
inline const Colour moduleTitle { 0xffc9bd9f };
inline const Colour label { 0xffb8ac93 };
inline const Colour value { 0xff8f8570 };
inline const Colour rockerText { 0xffcfc4a9 };

juce::Font oswald (float size, bool bold = false);
juce::Font mono (float size, bool semibold = false);
juce::Font tr909 (float size);
} // namespace theme

class BRLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BRLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider& s) override { return s.isVertical() ? 7 : 7; }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};
