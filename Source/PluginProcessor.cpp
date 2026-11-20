#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }

struct FloatDef { const char* id; const char* name; float min, max, step, def; const char* label; };

// Rangos y valores por defecto copiados de los <input type="range"> del prototipo
const FloatDef floatDefs[] = {
    { pid::balanceMic, "Balance Mic", 0, 10, 0.1f, 6, "" },
    { pid::balanceChoir, "Balance Voice", 0, 10, 0.1f, 8, "" },
    { pid::balanceStrings, "Balance Strings", 0, 10, 0.1f, 8, "" },
    { pid::release, "Release", 1, 2000, 1, 280, "ms" },
    { pid::stringsAttack, "Strings Attack", 1, 1200, 1, 40, "ms" },
    { pid::tone, "Strings Tone", 0, 100, 1, 100, "%" },
    { pid::attack, "Voice Attack", 1, 1200, 1, 60, "ms" },
    { pid::vibratoRate, "Vibrato Rate", 1, 9, 0.1f, 5.3f, "Hz" },
    { pid::vibratoDelay, "Vibrato Delay", 0, 600, 1, 150, "ms" },
    { pid::vibratoDepth, "Vibrato Depth", 0, 15, 0.1f, 2.2f, "ct" },
    { pid::vocoderTone, "Vocoder Tone", 0, 100, 1, 70, "%" },
    { pid::vocoderMicLevel, "Vocoder Mic Level", 0, 10, 0.1f, 6, "" },
    { pid::noisegate, "Vocoder Gate", 0, 15, 0.5f, 1.5f, "%" },
    { pid::noisemix, "Vocoder S/N", 0, 100, 1, 15, "%" },
    { pid::sens, "Vocoder Sens", 0, 30, 1, 4, "" },
    { pid::ensemble, "Ensemble Depth", 0, 100, 1, 55, "%" },
    { pid::lowerVolume, "Lower Vol", 0, 10, 0.1f, 10, "" },
    { pid::upperVolume, "Upper Vol", 0, 10, 0.1f, 10, "" },
    { pid::pitchTime, "Pitch Time", 30, 2000, 1, 400, "ms" },
    { pid::pitchTune, "Tune", -50, 50, 1, 0, "ct" },
    { pid::pitchSet, "Pitch Set", 1, 24, 1, 4, "st" },
    { pid::pitchShift, "Pitch Shift", 0, 100, 1, 0, "%" },
    { pid::delayTime, "Delay Time", 20, 800, 1, 350, "ms" },
    { pid::delayFeedback, "Delay Feedback", 0, 90, 1, 35, "%" },
    { pid::delayMix, "Delay Mix", 0, 100, 1, 0, "%" },
    { pid::reverbSize, "Reverb Size", 0.3f, 6, 0.1f, 2.2f, "s" },
    { pid::reverbTone, "Reverb Tone", 500, 12000, 1, 6000, "Hz" },
    { pid::reverbMix, "Reverb Mix", 0, 100, 1, 0, "%" },
    { pid::master, "Master", 0, 100, 1, 70, "%" },
};

struct BoolDef { const char* id; const char* name; bool def; };
const BoolDef boolDefs[] = {
    { pid::stringsLower, "Strings Lower", true }, { pid::stringsUpper, "Strings Upper", true },
    { pid::choirLower, "Voice Lower", true }, { pid::choirUpper, "Voice Upper", true },
    { pid::vocoderLower, "Vocoder Lower", false }, { pid::vocoderUpper, "Vocoder Upper", false },
    { pid::choirToEnsemble, "Voice > Ensemble", true }, { pid::vocoderToEnsemble, "Vocoder > Ensemble", false },
    { pid::mic, "Mic", false }, { pid::hold, "Hold", false },
};

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    for (auto& d : floatDefs)
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { d.id, 1 }, d.name, NormalisableRange<float> (d.min, d.max, d.step), d.def,
                                                            AudioParameterFloatAttributes().withLabel (d.label)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::splitPoint, 1 }, "Split", StringArray { "F3", "A3", "C4", "E4", "G4" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::voiceLower, 1 }, "Voice Lower", StringArray { "Male 8'", "Male 4'" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::voiceUpper, 1 }, "Voice Upper", StringArray { "Male 8'", "Female 4'" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { pid::pitchMode, 1 }, "Pitch Mode", StringArray { "Auto", "Off", "Manual" }, 1));
    for (auto& d : boolDefs)
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { d.id, 1 }, d.name, d.def));
    return { p.begin(), p.end() };
}
} // namespace

//==============================================================================
// Entradas de audio para el micro del vocoder:
//  - En el DAW, por un bus de sidechain (así se lo ofrecen los DAW a los instrumentos); la
//    entrada principal existe pero va desactivada.
//  - En la app independiente, por la entrada principal: la ventana standalone de JUCE solo
//    abre los canales de entrada del bus principal.
AudioProcessor::BusesProperties BladeRainProcessor::makeBuses()
{
    if (PluginHostType::getPluginLoadedAs() == AudioProcessor::wrapperType_Standalone)
        return BusesProperties()
            .withInput ("Mic", AudioChannelSet::stereo(), true)
            .withOutput ("Output", AudioChannelSet::stereo(), true);
    return BusesProperties()
        .withInput ("Input", AudioChannelSet::stereo(), false)
        .withInput ("Mic (sidechain)", AudioChannelSet::stereo(), true)
        .withOutput ("Output", AudioChannelSet::stereo(), true);
}

BladeRainProcessor::BladeRainProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            watched.push_back ({ nullptr, apvts.getRawParameterValue (r->getParameterID()) });
    // guardamos los id como punteros estables
    int k = 0;
    for (auto& d : floatDefs) watched[(size_t) k++].first = d.id;
    for (auto* id : { pid::splitPoint, pid::voiceLower, pid::voiceUpper, pid::pitchMode }) watched[(size_t) k++].first = id;
    for (auto& d : boolDefs) watched[(size_t) k++].first = d.id;
    lastValues.assign (watched.size(), std::numeric_limits<float>::quiet_NaN());
    startTimerHz (20);
}

BladeRainProcessor::~BladeRainProcessor() { stopTimer(); }

float BladeRainProcessor::param (const char* id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void BladeRainProcessor::setParam (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

bool BladeRainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
    for (auto& in : layouts.inputBuses)
        if (! (in.isDisabled() || in == AudioChannelSet::mono() || in == AudioChannelSet::stereo())) return false;
    return true;
}

bool BladeRainProcessor::isNoteLit (int midi) const
{
    if (midi < 0 || midi > 127) return false;
    return (lit[midi >> 6].load() >> (midi & 63)) & 1u;
}

void BladeRainProcessor::setLit (int midi, bool on)
{
    const uint64_t bit = uint64_t (1) << (midi & 63);
    if (on) lit[midi >> 6].fetch_or (bit); else lit[midi >> 6].fetch_and (~bit);
}

//==============================================================================
// Sin String: se compara con strcmp para no reservar memoria en el hilo de audio
static void applyParam (br::VP330Engine& e, const char* id, float v)
{
    auto is = [id] (const char* x) { return std::strcmp (id, x) == 0; };
    if (is (pid::balanceMic)) e.setBalanceMic (v);
    else if (is (pid::balanceChoir)) e.setBalanceChoir (v);
    else if (is (pid::balanceStrings)) e.setBalanceStrings (v);
    else if (is (pid::release)) e.setRelease (v);
    else if (is (pid::stringsAttack)) e.setStringsAttack (v);
    else if (is (pid::tone)) e.setTone (v);
    else if (is (pid::attack)) e.setAttack (v);
    else if (is (pid::vibratoRate)) e.setVibratoRate (v);
    else if (is (pid::vibratoDelay)) e.setVibratoDelay (v);
    else if (is (pid::vibratoDepth)) e.setVibratoDepth (v);
    else if (is (pid::vocoderTone)) e.setVocoderTone (v);
    else if (is (pid::vocoderMicLevel)) e.setVocoderMicLevel (v);
    else if (is (pid::noisegate)) e.setNoiseGate (v);
    else if (is (pid::noisemix)) e.setNoiseMix (v);
    else if (is (pid::sens)) e.setSensitivity (v);
    else if (is (pid::ensemble)) e.setEnsemble (v);
    else if (is (pid::lowerVolume)) e.setLowerVolume (v);
    else if (is (pid::upperVolume)) e.setUpperVolume (v);
    else if (is (pid::pitchTime)) e.setPitchTime (v);
    else if (is (pid::pitchTune)) e.setPitchTune (v);
    else if (is (pid::pitchSet)) e.setPitchSet (v);
    else if (is (pid::pitchShift)) e.setPitchShift (v);
    else if (is (pid::delayTime)) e.setDelayTime (v);
    else if (is (pid::delayFeedback)) e.setDelayFeedback (v);
    else if (is (pid::delayMix)) e.setDelayMix (v);
    else if (is (pid::reverbTone)) e.setReverbTone (v);
    else if (is (pid::reverbMix)) e.setReverbMix (v);
    else if (is (pid::master)) e.setMaster (v);
    else if (is (pid::splitPoint)) e.setSplitPoint (BladeRainProcessor::kSplitPoints[jlimit (0, 4, (int) v)]);
    else if (is (pid::voiceLower)) e.setVoiceVariant (false, (int) v == 0 ? 0 : 1);
    else if (is (pid::voiceUpper)) e.setVoiceVariant (true, (int) v == 0 ? 2 : 3);
    else if (is (pid::pitchMode)) e.setPitchMode ((br::PitchMode) jlimit (0, 2, (int) v));
    else if (is (pid::stringsLower)) e.setSectionSide (0, false, v > 0.5f);
    else if (is (pid::stringsUpper)) e.setSectionSide (0, true, v > 0.5f);
    else if (is (pid::choirLower)) e.setSectionSide (1, false, v > 0.5f);
    else if (is (pid::choirUpper)) e.setSectionSide (1, true, v > 0.5f);
    else if (is (pid::vocoderLower)) e.setSectionSide (2, false, v > 0.5f);
    else if (is (pid::vocoderUpper)) e.setSectionSide (2, true, v > 0.5f);
    else if (is (pid::choirToEnsemble)) e.setChoirToEnsemble (v > 0.5f);
    else if (is (pid::vocoderToEnsemble)) e.setVocoderToEnsemble (v > 0.5f);
    else if (is (pid::mic)) e.setMic (v > 0.5f);
    // reverbSize y hold se tratan aparte
}

void BladeRainProcessor::applyAllParams()
{
    for (size_t i = 0; i < watched.size(); ++i)
    {
        const float v = watched[i].second->load();
        lastValues[i] = v;
        applyParam (engine, watched[i].first, v);
    }
    // corrige los fallos del prototipo: el Balance se aplica desde el principio y el Tone
    // del vocoder sí actúa (ver VP330Engine::jsVocoderToneBug)
    engine.updateBalanceLevels();
    engine.setVocoderTone (param (pid::vocoderTone));
}

void BladeRainProcessor::applyChangedParams()
{
    for (size_t i = 0; i < watched.size(); ++i)
    {
        const float v = watched[i].second->load();
        if (v == lastValues[i]) continue;
        lastValues[i] = v;
        applyParam (engine, watched[i].first, v);
    }
}

void BladeRainProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    convolver.prepare (sr, jmax (samplesPerBlock, 512));
    micBuffer.assign ((size_t) jmax (samplesPerBlock, 4096), 0.0f);
    const SpinLock::ScopedLockType sl (lock);
    engine.prepare (sr, &convolver, 1);
    applyAllParams();
    lastReverbSize = 2.2f;
    const float rs = param (pid::reverbSize);
    if (std::abs (rs - lastReverbSize) > 1e-4f) { engine.setReverbSize (rs); lastReverbSize = rs; }
    for (int m = 0; m < 128; ++m) { latched[m] = sustained[m] = pressed[m] = false; }
    lit[0] = 0; lit[1] = 0;
    sustainDown = false;
    lastHold = flag (pid::hold);
    prepared = true;
    setLatencySamples (engine.latencySamples());
}

void BladeRainProcessor::timerCallback()
{
    // un IR nuevo de reverb reserva memoria: se genera aquí, en el hilo de mensajes
    if (! prepared) return;
    const float rs = param (pid::reverbSize);
    if (std::abs (rs - lastReverbSize) > 1e-4f)
    {
        lastReverbSize = rs;
        engine.setReverbSize (rs);
    }
}

//==============================================================================
void BladeRainProcessor::releaseLatched()
{
    for (int m = 0; m < 128; ++m)
        if (latched[m]) { latched[m] = false; engine.noteOff (m); setLit (m, false); }
}

void BladeRainProcessor::handleMidi (const MidiMessage& msg)
{
    const bool hold = flag (pid::hold);
    if (msg.isNoteOn())
    {
        const int m = msg.getNoteNumber();
        pressed[m] = true;
        if (hold)
        {
            if (latched[m]) { latched[m] = false; engine.noteOff (m); setLit (m, false); }
            else { latched[m] = true; engine.noteOn (m); setLit (m, true); }
        }
        else
        {
            sustained[m] = false;
            engine.noteOn (m);
            setLit (m, true);
        }
    }
    else if (msg.isNoteOff())
    {
        const int m = msg.getNoteNumber();
        pressed[m] = false;
        if (hold || latched[m]) return;
        if (sustainDown) { sustained[m] = true; return; }
        engine.noteOff (m);
        setLit (m, false);
    }
    else if (msg.isSustainPedalOn()) sustainDown = true;
    else if (msg.isSustainPedalOff())
    {
        sustainDown = false;
        for (int m = 0; m < 128; ++m)
            if (sustained[m]) { sustained[m] = false; engine.noteOff (m); setLit (m, false); }
        if (hold) releaseLatched();   // como el prototipo: soltar el pedal suelta lo retenido por Hold
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        engine.allNotesOff();
        for (int m = 0; m < 128; ++m) latched[m] = sustained[m] = false;
        lit[0] = 0; lit[1] = 0;
    }
}

void BladeRainProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    if ((int) micBuffer.size() < n) { buffer.clear(); return; }   // no debería ocurrir
    // micro = primer canal del primer bus de entrada activo (sidechain o principal)
    FloatVectorOperations::clear (micBuffer.data(), n);
    if (numIn > 0)
        for (int b = 0; b < getBusCount (true); ++b)
        {
            auto in = getBusBuffer (buffer, true, b);
            if (in.getNumChannels() > 0) { FloatVectorOperations::copy (micBuffer.data(), in.getReadPointer (0), n); break; }
        }
    buffer.clear();
    if (buffer.getNumChannels() < 2 || ! prepared) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    const SpinLock::ScopedLockType sl (lock);
    applyChangedParams();
    const bool hold = flag (pid::hold);
    if (lastHold && ! hold) releaseLatched();
    lastHold = hold;

    int pos = 0;
    for (const auto meta : midi)
    {
        const int s = jlimit (0, n, meta.samplePosition);
        if (s > pos) { engine.process (micBuffer.data() + pos, L + pos, R + pos, s - pos); pos = s; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) engine.process (micBuffer.data() + pos, L + pos, R + pos, n - pos);

    for (int b = 0; b < br::VP330Engine::kBands; ++b)
    {
        const float v = (float) engine.bandLevel (b);
        if (v > bandLevels[(size_t) b].load()) bandLevels[(size_t) b] = v;
    }
    if (engine.micLevel() > micLevel.load()) micLevel = (float) engine.micLevel();
    engine.clearMeters();
}

//==============================================================================
void BladeRainProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("octave", octaveShift, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void BladeRainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;
    octaveShift = jlimit (-3, 3, (int) state.getProperty ("octave", 0));
    apvts.replaceState (state);
}

AudioProcessorEditor* BladeRainProcessor::createEditor() { return new BladeRainEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BladeRainProcessor(); }
