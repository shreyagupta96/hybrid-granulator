/*
  ==============================================================================

    Grain.h
    Created: 24 Mar 2025 3:05:28pm
    Author:  Shreya Gupta

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "DelayLine.h"

class Grain{
public:
    // constructor
    // onset: when the grain starts (in samples relative to process time)
    // length: duration of the grain in samples
    // rate: playback rate (1.0 = normal, >1 = faster, <1 = slower)
    // level: amplitude multiplier (0.0 to 1.0)
    // position: start point in the source buffer (0.0 to 1.0 as a fraction)
    Grain(): onset(0), length(0), rate(1.0f), level(1.0f), position(0.0f), sr(48000.0f), delayOffset(0.0f){}

    Grain(int onset, int length, float rate, float level, float position, int delayOffset, float sr, float stereoWidth);
    
    void sampleProcess(juce::AudioBuffer<float>& output, const juce::AudioBuffer<float>& source, int time, int sampleIndex, int envelope, int activity);
    
    void delayProcess(juce::AudioBuffer<float>& output, DelayLine& source, int time, int sampleIndex, int envelope, int activity);
    
    bool isDone (int time) const;
    
    float getSample(const juce::AudioBuffer<float>& buffer, int channel, int currentIndex);
    
    float triEnvelope(int t) const;
    
    float hannEnvelope(int t) const;
    
    float expEnvelope(int t) const;

    
    float trapezoidEnvelope(int t) const;
    
    int getOnset();
    int getLength();
    float getRate();
    float getDelayOffset();
    float getSmoothedLevel();
    
private:
    int onset;
    int length;
    float rate;
    float level;
    float position;
    float sr;
    int delayOffset;
    float pan = 0.0f;
    
    juce::SmoothedValue<float> smoothLevel;
    juce::SmoothedValue<float> smoothRate;

};
