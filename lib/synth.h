#pragma once
#ifndef SYNTH_H
#define SYNTH_H

#ifndef FTYPE
#define FTYPE double
#endif /* ifndef FTYPE */

#include "olcNoiseMaker.h"
#include "wavegen.h"

namespace synth
{

    /**
     * Convert frequency (Hz) to angular velocity
     */
    FTYPE w(const FTYPE& frequency)
    {
        return frequency * 2.0 * PI;
    }

    struct instrument_base;

    struct note
    {
        int id;
        int offset;
        FTYPE on;
        FTYPE off;
        FTYPE velocity;
        bool active;
        instrument_base* channel;

        note()
        {
            id = 0;
            offset = 0;
            on = 0.0;
            off = 0.0;
            velocity = 0.7;
            active = false;
            channel = nullptr;
        }

        bool operator==(const note& other) { return id == other.id; };
    };

    FTYPE scale(const int& nNoteID)
    {
        return 8 * pow(1.0594630943592952645618252949463, nNoteID);
    }

    struct envelope
    {
        virtual FTYPE amplitude(const FTYPE& dTime, const FTYPE& dTimeOn, const FTYPE& dTimeOff, const FTYPE& dVelocity) = 0;
    };

    struct envelope_adsr : envelope
    {
        FTYPE dAttackTime;
        FTYPE dDecayTime;
        FTYPE dSustainAmplitude;
        FTYPE dReleaseTime;
        FTYPE dStartAmplitude;
        FTYPE dPreviousAmplitude;

        envelope_adsr()
        {
            dAttackTime = 0.1;
            dDecayTime = 0.1;
            dSustainAmplitude = 1.0;
            dReleaseTime = 0.2;
            dStartAmplitude = 1.0;
            dPreviousAmplitude = 0.0;
        }

        FTYPE amplitude(const FTYPE& dTime, const FTYPE& dTimeOn, const FTYPE& dTimeOff, const FTYPE& dVelocity) override
        {
            FTYPE dAmplitude = 0.0;
            FTYPE dReleaseAmplitude = 0.0;

            if (dTimeOn > dTimeOff)
            {
                // note is ON
                FTYPE dLifeTime = dTime - dTimeOn;
                if (dLifeTime <= dAttackTime)
                {
                    dAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude + 0.01;
                    if (dPreviousAmplitude != 0.0)
                        dAmplitude = (dAmplitude + dPreviousAmplitude) * 0.5;
                        // dAmplitude = std::max(dAmplitude, dPreviousAmplitude);
                }
                if (dLifeTime > dAttackTime && dLifeTime <= (dAttackTime + dDecayTime))
                    dAmplitude = ((dLifeTime - dAttackTime) / dDecayTime) * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;
                if (dLifeTime > (dAttackTime + dDecayTime))
                    dAmplitude = dSustainAmplitude;
            }
            else
            {
                // note is OFF
                FTYPE dLifeTime = dTimeOff - dTimeOn;
                if (dLifeTime <= dAttackTime)
                    dReleaseAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;
                if (dLifeTime > dAttackTime && dLifeTime <= (dAttackTime + dDecayTime))
                    dReleaseAmplitude = ((dLifeTime - dAttackTime) / dDecayTime) * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;
                if (dLifeTime > (dAttackTime + dDecayTime))
                    dReleaseAmplitude = dSustainAmplitude;
                dAmplitude = ((dTime - dTimeOff) / dReleaseTime) * (0.0 - dReleaseAmplitude) + dReleaseAmplitude;
            }

            // multiply by velocity
            // dAmplitude *= dVelocity;

            // amplitude should not be negative
            if (dAmplitude <= 0.001)
                dAmplitude = 0.0;
            
            // don't forget to update the previous amplitude
            dPreviousAmplitude = dAmplitude;
            
            return dAmplitude;
        }
    };

    FTYPE env(const FTYPE& dTime, envelope& env, const FTYPE& dTimeOn, const FTYPE& dTimeOff, const FTYPE& dVelocity)
    {
        return env.amplitude(dTime, dTimeOn, dTimeOff, dVelocity);
    }

    struct instrument_base
    {
        std::string name;
        FTYPE dVolume;
        synth::envelope_adsr env;
        FTYPE dMaxLifeTime;
        wavegen::WaveFunction function;
        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool& bNoteFinished) = 0;
    };

    struct instrument_single_osc : instrument_base
    {
        int nHarmonics = 8;

        instrument_single_osc()
        {
            name = "single oscillator";
            dVolume = 1.0;
            env.dAttackTime = 0.15;
            env.dDecayTime = 0.4;
            env.dSustainAmplitude = 0.9;
            env.dReleaseTime = 0.3;
            function = wavegen::WaveFunction::SINE;
        }

        FTYPE sound(const FTYPE dTime, synth::note n, bool& bNoteFinished) override
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off, n.velocity);
            if (dAmplitude <= 0.0)
                bNoteFinished = true;
            FTYPE dSound = wavegen::Generate(function, synth::scale(n.id), dTime, dVolume, nHarmonics);
            return dSound * dAmplitude * dVolume;
        }
    };

}

typedef bool(*lambda)(synth::note const& item);
template<class T>
void safe_remove(T &v, lambda f)
{
    auto n = v.begin();
    while (n != v.end())
        if (!f(*n))
            n = v.erase(n);
        else
            ++n;
}

#endif /* ifndef SYNTH_H */