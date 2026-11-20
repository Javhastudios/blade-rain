#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Views.h"

class Panel;

class BladeRainEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BladeRainEditor (BladeRainProcessor&);
    ~BladeRainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    void timerCallback() override;

    BladeRainProcessor& proc;
    BRLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };
    juce::Viewport viewport;
    std::unique_ptr<Panel> panel;
    std::map<char, int> computerKeysDown;   // tecla del ordenador → nota que suena

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BladeRainEditor)
};
