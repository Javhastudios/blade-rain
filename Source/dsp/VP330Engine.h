// Blade Rain — motor del emulador VJ-330 (VP-330), traducción del grafo Web Audio del
// prototipo web (web/vp330-emulator.html). Cada bloque replica un trozo de initAudio(),
// noteOn()/noteOff() y de los manejadores de los controles, con los mismos valores y las
// mismas constantes de tiempo. No depende de JUCE.
//
// Se procesa por "quantums" de 128 muestras como Web Audio (la convolución trabaja por
// bloques), pero las notas y los cambios de parámetro se aplican en el instante exacto.
#pragma once

#include "WebAudio.h"
#include <memory>

namespace br
{
using namespace wa;

enum class PitchMode { Auto = 0, Off = 1, Manual = 2 };

// Los mismos valores iniciales que el objeto `params` del prototipo
struct Settings
{
    double attackMs = 60, stringsAttackMs = 40, releaseMs = 280;
    double ensembleDepth = 0.55;
    double noiseMix = 0.15, sensitivity = 4, masterVol = 0.7, noiseGate = 0.015;
    double vocoderTone = 70, vocoderMicLevel = 6;
    int splitPoint = 60;
    bool stringsLower = true, stringsUpper = true, choirLower = true, choirUpper = true, vocoderLower = false, vocoderUpper = false;
    double lowerVolume = 10, upperVolume = 10;
    double delayTimeMs = 350, delayFeedback = 35, delayMix = 0;
    double reverbSize = 2.2, reverbTone = 6000, reverbMix = 0;
    int choirVariantLower = 0, choirVariantUpper = 2;
    double vibratoRate = 5.3, vibratoDepth = 2.2, vibratoDelayMs = 150, toneCutoff = 9000;
    bool choirToEnsemble = true, vocoderToEnsemble = false;
    double balanceMic = 6, balanceChoir = 8, balanceStrings = 8;
    double pitchTimeMs = 400, pitchTuneCents = 0, pitchSetSemitones = 4;
    PitchMode pitchMode = PitchMode::Off;
    double pitchManualAmount = 0;
};

class VP330Engine
{
public:
    static constexpr int kMaxVoices = 48;
    static constexpr int kBands = 10;
    static constexpr double kBandFreqs[kBands] = { 200, 280, 400, 600, 900, 1300, 2000, 2800, 4000, 6000 };
    static constexpr double kChoirPool[7] = { 200, 230, 600, 900, 1300, 2800, 3300 };
    static constexpr int kVoicings[4][4] = { { 0, 2, 3, 5 }, { 1, 2, 3, 5 }, { 1, 2, 3, 5 }, { 2, 3, 4, 6 } };

    Settings p;

    // Reproduce un fallo del prototipo para poder compararlo muestra a muestra: allí la
    // salida del vocoder se toma ANTES de su filtro de Tone (vocoderChain.output en vez de
    // gainNode), así que el fader Tone del vocoder no hace nada. El plugin lo deja en false.
    bool jsVocoderToneBug = false;

    // Equivale a initAudio(): deja el grafo como queda en el instante 0.
    // Reserva memoria: llamar fuera del hilo de audio.
    void prepare (double sampleRate, ConvolverBase* convolver, uint32_t seed = 1)
    {
        sr = sampleRate;
        conv = convolver;
        rng.s = seed;
        frame = 0;
        const double t = 0;

        masterGain.init (p.masterVol);
        ppDry.init (1); ppWet.init (0); ppTimeL.init (0.35); ppTimeR.init (0.35); ppFbL.init (0.35); ppFbR.init (0.35);
        for (auto* d : { &ppDL[0], &ppDL[1], &ppDR[0], &ppDR[1] }) d->setup (2.0, sr);
        rvDry.init (1); rvWet.init (0); rvDampFreq.init (6000);
        for (auto& f : rvDamp) f.setup (Biquad::Type::Lowpass, 6000, 0.707, sr);
        makeImpulse (2.2);           // primero el IR de la reverb (mismo orden que el original)
        limiter.setup (sr, -3, 0, 20, 0.003, 0.15);

        micDry.init (0);
        micGain.init (p.vocoderMicLevel / 10 * 3);

        ensWet.init (1); ensDry.init (1);
        for (auto& d : ensDelay) d.setup (0.05, sr);
        applyEnsembleDepth (t);

        toneFreq.init (p.toneCutoff);
        toneFilter.setup (Biquad::Type::Lowpass, p.toneCutoff, 0.707, sr);

        // ruido blanco del carrier (2 s en bucle)
        noise.assign ((size_t) (sr * 2), 0.0f);
        for (auto& v : noise) v = (float) (rng.next() * 2 - 1);
        noisePos = 0;
        noiseGain.init (p.noiseMix);

        stringsBp[0].setup (Biquad::Type::Bandpass, 850, 1.6, sr);
        stringsBp[1].setup (Biquad::Type::Bandpass, 2400, 1.8, sr);
        stringsGain.init (0);

        for (int side = 0; side < 2; ++side)
        {
            for (int i = 0; i < 7; ++i)
            {
                choirBp[side][i].setup (Biquad::Type::Bandpass, kChoirPool[i], 6.0, sr);
                choirFilterGain[side][i].init (0);
            }
            choirGain[side].init (0);
        }
        choirWet.init (p.choirToEnsemble ? 1 : 0); choirDry.init (p.choirToEnsemble ? 0 : 1);
        updateChoirVariant (0, p.choirVariantLower, t);
        updateChoirVariant (1, p.choirVariantUpper, t);

        vocToneFreq.init (9000);
        vocTone.setup (Biquad::Type::Lowpass, 9000, 0.707, sr);
        bypassFilter.setup (Biquad::Type::Highpass, 4500, 0.7, sr);
        bypassGain.init (0);
        for (int i = 0; i < 1024; ++i) rect.curve[(size_t) i] = (float) std::abs ((i / 1023.0) * 2 - 1);
        makeGateCurve (p.noiseGate);
        envMakeup.init (p.sensitivity);
        for (int b = 0; b < kBands; ++b)
        {
            analysis[b].setup (Biquad::Type::Bandpass, kBandFreqs[b], 3.0, sr);
            smoother[b].setup (Biquad::Type::Lowpass, 22, 0.707, sr);
            synth[b].setup (Biquad::Type::Bandpass, kBandFreqs[b], 3.0, sr);
            bandPeak[b] = 0;
        }
        vocWet.init (p.vocoderToEnsemble ? 1 : 0); vocDry.init (p.vocoderToEnsemble ? 0 : 1);

        for (auto& v : voices) v.used = false;
        micOn = false;
        micPeak = 0;

        chunkL.assign (4096, 0.0f); chunkR.assign (4096, 0.0f);
        convL.assign (4096, 0.0f); convR.assign (4096, 0.0f);
    }

    double now() const { return (double) frame / sr; }
    int latencySamples() const { return limiter.latency(); }

    // ── manejadores (equivalen a los de wireControls) ─────────────────────────
    void setAttack (double ms) { p.attackMs = ms; }
    void setStringsAttack (double ms) { p.stringsAttackMs = ms; }
    void setRelease (double ms) { p.releaseMs = ms; }
    void setBalanceMic (double v) { p.balanceMic = v; updateBalanceLevels(); }
    void setBalanceChoir (double v) { p.balanceChoir = v; updateBalanceLevels(); }
    void setBalanceStrings (double v) { p.balanceStrings = v; updateBalanceLevels(); }
    void setEnsemble (double pct) { p.ensembleDepth = pct / 100; applyEnsembleDepth (now()); }
    void setTone (double pct)
    {
        p.toneCutoff = 600 + (pct / 100) * 8400;
        target (toneFreq, p.toneCutoff, 0.02);
    }
    void setVibratoRate (double hz)
    {
        p.vibratoRate = hz;
        for (auto& v : voices) if (v.used && v.held) target (v.vibFreq, hz, 0.02);
    }
    void setVibratoDelay (double ms) { p.vibratoDelayMs = ms; }
    void setVibratoDepth (double ct)
    {
        p.vibratoDepth = ct;
        for (auto& v : voices) if (v.used && v.held) target (v.vibDepth, ct, 0.02);
    }
    void setVocoderTone (double pct) { p.vocoderTone = pct; target (vocToneFreq, 500 + (pct / 100) * 8500, 0.02); }
    void setLowerVolume (double v) { p.lowerVolume = v; updateZoneVolume(); }
    void setUpperVolume (double v) { p.upperVolume = v; updateZoneVolume(); }
    void setVocoderMicLevel (double v) { p.vocoderMicLevel = v; target (micGain, v / 10 * 3, 0.02); }
    void setNoiseGate (double pct) { p.noiseGate = pct / 100; makeGateCurve (p.noiseGate); }
    void setNoiseMix (double pct) { p.noiseMix = pct / 100; target (noiseGain, p.noiseMix, 0.02); }
    void setSensitivity (double v) { p.sensitivity = v; target (envMakeup, v, 0.02); }
    void setMaster (double pct) { p.masterVol = pct / 100; target (masterGain, p.masterVol, 0.02); }
    void setDelayTime (double ms) { p.delayTimeMs = ms; target (ppTimeL, ms / 1000, 0.02); target (ppTimeR, ms / 1000, 0.02); }
    void setDelayFeedback (double pct) { p.delayFeedback = pct; target (ppFbL, pct / 100, 0.02); target (ppFbR, pct / 100, 0.02); }
    void setDelayMix (double pct) { p.delayMix = pct; target (ppWet, pct / 100, 0.02); }
    // Genera un IR nuevo: reserva memoria, llamar fuera del hilo de audio.
    void setReverbSize (double s) { p.reverbSize = s; makeImpulse (s); }
    void setReverbTone (double hz) { p.reverbTone = hz; target (rvDampFreq, hz, 0.02); }
    void setReverbMix (double pct) { p.reverbMix = pct; target (rvWet, pct / 100, 0.02); }
    void setPitchTime (double ms) { p.pitchTimeMs = ms; }
    void setPitchTune (double ct) { p.pitchTuneCents = ct; }
    void setPitchSet (double st) { p.pitchSetSemitones = st; updatePitchManual(); }
    void setPitchShift (double pct) { p.pitchManualAmount = pct / 100; updatePitchManual(); }
    void setPitchMode (PitchMode m) { p.pitchMode = m; updatePitchManual(); }
    void setSplitPoint (int midi) { p.splitPoint = midi; }
    // section: 0 strings, 1 human voice, 2 vocoder · upper: false = Lower
    void setSectionSide (int section, bool upper, bool on)
    {
        bool* b[3][2] = { { &p.stringsLower, &p.stringsUpper }, { &p.choirLower, &p.choirUpper }, { &p.vocoderLower, &p.vocoderUpper } };
        *b[section][upper ? 1 : 0] = on;
        updateBalanceLevels();
        updateVocoderBypassLevel();
    }
    void setVoiceVariant (bool upper, int idx)
    {
        if (upper) p.choirVariantUpper = idx; else p.choirVariantLower = idx;
        updateChoirVariant (upper ? 1 : 0, idx, now());
    }
    void setChoirToEnsemble (bool on) { p.choirToEnsemble = on; target (choirWet, on ? 1 : 0, 0.02); target (choirDry, on ? 0 : 1, 0.02); }
    void setVocoderToEnsemble (bool on) { p.vocoderToEnsemble = on; target (vocWet, on ? 1 : 0, 0.02); target (vocDry, on ? 0 : 1, 0.02); }
    // jsOrder = true reproduce el orden del prototipo al encender el micro (actualiza los
    // niveles ANTES de marcarlo encendido, así que el "Direct Mic" no suena hasta que se mueve
    // su fader). Solo para las pruebas de comparación; el plugin usa el orden correcto.
    void setMic (bool on, bool jsOrder = false)
    {
        if (jsOrder && on) { updateBalanceLevels(); updateVocoderBypassLevel(); micOn = true; return; }
        micOn = on; updateBalanceLevels(); updateVocoderBypassLevel();
    }
    bool isMicOn() const { return micOn; }

    void updateBalanceLevels()
    {
        target (micDry, micOn ? p.balanceMic / 10 : 0, 0.02);
        target (choirGain[0], p.balanceChoir / 10, 0.03);
        target (choirGain[1], p.balanceChoir / 10, 0.03);
        target (stringsGain, p.balanceStrings / 10, 0.03);
    }

    // ── notas ─────────────────────────────────────────────────────────────────
    void noteOn (int midi)
    {
        for (auto& v : voices) if (v.used && v.held && v.midi == midi) return;
        Voice& v = allocVoice();
        const double t = now();
        v.used = true; v.held = true; v.midi = midi; v.start = t; v.stop = 1e300;
        v.osc.phase = 0; v.vibPhase = 0;
        v.baseFreq = 440 * std::pow (2.0, (midi - 69) / 12.0) * std::pow (2.0, p.pitchTuneCents / 1200);

        const double shiftCents = p.pitchSetSemitones * 100;
        v.detune.init (0);
        if (p.pitchMode == PitchMode::Auto)
        {
            v.detune.setValueAtTime (-shiftCents, t);
            v.detune.linearRampToValueAtTime (0, t + p.pitchTimeMs / 1000, t);
        }
        else if (p.pitchMode == PitchMode::Manual)
            v.detune.setValueAtTime (-shiftCents * p.pitchManualAmount, t);

        v.vibFreq.init (p.vibratoRate);
        v.vibDepth.init (0);
        const double delaySec = p.vibratoDelayMs / 1000;
        v.vibDepth.setValueAtTime (0, t);
        v.vibDepth.setValueAtTime (0, t + delaySec);
        v.vibDepth.linearRampToValueAtTime (p.vibratoDepth, t + delaySec + 0.08, t);

        const bool lower = midi < p.splitPoint;
        v.hasS = lower ? p.stringsLower : p.stringsUpper;
        const bool choirOn = lower ? p.choirLower : p.choirUpper;
        v.choirSide = choirOn ? (lower ? 0 : 1) : -1;
        v.hasV = lower ? p.vocoderLower : p.vocoderUpper;
        const double zoneVol = (lower ? p.lowerVolume : p.upperVolume) / 10;
        auto ramp = [&] (Param& g, double timeSec)
        {
            g.init (0);
            g.setValueAtTime (0, t);
            g.linearRampToValueAtTime (0.22 * zoneVol, t + timeSec, t);
        };
        if (v.hasS) ramp (v.gS, p.stringsAttackMs / 1000);
        if (v.choirSide >= 0) ramp (v.gC, p.attackMs / 1000);
        if (v.hasV) ramp (v.gV, 0.005);
    }

    void noteOff (int midi)
    {
        for (auto& v : voices)
        {
            if (! (v.used && v.held && v.midi == midi)) continue;
            const double t = now();
            const double rel = p.releaseMs / 1000;
            auto release = [&] (Param& g)
            {
                const double cur = g.evalAt (lastQuantumStart (t));
                g.cancelScheduledValues (t);
                g.setValueAtTime (cur, t);
                g.linearRampToValueAtTime (0, t + rel, t);
            };
            if (v.hasS) release (v.gS);
            if (v.choirSide >= 0) release (v.gC);
            if (v.hasV) release (v.gV);
            v.stop = t + rel + 0.05;
            v.held = false;
            return;
        }
    }

    void allNotesOff() { for (auto& v : voices) if (v.used && v.held) noteOff (v.midi); }

    // ── medidores para la interfaz ────────────────────────────────────────────
    double bandLevel (int b) const { return bandPeak[b]; }
    double micLevel() const { return micPeak; }
    void clearMeters() { for (auto& b : bandPeak) b = 0; micPeak = 0; }

    // ── proceso ───────────────────────────────────────────────────────────────
    // mic puede ser nullptr. Procesa n muestras (cualquier tamaño).
    void process (const float* mic, float* outL, float* outR, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int toBoundary = kQuantum - (int) (frame % kQuantum);
            const int m = std::min ({ n - done, toBoundary, 4096 });
            processChunk (mic ? mic + done : nullptr, outL + done, outR + done, m);
            done += m;
        }
    }

private:
    struct Voice
    {
        bool used = false, held = false;
        int midi = 0, choirSide = -1;
        bool hasS = false, hasV = false;
        double start = 0, stop = 0, baseFreq = 440, vibPhase = 0;
        SawOsc osc;
        Param detune, vibFreq, vibDepth, gS, gC, gV;
    };

    double lastQuantumStart (double t) const
    {
        // [[current value]] de Web Audio: el valor al principio del último quantum procesado
        const double q = std::floor (t * sr / kQuantum + 1e-9) * kQuantum - kQuantum;
        return std::max (0.0, q / sr);
    }

    void target (Param& prm, double v, double tau)
    {
        const double t = now();
        prm.cancelScheduledValues (t);
        prm.setTargetAtTime (v, t, tau);
    }

    Voice& allocVoice()
    {
        for (auto& v : voices) if (! v.used) return v;
        // sin voces libres: se reutiliza la que antes termine (o la más antigua)
        Voice* best = &voices[0];
        for (auto& v : voices)
        {
            if (! v.held && (best->held || v.stop < best->stop)) best = &v;
            else if (v.held && best->held && v.start < best->start) best = &v;
        }
        return *best;
    }

    void applyEnsembleDepth (double t)
    {
        ensWet.cancelScheduledValues (t); ensWet.setTargetAtTime (p.ensembleDepth, t, 0.02);
        ensDry.cancelScheduledValues (t); ensDry.setTargetAtTime (1 - p.ensembleDepth * 0.5, t, 0.02);
    }

    void updateChoirVariant (int side, int idx, double t)
    {
        for (int i = 0; i < 7; ++i)
        {
            bool active = false;
            for (int k : kVoicings[idx]) if (k == i) active = true;
            choirFilterGain[side][i].cancelScheduledValues (t);
            choirFilterGain[side][i].setTargetAtTime (active ? 1 : 0, t, 0.02);
        }
    }

    void updateVocoderBypassLevel()
    {
        const bool active = p.vocoderLower || p.vocoderUpper;
        target (bypassGain, active ? 0.35 : 0, 0.03);
    }

    void updateZoneVolume()
    {
        for (auto& v : voices)
        {
            if (! (v.used && v.held)) continue;
            const double zone = (v.midi < p.splitPoint ? p.lowerVolume : p.upperVolume) / 10;
            const double tg = 0.22 * zone;
            if (v.hasS) target (v.gS, tg, 0.03);
            if (v.choirSide >= 0) target (v.gC, tg, 0.03);
            if (v.hasV) target (v.gV, tg, 0.01);
        }
    }

    void updatePitchManual()
    {
        if (p.pitchMode != PitchMode::Manual) return;
        const double shiftCents = p.pitchSetSemitones * 100;
        for (auto& v : voices) if (v.used && v.held) target (v.detune, -shiftCents * p.pitchManualAmount, 0.01);
    }

    void makeGateCurve (double threshold)
    {
        for (int i = 0; i < 1024; ++i)
        {
            const double x = (i / 1023.0) * 2 - 1;
            const double v = std::max (0.0, x);
            gate.curve[(size_t) i] = (float) (v <= threshold ? 0 : v - threshold);
        }
    }

    void makeImpulse (double durationSec)
    {
        const int len = std::max (1, (int) std::floor (sr * durationSec));
        std::vector<float> ir[2];
        float power = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            ir[ch].resize ((size_t) len);
            float chPower = 0;
            for (int i = 0; i < len; ++i)
            {
                ir[ch][(size_t) i] = (float) ((rng.next() * 2 - 1) * std::pow (1.0 - (double) i / len, 2.2));
                chPower += ir[ch][(size_t) i] * ir[ch][(size_t) i];
            }
            power += chPower;
        }
        // normalización del ConvolverNode (normalize = true)
        power = std::sqrt (power / (float) (2 * len));
        if (! std::isfinite (power) || power < 0.000125f) power = 0.000125f;
        const float scale = (float) (1.0 / power * 0.00125 * (44100.0 / sr));
        for (auto& c : ir) for (auto& v : c) v *= scale;
        if (conv != nullptr) conv->setImpulse (ir[0], ir[1], sr);
    }

    void processChunk (const float* mic, float* outL, float* outR, int m)
    {
        // etapa 1: voces → secciones → ensemble → master → ping-pong
        for (int i = 0; i < m; ++i)
        {
            const double t = (double) (frame + i) / sr;
            double stringsIn = 0, choirIn[2] = { 0, 0 }, carrier = 0;

            for (auto& v : voices)
            {
                if (! v.used) continue;
                if (t >= v.stop) { v.used = false; continue; }
                const double vib = std::sin (2 * kPi * v.vibPhase) * v.vibDepth.valueAt (t);
                v.vibPhase += v.vibFreq.valueAt (t) / sr;
                v.vibPhase -= std::floor (v.vibPhase);
                const double detune = v.detune.valueAt (t) + vib;
                const double f = v.baseFreq * std::pow (2.0, detune / 1200);
                const double s = v.osc.next (f, sr);
                if (v.hasS) stringsIn += s * v.gS.valueAt (t);
                if (v.choirSide >= 0) choirIn[v.choirSide] += s * v.gC.valueAt (t);
                if (v.hasV) carrier += s * v.gV.valueAt (t);
            }
            carrier += noise[(size_t) noisePos] * noiseGain.valueAt (t);
            noisePos = noisePos + 1 == (int) noise.size() ? 0 : noisePos + 1;

            // STRINGS
            double sx = stringsIn * 0.9;
            double strings = stringsBp[0].process (sx) * 1.0 + stringsBp[1].process (sx) * 0.6;
            strings *= stringsGain.valueAt (t);
            toneFilter.setFrequency (toneFreq.valueAt (t));
            const double toneOut = toneFilter.process (strings);

            // HUMAN VOICE
            double choirSum = 0;
            for (int side = 0; side < 2; ++side)
            {
                const double cx = choirIn[side] * 0.8;
                double sum = 0;
                for (int k = 0; k < 7; ++k)
                    sum += choirBp[side][k].process (cx) * choirFilterGain[side][k].valueAt (t);
                choirSum += sum * choirGain[side].valueAt (t);
            }

            // VOCODER
            const double micIn = (micOn && mic != nullptr) ? (double) mic[i] : 0.0;
            const double mg = micIn * micGain.valueAt (t);
            micPeak = std::max (micPeak, std::abs (mg));
            const double makeup = envMakeup.valueAt (t);
            double voc = 0;
            for (int b = 0; b < kBands; ++b)
            {
                const double a = analysis[b].process (mg);
                const double sm = smoother[b].process (gate.process (rect.process (a)));
                bandPeak[b] = std::max (bandPeak[b], std::abs (sm));
                voc += synth[b].process (carrier) * (sm * makeup);
            }
            voc += bypassFilter.process (mg) * bypassGain.valueAt (t);
            vocTone.setFrequency (vocToneFreq.valueAt (t));
            const double vocToned = vocTone.process (voc);   // * 1.0 (gainNode del vocoder)
            if (! jsVocoderToneBug) voc = vocToned;

            // envíos al ensemble o al bus seco
            const double ensIn = toneOut + choirSum * choirWet.valueAt (t) + voc * vocWet.valueAt (t);
            const double dryBus = choirSum * choirDry.valueAt (t) + voc * vocDry.valueAt (t) + micIn * micDry.valueAt (t);

            // ENSEMBLE (2 BBD con LFO, uno a cada lado)
            double branch[2];
            for (int k = 0; k < 2; ++k)
            {
                const double lfo = std::sin (2 * kPi * (0.35 + k * 0.22) * t) * 0.0025;
                branch[k] = ensDelay[k].read ((0.008 + k * 0.003 + lfo) * sr);
                ensDelay[k].write (ensIn);
            }
            const double ed = ensDry.valueAt (t) * ensIn, ew = ensWet.valueAt (t);
            const double mgain = masterGain.valueAt (t);
            const double inL = (ed + ew * branch[0] + dryBus) * mgain;
            const double inR = (ed + ew * branch[1] + dryBus) * mgain;

            // DELAY PING-PONG (las dos líneas son estéreo y se realimentan cruzadas)
            const double dL = ppTimeL.valueAt (t) * sr, dR = ppTimeR.valueAt (t) * sr;
            const double lL = ppDL[0].read (dL), lR = ppDL[1].read (dL);
            const double rL = ppDR[0].read (dR), rR = ppDR[1].read (dR);
            const double fbL = ppFbL.valueAt (t), fbR = ppFbR.valueAt (t);
            ppDL[0].write (inL + fbR * rL); ppDL[1].write (inR + fbR * rR);
            ppDR[0].write (fbL * lL); ppDR[1].write (fbL * lR);
            const double wet = ppWet.valueAt (t), dry = ppDry.valueAt (t);
            chunkL[(size_t) i] = (float) (inL * dry + wet * (lL + lR));
            chunkR[(size_t) i] = (float) (inR * dry + wet * (rL + rR));
        }

        // etapa 2: convolución (por bloque)
        std::copy (chunkL.begin(), chunkL.begin() + m, convL.begin());
        std::copy (chunkR.begin(), chunkR.begin() + m, convR.begin());
        if (conv != nullptr) conv->process (convL.data(), convR.data(), m);
        else { std::fill (convL.begin(), convL.begin() + m, 0.0f); std::fill (convR.begin(), convR.begin() + m, 0.0f); }

        // etapa 3: reverb (amortiguación + mezcla) y limitador
        for (int i = 0; i < m; ++i)
        {
            const double t = (double) (frame + i) / sr;
            const double df = rvDampFreq.valueAt (t);
            rvDamp[0].setFrequency (df); rvDamp[1].setFrequency (df);
            const double wl = rvDamp[0].process (convL[(size_t) i]), wr = rvDamp[1].process (convR[(size_t) i]);
            const double dry = rvDry.valueAt (t), wet = rvWet.valueAt (t);
            float l = (float) (chunkL[(size_t) i] * dry + wl * wet);
            float r = (float) (chunkR[(size_t) i] * dry + wr * wet);
            limiter.process (l, r);
            outL[i] = l; outR[i] = r;
        }
        frame += m;
    }

    double sr = 48000;
    int64_t frame = 0;
    Lcg rng;
    ConvolverBase* conv = nullptr;
    bool micOn = false;

    std::array<Voice, kMaxVoices> voices {};
    std::vector<float> noise;
    int noisePos = 0;
    Param noiseGain;

    Biquad stringsBp[2], toneFilter;
    Param stringsGain, toneFreq;
    Biquad choirBp[2][7];
    Param choirFilterGain[2][7], choirGain[2], choirWet, choirDry;

    Param micGain, micDry, envMakeup, bypassGain, vocToneFreq, vocWet, vocDry;
    Biquad analysis[kBands], smoother[kBands], synth[kBands], bypassFilter, vocTone;
    WaveShaper rect, gate;
    double bandPeak[kBands] {};
    double micPeak = 0;

    Param ensWet, ensDry;
    DelayLine ensDelay[2];
    Param masterGain;

    Param ppDry, ppWet, ppTimeL, ppTimeR, ppFbL, ppFbR;
    DelayLine ppDL[2], ppDR[2];

    Param rvDry, rvWet, rvDampFreq;
    Biquad rvDamp[2];
    Compressor limiter;

    std::vector<float> chunkL, chunkR, convL, convR;
};
} // namespace br
