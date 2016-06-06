#include "Clip.h"
#include <algorithm>

// Default constructor tries to init to a sane state
Clip::Clip() :
	m_uSamplesInHead( 0 ),
	m_uFadeSamples( 0 )
{
}

// More interesting
Clip::Clip( const std::string strName,				// The friendly name of the loop
			const float * const pHeadBuffer,		// The head buffer
			const size_t uSamplesInHeadBuffer,		// and its sample count
			const float * const pTailBuffer,		// The tail buffer
			const size_t uSamplesInTailBuffer,		// and its sample count
			const size_t uFadeDuration ) :			// The fade duration
	Clip()
{
	// Don't assign any members unless there's a head buffer
	if ( pHeadBuffer != nullptr && uSamplesInHeadBuffer > 0 )
	{
		// If the pointers are good, assign the members
		m_strName = strName;
		m_uSamplesInHead = uSamplesInHeadBuffer;
		m_uFadeSamples = uFadeDuration;

		// Copy the head buffer into the vector	
		m_vAudioBuffer.resize( m_uSamplesInHead );
		memcpy( m_vAudioBuffer.data(), pHeadBuffer, sizeof( float ) * m_uSamplesInHead );

		// If there's a tail
		if ( uSamplesInTailBuffer > 0 && pTailBuffer != nullptr )
		{
			// The tail must end when or before the fade-out starts
			const size_t uFadeBegin = m_uSamplesInHead - m_uFadeSamples;
			const size_t uTailSampleCount = std::min( uFadeBegin, uSamplesInTailBuffer );

			// Copy its samples into the vector
			m_vAudioBuffer.resize( m_vAudioBuffer.size() + uTailSampleCount );
			memcpy( &m_vAudioBuffer[m_uSamplesInHead], pTailBuffer, sizeof( float ) * uTailSampleCount );

			// The tail fade to zero duration is either ours or the duration of the tail itself
			// (in which case the entire tail is fading to zero)
			const size_t uTailFadeSamples = std::min( m_uFadeSamples, uTailSampleCount );
			const size_t uTailFadeBegin = uTailSampleCount - uTailFadeSamples;

			// Bake in the tail's fade to zero
			for ( size_t uTailIdx = uTailFadeBegin; uTailIdx < uTailSampleCount; uTailIdx++ )
			{
				const size_t uTailIdxInBuf = m_uSamplesInHead + uTailIdx;
				m_vAudioBuffer[uTailIdxInBuf] = remap( uTailIdx, uTailFadeBegin, uTailSampleCount, m_vAudioBuffer[uTailIdxInBuf], 0.f );
			}
		}

		// Shrink audio buffer, it won't be resized
		m_vAudioBuffer.shrink_to_fit();
	}
}

std::string Clip::GetName() const
{
	return m_strName;
}

size_t Clip::GetNumSamples( bool bTail /*= false*/ ) const
{
	if ( bTail )
		return m_vAudioBuffer.size();
	return m_uSamplesInHead;
}

size_t Clip::GetNumFadeSamples() const
{
	return m_uFadeSamples;
}

float const * Clip::GetAudioData() const
{
	return m_vAudioBuffer.empty() ? nullptr : m_vAudioBuffer.data();
}