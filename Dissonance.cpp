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

#include "Dissonance.h"
#include <algorithm>

typedef std::pair<float, int> IdxSortPair;
typedef std::pair<float,float> FreqSortPair;

bool IdxComparator ( const IdxSortPair& l, const IdxSortPair& r)
    { return l.first < r.first; }

bool FreqComparator ( const FreqSortPair& l, const FreqSortPair& r)
{ return l.first < r.first; }


using std::string;
using std::vector;
using std::cerr;
using std::endl;

#include <math.h>
#include <stdlib.h>

#ifdef __SUNPRO_CC
#include <ieeefp.h>
#define isinf(x) (!finite(x))
#endif

#ifdef WIN32
#define isnan(x) false
#define isinf(x) false
#endif


Dissonance::Dissonance(float inputSampleRate) :
    Plugin(inputSampleRate),
    m_stepSize(0),
    m_blockSize(0)
{

    // quick and dirty Butterworth low-pass filter coefficients (from scipy, cutoff = 0.25)
    // b, a
    #define LPF_ORDER 11
    float lpf_coeffs[2][LPF_ORDER] = {{1.10559099e-05,   1.10559099e-04,   4.97515946e-04,
                                       1.32670919e-03,   2.32174108e-03,   2.78608930e-03,
                                       2.32174108e-03,   1.32670919e-03,   4.97515946e-04,
                                       1.10559099e-04,   1.10559099e-05},
                                      {1.00000000e+00,  -4.98698526e+00,   1.19364368e+01,
                                       -1.77423718e+01,   1.79732280e+01,  -1.28862417e+01,
                                       6.59320221e+00,  -2.36909169e+00,   5.70632706e-01,
                                       -8.30176785e-02,   5.52971437e-03}};

    lpf = (FILTER*) calloc(1,sizeof(FILTER));
    lpf->numb = LPF_ORDER;
    lpf->numa = LPF_ORDER; // Assume A[0]=1 and crop array
    int i;
    for(i=0; i<lpf->numb; i++){
	lpf->coeffs[i] = lpf_coeffs[0][i];
    }
    for(i=1; i<lpf->numa; i++){ // Assume A[0]=1 and crop array
        lpf->coeffs[lpf->numb+i-1] = lpf_coeffs[1][i];
    }
    ifilter(lpf);
}

Dissonance::~Dissonance()
{
    if(lpf){
        free_filter(lpf);
    }
}

string
Dissonance::getIdentifier() const
{
    return "dissonance";
}

string
Dissonance::getName() const
{
    return "Dissonance";
}

string
Dissonance::getDescription() const
{
    return "Calculate the dissonance function of the spectrum of the input signal";
}

string
Dissonance::getMaker() const
{
    return "Bregman Media Labs";
}

int
Dissonance::getPluginVersion() const
{
    return 2;
}

string
Dissonance::getCopyright() const
{
    return "Freely redistributable (BSD license)";
}

bool
Dissonance::initialise(size_t channels, size_t stepSize, size_t blockSize)
{
    if (channels < getMinChannelCount() ||
	channels > getMaxChannelCount()) return false;

    m_stepSize = stepSize;
    m_blockSize = blockSize;
    
    return true;
}

void
Dissonance::reset()
{
}

Dissonance::OutputList
Dissonance::getOutputDescriptors() const
{
    OutputList list;

    OutputDescriptor d;
    d.identifier = "lineardissonance";
    d.name = "Dissonance";
    d.description = "Dissonance function of the linear frequency spectrum";
    d.unit = "Diss";
    d.hasFixedBinCount = true;
    d.binCount = 1;
    d.hasKnownExtents = false;
    d.isQuantized = false;
    d.sampleType = OutputDescriptor::OneSamplePerStep;
    list.push_back(d);

    //    d.identifier = "logdissonance";
    //    d.name = "Log Dissonance";
    //    d.description = "Dissonance function of the log weighted frequency spectrum";
    //    list.push_back(d);

    return list;
}

Dissonance::FeatureSet
Dissonance::process(const float *const *inputBuffers, Vamp::RealTime timestamp)
{
    if (m_stepSize == 0) {
	cerr << "ERROR: Dissonance::process: "
	     << "Dissonance has not been initialised"
	     << endl;
	return FeatureSet();
    }
    FeatureSet returnFeatures; // output "scale" aggregator
    Feature feature; // output feature

    Feature freqs;
    Feature mags;
    
    for (size_t i = 1; i <= m_blockSize/2; ++i) {
	double freq = (double(i) * m_inputSampleRate) / m_blockSize;
	double real = inputBuffers[0][i*2];
	double imag = inputBuffers[0][i*2 + 1];
	mags.values.push_back(sqrt(real * real + imag * imag) / (m_blockSize/2));
        freqs.values.push_back(freq);
    }
    // Low-pass filtering the spectrum: Reversal for backward-forward filtering
    // backward-forward filtering results in a linear-phase filter
    Feature rev_mags;
    for(size_t i = 0; i <= m_blockSize/2; ++i){
        rev_mags.values.push_back(mags.values[m_blockSize/2-i]);
    }   
    Feature rev_outs;
    rev_outs.values.resize(m_blockSize/2);
    lpf->in = rev_mags.values.data();
    lpf->out = rev_outs.values.data();
    afilter(lpf, m_blockSize/2); // backward filter
    for(size_t i = 0; i <= m_blockSize/2; ++i){
        lpf->in[i] = lpf->out[m_blockSize/2-i];        
    }
    afilter(lpf, m_blockSize/2); // forward filter
    // Half-wave rectification
    for(size_t i = 0; i <= m_blockSize/2; ++i){
        if(lpf->in[i]<0.0f)
            lpf->in[i]=0.0f;     // half-wave rectify
    }
    // Magnitude derivatives wrt frequency
    Feature diffs;
    diffs.values.push_back(0.0f);
    for(size_t i = 1; i <= m_blockSize/2; ++i){
        diffs.values.push_back(mags.values[i] - mags.values[i-1]);
    }

    // Peak finding (spectral derivatives' zero crossings)
    float thresh = 1e-9f;
    vector<size_t> peak_idx;
    for(size_t i = 1; i <= m_blockSize/2; ++i){
        // zero crossing detector
        if( (diffs.values[i-1] > thresh) && (diffs.values[i] < -thresh) )
            peak_idx.push_back(i);
    }

    if (!peak_idx.size()){ // Abort if no peaks
        std::cout << "Warning: zero-length peak_idx" << endl;
        feature.hasTimestamp = false;
        feature.values.push_back(0.0f);
        returnFeatures[0].push_back(feature);
        returnFeatures[1].push_back(feature);
        return returnFeatures;        
    }

    std::vector<IdxSortPair> arg_idx;
    for(size_t i = 0; i < peak_idx.size(); ++i){
        arg_idx.push_back(IdxSortPair(mags.values[peak_idx[i]], peak_idx[i]));
        std::cout << "(" << mags.values[peak_idx[i]] << "," << peak_idx[i] << ")" << endl;
    }
    // Reverse sorting by magnitude
    std::sort(arg_idx.rbegin(), arg_idx.rend(), IdxComparator);
    // Now grab the sorted list of freq, mags as a new pair
    size_t num_partials = std::min((int)peak_idx.size(),20); // How many partials to use in dissonance function
    std::vector<FreqSortPair> freqs_mags;
    for(size_t i = 0; i < num_partials; ++i){
        freqs_mags.push_back(FreqSortPair(freqs.values[arg_idx[i].second],mags.values[arg_idx[i].second]));
    }    
    std::sort(freqs_mags.begin(), freqs_mags.end(), FreqComparator); // sort by freq,mag pairs by ascending frequencies
    // Finally, compute the dissonance function
    float b1=-3.51f, b2=-5.75f, s1=0.0207f, s2=19.96f, c1=5.0f, c2=-5.0f, Dstar=0.24f;
    float diss_val = 0.0f;
    size_t N = freqs_mags.size();
    for(size_t i = 1; i<N; ++i){
        for(size_t j = 0; j < N-i ; ++j){
            float S = Dstar / (s1 * freqs_mags[j].first + s2 );
            float Fdif = freqs_mags[j+i].first - freqs_mags[j].first;
            float am = freqs_mags[j+i].second * freqs_mags[j].second;
            diss_val += am * ( c1 * exp(b1 * S * Fdif) + c2 * exp(b2 * S * Fdif) );
        }
    }

    feature.hasTimestamp = false;
    if (!isnan(diss_val) && !isinf(diss_val)) {
        feature.values.push_back(diss_val);
    }
    returnFeatures[0].push_back(feature);
    feature.values.clear();
    if (!isnan(diss_val) && !isinf(diss_val)) {
        feature.values.push_back(log10f(diss_val));
    }
    returnFeatures[1].push_back(feature);
    return returnFeatures;
}

Dissonance::FeatureSet
Dissonance::getRemainingFeatures()
{
    return FeatureSet();
}

