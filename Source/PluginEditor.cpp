#include "PluginEditor.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }
String fmtFixed1 (double v) { return String (v, 1); }
String fmtMs (double v) { return String (roundToInt (v)) + " ms"; }
String fmtPct (double v) { return String (roundToInt (v)) + "%"; }

// Botón naranja (.power-btn)
class OrangeButton : public Button
{
public:
    explicit OrangeButton (const String& t) : Button (t) { setWantsKeyboardFocus (false); }
    void paintButton (Graphics& g, bool hi, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0, 2);
        if (down) r = r.translated (0, 2);
        else { g.setColour (Colour (0xff6e380e)); g.fillRoundedRectangle (r.translated (0, 3), 4); }
        g.setGradientFill (ColourGradient (theme::orange.brighter (hi ? 0.1f : 0.0f), 0, r.getY(), theme::orangeDim, 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4);
        g.setColour (Colour (0xff1c1108));
        g.setFont (theme::oswald (14.0f, true));
        g.drawText (getButtonText(), r, Justification::centred, false);
    }
};
} // namespace

//==============================================================================
class Panel : public Component
{
public:
    explicit Panel (BladeRainProcessor& p) : proc (p), meter (p), keyboard (p), split (p),
        ledRed (Colour (0xffe04b3a), Colour (0xff5a2f2f)), ledGreen (Colour (0xff57c46b), Colour (0xff2f5a35)),
        octDown ("-"), octUp ("+")
    {
        auto toggle = [this] (const char* id) { return std::make_unique<Rocker> ("", [this, id] { return proc.flag (id); }, [this, id] { proc.toggleParam (id); }); };
        auto choice = [this] (const char* id, int idx, const char* text)
        {
            return std::make_unique<Rocker> (u8 (text), [this, id, idx] { return (int) proc.param (id) == idx; }, [this, id, idx] { proc.setParam (id, (float) idx); });
        };

        // fila 1
        micRocker = std::make_unique<Rocker> ("Mic", [this] { return proc.flag (pid::mic); }, [this] { proc.toggleParam (pid::mic); });
        micRocker->setTooltip (u8 ("Activa la entrada de audio como micrófono del vocoder (y del Direct Mic del Balance)"));
        holdRocker = std::make_unique<Rocker> ("Hold", [this] { return proc.flag (pid::hold); }, [this] { proc.toggleParam (pid::hold); });
        holdRocker->setTooltip (u8 ("Cada nota se queda sonando hasta que la vuelves a pulsar"));
        styleInfo (inputStatus);
        styleInfo (octaveLabel);
        styleInfo (keysHint);
        keysHint.setText (u8 ("Teclado del ordenador: A W S E D F T G Y H U J K O L P · Z / X: octava"), dontSendNotification);
        octDown.onClick = [this] { proc.octaveShift = jmax (-3, proc.octaveShift - 1); };
        octUp.onClick = [this] { proc.octaveShift = jmin (3, proc.octaveShift + 1); };

        // fila 2
        addFader (pid::balanceMic, "Mic", fmtFixed1);
        addFader (pid::balanceChoir, "Voice", fmtFixed1);
        addFader (pid::balanceStrings, "Strings", fmtFixed1);
        addFader (pid::release, "Release", fmtMs);
        addFader (pid::stringsAttack, "Attack", fmtMs);
        addFader (pid::tone, "Tone", fmtPct);
        voiceUpper[0] = choice (pid::voiceUpper, 0, "Male 8'");
        voiceUpper[1] = choice (pid::voiceUpper, 1, "Female 4'");
        voiceLower[0] = choice (pid::voiceLower, 0, "Male 8'");
        voiceLower[1] = choice (pid::voiceLower, 1, "Male 4'");
        addFader (pid::attack, "Attack", fmtMs);
        addFader (pid::vibratoRate, "Rate", [] (double v) { return String (v, 1) + " Hz"; });
        addFader (pid::vibratoDelay, "Del", fmtMs);
        addFader (pid::vibratoDepth, "Depth", [] (double v) { return String (v, 1) + " ct"; });
        addFader (pid::vocoderTone, "Tone", fmtPct);
        addFader (pid::vocoderMicLevel, "M.Lev", fmtFixed1);
        addFader (pid::noisegate, "Gate", [] (double v) { return String (v, v == std::floor (v) ? 0 : 1) + "%"; });
        addFader (pid::noisemix, "S/N", fmtPct);
        addFader (pid::sens, "Sens", [] (double v) { return String (roundToInt (v)); });
        addFader (pid::ensemble, "Depth", fmtPct);
        choirEns = std::make_unique<Rocker> (u8 ("Voice→Ens."), [this] { return proc.flag (pid::choirToEnsemble); }, [this] { proc.toggleParam (pid::choirToEnsemble); });
        vocEns = std::make_unique<Rocker> (u8 ("Vocoder→Ens."), [this] { return proc.flag (pid::vocoderToEnsemble); }, [this] { proc.toggleParam (pid::vocoderToEnsemble); });

        // fila 3
        const char* sideIds[3][2] = { { pid::stringsLower, pid::stringsUpper }, { pid::choirLower, pid::choirUpper }, { pid::vocoderLower, pid::vocoderUpper } };
        for (int s = 0; s < 3; ++s) for (int u = 0; u < 2; ++u) sides[s][u] = toggle (sideIds[s][u]);
        addFader (pid::lowerVolume, "Lower vol", fmtFixed1);
        addFader (pid::upperVolume, "Upper vol", fmtFixed1);
        addFader (pid::pitchTime, "Time", fmtMs);
        addFader (pid::pitchTune, "Tune", [] (double v) { const int c = roundToInt (v); return (c > 0 ? "+" : "") + String (c) + " ct"; });
        addFader (pid::pitchSet, "Pitch set", [] (double v) { return String (roundToInt (v)) + " st"; });
        addFader (pid::pitchShift, "Pitch shift", [] (double v) { const int c = roundToInt (v); return c == 0 ? String ("Normal") : "Down " + String (c) + "%"; });
        pitchModes[0] = choice (pid::pitchMode, 0, "Auto");
        pitchModes[1] = choice (pid::pitchMode, 1, "Off (ext cont)");
        pitchModes[2] = choice (pid::pitchMode, 2, "Manual");
        addFader (pid::delayTime, "Time", fmtMs);
        addFader (pid::delayFeedback, "Feedback", fmtPct);
        addFader (pid::delayMix, "Mix", fmtPct);
        addFader (pid::reverbSize, "Size", [] (double v) { return String (v, 1) + " s"; });
        addFader (pid::reverbTone, "Tone", [] (double v) { return String (v / 1000, 1) + "k"; });
        addFader (pid::reverbMix, "Mix", fmtPct);
        addFader (pid::master, "Volume", fmtPct);

        for (auto* r : allRockers()) addAndMakeVisible (r);
        for (auto* c : std::initializer_list<Component*> { &inputStatus, &octaveLabel, &keysHint, &octDown, &octUp, &meter, &keyboard, &split, &ledRed, &ledGreen })
            addAndMakeVisible (c);
        for (auto* c : getChildren()) c->setWantsKeyboardFocus (false);
        layout();
        refresh();
    }

    int contentWidth() const { return totalW; }
    int contentHeight() const { return totalH; }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::bg);
        // mueble de madera
        g.setGradientFill (ColourGradient (theme::woodA, 0, (float) unit.getY(), theme::woodB, 0, (float) unit.getBottom(), false));
        g.fillRoundedRectangle (unit.toFloat(), 10);
        g.setColour (Colour (0x59000000));
        g.drawRoundedRectangle (unit.toFloat().reduced (1), 10, 2);
        // panel metálico cepillado
        auto pr = panelRect.toFloat();
        g.setGradientFill (ColourGradient (theme::panelA, 0, pr.getY(), theme::panelB, 0, pr.getBottom(), false));
        g.fillRoundedRectangle (pr, 6);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (panelRect);
            g.setColour (Colour (0x09ffffff));
            for (float x = pr.getX() - pr.getHeight(); x < pr.getRight(); x += 3)
                g.drawLine (x, pr.getBottom(), x + pr.getHeight() * 0.18f, pr.getY(), 1.0f);
        }
        g.setColour (Colour (0x0fffffff));
        g.drawRoundedRectangle (pr, 6, 1);

        // marca
        g.setFont (theme::tr909 (52.0f));
        const String vj = "VJ", num = "-330";
        const float w1 = GlyphArrangement::getStringWidth (g.getCurrentFont(), vj), w2 = GlyphArrangement::getStringWidth (g.getCurrentFont(), num);
        const float bx = brand.getCentreX() - (w1 + w2 + 4) / 2;
        g.setColour (Colour (0x80000000));
        g.drawText (vj, Rectangle<float> (bx, (float) brand.getY() + 1, w1 + 4, (float) brand.getHeight()), Justification::centredLeft, false);
        g.setColour (theme::cream);
        g.drawText (vj, Rectangle<float> (bx, (float) brand.getY(), w1 + 4, (float) brand.getHeight()), Justification::centredLeft, false);
        g.setColour (theme::orange);
        g.drawText (num, Rectangle<float> (bx + w1 + 4, (float) brand.getY(), w2 + 4, (float) brand.getHeight()), Justification::centredLeft, false);
        g.setColour (theme::cream.withAlpha (0.15f));
        g.fillRect (brand.getX(), brand.getBottom() + 8, brand.getWidth(), 2);

        // módulos
        for (auto& row : rows)
            for (size_t i = 0; i < row.size(); ++i)
            {
                const auto& m = row[i];
                auto r = m.r.toFloat();
                Path p;
                const bool first = i == 0, last = i + 1 == row.size();
                p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 5, 5, first, last, first, last);
                g.setColour (Colour (0x38000000));
                g.fillPath (p);
                if (! last) { g.setColour (theme::orange.withAlpha (0.28f)); g.fillRect (r.getRight() - 1, r.getY(), 1.0f, r.getHeight()); }
                g.setColour (theme::moduleTitle);
                g.setFont (theme::mono (11.0f, true).withExtraKerningFactor (0.16f));   // letter-spacing: 2px
                g.drawText (m.title.toUpperCase(), Rectangle<float> (r.getX() + 11, r.getY() + 9, r.getWidth() - 14, 14), Justification::centredLeft, false);
            }

        // textos fijos dentro de los módulos
        g.setFont (theme::mono (9.0f));
        g.setColour (theme::label);
        g.drawText ("UPPER", upperLbl, Justification::centredLeft, false);
        g.drawText ("LOWER", lowerLbl, Justification::centredLeft, false);
        g.setFont (theme::oswald (11.0f));
        g.setColour (Colour (0xff888888));
        g.drawText ("Lower", gridHead[0], Justification::centred, false);
        g.drawText ("Upper", gridHead[1], Justification::centred, false);
        const char* rowNames[3] = { "Strings", "Human voice", "Vocoder" };
        for (int i = 0; i < 3; ++i) g.drawText (rowNames[i], gridRows[i], Justification::centredLeft, false);
    }

    void resized() override {}

    void refresh()
    {
        for (auto* r : allRockers()) r->refresh();
        micRocker->setDim (! proc.hasMicInput());
        for (auto& f : faders) f->refresh();
        meter.update();
        keyboard.refresh();
        const float mic = proc.takeMicLevel();
        micPeakHold = jmax (mic, micPeakHold * 0.7f);
        ledRed.set (micPeakHold > 0.85f);
        ledGreen.set (micPeakHold > 0.04f);
        const String st = ! proc.hasMicInput() ? u8 ("Sin entrada de audio")
                                               : (proc.flag (pid::mic) ? u8 ("Micro activo (entrada 1)") : u8 ("Micro apagado"));
        inputStatus.setText (st, dontSendNotification);
        octaveLabel.setText ("Octava: " + String (proc.octaveShift > 0 ? "+" : "") + String (proc.octaveShift), dontSendNotification);
    }

private:
    struct Module { String title; Rectangle<int> r; };

    static void styleInfo (Label& l)
    {
        l.setFont (theme::mono (11.0f));
        l.setColour (Label::textColourId, theme::label);
        l.setBorderSize ({ 0, 0, 0, 0 });
        l.setInterceptsMouseClicks (false, false);
    }

    void addFader (const char* id, const char* name, std::function<String (double)> fmt)
    {
        faders.push_back (std::make_unique<Fader> (proc, id, u8 (name), std::move (fmt)));
        addAndMakeVisible (*faders.back());
        faderById[id] = faders.back().get();
    }

    Fader* F (const char* id) { return faderById[id]; }

    std::vector<Rocker*> allRockers()
    {
        std::vector<Rocker*> v { micRocker.get(), holdRocker.get(), choirEns.get(), vocEns.get() };
        for (auto& r : voiceUpper) v.push_back (r.get());
        for (auto& r : voiceLower) v.push_back (r.get());
        for (auto& r : pitchModes) v.push_back (r.get());
        for (auto& s : sides) for (auto& r : s) v.push_back (r.get());
        return v;
    }

    // coloca faders en fila desde x; devuelve el ancho ocupado
    int placeFaders (std::initializer_list<const char*> ids, int x, int y)
    {
        int cx = x;
        for (auto* id : ids) { F (id)->setBounds (cx, y, Fader::kWidth, Fader::kHeight); cx += Fader::kWidth + 8; }
        return cx - 8 - x;
    }

    void layout()
    {
        constexpr int unitPad = 14, panelPadX = 22, panelPadTop = 18, modPadX = 11, titleH = 22;
        const int x0 = unitPad + panelPadX;
        int y = unitPad + panelPadTop;
        rows.clear();

        brand = { x0, y, 1080, 58 };
        y += 58 + 10 + 16;

        auto beginRow = [&] { rows.emplace_back(); };
        auto addModule = [&] (const String& title, int x, int w, int top, int h) { rows.back().push_back ({ title, { x, top, w, h } }); };

        // ── fila 1 ──
        beginRow();
        {
            const int top = y, cy = y + titleH + 4;
            int x = x0;
            int cx = x + modPadX;
            micRocker->setBounds (cx, cy, 36, 44); cx += 36 + 8;
            holdRocker->setBounds (cx, cy, 36, 44); cx += 36 + 12;
            inputStatus.setBounds (cx, cy + 8, 190, 16); cx += 190;
            const int w1 = cx - x + modPadX;
            addModule (u8 ("Entrada & hold"), x, w1, top, 80);
            x += w1;
            cx = x + modPadX;
            octDown.setBounds (cx, cy + 2, 34, 30); cx += 40;
            octaveLabel.setBounds (cx, cy + 8, 80, 16); cx += 82;
            octUp.setBounds (cx, cy + 2, 34, 30); cx += 46;
            keysHint.setBounds (cx, cy + 8, 430, 16); cx += 430;
            addModule ("Teclado", x, cx - x + modPadX, top, 80);
            y += 80 + 14;
        }

        // ── fila 2 ──
        beginRow();
        {
            const int top = y, cy = y + titleH + 4, h = titleH + 4 + Fader::kHeight + 12;
            int x = x0, w;
            w = placeFaders ({ pid::balanceMic, pid::balanceChoir, pid::balanceStrings }, x + modPadX, cy) + 2 * modPadX;
            addModule ("Balance", x, w, top, h); x += w;
            w = placeFaders ({ pid::release }, x + modPadX, cy) + 2 * modPadX + 14;
            addModule ("Release", x, w, top, h); x += w;
            w = placeFaders ({ pid::stringsAttack, pid::tone }, x + modPadX, cy) + 2 * modPadX;
            addModule ("Strings", x, w, top, h); x += w;
            {
                int cx = x + modPadX;
                upperLbl = { cx, cy, 120, 12 };
                voiceUpper[0]->setBounds (cx, cy + 16, 50, 44);
                voiceUpper[1]->setBounds (cx + 58, cy + 16, 56, 44);
                lowerLbl = { cx, cy + 66, 120, 12 };
                voiceLower[0]->setBounds (cx, cy + 82, 50, 44);
                voiceLower[1]->setBounds (cx + 58, cy + 82, 56, 44);
                cx += 126;
                cx += placeFaders ({ pid::attack, pid::vibratoRate, pid::vibratoDelay, pid::vibratoDepth }, cx, cy);
                w = cx - x + modPadX;
                addModule ("Human voice (Attack & Vibrato)", x, w, top, h); x += w;
            }
            {
                int cx = x + modPadX;
                cx += placeFaders ({ pid::vocoderTone, pid::vocoderMicLevel, pid::noisegate, pid::noisemix, pid::sens }, cx, cy) + 6;
                ledRed.setBounds (cx, cy + Fader::kHeight - 44, 14, 14);
                ledGreen.setBounds (cx, cy + Fader::kHeight - 26, 14, 14);
                cx += 14;
                w = cx - x + modPadX;
                addModule ("Vocoder", x, w, top, h); x += w;
            }
            {
                int cx = x + modPadX;
                cx += placeFaders ({ pid::ensemble }, cx, cy) + 10;
                choirEns->setBounds (cx, cy + 30, 70, 44);
                vocEns->setBounds (cx, cy + 80, 70, 44);
                cx += 70;
                w = cx - x + modPadX;
                addModule ("Ensemble", x, w, top, h); x += w;
            }
            row2Right = x;
            y += h + 14;
        }

        // ── fila 3 ──
        beginRow();
        {
            const int top = y, cy = y + titleH + 4, h = titleH + 4 + Fader::kHeight + 70;
            int x = x0, w;
            {
                int cx = x + modPadX;
                gridHead[0] = { cx + 84, cy, 40, 14 };
                gridHead[1] = { cx + 130, cy, 40, 14 };
                for (int i = 0; i < 3; ++i)
                {
                    const int ry = cy + 20 + i * 34;
                    gridRows[i] = { cx, ry + 2, 82, 18 };
                    sides[i][0]->setBounds (cx + 84, ry, 40, 30);
                    sides[i][1]->setBounds (cx + 130, ry, 40, 30);
                }
                split.setBounds (cx, cy + Fader::kHeight + 6, 160, 52);
                cx += 186;
                cx += placeFaders ({ pid::lowerVolume, pid::upperVolume }, cx, cy);
                w = cx - x + modPadX;
                addModule ("Lower / Upper", x, w, top, h); x += w;
            }
            {
                int cx = x + modPadX;
                cx += placeFaders ({ pid::pitchTime, pid::pitchTune, pid::pitchSet, pid::pitchShift }, cx, cy) + 12;
                for (int i = 0; i < 3; ++i) pitchModes[i]->setBounds (cx, cy + i * 48, 80, 44);
                cx += 80;
                w = cx - x + modPadX;
                addModule ("Pitch shift", x, w, top, h); x += w;
            }
            w = placeFaders ({ pid::delayTime, pid::delayFeedback, pid::delayMix }, x + modPadX, cy) + 2 * modPadX;
            addModule ("Delay ping-pong", x, w, top, h); x += w;
            w = placeFaders ({ pid::reverbSize, pid::reverbTone, pid::reverbMix }, x + modPadX, cy) + 2 * modPadX;
            addModule ("Reverb", x, w, top, h); x += w;
            w = placeFaders ({ pid::master }, x + modPadX, cy) + 2 * modPadX + 10;
            addModule ("Master", x, w, top, h); x += w;
            y += h + 14;
            row3Right = x;
        }

        const int innerRight = jmax (row2Right, row3Right);
        const int innerW = innerRight - x0;

        // ── medidor ──
        beginRow();
        {
            const int top = y;
            meter.setBounds (x0 + modPadX, y + titleH + 4, innerW - 2 * modPadX, 112);
            addModule (u8 ("Vocoder — banco de 10 bandas"), x0, innerW, top, titleH + 4 + 112 + 12);
            y += titleH + 4 + 112 + 12 + 14;
        }

        keyboard.setBounds (x0, y, innerW, 120);
        y += 120;

        brand.setWidth (innerW);
        panelRect = { unitPad, unitPad, innerW + 2 * panelPadX, y + 22 - unitPad };
        unit = { 0, 0, panelRect.getRight() + unitPad, panelRect.getBottom() + unitPad };
        totalW = unit.getWidth();
        totalH = unit.getHeight();
    }

    BladeRainProcessor& proc;
    std::vector<std::unique_ptr<Fader>> faders;
    std::map<String, Fader*> faderById;
    std::unique_ptr<Rocker> micRocker, holdRocker, choirEns, vocEns;
    std::unique_ptr<Rocker> voiceUpper[2], voiceLower[2], pitchModes[3], sides[3][2];
    VocoderMeter meter;
    Keyboard keyboard;
    SplitSlider split;
    Led ledRed, ledGreen;
    Label inputStatus, octaveLabel, keysHint;
    OrangeButton octDown, octUp;

    std::vector<std::vector<Module>> rows;
    Rectangle<int> unit, panelRect, brand, upperLbl, lowerLbl, gridHead[2], gridRows[3];
    int row2Right = 0, row3Right = 0, totalW = 1200, totalH = 900;
    float micPeakHold = 0;
};

//==============================================================================
BladeRainEditor::BladeRainEditor (BladeRainProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    panel = std::make_unique<Panel> (p);
    panel->setSize (panel->contentWidth(), panel->contentHeight());
    viewport.setViewedComponent (panel.get(), false);
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (640, 400, panel->contentWidth() + 40, panel->contentHeight() + 40);
    setSize (jmin (panel->contentWidth() + 24, 1240), jmin (panel->contentHeight() + 24, 900));
    startTimerHz (30);
}

BladeRainEditor::~BladeRainEditor()
{
    stopTimer();
    for (auto& [k, m] : computerKeysDown) proc.keyboardState.noteOff (1, m, 0.0f);
    setLookAndFeel (nullptr);
}

void BladeRainEditor::paint (Graphics& g) { g.fillAll (theme::bg); }

void BladeRainEditor::resized()
{
    viewport.setBounds (getLocalBounds());
    // centra el instrumento si sobra espacio
    const int w = panel->contentWidth(), h = panel->contentHeight();
    panel->setSize (jmax (w, viewport.getMaximumVisibleWidth()), jmax (h, viewport.getMaximumVisibleHeight()));
}

void BladeRainEditor::timerCallback() { panel->refresh(); }

static const char* kKeyChars = "awsedftgyhujkolp";

bool BladeRainEditor::keyPressed (const KeyPress& k)
{
    const auto c = (char) CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c == 'z') { proc.octaveShift = jmax (-3, proc.octaveShift - 1); return true; }
    if (c == 'x') { proc.octaveShift = jmin (3, proc.octaveShift + 1); return true; }
    return std::strchr (kKeyChars, c) != nullptr && c != 0;
}

bool BladeRainEditor::keyStateChanged (bool)
{
    bool handled = false;
    for (int i = 0; kKeyChars[i] != 0; ++i)
    {
        const char c = kKeyChars[i];
        const bool down = KeyPress::isKeyCurrentlyDown (c) || KeyPress::isKeyCurrentlyDown (CharacterFunctions::toUpperCase ((juce_wchar) c));
        const auto it = computerKeysDown.find (c);
        if (down && it == computerKeysDown.end())
        {
            const int midi = jlimit (0, 127, 60 + i + proc.octaveShift * 12);
            computerKeysDown[c] = midi;
            proc.keyboardState.noteOn (1, midi, 1.0f);
            handled = true;
        }
        else if (! down && it != computerKeysDown.end())
        {
            proc.keyboardState.noteOff (1, it->second, 0.0f);
            computerKeysDown.erase (it);
            handled = true;
        }
    }
    return handled;
}
