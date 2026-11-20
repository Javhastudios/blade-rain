// Controles con el aspecto del panel del prototipo: tablets con LED, faders verticales,
// selector de punto de corte, medidor LED del vocoder y teclado.
#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

// Tablet vertical con LED (.rocker)
class Rocker : public juce::Component, public juce::SettableTooltipClient
{
public:
    Rocker (juce::String text, std::function<bool()> isOnFn, std::function<void()> clickFn)
        : label (std::move (text)), isOn (std::move (isOnFn)), onClick (std::move (clickFn)) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); if (onClick) onClick(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }
    void refresh() { const bool o = isOn(); if (o != lastOn) { lastOn = o; repaint(); } }
    int idealWidth() const;
    void setDim (bool d) { if (dim != d) { dim = d; repaint(); } }

private:
    juce::String label;
    std::function<bool()> isOn;
    std::function<void()> onClick;
    bool down = false, lastOn = false, dim = false;
};

// Fader vertical con su valor arriba y su nombre debajo (.vslider-group)
class Fader : public juce::Component
{
public:
    Fader (BladeRainProcessor& p, const char* paramId, juce::String name, std::function<juce::String (double)> fmt);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    static constexpr int kWidth = 46, kHeight = 128;
    juce::Slider slider;

private:
    juce::String name;
    std::function<juce::String (double)> format;
    juce::String valueText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Punto de corte: fader horizontal de 5 posiciones con las notas debajo
class SplitSlider : public juce::Component
{
public:
    explicit SplitSlider (BladeRainProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Medidor LED de 10 bandas (.vocoder-meter)
class VocoderMeter : public juce::Component
{
public:
    explicit VocoderMeter (BladeRainProcessor& p) : proc (p) {}
    void update();
    void paint (juce::Graphics&) override;

private:
    BladeRainProcessor& proc;
    float levels[br::VP330Engine::kBands] {};
};

// Pequeño LED redondo (indicadores del micro, estado)
class Led : public juce::Component
{
public:
    Led (juce::Colour onC, juce::Colour offC) : on (onC), off (offC) {}
    void set (bool v) { if (v != state) { state = v; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    juce::Colour on, off;
    bool state = false;
};

// Teclado en pantalla (2 octavas desde C3) con el marcador del punto de corte
class Keyboard : public juce::Component
{
public:
    explicit Keyboard (BladeRainProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

    static constexpr int kStart = 48, kOctaves = 2;

private:
    struct Key { int midi; juce::Rectangle<float> r; bool black; };
    std::vector<Key> layout() const;
    int noteAt (juce::Point<float>) const;
    void press (int midi);
    void releasePressed();

    BladeRainProcessor& proc;
    int mouseNote = -1;
    uint64_t litCache[2] {};
    int splitCache = -1;
};
