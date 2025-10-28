/*
  ==============================================================================

    DelayLine.h
    Created: 2 Mar 2025 7:25:10am
    Author:  Shreya Gupta

  ==============================================================================
*/

/**
 @class DelayLine - a basic feedback delay
 */

#pragma once
class DelayLine
{
public:
    /**
     setting the size of the delay buffer
     @param size integer value of the size (number of samples)
     */
    void setMaxSize (int size)
    {
        delayBuffer.resize (size);
        maxDelaySize = size;
    }
    
    /**
     sets the incoming sample to the writeHeadPosition
     @param sample float value of the sample at a certain position
     */
    void setInputSample (float sample)
    {
        delayBuffer[writeHeadPosition] = sample;
    }
    
    /**
     returns the output sample from the readHeadPosition in the delay buffer
     @return float sample value at the readHeadPosition in the buffer
     */
    float outputSample()
    {
        return delayBuffer[readHeadPosition];
    }
        
    /**
     sets the delaytime in samples and wraps around to make sure readHeadPostion and writeHeadPostition don't get out of bounds
     @param delayTime_ setting the delaytime - gap between readHead and writeHead Postion in buffer
     */
    void setDelayTime (float delayTime_)
    {
        delayTimeSamples = delayTime_;
        
        if (writeHeadPosition > delayTime_)
            readHeadPosition = writeHeadPosition - delayTime_;
        else
            readHeadPosition = maxDelaySize - (delayTime_ - writeHeadPosition);
    }
    
    /**
     outputs the delayed sample along with the feedback from the current input sample
     @param inputSample float input incoming sample to be processed
     @return outputSample - delayed sample and the feedback of the current sample
     */
    float process (float inputSample)
    {
        float outputSample = linearInterpolation();
        delayBuffer[writeHeadPosition] = inputSample + (outputSample * feedbackAmt);
        writeHeadPosition = (writeHeadPosition + 1) % maxDelaySize;
        readHeadPosition = fmod((readHeadPosition + 1), maxDelaySize);
        return outputSample;
    }
    
    /**
     sets the feedback amount to the feedbackAmt private variable
     @param feedback_ feedback gain to be set
     */
    void setFeedback (float feedback_)
    {
        if (feedback_ > 0 && feedback_ <= 1)
            feedbackAmt = feedback_;
    }
    
    /**
     liner interpolation between samples
     */
    float linearInterpolation ()
    {
        int lowerBound = floor(readHeadPosition);
        int higherBound = lowerBound + 1.0f;
        
        float lowerVal = delayBuffer[lowerBound];
        float higherVal = delayBuffer[higherBound];
        
        float frac = readHeadPosition - lowerBound;
        
        float output = (1 - frac) * lowerVal + frac * higherVal;
        
        return output;
    }
    
    /**
     Returns the current write head position in the buffer
     */
    int getWriteHeadPosition()
    {
        return writeHeadPosition;
    }
    
    /**
     Returns the size of the delay buffer in samples
     */
    int getDelaySize ()
    {
        return maxDelaySize;
    }
    
    /**
     Reads a sample at a specific index in the delay buffer
     */
    float getSampleAtIndex(int index) const
    {
        index = index % maxDelaySize;
        return delayBuffer[index];
    }
    
private:
    std::vector<float> delayBuffer; // Circular buffer for storing samples
    int maxDelaySize; // set the size of the delay buffer (currently set to 3 secs)
    float readHeadPosition = 0.0f; // Current read position in the buffer
    int writeHeadPosition = 0; // Current write position in the buffer
    float delayTimeSamples = 0.0f; // set delayTime in samples
    float feedbackAmt = 0.0; // Amount of feedback apploed in process()
};

