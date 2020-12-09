#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
#include "synth.h"
#include "sfx.h"
#include "Iir.h"
#include <vector>
#include "fft.h"


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
FTYPE dHpfFrequency = 100.0;
FTYPE dLpfFrequency = 1500.0;
FTYPE dHpfQ = 0.3;
FTYPE dLpfQ = 0.7;
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
int nVisMode = 0;
bool bVisEnabled = true;
int nVisMemorySize = 0;
FTYPE** dVisMemory = nullptr;
int nVisPhase = 0;

// visualizer / FFT
int nFFTMemorySize = 1;
int nFFTMemorySizeHalf = 1;
FTYPE** dFFTMemoryPre = nullptr;
FTYPE** dFFTMemoryPost = nullptr;
std::mutex muxVis;
int nFFTPhase = 0;
std::mutex muxFFT;


FTYPE ProcessChannel(int nChannel, FTYPE dTime)
{
    unique_lock<mutex> lm(muxNotes);
    FTYPE dMixedOutput = 0.0;
    for (auto &n : vNotes)
    {
        bool bNoteFinished = false;
        FTYPE dSound = 0.0;
        if (n.channel != nullptr)
            dSound = n.channel->sound(dTime, n, bNoteFinished);
        dMixedOutput += dSound;
        if (bNoteFinished)
        {
            n.active = false;
            n.channel->env.state = synth::adsr_state::inactive;
        }
    }
    safe_remove<std::vector<synth::note>>(vNotes, [](synth::note const& item) { return item.active; });
    return dMixedOutput * 0.2;
}

void ProcessAllChannels(int nChans, FTYPE *samples, FTYPE dTime)
{
    // perform mono processing per channel
    for (int c = 0; c < nChans; c++)
        samples[c] = ProcessChannel(c, dTime);
    
    // mono delay
    if (bMonoDelayEnabled)
    {
        FTYPE dSummedOutput = 0.0;
        for (int c = 0; c < nChans; c++)
            dSummedOutput += samples[c];
        dSummedOutput = dSummedOutput / (FTYPE)nChans;
        sfxMonoDelay.process(dSummedOutput, dDelayTime, dDelayFeedback, fDelayMix);
        for (int c = 0; c < nChans; c++)
            samples[c] = dSummedOutput;
    }

    // perform stereo processing (ping pong delay for now)
    // if (bStereoDelayEnabled)
    sfxPingPong.process(nChans, samples, ppDelayTime, ppDelayFb, bStereoDelayEnabled ? fPpDelayMix : 0.0f);
    
    // filters
    if (bHpfEnabled || bLpfEnabled)
    {
        for (int c = 0; c < nChans; c++)
        {
            if (bHpfEnabled)
                samples[c] = hpFilters[c].filter(samples[c]);
            if (bLpfEnabled)
                samples[c] = lpFilters[c].filter(samples[c]);
        }
    }

    // store samples in visualizer memory
    if (bVisEnabled && nVisMode == 0 && dVisMemory != nullptr)
    {
        unique_lock<mutex> lm(muxVis);
        nVisPhase %= nVisMemorySize;
        for (int c = 0; c < nChans; c++)
        {
            if (dVisMemory[c] != nullptr)
                dVisMemory[c][nVisPhase] = samples[c];
        }
        nVisPhase++;
    }

    // store samples in FFT memory
    if (bVisEnabled && nVisMode == 1 && dFFTMemoryPre != nullptr && dFFTMemoryPost != nullptr)
    {
        unique_lock<mutex> lm(muxFFT);
        nFFTPhase %= nFFTMemorySize;
        for (int c = 0; c < nChans; c++)
        {
            if (dFFTMemoryPre[c] != nullptr)
                dFFTMemoryPre[c][nFFTPhase] = samples[c];
            if (nFFTPhase == 0)
                fft_magnitude(dFFTMemoryPre[c], dFFTMemoryPost[c], nFFTMemorySize);
        }
        nFFTPhase++;
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
        nFFTMemorySize = ScreenWidth() * 2;
        nFFTMemorySizeHalf = nFFTMemorySize / 2;
        dFFTMemoryPre = new FTYPE*[nChannels];
        dFFTMemoryPost = new FTYPE*[nChannels];
        for (int i = 0; i < nChannels; i++)
        {
            dVisMemory[i] = new FTYPE[nVisMemorySize];
            dFFTMemoryPre[i] = new FTYPE[nFFTMemorySize];
            dFFTMemoryPost[i] = new FTYPE[nFFTMemorySizeHalf];
            memset(&(dVisMemory[i])[0], 0.0, nVisMemorySize * sizeof(FTYPE));
            memset(&(dFFTMemoryPre[i])[0], 0.0, nFFTMemorySize * sizeof(FTYPE));
            memset(&(dFFTMemoryPost[i])[0], 0.0, nFFTMemorySizeHalf * sizeof(FTYPE));
        }
        bVisEnabled = true;
        
        return true;
    }

    bool OnUserDestroy() override
    {
        // cleanup visualizer
        bVisEnabled = false;
        for (int i = 0; i < nChannels; i++)
        {
            delete[] dVisMemory[i];
            delete[] dFFTMemoryPre[i];
            delete[] dFFTMemoryPost[i];
        }
        delete[] dVisMemory;
        delete[] dFFTMemoryPre;
        delete[] dFFTMemoryPost;
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

    void DrawFFT(FTYPE* mem, int yOffset, int yScale, const olc::Pixel& p = olc::RED)
    {
        olc::vi2d vPrevPixel;
        for (int x = 0; x < ScreenWidth(); x++)
        {
            int wx = (int)(x * (double)nFFTMemorySizeHalf / (double)ScreenWidth());
            FTYPE lx = -(wx / PI) * log10((nFFTMemorySizeHalf - wx) / (double)nFFTMemorySizeHalf);
            int i = (int)std::min<FTYPE>(nFFTMemorySizeHalf - 1, std::max<FTYPE>(0, floor(lx)));
            
            int y = -mem[i] * 0.005 * yScale + yOffset;
            if (x != 0)
                DrawLine(vPrevPixel, { x, y }, p);
            vPrevPixel = { x, y };
        }
    }

    bool OnUserUpdate(float fElapsedTime) override
    {
        if (!UpdateSound(fElapsedTime)) return false;
        if (!UpdateSFX(fElapsedTime)) return false;

        if (GetKey(olc::TAB).bPressed)
        {
            nVisMode = (nVisMode == 0) ? 1 : 0;
        }

        Clear(0);

        // visualizer
        for (int c = 0; c < nChannels; c++)
        {          
            muxVis.lock();
            muxFFT.lock();

            int yScale = ScreenHeight() / nChannels - 50;
            int yOffset = (c + 1) * yScale - yScale / 2 + 100;
            switch (nVisMode)
            {
            case 0: DrawVisualizer(dVisMemory[c], yOffset, yScale); break;
            case 1: DrawFFT(dFFTMemoryPost[c], yOffset + c * 15 + 60, yScale); break;
            }

            muxVis.unlock();
            muxFFT.unlock();
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
        std::string sOctave             = "Octave: " + std::to_string(nNoteOffset / 12) + " Total Offset: " + std::to_string(nNoteOffset);
        std::string sHarmonics          = "Harmonics: " + std::to_string(instrument.nHarmonics);

        DrawString({ 10, ScreenHeight() - 20 }, sNotes);

        using wf = wavegen::WaveFunction;
        DrawString({ 10, 10 }, sSin, instrument.function == wf::SINE ? olc::WHITE : olc::GREY);
        DrawString({ 110, 10 }, sSaw, instrument.function == wf::SAWTOOTH ? olc::WHITE : olc::GREY);
        DrawString({ 210, 10 }, sSqr, instrument.function == wf::SQUARE ? olc::WHITE : olc::GREY);
        DrawString({ 310, 10 }, sTri, instrument.function == wf::TRIANGLE ? olc::WHITE : olc::GREY);
        
        DrawString({ 10, 30 }, sMonoDelayStatus, bMonoDelayEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 200, 30 }, sStereoDelayStatus, bStereoDelayEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10, 50 }, sHPFStatus, bHpfEnabled ? olc::WHITE : olc::GREY);
        DrawString({ 10 + 200, 50 }, sLPFStatus, bLpfEnabled ? olc::WHITE : olc::GREY);

        DrawString({ (int)(ScreenWidth() - sVolume.length() * 8 - 10), 10 }, sVolume);
        DrawString({ (int)(ScreenWidth() - sOctave.length() * 8 - 10), 30 }, sOctave);

        if (instrument.function != wf::SINE)
            DrawString({ (int)(ScreenWidth() - sHarmonics.length() * 8 - 10), 50 }, sHarmonics);

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
        if (GetKey(olc::NP_DIV).bPressed)
            nNoteOffset -= 12;
        if (GetKey(olc::NP_MUL).bPressed)
            nNoteOffset += 12;
        

        if (nNoteOffset / 12 <= 1)
            nNoteOffset = 16;
        else if (nNoteOffset / 12 >= 10)
            nNoteOffset = nNoteOffset = 124;

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
                    n.offset = nNoteOffset;
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
                        noteFound->on = dTimeNow;
                        noteFound->active = true;
                    }
                }
            }

            // double check notes outside of the current key ids
            for (auto& n : vNotes)
                if (!GetKey(vKeys[n.id - n.offset]).bHeld)
                    if (n.off < n.on)
                        n.off = dTimeNow;

            muxNotes.unlock();
        }
        return true;
    }
};

int main()
{
    // setup noise maker
    vector<string> devices = olcNoiseMaker<short>::Enumerate();
    olcNoiseMaker<short> sound(devices[0], nSampleRate, nChannels, 8, 1024);
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