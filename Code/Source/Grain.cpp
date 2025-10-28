/*
  ==============================================================================

    Grain.cpp
    Created: 24 Mar 2025 3:05:34pm
    Author:  Shreya Gupta

  ==============================================================================
*/

#include "Grain.h"


Grain::Grain(int onset_, int length_, float rate_, float level_, float position_, int delayOffset_, float sr_, float stereoWidth) : onset(onset_), length(length_), rate(rate_), level(level_), position(position_), sr(sr_), delayOffset(delayOffset_)
{
    // Smooth value initialization
    smoothLevel.reset(sr, 0.1); // fade over 0.3 seconds — or use sampleRate //==========================================================================
    smoothLevel.setCurrentAndTargetValue(level_);
    
    smoothRate.reset(sr, 0.1); // fade over 0.3 seconds — or use sampleRate //==========================================================================
    smoothRate.setCurrentAndTargetValue(rate_);
    
    // Random pan value assigned once per grain
    pan = (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * stereoWidth;
}

/**
 Renders the grain from source sample into the output buffer, shaped by an envelope
 
 @param output juce::AudioBuffer<float>&
 @param source juce::AudioBuffer<float>&
 @param time int
 @param sampleIndex int
 @param envelope int
 @param activity int
 */
void Grain::sampleProcess(juce::AudioBuffer<float>& output, const juce::AudioBuffer<float>& source, int time, int sampleIndex, int envelope, int activity){
    int t = time - onset;
    if (t < 0 || t >= length) return; // If grain hasn't started or is finished, skip
    
    // Calculate playback position in the source buffer
    float rateSmoothed = smoothRate.getNextValue();
    int scrSample = juce::jlimit(0, source.getNumSamples() - 1, int (position * source.getNumSamples()) + int(t*rateSmoothed));
    
    // Loop through channels (usually 2: left and right)
    for (int ch = 0; ch < output.getNumChannels(); ++ch)
    {
        // Fetch sample from source buffer
        float sample = source.getSample(ch % source.getNumChannels(), scrSample);
        
        // Apply selected envelope shape
        float env = triEnvelope(t);
        if (envelope == 0)
        {
            env = triEnvelope(t);
        }
        else if (envelope == 1)
        {
            env = hannEnvelope(t);
        }
        else if (envelope == 2)
        {
            env = expEnvelope(t);
        }
        else
        {
            env = trapezoidEnvelope(t);
        }
        
        // Apply gain shaping
        float levelSmoothed = smoothLevel.getNextValue();
        float grainGain = levelSmoothed / juce::jmax(1, activity);
        
        // Pan for stereo spread
        float leftGain = std::sqrt(0.5f * (1.0f - pan));
        float rightGain = std::sqrt(0.5f * (1.0f + pan));

        // Apply to output buffer
        if (output.getNumChannels() >= 2)
        {
            output.addSample(0, sampleIndex, sample * env * grainGain * leftGain);
            output.addSample(1, sampleIndex, sample * env * grainGain * rightGain);
        }
        else
        {
            output.addSample(0, sampleIndex, sample * env * grainGain);
        }
    }
}

/**
 Renders the grain from delay buffer into the output buffer, shaped by an envelope
 
 @param output juce::AudioBuffer<float>&
 @param source DelayLine&
 @param time int
 @param sampleIndex int
 @param envelope int
 @param activity int
 */
void Grain::delayProcess(juce::AudioBuffer<float>& output, DelayLine& source, int time, int sampleIndex, int envelope, int activity){
    int t = time - onset;
    if (t < 0 || t >= length) return;

    float readPos = delayOffset + t * rate;

    // Wrap around
    while (readPos < 0) readPos += source.getDelaySize();
    while (readPos >= source.getDelaySize()) readPos -= source.getDelaySize();

    // Interpolate manually — don't move global read head
    int lower = floor(readPos);
    int upper = (lower + 1) % source.getDelaySize();
    float frac = readPos - lower;

    for (int ch = 0; ch < output.getNumChannels(); ++ch)
    {
        float lowerVal = source.getSampleAtIndex(lower);
        float upperVal = source.getSampleAtIndex(upper);
        float sample = (1.0f - frac) * lowerVal + frac * upperVal;

        float env = triEnvelope(t);
        if (envelope == 0)
        {
            env = triEnvelope(t);
        }
        else if (envelope == 1)
        {
            env = hannEnvelope(t);
        }
        else if (envelope == 2)
        {
            env = expEnvelope(t);
        }
        else
        {
            env = trapezoidEnvelope(t);
        }
        float levelSmoothed = smoothLevel.getNextValue();
        float grainGain = levelSmoothed / juce::jmax(1, activity);

        float leftGain = std::sqrt(0.5f * (1.0f - pan));
        float rightGain = std::sqrt(0.5f * (1.0f + pan));

        if (output.getNumChannels() >= 2)
        {
            output.addSample(0, sampleIndex, sample * env * grainGain * leftGain);
            output.addSample(1, sampleIndex, sample * env * grainGain * rightGain);
        }
        else
        {
            output.addSample(0, sampleIndex, sample * env * grainGain);
        }
    }
}

/**
 checks when the grain is done, i.e. when the current time is greater than onset and length
 @param time int
 */
bool Grain::isDone(int time) const {
    return time > onset + length;
}

//==================================================== Envelope Shape Functions =========================================================
    
/**
 Triangle envelope
 
 @param t
 */
float Grain::triEnvelope(int t)const {
    if (t < 0 || t >= length) return 0.0f;
    if (t < length /2 ) return 2.0f * t / float(length);
    return 2.0f * (1.0f - float(t) / length);
}

/**
 Hann envelope
 
 @param t
 */
float Grain::hannEnvelope(int t) const {
    if (t < 0 || t >= length) return 0.0f;
    float phase = float(t) / length;
    return 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * phase));
}

/**
 Exponential envelope
 
 @param t
 */
float Grain::expEnvelope(int t) const {
    if (t < 0 || t >= length) return 0.0f;
    float phase = float(t) / length;

    if (phase < 0.5f)
        return std::pow(phase * 2.0f, 3.0f); // fade-in
    else
        return std::pow((1.0f - phase) * 2.0f, 3.0f); // fade-out
}

/**
 Trapezoid envelope
 
 @param t
 */
float Grain::trapezoidEnvelope(int t) const {
    if (t < 0 || t >= length) return 0.0f;

    float phase = float(t) / length;

    // Define attack and release portion (20% each)
    float attackPortion = 0.2f;
    float releasePortion = 0.2f;

    if (phase < attackPortion) {
        // Fade in: ramp from 0 to 1
        return phase / attackPortion;
    } else if (phase > 1.0f - releasePortion) {
        // Fade out: ramp from 1 to 0
        return (1.0f - phase) / releasePortion;
    } else {
        // Sustain: flat 1.0
        return 1.0f;
    }
}

// ================================================================ getter functions ============================================================

/**
 getter function for onset parameter
 */
int Grain::getOnset()
{
    return onset;
}

/**
 getter functino for grain length
 */
int Grain::getLength()
{
    return length;
}

/**
 getter function for grainRate
 */
float Grain::getRate()
{
    return rate;
}

/**
 getter function for delay offset
 */
float Grain::getDelayOffset()
{
    return delayOffset;
}

/**
 getter function for smoothed level
 */
float Grain::getSmoothedLevel()
{
    return smoothLevel.getNextValue();
}










