#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
#include "synth.h"
#include "sfx.h"
#include "Iir.h"
#include <vector>


// constants
const int nChannels = 2;
const int nSampleRate = 44100;

// synth
synth::instrument_single_osc instrument;
std::vector<synth::note> vNotes;
std::mutex muxNotes;
int nNoteOffset = 64;

// filters
bool bLpfEnabled = true;
bool bHpfEnabled = true;
FTYPE dHpfFrequency = 60.0;
FTYPE dLpfFrequency = 1200.0;
FTYPE dHpfQ = 0.5;
FTYPE dLpfQ = 0.5;
Iir::RBJ::HighPass* hpFilters = nullptr;
Iir::RBJ::LowPass* lpFilters = nullptr;

// mono delay
bool bMonoDelayEnabled = false;
sfx::monodelay sfxMonoDelay{nSampleRate, 4.0};
FTYPE dDelayTime = 1.0;
FTYPE dDelayFeedback = 0.6;
float fDelayMix = 0.5f;

// stereo ping pong delay
bool bStereoDelayEnabled = false;
sfx::pingpongdelay sfxPingPong{nSampleRate, 4.0};
sfx::pingpongdelay::stereo_sample ppDelayTime(0.3, 0.5);
sfx::pingpongdelay::stereo_sample ppDelayFb(0.75, 0.75);
float fPpDelayMix = 0.5f;

// visualizer
bool bVisEnabled = true;
int nVisMemorySize = 0;
FTYPE** dVisMemory = nullptr;
int nVisPhase = 0;


FTYPE ProcessChannel(int nChannel, FTYPE dTime)
{
    unique_lock<mutex> lm(muxNotes);
    FTYPE dMixedOutput = 0.0;
    for (auto &n : vNotes)
    {
        bool bNoteFinished = false;
        FTYPE dSound = 0.0;
        if (n.channel != nullptr)
            dSound = instrument.sound(dTime, n, bNoteFinished);
        dMixedOutput += dSound;
        if (bNoteFinished)
            n.active = false;
    }
    safe_remove<std::vector<synth::note>>(vNotes, [](synth::note const& item) { return item.active; });
    return dMixedOutput * 0.2;
}

void ProcessAllChannels(std::vector<FTYPE>& samples, FTYPE dTime)
{
    // perform mono processing per channel
    for (int c = 0; c < samples.size(); c++)
        samples[c] = ProcessChannel(c, dTime);
    
    // mono delay
    if (bMonoDelayEnabled)
    {
        FTYPE dSummedOutput = 0.0;
        for (int c = 0; c < samples.size(); c++)
            dSummedOutput += samples[c];
        dSummedOutput = dSummedOutput / (FTYPE)samples.size();
        sfxMonoDelay.process(dSummedOutput, dDelayTime, dDelayFeedback, fDelayMix);
        for (int c = 0; c < samples.size(); c++)
            samples[c] = dSummedOutput;
    }

    // perform stereo processing (ping pong delay for now)
    if (bStereoDelayEnabled)
        sfxPingPong.process(samples, ppDelayTime, ppDelayFb, fPpDelayMix);
    
    // filters
    if (bHpfEnabled || bLpfEnabled)
    {
        for (int c = 0; c < samples.size(); c++)
        {
            if (bHpfEnabled)
                samples[c] = hpFilters[c].filter(samples[c]);
            if (bLpfEnabled)
                samples[c] = lpFilters[c].filter(samples[c]);
        }
    }

    // store samples in visualizer memory
    if (bVisEnabled && dVisMemory != nullptr)
    {
        nVisPhase %= nVisMemorySize;
        for (int c = 0; c < samples.size(); c++)
            if (dVisMemory[c] != nullptr)
                dVisMemory[c][nVisPhase] = samples[c];
        nVisPhase++;
    }
}


class olcSynth : public olc::PixelGameEngine
{
private:
    FTYPE dWallTime = 0.0;
    std::vector<olc::Key> vKeys = { olc::Z, olc::S, olc::X, olc::C, olc::F, olc::V, olc::G, olc::B, olc::H, olc::N, olc::M, olc::K, olc::COMMA, olc::L, olc::PERIOD };

public:
    olcNoiseMaker<short> *pSound = nullptr;

    olcSynth()
    {
        sAppName = "Synth! Now with visualizer :)";
    }

    bool OnUserCreate() override
    {
        // setup visualizer
        nVisMemorySize = ScreenWidth();
        dVisMemory = new FTYPE*[nChannels];
        for (int i = 0; i < nChannels; i++)
        {
            dVisMemory[i] = new FTYPE[nVisMemorySize];
            memset(&(dVisMemory[i])[0], 0.0, nVisMemorySize * sizeof(FTYPE));
        }
        bVisEnabled = true;
        
        return true;
    }

    bool OnUserDestroy() override
    {
        // cleanup visualizer
        bVisEnabled = false;
        for (int i = 0; i < nChannels; i++)
            delete[] dVisMemory[i];
        delete[] dVisMemory;

        return true;
    }

    void DrawVisualizer(FTYPE* mem, int yOffset, int yScale, const olc::Pixel& p = olc::YELLOW)
    {
        olc::vi2d vPrevPixel;
        for (int x = 0; x < ScreenWidth() - 1; x++)
        {
            int i = (nVisPhase + x + 1) % nVisMemorySize;
            int y = mem[i] * yScale + yOffset;
            if (x != 0)
                DrawLine(vPrevPixel, { x, y }, p);
            vPrevPixel = { x, y };
        }
    }

    bool OnUserUpdate(float fElapsedTime) override
    {
        if (!UpdateSound(fElapsedTime)) return false;
        if (!UpdateSFX(fElapsedTime)) return false;

        Clear(0);

        // visualizer
        for (int c = 0; c < nChannels; c++)
        {
            int yScale = ScreenHeight() / nChannels - 50;
            int yOffset = (c + 1) * yScale - yScale / 2 + 100;
            DrawVisualizer(dVisMemory[c], yOffset, yScale);
        }

        // ui
        FTYPE dTimeNow = pSound->GetTime();
        
        std::string sNotes = "Notes: " + to_string(vNotes.size()) + " Wall Time: " + to_string(dWallTime) + " CPU Time: " + to_string(dTimeNow) + " Latency: " + to_string(dWallTime - dTimeNow) ;
        
        std::string sSin = "1) Sine";
        std::string sSaw = "2) Sawtooth";
        std::string sSqr = "3) Square";
        std::string sTri = "4) Triangle";

        std::string sMonoDelayStatus    = "Q) Mono Delay:   " + std::string(bMonoDelayEnabled ? "ON" : "OFF");
        std::string sStereoDelayStatus  = "W) Stereo Delay: " + std::string(bStereoDelayEnabled ? "ON" : "OFF");
        std::string sHPFStatus          = "O) HPF: " + std::string(bHpfEnabled ? "ON" : "OFF");
        std::string sLPFStatus          = "P) LPF: " + std::string(bLpfEnabled ? "ON" : "OFF");
        std::string sVolume             = "Volume: " + to_string(instrument.dVolume);

        DrawString({ 10, 10 }, sNotes);

        using wf = wavegen::WaveFunction;
        DrawString({ 10, 30 }, sSin, instrument.function == wf::SINE ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 100 - (int)sSaw.length(), 30 }, sSaw, instrument.function == wf::SAWTOOTH ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 200 - (int)sSqr.length(), 30 }, sSqr, instrument.function == wf::SQUARE ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 300 - (int)sTri.length(), 30 }, sTri, instrument.function == wf::TRIANGLE ? olc::WHITE : olc::GREY);
        
        DrawString({ 10, 50 }, sMonoDelayStatus, bMonoDelayEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 200, 50 }, sStereoDelayStatus, bStereoDelayEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10, 70 }, sHPFStatus, bHpfEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 200, 70 }, sLPFStatus, bLpfEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10, 90 }, sVolume);

        if (instrument.function != wf::SINE)
            DrawString({ 10, 110 }, "Harmonics: " + std::to_string(instrument.nHarmonics));

        return !(GetKey(olc::ESCAPE).bPressed);
    }


    bool UpdateSFX(float fElapsedTime)
    {
        if (GetKey(olc::Q).bPressed)
            bMonoDelayEnabled = !bMonoDelayEnabled;
        if (GetKey(olc::W).bPressed)
            bStereoDelayEnabled = !bStereoDelayEnabled;
        if (GetKey(olc::O).bPressed)
            bHpfEnabled = !bHpfEnabled;
        if (GetKey(olc::P).bPressed)
            bLpfEnabled = !bLpfEnabled;
        if (GetKey(olc::UP).bHeld)
        {
            instrument.dVolume += instrument.dVolume * 0.5 * fElapsedTime;
            if (instrument.dVolume >= 1.0)
                instrument.dVolume = 1.0;
        }
        if (GetKey(olc::DOWN).bHeld)
        {
            instrument.dVolume -= instrument.dVolume * 0.5 * fElapsedTime;
            if (instrument.dVolume < 0.1)
                instrument.dVolume = 0.1;
        }
        return true;
    }


    bool UpdateSound(float fElapsedTime)
    {
        if (pSound == nullptr) return true;

        dWallTime += fElapsedTime;
        FTYPE dTimeNow = pSound->GetTime();

        if (GetKey(olc::K1).bPressed)
            instrument.function = wavegen::WaveFunction::SINE;
        if (GetKey(olc::K2).bPressed) 
            instrument.function = wavegen::WaveFunction::SAWTOOTH;
        if (GetKey(olc::K3).bPressed) 
            instrument.function = wavegen::WaveFunction::SQUARE;
        if (GetKey(olc::K4).bPressed) 
            instrument.function = wavegen::WaveFunction::TRIANGLE;
        if (GetKey(olc::NP_ADD).bPressed)
            instrument.nHarmonics++;
        if (GetKey(olc::NP_SUB).bPressed)
            instrument.nHarmonics--;

        if (instrument.nHarmonics < 1)
            instrument.nHarmonics = 1;

        // check key states to add/remove notes
        for (int k = 0; k < vKeys.size(); k++)
        {
            // Check if note already exists in currently playing notes
            muxNotes.lock();
            auto noteFound = find_if(vNotes.begin(), vNotes.end(), [&k](synth::note const& item) { return item.id == k + nNoteOffset; });
            if (noteFound == vNotes.end())
            {
                // note not found in vector
                if (GetKey(vKeys[k]).bPressed)
                {
                    // key is pressed, make a new note
                    synth::note n;
                    n.id = k + nNoteOffset;
                    n.on = dTimeNow;
                    n.active = true;
                    n.channel = &instrument;
                    n.velocity = (FTYPE)rand() / (FTYPE)RAND_MAX * 0.6 + 0.4;   // random velocity for now
                    // Add note to vector
                    vNotes.emplace_back(n);
                }
            }
            else
            {
                // note does exist in vector
                if (GetKey(vKeys[k]).bHeld)
                {
                    // key still held, do nothing
                    if (noteFound->off > noteFound->on)
                    {
                        // key pressed again during release phase
                        noteFound->on = dTimeNow - 0.1;
                        noteFound->active = true;
                    }
                }
                else if (GetKey(vKeys[k]).bReleased)
                {
                    // key has been released, so switch off..
                    if (noteFound->off < noteFound->on)
                        noteFound->off = dTimeNow;
                }
            }
            muxNotes.unlock();
        }
        return true;
    }
};

int main()
{
    // setup noise maker
    vector<string> devices = olcNoiseMaker<short>::Enumerate();
    olcNoiseMaker<short> sound(devices[0], nSampleRate, nChannels, 8, 2048);
    sound.SetUserFunctionAllChans(ProcessAllChannels);

    // setup filters
    hpFilters = new Iir::RBJ::HighPass[nChannels];
    lpFilters = new Iir::RBJ::LowPass[nChannels];
    for (int c = 0; c < nChannels; c++)
    {
        lpFilters[c].setup((FTYPE)nSampleRate, dLpfFrequency, dLpfQ);
        hpFilters[c].setup((FTYPE)nSampleRate, dHpfFrequency, dHpfQ);
    }

    // setup olc pge app
    olcSynth app;
    app.pSound = &sound;
    app.Construct(1280, 720, 1, 1);
    app.Start();

    // delete filters
    delete[] hpFilters;
    delete[] lpFilters;

    return 0;
}