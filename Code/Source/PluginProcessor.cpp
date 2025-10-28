/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Grain.h"
#include "GrainSampler.h"

//==============================================================================
TryGranulatorAudioProcessor::TryGranulatorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
apvts(*this, nullptr, "TryGranulator", createParameterLayout())
{
    // load a sample from the memory
    loadSampleFromMemory();
    
    // retrieving the values of parameters
    reverbMixParam = apvts.getRawParameterValue("ReverbMix");
    reverbOnParam = apvts.getRawParameterValue("ReverbOn");
    filterCutoffParam = apvts.getRawParameterValue("FilterCutoff");
    filterTypeParam = apvts.getRawParameterValue("FilterType");
    filterResonanceParam = apvts.getRawParameterValue("FilterResonance");
    
}

TryGranulatorAudioProcessor::~TryGranulatorAudioProcessor()
{
}

//==============================================================================
const juce::String TryGranulatorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TryGranulatorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TryGranulatorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TryGranulatorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TryGranulatorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TryGranulatorAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int TryGranulatorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TryGranulatorAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String TryGranulatorAudioProcessor::getProgramName (int index)
{
    return {};
}

void TryGranulatorAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void TryGranulatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // ============================================================ Synthesiser setup ========================================
    synth.clearVoices();
    for (int i = 0; i < 3; ++i) // 3 voices (can increase this, depending on CPU power)
    {
        auto* voice = new GrainVoice();
        
        // Attach sample buffer to the voice
        voice->setSampleBuffer(sampleBuffer.get());
        synth.addVoice(voice);
        
        // Cast voice to GrainVoice and link parameter tree
        GrainVoice* voicePtr = dynamic_cast<GrainVoice*>(synth.getVoice(i));
        voicePtr -> connectParam(apvts);
    }

    synth.clearSounds();
    synth.addSound(new GrainSound());

    synth.setCurrentPlaybackSampleRate(sampleRate);
    
    // Reverb reset internal buffers
    reverb.reset();
    // Reverb initialisation
    smoothedReverbMix.reset(getSampleRate(), 0.1);
    smoothedReverbMix.setCurrentAndTargetValue(*reverbMixParam);
    
    // Filter configuration (stereo)
    filterL.setCoefficients(juce::IIRCoefficients::makeLowPass(getSampleRate(), 3000.0));
    filterR.setCoefficients(juce::IIRCoefficients::makeLowPass(getSampleRate(), 3000.0));
    filterL.reset();
    filterR.reset();
}

void TryGranulatorAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TryGranulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void TryGranulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    /**juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
*/
    buffer.clear(); //clears the output audio buffer before we write anything new into it.
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    
    // =================================================== bpm =========================================
    
    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            if (pos->getBpm().hasValue())
                bpm = *pos->getBpm();
            DBG("BPM: " << bpm);
        }
    }

    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<GrainVoice*>(synth.getVoice(i)))
        {
            v->setCurrentBpm(bpm);
        }
    }

    
    // ======================================================= filter =====================================================================
    
    float cutoff = *filterCutoffParam;
    float resonance = *filterResonanceParam;
    int type = static_cast<int>(*filterTypeParam);
    int sampleRate = getSampleRate();

    juce::IIRCoefficients coeffs;

    if (type == 0)
        coeffs = juce::IIRCoefficients::makeLowPass(sampleRate, cutoff, resonance);
    else if (type == 1)
        coeffs = juce::IIRCoefficients::makeHighPass(sampleRate, cutoff, resonance);
    else if (type == 2)
        coeffs = juce::IIRCoefficients::makeBandPass(sampleRate, cutoff, resonance);
    else
        coeffs = juce::IIRCoefficients::makeLowPass(sampleRate, cutoff, resonance); // fallback

    filterL.setCoefficients(coeffs);
    filterR.setCoefficients(coeffs);

    // ===== Apply filter to each sample in buffer ===== // ==================================================
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float left = filterL.processSingleSampleRaw(buffer.getSample(0, i));
        buffer.setSample(0, i, left);

        if (buffer.getNumChannels() > 1)
        {
            float right = filterR.processSingleSampleRaw(buffer.getSample(1, i));
            buffer.setSample(1, i, right);
        }
    }
    
    //==================================================================== Reverb =================================================================
    
    if (*reverbOnParam > 0.5f)
    {
        smoothedReverbMix.setTargetValue(*reverbMixParam);

        // Copy current buffer to a reverb buffer
        juce::AudioBuffer<float> reverbBuffer;
        reverbBuffer.makeCopyOf(buffer);

        // parameters
        reverbParams.roomSize   = 0.2f;
        reverbParams.damping    = 0.5f;
        reverbParams.wetLevel   = *reverbMixParam; // shouldnt the value here be smoothedReverbMix.getNextValue();
        reverbParams.dryLevel   = 0.0f;
        reverbParams.width      = 1.0f;
        reverbParams.freezeMode = 0.0f;

        reverb.setParameters(reverbParams);
        reverb.processStereo(reverbBuffer.getWritePointer(0), reverbBuffer.getWritePointer(1), buffer.getNumSamples());

        // Mix the reverb back in with smoothed mix - Final Output
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float mix = smoothedReverbMix.getNextValue();
                float dry = buffer.getSample(ch, i);
                float wet = reverbBuffer.getSample(ch, i);
                float mixed = (1.0f - mix) * dry + mix * wet;
                buffer.setSample(ch, i, mixed);
            }
        }
    }
    
}

//==============================================================================
bool TryGranulatorAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* TryGranulatorAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
void TryGranulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr <juce::XmlElement > xml (state.createXml());
    copyXmlToBinary (*xml, destData);

}

void TryGranulatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
    if (xmlState ->hasTagName (apvts.state.getType()))
    apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TryGranulatorAudioProcessor();
}

/**
 loads a file from disk and stores it in the plugin's sampleBuffer
 */
void TryGranulatorAudioProcessor::loadSample(const juce::String& path)
{
    juce::File file(path);
    auto* reader = formatManager.createReaderFor(file);
    if (reader)
    {
        sampleBuffer.reset(new juce::AudioBuffer<float>(reader->numChannels, (int)reader->lengthInSamples));
        
        // read sample into buffer starting from 0
        reader->read(sampleBuffer.get(), 0, (int)reader->lengthInSamples, 0 , true, true);
        delete reader;
    }
}

/**
 loads the sample from binary data into the sampleBuffer
 */
void TryGranulatorAudioProcessor::loadSampleFromMemory()
{
    formatManager.registerBasicFormats();

    // Wrap binary sample data in JUCE MemoryInputStream
    std::unique_ptr<juce::MemoryInputStream> inputStream =
        std::make_unique<juce::MemoryInputStream>(
            BinaryData::Ad_Privatecaller_wav,
            BinaryData::Ad_Privatecaller_wavSize,
            false
        );

    if (inputStream)
    {
        // create reader from input stream
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(inputStream)));

        if (reader)
        {
            sampleBuffer.reset(new juce::AudioBuffer<float>(reader->numChannels, (int)reader->lengthInSamples));
            
            // read into audio buffer
            reader->read(sampleBuffer.get(), 0, (int)reader->lengthInSamples, 0, true, true);
        }
    }
}
