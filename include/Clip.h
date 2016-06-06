#pragma once

#include <string>
#include <vector>

// remaps x : [m0, M0] to the range of [m1, M1]
inline float remap( float x, float m0, float M0, float m1, float M1 )
{
	return m1 + ((x - m0) / (M0 - m0)) * (M1 - m1);
}

class Clip
{
public:
	Clip();
	Clip( const std::string strName,			// The friendly name of the loop
		  const float * const pHeadBuffer,		// The head buffer
		  const size_t uSamplesInHeadBuffer,	// and its sample count
		  const float * const pTailBuffer,		// The tail buffer
		  const size_t uSamplesInTailBuffer,
		  const size_t m_uFadeSamples);			// and its sample count

	std::string GetName() const;

	// The sample count, if bTail is true tail samples included
	size_t GetNumSamples( bool bTail = false ) const;
	size_t GetNumFadeSamples() const;
	float const * GetAudioData() const;

private:
	size_t m_uSamplesInHead;					// The number of samples in the head
	size_t m_uFadeSamples;						// The target sample for the fade-out when stopping
	std::string m_strName;						// The name of the loop (this is never touched by aud thread)
	std::vector<float> m_vAudioBuffer;			// The vector storing the entire head and tail (with fades baked)
};