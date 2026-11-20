// Ejecuta el motor en C++ con los mismos escenarios que tests/run_js.mjs.
//   compare_cpp <carpeta>
#include "../Source/dsp/VP330Engine.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace br;

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "tests/out";
    constexpr double SR = 48000;

    std::vector<float> mic;
    {
        std::ifstream in (dir + "/mic.f32", std::ios::binary);
        in.seekg (0, std::ios::end);
        mic.resize ((size_t) in.tellg() / 4);
        in.seekg (0);
        in.read ((char*) mic.data(), (std::streamsize) (mic.size() * 4));
    }

    std::ifstream list (dir + "/scenarios.json");
    std::string json ((std::istreambuf_iterator<char> (list)), std::istreambuf_iterator<char>());
    std::vector<std::string> names;
    for (size_t p = 0; (p = json.find ('"', p)) != std::string::npos;)
    {
        const size_t e = json.find ('"', p + 1);
        names.push_back (json.substr (p + 1, e - p - 1));
        p = e + 1;
    }

    const int splitPoints[5] = { 53, 57, 60, 64, 67 };
    wa::SawOsc::kNorm = 1.0;   // como node-web-audio-api (ver WebAudio.h)

    for (const auto& name : names)
    {
        std::multimap<long, std::vector<std::string>> acts;
        long endQ = 0, micStartQ = -1;
        std::ifstream sc (dir + "/" + name + ".txt");
        std::string line;
        while (std::getline (sc, line))
        {
            std::stringstream ls (line);
            std::string qq, cmd;
            long q;
            ls >> qq >> q >> cmd;
            if (cmd.empty()) continue;
            if (cmd == "end") { endQ = q; continue; }
            if (cmd == "micstart") { micStartQ = q; continue; }
            std::vector<std::string> a { cmd };
            std::string x;
            while (ls >> x) a.push_back (x);
            acts.insert ({ q, a });
        }

        UniformConvolver conv;
        VP330Engine eng;
        eng.jsVocoderToneBug = true;
        eng.prepare (SR, &conv, 1);
        bool sides[3][2] = { { true, true }, { true, true }, { false, false } };
        bool choirEns = true, vocEns = false, micOn = false;

        std::vector<float> outL ((size_t) (endQ * 128)), outR ((size_t) (endQ * 128));
        std::vector<float> micBlock (128);
        for (long q = 0; q < endQ; ++q)
        {
            auto range = acts.equal_range (q);
            for (auto it = range.first; it != range.second; ++it)
            {
                const auto& a = it->second;
                const std::string& c = a[0];
                if (c == "on") eng.noteOn (std::stoi (a[1]));
                else if (c == "off") eng.noteOff (std::stoi (a[1]));
                else if (c == "slider")
                {
                    const std::string& id = a[1];
                    const double v = std::stod (a[2]);
                    if (id == "balanceStrings") eng.setBalanceStrings (v);
                    else if (id == "balanceChoir") eng.setBalanceChoir (v);
                    else if (id == "balanceMic") eng.setBalanceMic (v);
                    else if (id == "tone") eng.setTone (v);
                    else if (id == "stringsAttack") eng.setStringsAttack (v);
                    else if (id == "release") eng.setRelease (v);
                    else if (id == "attack") eng.setAttack (v);
                    else if (id == "ensemble") eng.setEnsemble (v);
                    else if (id == "vibratoRate") eng.setVibratoRate (v);
                    else if (id == "vibratoDelay") eng.setVibratoDelay (v);
                    else if (id == "vibratoDepth") eng.setVibratoDepth (v);
                    else if (id == "vocoderTone") eng.setVocoderTone (v);
                    else if (id == "vocoderMicLevel") eng.setVocoderMicLevel (v);
                    else if (id == "noisegate") eng.setNoiseGate (v);
                    else if (id == "noisemix") eng.setNoiseMix (v);
                    else if (id == "sens") eng.setSensitivity (v);
                    else if (id == "master") eng.setMaster (v);
                    else if (id == "lowerVolume") eng.setLowerVolume (v);
                    else if (id == "upperVolume") eng.setUpperVolume (v);
                    else if (id == "delayTime") eng.setDelayTime (v);
                    else if (id == "delayFeedback") eng.setDelayFeedback (v);
                    else if (id == "delayMix") eng.setDelayMix (v);
                    else if (id == "reverbSize") eng.setReverbSize (v);
                    else if (id == "reverbTone") eng.setReverbTone (v);
                    else if (id == "reverbMix") eng.setReverbMix (v);
                    else if (id == "pitchTime") eng.setPitchTime (v);
                    else if (id == "pitchTune") eng.setPitchTune (v);
                    else if (id == "pitchSet") eng.setPitchSet (v);
                    else if (id == "pitchShift") eng.setPitchShift (v);
                    else if (id == "splitPoint") eng.setSplitPoint (splitPoints[(int) v]);
                    else std::cerr << "slider desconocido " << id << "\n";
                }
                else if (c == "click")
                {
                    const std::string& id = a[1];
                    const char* secNames[3] = { "Strings", "Choir", "Vocoder" };
                    bool handled = false;
                    for (int s = 0; s < 3 && ! handled; ++s)
                        for (int u = 0; u < 2 && ! handled; ++u)
                            if (id == std::string ("toggle") + secNames[s] + (u ? "Upper" : "Lower"))
                            {
                                sides[s][u] = ! sides[s][u];
                                eng.setSectionSide (s, u == 1, sides[s][u]);
                                handled = true;
                            }
                    if (handled) continue;
                    if (id == "voiceLowerMale8") eng.setVoiceVariant (false, 0);
                    else if (id == "voiceLowerMale4") eng.setVoiceVariant (false, 1);
                    else if (id == "voiceUpperMale8") eng.setVoiceVariant (true, 2);
                    else if (id == "voiceUpperFemale4") eng.setVoiceVariant (true, 3);
                    else if (id == "toggleChoirEnsemble") { choirEns = ! choirEns; eng.setChoirToEnsemble (choirEns); }
                    else if (id == "toggleVocoderEnsemble") { vocEns = ! vocEns; eng.setVocoderToEnsemble (vocEns); }
                    else if (id == "pitchModeAuto") eng.setPitchMode (PitchMode::Auto);
                    else if (id == "pitchModeOff") eng.setPitchMode (PitchMode::Off);
                    else if (id == "pitchModeManual") eng.setPitchMode (PitchMode::Manual);
                    else if (id == "toggleMic") { micOn = ! micOn; eng.setMic (micOn, true); }
                    else std::cerr << "click desconocido " << id << "\n";
                }
            }
            for (int i = 0; i < 128; ++i)
            {
                const long idx = (q - micStartQ) * 128 + i;
                micBlock[(size_t) i] = (micStartQ >= 0 && q >= micStartQ && idx < (long) mic.size()) ? mic[(size_t) idx] : 0.0f;
            }
            eng.process (micBlock.data(), outL.data() + q * 128, outR.data() + q * 128, 128);
        }
        std::ofstream out (dir + "/" + name + ".cpp.f32", std::ios::binary);
        out.write ((const char*) outL.data(), (std::streamsize) (outL.size() * 4));
        out.write ((const char*) outR.data(), (std::streamsize) (outR.size() * 4));
    }
    std::cout << "escenarios C++: " << names.size() << "\n";
    return 0;
}
