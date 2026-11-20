#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/VP330Engine.h"

// Identificadores de parámetros: los mismos id que los controles del prototipo web
namespace pid
{
inline constexpr const char* balanceMic = "balanceMic";
inline constexpr const char* balanceChoir = "balanceChoir";
inline constexpr const char* balanceStrings = "balanceStrings";
inline constexpr const char* release = "release";
inline constexpr const char* stringsAttack = "stringsAttack";
inline constexpr const char* tone = "tone";
inline constexpr const char* attack = "attack";
inline constexpr const char* vibratoRate = "vibratoRate";
inline constexpr const char* vibratoDelay = "vibratoDelay";
inline constexpr const char* vibratoDepth = "vibratoDepth";
inline constexpr const char* vocoderTone = "vocoderTone";
inline constexpr const char* vocoderMicLevel = "vocoderMicLevel";
inline constexpr const char* noisegate = "noisegate";
inline constexpr const char* noisemix = "noisemix";
inline constexpr const char* sens = "sens";
inline constexpr const char* ensemble = "ensemble";
inline constexpr const char* lowerVolume = "lowerVolume";
inline constexpr const char* upperVolume = "upperVolume";
inline constexpr const char* pitchTime = "pitchTime";
inline constexpr const char* pitchTune = "pitchTune";
inline constexpr const char* pitchSet = "pitchSet";
inline constexpr const char* pitchShift = "pitchShift";
inline constexpr const char* delayTime = "delayTime";
inline constexpr const char* delayFeedback = "delayFeedback";
inline constexpr const char* delayMix = "delayMix";
inline constexpr const char* reverbSize = "reverbSize";
inline constexpr const char* reverbTone = "reverbTone";
inline constexpr const char* reverbMix = "reverbMix";
inline constexpr const char* master = "master";
inline constexpr const char* splitPoint = "splitPoint";
inline constexpr const char* voiceLower = "voiceLower";
inline constexpr const char* voiceUpper = "voiceUpper";
inline constexpr const char* pitchMode = "pitchMode";
inline constexpr const char* stringsLower = "stringsLower";
inline constexpr const char* stringsUpper = "stringsUpper";
inline constexpr const char* choirLower = "choirLower";
inline constexpr const char* choirUpper = "choirUpper";
inline constexpr const char* vocoderLower = "vocoderLower";
inline constexpr const char* vocoderUpper = "vocoderUpper";
inline constexpr const char* choirToEnsemble = "choirToEnsemble";
inline constexpr const char* vocoderToEnsemble = "vocoderToEnsemble";
inline constexpr const char* mic = "mic";
inline constexpr const char* hold = "hold";
} // namespace pid

// Convolución de la reverb con la de JUCE (latencia cero, mucho más rápida)
class JuceConvolver : public wa::ConvolverBase
{
public:
    void prepare (double sr, int maxBlock)
    {
        conv.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 });
    }
    void setImpulse (const std::vector<float>& L, const std::vector<float>& R, double sr) override
    {
        juce::AudioBuffer<float> b (2, (int) L.size());
        b.copyFrom (0, 0, L.data(), (int) L.size());
        b.copyFrom (1, 0, R.data(), (int) R.size());
        conv.loadImpulseResponse (std::move (b), sr, juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }
    void process (float* L, float* R, int n) override
    {
        float* ch[2] = { L, R };
        juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    void reset() override { conv.reset(); }

private:
    juce::dsp::Convolution conv;
};

class BladeRainProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BladeRainProcessor();
    ~BladeRainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Blade Rain"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // teclado en pantalla y teclado del ordenador

    float param (const char* id) const;
    bool flag (const char* id) const { return param (id) > 0.5f; }
    void setParam (const char* id, float plainValue);
    void toggleParam (const char* id) { setParam (id, flag (id) ? 0.0f : 1.0f); }

    // notas que suenan (pulsadas o retenidas por Hold/pedal), para iluminar el teclado
    bool isNoteLit (int midi) const;
    int octaveShift = 0;   // Z / X del teclado del ordenador

    // medidores (se leen y se ponen a cero)
    float takeBandLevel (int band) { return bandLevels[(size_t) band].exchange (0.0f); }
    float takeMicLevel() { return micLevel.exchange (0.0f); }
    bool hasMicInput() const { return getTotalNumInputChannels() > 0; }

    static constexpr int kSplitPoints[5] = { 53, 57, 60, 64, 67 };

private:
    void timerCallback() override;
    void applyAllParams();
    void applyChangedParams();
    void handleMidi (const juce::MidiMessage& m);
    void releaseLatched();
    void setLit (int midi, bool on);

    br::VP330Engine engine;
    JuceConvolver convolver;
    juce::SpinLock lock;
    double sampleRate = 48000;
    bool prepared = false;

    std::vector<std::pair<const char*, std::atomic<float>*>> watched;
    std::vector<float> lastValues;
    float lastReverbSize = -1;

    // Hold y pedal de sustain
    bool latched[128] {};
    bool sustained[128] {};
    bool pressed[128] {};
    bool sustainDown = false, lastHold = false;
    std::atomic<uint64_t> lit[2] {};

    std::array<std::atomic<float>, br::VP330Engine::kBands> bandLevels {};
    std::atomic<float> micLevel { 0.0f };
    std::vector<float> micBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BladeRainProcessor)
};
