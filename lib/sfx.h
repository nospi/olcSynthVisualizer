#pragma once
#ifndef SFX_H
#define SFX_H

#ifndef FTYPE
#define FTYPE double
#endif

#include <memory>
#include <stdexcept>
#include <vector>

namespace sfx
{

    class monodelay
    {
    private:
        FTYPE *memory = nullptr;
        int nSampleRate;
        int nMaxSamples;
        int nPhase = 0;
        
    public:        
        monodelay(int sampleRate, FTYPE maxTime)
        {
            nSampleRate = sampleRate;
            nMaxSamples = (int)(maxTime * nSampleRate);
            memory = new FTYPE[nMaxSamples];
            memset(memory, (FTYPE)0.0, nMaxSamples * sizeof(FTYPE));
        }

        ~monodelay()
        {
            delete[] memory;
        }

        void process(FTYPE& sample, const FTYPE& time, const FTYPE& feedback, const float& fMix)
        {
            if (memory == nullptr) return;
            if (nPhase >= (int)(time * nSampleRate) || nPhase >= nMaxSamples)
                nPhase = 0;
            FTYPE output = memory[nPhase];
            memory[nPhase++] = output * feedback + sample;
            sample = fMix * output + (1.0 - fMix) * sample;
        }
    };


    class pingpongdelay
    {
    private:
        int nSampleRate = 0;
        int nMaxSamples = 0;
        FTYPE* memoryL = nullptr;
        FTYPE* memoryR = nullptr;
        int nPhaseL = 0;
        int nPhaseR = 0;

    public:
        pingpongdelay(int sampleRate, FTYPE maxTime)
        {
            nSampleRate = sampleRate;
            nMaxSamples = (int)(nSampleRate * maxTime);
            memoryL = new FTYPE[nMaxSamples];
            memoryR = new FTYPE[nMaxSamples];
            memset(&memoryL[0], 0.0, nMaxSamples * sizeof(FTYPE));
            memset(&memoryR[0], 0.0, nMaxSamples * sizeof(FTYPE));
        }

        ~pingpongdelay()
        {
            if (memoryL != nullptr) delete[] memoryL;
            if (memoryR != nullptr) delete[] memoryR;
        }

        struct stereo_sample
        {
            FTYPE l;
            FTYPE r;
            stereo_sample(FTYPE l, FTYPE r)
            {
                this->l = l;
                this->r = r;
            }
        };

        void process(int nChans, FTYPE *samples, const stereo_sample& time, const stereo_sample& fb, const float& fMix = 1.0)
        {
            if (nChans < 2) return;

            stereo_sample in(samples[0], samples[1]);

            // boundary check phase counters
            if (nPhaseL >= (int)(time.l * nSampleRate) || nPhaseL >= nMaxSamples)
                nPhaseL = 0;
            if (nPhaseR >= (int)(time.r * nSampleRate) || nPhaseR >= nMaxSamples)
                nPhaseR = 0;

            // perform delay process
            samples[0] = memoryL[nPhaseL];
            samples[1] = memoryR[nPhaseR];
            memoryL[nPhaseL++] = samples[1] * fb.l + in.l;
            memoryR[nPhaseR++] = samples[0] * fb.r + in.r;

            // apply mix
            samples[0] = fMix * samples[0] + (1.0f - fMix) * in.l;
            samples[1] = fMix * samples[1] + (1.0f - fMix) * in.r;
        }
    };

}

#endif /* ifndef SFX_H */