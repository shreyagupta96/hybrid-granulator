
#include <JuceHeader.h>
#include "DelayLine.h"
#include "Grain.h"

// =================================================== Grain Sound =================================================================================

/**
 Describes one of the sounds that a Grain Synth can play.
 */
class GrainSound : public juce::SynthesiserSound
{
public:
    /**
     Returns true if the sound is played when a given midi note is pressed
     */
    bool appliesToNote (int midiNoteNumber) override
    {
        return true;
    }
    
    /**
     Returns true if the sound triggered by midi events on a given channel.
     */
    bool appliesToChannel (int midiChannel) override
    {
        return true;
    }
};

// ==================================================== Grain Voice =================================================================================

/**
 Represents a voice that a Synthesiser can use to play.
 
 GrainVoice class for MIDI-triggered grain playback
 */
class GrainVoice : public juce::SynthesiserVoice
{
public:
    GrainVoice() {
        
        maxDelaySize = getSampleRate() * 3;
        delayLine.setMaxSize (maxDelaySize);
        
        smoothSparse.reset(getSampleRate(), 0.1);
        smoothSparse.setCurrentAndTargetValue(0.0f);
        
        smoothedMix.reset(getSampleRate(), 0.1);
        smoothedMix.setCurrentAndTargetValue(0.0f);
        
        smoothedFeedback.reset(getSampleRate(), 0.1);
        smoothedFeedback.setCurrentAndTargetValue(0.0f);
    }
    
    /**
     function used to connect the parameters from the plugin processor to synth class.
     
     @param apvts juce::AudioProcessorTrueValueTreeState
     */
    void connectParam(juce::AudioProcessorValueTreeState& apvts)
    {
        levelParam = apvts.getRawParameterValue("Level");
        positionParam = apvts.getRawParameterValue ("Position");
        spreadParam = apvts.getRawParameterValue("stereoWidth");
        activityParam = apvts.getRawParameterValue ("Activity");
        envelopeParam = apvts.getRawParameterValue ("Envelope");
        sparseParam = apvts.getRawParameterValue ("Sparse");
        lengthParam = apvts.getRawParameterValue ("Length");
        jitterParam = apvts.getRawParameterValue("jitter");
        probParam = apvts.getRawParameterValue("LevelRand");
        modeParam = apvts.getRawParameterValue ("Mode");
        densityParam = apvts.getRawParameterValue ("Density");
        mixParam = apvts.getRawParameterValue ("Mix");
        playbackParam = apvts.getRawParameterValue ("Playback");
        quantiseParam = apvts.getRawParameterValue("Quantise");
        quantiseDivisionParam = apvts.getRawParameterValue("QuantiseDivision");
        grainFeedbackParam = apvts.getRawParameterValue("GrainFeedback");
        feedbackParam = apvts.getRawParameterValue("Feedback");
    }
    
    /**
     Sets the current BPM (used for quantisation and timing)
     
     @param bpm double
     */
    void setCurrentBpm(double bpm)
    {
        currentBpm = bpm;
    }
    
    /**
     Returns true if this voice can play the given sound
     
     @param sound juce::SynthesiserSound*
     */
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<GrainSound*> (sound) != nullptr; // from Chatgpt
    }

    /**
     Triggered when a note starts; initialises grain parameters
     
     @param midiNoteNumber int
     @param velocity float
     @param sound juce::SynthesiserSound*
     @param currentPitchWheelPosition int
     */
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override
    {
        // basic grain setup
        grains.clear();
        
        dryReadHeads.clear();
        dryReadHeads.resize (sampleBuffer->getNumChannels(), 0.0f);


        noteOn = true;
        
        currentSampleIndex = 0;
        density = 500; //set just for now
        playbackRate = std::pow (2.0f, (midiNoteNumber - 60) / 12.0f);
        
        envelope.setSampleRate(getSampleRate());
        
        juce::ADSR::Parameters envelopeParams;
        envelopeParams.attack = 1.0f;
        envelopeParams.sustain = 1.0f;
        envelopeParams.decay = 1.0f;
        envelopeParams.release = 1.0f;
        
        envelope.setParameters (envelopeParams);
        
        envelope.noteOn();
        
        activeVoiceOn += 1;
    }

    /**
     Called when the note ends; handles envelope release and state reset
     
     @param velocity float
     @param allowTailOff bool
     */
    void stopNote (float velocity, bool allowTailOff) override
    {
        envelope.noteOff();
        activeVoiceOn -= 1;
    }
    
    /**
     Main audio processing loop for the voice
     
     @param outputBuffer juce::AudioBuffer<float>&
     @param startSample int
     @param numSamples int
     */
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        // check if the note is active
        if (!noteOn || sampleBuffer == nullptr)
            return;
        
        // prepare dry buffer for blending into the mix
        juce::AudioBuffer<float> dryBuffer;
        dryBuffer.setSize (outputBuffer.getNumChannels(), outputBuffer.getNumSamples());
        dryBuffer.clear();
        
        if (sampleBuffer && sampleBuffer->getNumSamples() > 0)
        {
            int numChannels = outputBuffer.getNumChannels();
            int numSamples = outputBuffer.getNumSamples();
            int numSourceSamples = sampleBuffer->getNumSamples();

            // mixing dry signal according to the pitch
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& readHead = dryReadHeads[ch % dryReadHeads.size()];

                for (int i = 0; i < numSamples; ++i)
                {
                    int lowerIndex = static_cast<int>(readHead);
                    int upperIndex = juce::jmin(lowerIndex + 1, numSourceSamples - 1);  // prevent wrap discontinuity
                    float frac = readHead - float(lowerIndex);

                    float sampleLower = sampleBuffer->getSample(ch % sampleBuffer->getNumChannels(), lowerIndex);
                    float sampleUpper = sampleBuffer->getSample(ch % sampleBuffer->getNumChannels(), upperIndex);

                    float interpolatedSample = sampleLower * (1.0f - frac) + sampleUpper * frac;
                    dryBuffer.setSample(ch, i, interpolatedSample);

                    readHead += playbackRate;
                    if (readHead >= numSourceSamples - 1)
                        readHead -= (numSourceSamples - 1); // wrap just before last sample to avoid read beyond buffer
                }
            }
        }
        
        //==================================== Quantise =======================================================

        if (*quantiseParam > 0.5f) // quantise ON
        {
            double msPerQuarter = (60.0 / currentBpm) * 1000.0;

            // --- Choose a rhythmic subdivision ---
            int division = 1; // 1 = quarter, 2 = 8th, 4 = 16th, 6 = 16th triplet, etc.

            switch (static_cast<int>(*quantiseDivisionParam))
            {
                case 0: division = 1; break;  // quarter
                case 1: division = 2; break;  // eighth
                case 2: division = 4; break;  // sixteenth
                default: division = 2; break;
            }

            double msBetweenGrains = msPerQuarter / division;
            density = msToSamples(msBetweenGrains);
        }
        else // quantise OFF
        {
            density = msToSamples(*densityParam); // freeform mode
        }

        // ================================================

        for (int i = startSample; i < startSample + numSamples; ++i)
        {
            // determine envelope value
            float enVal = envelope.getNextSample();
            int mode = static_cast<int>(*modeParam);
            smoothSparse.setTargetValue (*sparseParam);
            float sparse = smoothSparse.getNextValue();

            // Spawn grain every N samples (e.g. based on a density param or interval)
            if (currentSampleIndex % density == 0)
            {
                float level = *levelParam * enVal;

                // set the rate and playback method
                float rate = playbackRate;
                grainPosition = *positionParam; //0.2f + (static_cast<float>(rand()) / RAND_MAX) * 0.6f;
                
                int playbackMode = static_cast<int>(*playbackParam);
                float grainRate = rate;
                
                if (playbackMode == 0)
                {
                    grainRate = rate;
                }
                else if (playbackMode == 1)
                {
                    grainRate = -(rate);
                }
                else
                {
                    bool flip =( rand() % 2 == 0);
                    if (flip)
                    {
                        grainRate = rate;
                    }
                    else
                    {
                        grainRate = -(rate);
                    }
                }
                
                // deviation from the position
                float deviation = (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f); // -1 to +1
                float spreadAmount = sparse * 0.5f;  // max spread = ±0.5

                float position = juce::jlimit(0.0f, 1.0f, grainPosition + (deviation * spreadAmount));
                float spread = *spreadParam;
                
                // setting the length and the randomness jitter around it
                int baseLength = msToSamples(*lengthParam);
                float jitterAmount = *jitterParam; // 0.0 to 1.0
                float randVal = ((rand() / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f); // -1 to +1
                int jitterSamples = static_cast<int>(baseLength * jitterAmount * randVal);
                int length = std::max(1, baseLength + jitterSamples); // keep length at least 1
                
                // skip a few grains on generation by choosing probability levels
                float levelRandomness = *probParam;
                if (levelRandomness > 0.0f)
                {
                    float randProb = rand()/float(RAND_MAX);
                    
                    if (randProb > levelRandomness)
                    {
                        level = 0.0f;
                    }
                }
                
                // set the onset
                int onset = i + currentSampleIndex;
                
                // choose the mode: Delay process
                if (mode == 0)
                {
                    int delayOffset = (delayLine.getWriteHeadPosition() - int(position * delayLine.getDelaySize()) + delayLine.getDelaySize()) % delayLine.getDelaySize();
                    grains.add (Grain (onset, length, grainRate, level,0, delayOffset, getSampleRate(), spread));
                }
                // choose the mode: Sample process
                else
                {
                    grains.add (Grain (onset, length, grainRate, level, position, 0, getSampleRate(), spread));
                }
                
                // smooth out the release of the ADSR
                if (!envelope.isActive())
                {
                    noteOn = false;
                    clearCurrentNote();
                }
            }
            
            // Setting envelope
            int envelope = static_cast<int>(*envelopeParam);
            // setting number of grains - helps with layering
            int activity = (static_cast<int>(*activityParam))*activeVoiceOn;
            
            //delayline input
            float input = sampleBuffer -> getSample(0, currentSampleIndex % sampleBuffer -> getNumSamples());
            //delayLine.setInputSample(input);
            delayLine.process (input);
            
            float grainSum=0.0f;
            // process grains back into the delay line =======================================================================
            for (int g = grains.size() - 1; g >=0; --g)
            {
                // Delay Granular
                if (mode == 0)
                {
                    grains[g].delayProcess (outputBuffer, delayLine, currentSampleIndex, i, envelope, activity);
                    
                    // Manual re-render for feeding — same calculation as inside delayProcess
                    int t = currentSampleIndex - grains[g].getOnset();
                    if (t >= 0 && t < grains[g].getLength())
                    {
                        float readPos = grains[g].getDelayOffset() + t * grains[g].getRate();
                        while (readPos < 0) readPos += delayLine.getDelaySize();
                        while (readPos >= delayLine.getDelaySize()) readPos -= delayLine.getDelaySize();

                        int lower = floor(readPos);
                        int upper = (lower + 1) % delayLine.getDelaySize();
                        float frac = readPos - lower;

                        float lowerVal = delayLine.getSampleAtIndex(lower);
                        float upperVal = delayLine.getSampleAtIndex(upper);
                        float sample = (1.0f - frac) * lowerVal + frac * upperVal;

                        float env = grains[g].triEnvelope(t); // or select based on type
                        float levelSmoothed = grains[g].getSmoothedLevel();
                        float gain = levelSmoothed / std::max(1, activity);

                        grainSum += sample * env * gain;
                    }
                }
                // Normal Sample
                else
                {
                    grains[g].sampleProcess (outputBuffer, *sampleBuffer, currentSampleIndex, i, envelope, activity);
                }
                
                // the grain gets erased out 
                if (grains[g].isDone(currentSampleIndex)) grains.remove(g);
               
            }
            
            // parameters that control the feedback amount and the grain feedback amount
            float feedbackGain = 0.0f;
            smoothedFeedback.setTargetValue (*feedbackParam);
            float feedbackAmt = smoothedFeedback.getNextValue();
            delayLine.setFeedback(feedbackAmt);
            if (grainFeedbackParam != nullptr)
                feedbackGain = *grainFeedbackParam;
            //delayLine.setInputSample(grainSum * feedbackGain);
            delayLine.process(grainSum * feedbackGain);
            
            // global timer
            currentSampleIndex += 1; // global counter
        }
        
        // mix of dry and granulated output ==========================================================
        smoothedMix.setTargetValue (*mixParam);

        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
        {
            for (int i = startSample; i < startSample + numSamples; ++i)
            {
                float wetMix = smoothedMix.getNextValue();
                float dryMix = 1.0f - wetMix;
                
                float dry = dryBuffer.getSample (ch, i);
                float wet = outputBuffer.getSample (ch, i);

                float mixed = dry * dryMix + wet * wetMix;
                //limiter
                mixed = juce::jlimit(-1.0f, 1.0f, mixed);
                
                outputBuffer.setSample (ch, i, mixed);
            }
        }
        
        
    }
    
    /**
     Sets the pointer to the sample buffer from which grains will be generated
     
     @param buffer juce::AudioBuffer<float>*
     */
    void setSampleBuffer (juce::AudioBuffer<float>* buffer)
    {
        sampleBuffer = buffer;
    }
    
    /**
     MIDI pitch wheel handler
     */
    void pitchWheelMoved (int) override
    {}
    
    /**
     MIDI controller handler
     */
    void controllerMoved (int, int) override
    {}

    /**
     convert milliseconds to samples, given the current sample rate
     
     @param ms float
     */
    int msToSamples(float ms)
    {
        return int((ms / 1000.0f) * getSampleRate());
    }


private:
    // Internal State
    bool noteOn = false;
    float grainPosition = 0.0f;
    float gain = 1.0f;
    int currentSampleIndex = 0;
    int activeVoiceOn;
    float playbackRate = 1.0f;
    int density;
    
    // Audio data
    juce::AudioBuffer<float>* sampleBuffer = nullptr;
    std::vector<float> dryReadHeads;
    double currentBpm = 120.0;
    
    // Grain management
    juce::Array<Grain> grains;
    // Tapped-Delay Line
    DelayLine delayLine;
    int maxDelaySize = 0;
    
    // ADSR and smoothing
    juce::ADSR envelope;
    juce::SmoothedValue<float> smoothSparse;
    juce::SmoothedValue<float> smoothedMix;
    juce::SmoothedValue<float> smoothedFeedback;

    // Parameters
    std::atomic<float>* levelParam;
    std::atomic<float>* positionParam;
    std::atomic<float>* spreadParam;
    std::atomic<float>* envelopeParam;
    std::atomic<float>* activityParam;
    std::atomic<float>* sparseParam;
    std::atomic<float>* jitterParam;
    std::atomic<float>* lengthParam;
    std::atomic<float>* probParam;
    std::atomic<float>* modeParam;
    std::atomic<float>* densityParam;
    std::atomic<float>* mixParam;
    std::atomic<float>* playbackParam;
    std::atomic<float>* quantiseParam;
    std::atomic<float>* quantiseDivisionParam;
    std::atomic<float>* grainFeedbackParam;
    std::atomic<float>* feedbackParam;
};

