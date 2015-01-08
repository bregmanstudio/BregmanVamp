/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
 * 
 * Dissonance - 
 * A Vamp plugin that calculates the dissonance function of the
 * frequency domain representation of each block of audio.
 *
 * Author: Michael A. Casey, Dartmouth College, USA (2015)
 *
*/

#ifndef _SPECTRAL_DISSONANCE_PLUGIN_H_
#define _SPECTRAL_DISSONANCE_PLUGIN_H_

#include "vamp-sdk/Plugin.h"

extern "C" {
#include "iirfilter.h"
}


/**
 * Plugin that calculates the dissonance function of the
 * frequency domain representation of each block of audio.
 */

class Dissonance : public Vamp::Plugin
{
public:
  FILTER * lpf;
    Dissonance(float inputSampleRate);
    virtual ~Dissonance();

    bool initialise(size_t channels, size_t stepSize, size_t blockSize);
    void initialise_filter();
    void reset();

    InputDomain getInputDomain() const { return FrequencyDomain; }

    std::string getIdentifier() const;
    std::string getName() const;
    std::string getDescription() const;
    std::string getMaker() const;
    int getPluginVersion() const;
    size_t getPreferredStepSize() const; 
    size_t getPreferredBlockSize() const;

    std::string getCopyright() const;

    OutputList getOutputDescriptors() const;

    FeatureSet process(const float *const *inputBuffers,
                       Vamp::RealTime timestamp);

    FeatureSet getRemainingFeatures();

protected:
    size_t m_stepSize;
    size_t m_blockSize;
};


#endif
