/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Grain.h"

//==============================================================================
/**
*/
class TryGranulatorAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    TryGranulatorAudioProcessor();
    ~TryGranulatorAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    void loadSample(const juce::String& path);
    void loadSampleFromMemory();

private:
    // Handles audio format registration and decoding (WAV, AIFF, MP3, etc.)
    juce::AudioFormatManager formatManager;
    
    // Pointer to loaded sample used for sample-based granulation
    std::unique_ptr<juce::AudioBuffer<float>> sampleBuffer;
    
    // Global grain array
    juce::Array<Grain> grains;
    
    // JUCE synthesiser object managing GrainVoice and GrainSound
    juce::Synthesiser synth;
    
    // Manages all plugin parameters and their mapping
    juce::AudioProcessorValueTreeState apvts;
    
    // Raw pointers to parametrs
    std::atomic<float>* reverbOnParam;
    std::atomic<float>* reverbMixParam;
    std::atomic<float>* filterCutoffParam;
    std::atomic<float>* filterTypeParam;
    std::atomic<float>* filterResonanceParam;
    
    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        // Vector to hold all plugin parameters
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        
        // Granular mode
        params.push_back (std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("Mode", 1), "Granular Mode", juce::StringArray ("Delay", "Sample"), 0));
        
        // Envelope type for amplitude shaping
        params.push_back (std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("Envelope", 1), "Grain Envelope", juce::StringArray ("Triangle", "Hann", "Exponential", "Trapezoid"), 0));
        
        // Grain duration in milliseconds
        params.push_back (std::make_unique<juce::AudioParameterInt>(juce::ParameterID("Length", 1), "Grain Length", 5, 2000, 500));
        
        // Randomisation to grain length
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("jitter", 1), "Grain Length Jitter", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
        
        // Time between grain spawns (lower = more grains)
        params.push_back (std::make_unique<juce::AudioParameterInt>(juce::ParameterID("Density", 1), "Density", 2, 500, 50));
        
        // Maximum number of active overlapping grain streams
        params.push_back (std::make_unique<juce::AudioParameterInt>(juce::ParameterID("Activity", 1), "Activity", 1, 10, 3));
        
        // Amplitude of each grain
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID ("Level", 1), "Grain Level", 0.0f, 1.0f, 0.5f));
        
        // Chance of spawning a grain (1 = full chance)
        params.push_back (std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LevelRand", 1), "Probability", 0.0f, 1.0f, 1.0f));
        
        // Tap offset into the sample or delay buffer
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Position", 1), "Position/Tap", 0.0f, 1.0f, 0.5f));
        
        // Introduce randomness in position
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Sparse", 1), "Sparse", 0.0f, 1.0f, 0.0f )); // 0 = no spread, 1 = full random spread
        
        // Direction of grain playback
        params.push_back (std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("Playback", 1), "Playback", juce::StringArray ("Forward", "Backward", "Random"), 0));
        
        // Dry/wet mix control
        params.push_back (std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Mix", 1), "Mix", 0.0f, 1.0f, 1.0f));
        
        // stereo spread for grains
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("stereoWidth", 1), "Stereo Width", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),0.0f));
        
        // enables bpm based grain spawning - Quantisation
        params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("Quantise", 1), "Quantise", false));
        
        // Grain sync rate - subdivision
        params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("QuantiseDivision", 1),"Quantise Division", juce::StringArray("1/4", "1/8", "1/16"),1));
        
        // Filter type on overall buffer
        params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("FilterType", 1), "Filter Type", juce::StringArray("Lowpass", "Bandpass", "Highpass"), 0));
        
        // Filter cut off in Hz
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("FilterCutoff", 1), "Filter Cutoff", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 3000.0f));
        
        // Filter resonance
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("FilterResonance", 1), "Filter Resonance", juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.4f), 1.0f));
        
        // Reverb Toggle
        params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("ReverbOn", 1), "Reverb On", false));

        // Dry/wet mix for reverb
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ReverbMix", 1), "Reverb Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
        
        // Internal delay line feedback
        params.push_back(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID("Feedback", 1), "Feedback Amt", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
        
        // how much of the grain output is fed back into the delay line
        params.push_back(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID("GrainFeedback", 1), "Grain Feedback", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

        return {params.begin(), params.end()};
    }

    // Reverb Processor
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    
    // Smoothed variables
    juce::SmoothedValue<float> smoothedReverbMix; // for reverb blend
    
    // Filtering
    juce::IIRFilter filterL;
    juce::IIRFilter filterR;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TryGranulatorAudioProcessor)
};
