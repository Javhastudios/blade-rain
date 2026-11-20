// Prueba de humo del plugin completo (procesador + interfaz), sin DAW.
// Toca notas por MIDI en todas las secciones, prueba Hold, pedal, vocoder con una entrada,
// guarda/restaura el estado y saca capturas de la interfaz.  Uso: br_smoke <carpeta-salida>
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

using namespace juce;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::cout << "  ok   " << msg << "\n"; else { std::cout << "  FALLO " << msg << "\n"; ++failures; } } while (0)

struct Stats { double rms = 0, peak = 0; bool finite = true; };

// Procesa "blocks" bloques de 512 con los eventos MIDI indicados (en el primer bloque) y una
// entrada de micro opcional (voz sintética).
static Stats run (BladeRainProcessor& p, int blocks, std::vector<MidiMessage> events = {}, bool mic = false)
{
    AudioBuffer<float> buf (2, 512);
    Stats s;
    double e = 0;
    int64 n = 0;
    static double ph = 0;
    for (int k = 0; k < blocks; ++k)
    {
        buf.clear();
        if (mic)
            for (int i = 0; i < 512; ++i)
            {
                ph += 115.0 / 48000;
                const float v = ph - std::floor (ph) < 0.02 ? 0.8f : 0.0f;   // tren de pulsos tipo voz
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
        MidiBuffer midi;
        if (k == 0) for (auto& m : events) midi.addEvent (m, 5);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) s.finite = false;
                e += v * v; ++n;
                s.peak = jmax (s.peak, (double) std::abs (v));
            }
    }
    s.rms = std::sqrt (e / (double) n);
    return s;
}

static void snapshot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
}

static MidiMessage on (int n) { return MidiMessage::noteOn (1, n, (uint8) 100); }
static MidiMessage off (int n) { return MidiMessage::noteOff (1, n); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File out (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getChildFile ("smoke-out").getFullPathName());
    out.createDirectory();

    auto proc = std::make_unique<BladeRainProcessor>();
    proc->setRateAndBufferSizeDetails (48000, 512);
    std::cout << "entradas: " << proc->getTotalNumInputChannels() << " (buses " << proc->getBusCount (true) << ")\n";
    proc->prepareToPlay (48000, 512);

    std::cout << "secciones\n";
    auto silence = run (*proc, 20);
    CHECK (silence.finite && silence.peak < 1e-4, "silencio sin notas (pico " << silence.peak << ")");
    auto strings = run (*proc, 60, { on (48), on (64) });
    CHECK (strings.finite && strings.rms > 0.01, "Strings + Human voice suenan desde el principio (rms " << strings.rms << ")");
    run (*proc, 2, { off (48), off (64) });
    run (*proc, 150);
    auto tail = run (*proc, 10);
    CHECK (tail.peak < 1e-3, "tras el release vuelve el silencio (pico " << tail.peak << ")");

    proc->setParam (pid::choirLower, 0); proc->setParam (pid::choirUpper, 0);
    CHECK (run (*proc, 40, { on (60) }).rms > 0.01, "solo Strings");
    run (*proc, 100, { off (60) });
    proc->setParam (pid::choirLower, 1); proc->setParam (pid::choirUpper, 1);
    proc->setParam (pid::stringsLower, 0); proc->setParam (pid::stringsUpper, 0);
    proc->setParam (pid::voiceUpper, 1);
    CHECK (run (*proc, 40, { on (69) }).rms > 0.005, "solo Human voice (Female 4')");
    run (*proc, 100, { off (69) });
    proc->setParam (pid::stringsLower, 1); proc->setParam (pid::stringsUpper, 1);

    std::cout << "hold y pedal\n";
    proc->setParam (pid::hold, 1);
    run (*proc, 4, { on (60), off (60) });
    auto held = run (*proc, 40);
    CHECK (held.rms > 0.01 && proc->isNoteLit (60), "con Hold la nota sigue tras soltarla");
    run (*proc, 4, { on (60) });
    run (*proc, 100);
    CHECK (! proc->isNoteLit (60) && run (*proc, 10).peak < 1e-3, "pulsarla otra vez la suelta");
    proc->setParam (pid::hold, 0);
    run (*proc, 4, { MidiMessage::controllerEvent (1, 64, 127), on (62), off (62) });
    CHECK (run (*proc, 30).rms > 0.01, "pedal de sustain mantiene la nota");
    run (*proc, 4, { MidiMessage::controllerEvent (1, 64, 0) });
    run (*proc, 100);
    CHECK (run (*proc, 10).peak < 1e-3, "al soltar el pedal se apaga");

    std::cout << "vocoder\n";
    proc->setParam (pid::stringsLower, 0); proc->setParam (pid::stringsUpper, 0);
    proc->setParam (pid::choirLower, 0); proc->setParam (pid::choirUpper, 0);
    proc->setParam (pid::vocoderLower, 1); proc->setParam (pid::vocoderUpper, 1);
    proc->setParam (pid::balanceMic, 0);
    CHECK (run (*proc, 40, { on (48) }, false).rms < 0.02, "vocoder sin micro casi no suena");
    proc->setParam (pid::mic, 1);
    auto voc = run (*proc, 80, {}, true);
    CHECK (voc.finite && voc.rms > 0.005, "vocoder con micro suena (rms " << voc.rms << ")");
    proc->setParam (pid::vocoderTone, 0);
    auto dark = run (*proc, 80, {}, true);
    proc->setParam (pid::vocoderTone, 100);
    auto bright = run (*proc, 80, {}, true);
    CHECK (std::abs (bright.rms - dark.rms) > 0.1 * bright.rms, "el Tone del vocoder sí cambia el sonido (" << dark.rms << " vs " << bright.rms << ")");
    float mx = 0;
    for (int b = 0; b < 10; ++b) mx = jmax (mx, proc->takeBandLevel (b));
    CHECK (mx > 0, "el medidor de bandas recibe señal");
    run (*proc, 4, { off (48) });
    proc->setParam (pid::mic, 0);
    proc->setParam (pid::vocoderLower, 0); proc->setParam (pid::vocoderUpper, 0);
    proc->setParam (pid::stringsLower, 1); proc->setParam (pid::stringsUpper, 1);
    proc->setParam (pid::choirLower, 1); proc->setParam (pid::choirUpper, 1);
    run (*proc, 200);

    std::cout << "efectos y pitch\n";
    proc->setParam (pid::reverbMix, 60);
    proc->setParam (pid::reverbSize, 4.0f);
    MessageManager::getInstance()->runDispatchLoopUntil (200);   // el IR nuevo se genera en el hilo de mensajes
    proc->setParam (pid::delayMix, 50);
    proc->setParam (pid::pitchMode, 0);
    proc->setParam (pid::master, 100);
    auto fx = run (*proc, 40, { on (48), on (52), on (55), on (60), on (64), on (67), on (72) });
    CHECK (fx.finite && fx.peak < 1.2, "acorde fuerte con efectos: pico " << fx.peak << " (limitador)");
    run (*proc, 4, { off (48), off (52), off (55), off (60), off (64), off (67), off (72) });
    auto rvTail = run (*proc, 60);
    CHECK (rvTail.rms > 1e-4, "cola de reverb/delay tras soltar");
    proc->setParam (pid::pitchMode, 1);

    std::cout << "estado\n";
    proc->setParam (pid::splitPoint, 4);
    proc->setParam (pid::vibratoDepth, 7.5f);
    proc->octaveShift = 2;
    MemoryBlock state;
    proc->getStateInformation (state);
    auto proc2 = std::make_unique<BladeRainProcessor>();
    proc2->setStateInformation (state.getData(), (int) state.getSize());
    CHECK ((int) proc2->param (pid::splitPoint) == 4 && std::abs (proc2->param (pid::vibratoDepth) - 7.5f) < 1e-3 && proc2->octaveShift == 2
               && std::abs (proc2->param (pid::reverbSize) - 4.0f) < 1e-3,
           "estado restaurado en otra instancia");
    proc2->setPlayConfigDetails (0, 2, 44100, 256);
    proc2->prepareToPlay (44100, 256);
    AudioBuffer<float> b2 (2, 256);
    MidiBuffer m2; m2.addEvent (on (60), 0);
    double e2 = 0;
    for (int k = 0; k < 40; ++k) { b2.clear(); proc2->processBlock (b2, m2); m2.clear(); e2 += b2.getRMSLevel (0, 0, 256); }
    CHECK (e2 > 0.1, "suena a 44,1 kHz sin entrada de audio");

    std::cout << "app independiente\n";
    {
        // la ventana standalone de JUCE solo abre las entradas del bus principal
        PluginHostType::jucePlugInClientCurrentWrapperType = AudioProcessor::wrapperType_Standalone;
        auto sa = std::make_unique<BladeRainProcessor>();
        PluginHostType::jucePlugInClientCurrentWrapperType = AudioProcessor::wrapperType_Undefined;
        CHECK (sa->getMainBusNumInputChannels() == 2, "como app, el micro va por la entrada principal (" << sa->getMainBusNumInputChannels() << " canales)");
        sa->setRateAndBufferSizeDetails (48000, 512);
        sa->prepareToPlay (48000, 512);
        sa->setParam (pid::stringsLower, 0); sa->setParam (pid::stringsUpper, 0);
        sa->setParam (pid::choirLower, 0); sa->setParam (pid::choirUpper, 0);
        sa->setParam (pid::vocoderLower, 1); sa->setParam (pid::vocoderUpper, 1);
        sa->setParam (pid::balanceMic, 0);
        sa->setParam (pid::mic, 1);
        run (*sa, 4, { on (48) });
        auto v = run (*sa, 60, {}, true);
        CHECK (v.rms > 0.005, "como app, el vocoder suena con el micro (rms " << v.rms << ")");
    }

    std::cout << "interfaz\n";
    {
        proc->setParam (pid::mic, 1);
        proc->setParam (pid::vocoderLower, 1);
        proc->setParam (pid::hold, 1);
        run (*proc, 2, { on (55), on (64) });
        std::unique_ptr<AudioProcessorEditor> ed (proc->createEditor());
        for (int k = 0; k < 12; ++k) { run (*proc, 3, {}, true); MessageManager::getInstance()->runDispatchLoopUntil (35); }
        snapshot (*ed, out.getChildFile ("ui_editor.png"));
        if (auto* vp = dynamic_cast<Viewport*> (ed->getChildComponent (0)))
            if (auto* panel = vp->getViewedComponent())
                snapshot (*panel, out.getChildFile ("ui_full.png"));
        CHECK (out.getChildFile ("ui_full.png").getSize() > 10000, "captura de la interfaz");
    }

    std::cout << (failures == 0 ? "TODO OK\n" : String (failures) + " FALLOS\n");
    return failures == 0 ? 0 : 1;
}
