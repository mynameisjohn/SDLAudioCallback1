#include "Loop.h"

#include <algorithm>

// remaps x : [m0, M0] to the range of [m1, M1]
inline float remap( float x, float m0, float M0, float m1, float M1 )
{
	return m1 + ((x - m0) / (M0 - m0)) * (M1 - m1);
}

// Default constructor tries to init to a sane state
Loop::Loop() :
	m_ePrevState( State::Stopped ),
	m_eState( State::Stopped ),
	m_fVolume( 1.f ),
	m_uSamplesInHead( 0 ),
	m_uTriggerResolution( 0 ),
	m_uFadeSamples( 0 )
{
}

// More interesting
Loop::Loop( const std::string strName,				// The friendly name of the loop
			const float * const pHeadBuffer,		// The head buffer
			const size_t uSamplesInHeadBuffer,		// and its sample count
			const float * const pTailBuffer,		// The tail buffer
			const size_t uSamplesInTailBuffer,		// and its sample count
			const size_t uFadeDuration,				// The fade duration
			const float fVolume ) :					// The loop's volume
	Loop()
{
	// Don't assign any members unless there's a head buffer
	if ( pHeadBuffer != nullptr && uSamplesInHeadBuffer > 0 )
	{
		// If the pointers are good, assign the members
		m_strName = strName;
		m_uSamplesInHead = uSamplesInHeadBuffer;
		m_uFadeSamples = uFadeDuration;
		m_fVolume = std::min( std::max( fVolume, 0.f ), 1.f );

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

// Called by loopmanager, fills up pMixBuffer with audio data (unless pending or stopped)
void Loop::GetData( float * const pMixBuffer, const size_t uSamplesDesired, const size_t uSamplePos )
{
	// Possible early out
	if ( m_eState == State::Stopped || pMixBuffer == nullptr || m_vAudioBuffer.empty() || m_fVolume <= 0.f )
		return;

	// These can be computed outside of the loop
	const size_t uFadeBegin = m_uSamplesInHead - m_uFadeSamples;
	const size_t uSamplesInTail = m_vAudioBuffer.size() - m_uSamplesInHead;

	// While there are samples left to add (and the # of this while loop's iterations)
	size_t uSamplesAdded( 0 ), uWhileLoop( 0 );
	while ( uSamplesAdded < uSamplesDesired )
	{
		// The current sample pos, # left to add, pos within our buffer
		const size_t uCurrentSamplePos = uSamplesAdded + uSamplePos;
		const size_t uSamplesLeftToAdd = uSamplesDesired - uSamplesAdded;
		size_t uPosInBuf = uCurrentSamplePos % m_uSamplesInHead;
		
		// Boundaries for the three loops
		const size_t uTentativeLastSample = uPosInBuf + uSamplesLeftToAdd;
		size_t uLastHeadSample = std::min( uTentativeLastSample, uFadeBegin );
		size_t uLastTailSample = std::min( uTentativeLastSample, uSamplesInTail );
		size_t uLastFadeoutToBegin = std::min( uTentativeLastSample, m_uSamplesInHead );

		// Quantities used for trigger states (like pending, stopping)
		const size_t uPosAlongTrigger = m_uTriggerResolution ? uCurrentSamplePos % m_uTriggerResolution : 0;
		const size_t uSamplesLeftTillTrigger = m_uTriggerResolution - uPosAlongTrigger;

		// The state we may switch to after this loop
		State eNextState = m_eState;

		// The fade target, which depends on the state
		float fTargetVal( 0 );

		// Before adding any head samples, cache a pointer to the first tail sample
		float * pFirstTailMixSample = &pMixBuffer[uSamplesAdded];

		switch ( m_eState )
		{
			// We're pending, and we might also be mixing in the tail
			case State::TailPending:
			case State::Pending:
				// If we'll hit the trigger resolution
				if ( uTentativeLastSample > m_uTriggerResolution )
				{
					// Advance the number of samples added to the remainder 
					// of the way to the trigger resoution
					uSamplesAdded += uSamplesLeftTillTrigger;

					// If we're pending and we hit the end of the trigger res,
					// advance state and continue, otherwise postpone state change
					if ( m_eState == State::Pending )
					{
						setState( State::Starting );
						continue;
					}
					else
						eNextState = State::Starting;
				}

				// If we're currently mixing in the tail, jump down
				if ( m_eState == State::TailPending )
					goto Case_TailPending;

				// We're pending and we won't play this iteration, get out
				return;

				// Fade up from zero until we hit our fade duration,
				// render only the head until we hit the fade-out,
				// then fade out to (head+tail)[0]
			case State::Starting:
				// If we're still within the initial fade up
				if ( uPosInBuf < m_uFadeSamples )
				{
					// Fade up from zero (this is the only loop of it's kind, so just do it here
					const size_t uLastFadeFromZero = std::min( uTentativeLastSample, m_uFadeSamples );
					for ( ; uPosInBuf < uLastFadeFromZero; uPosInBuf++ )
					{
						float fSampleVal = m_fVolume * m_vAudioBuffer[uPosInBuf];
						pMixBuffer[uSamplesAdded++] += remap( uPosInBuf, 0, m_uFadeSamples, 0.f, fSampleVal );
					}

					// Continue here to get fade-in out of the way
					continue;
				}

				// If we'll be fading
				if ( uLastHeadSample == uFadeBegin )
				{
					// Compute the target value (head+tail)[0]
					fTargetVal = m_vAudioBuffer.front();
					if ( uSamplesInTail )
						fTargetVal += m_vAudioBuffer[m_uSamplesInHead];
				}

				// If we'll hit the end of the buffer, we'll be looping afterwards
				if ( uLastFadeoutToBegin == m_uSamplesInHead )
					eNextState = State::Looping;

				// If starting, don't add tail
				uLastTailSample = 0;
				break;

				// We're fading out to a tail if there is one, zero otherwise,
				// once we hit the trigger resolution
			case State::Stopping:
				// If we'll be fading, and if the samples till trigger is less
				// than our head size (meaning we'll hit it this loop iteration)
				// then set the fadeout sample to either 0 or the first tail sample
				if ( uLastHeadSample == uFadeBegin && uSamplesLeftTillTrigger < m_uSamplesInHead )
				{
					// Only assign if there are tail samples; it's already 0
					if ( uSamplesInTail )
						fTargetVal = m_vAudioBuffer[m_uSamplesInHead];

					// If we'll hit the end of the buffer, advance to either Tail or Stopped
					if ( uLastFadeoutToBegin == m_uSamplesInHead )
						eNextState = uSamplesInTail ? State::Tail : State::Stopped;

					// break if we're fading for the trigger
					break;
				}

				// Otherwise we're going to treat ourselves as either starting or looping;
				// the only difference between starting and looping is that looping involves
				// mixing the tail back in, so if we were starting then turn off that loop
				if ( m_ePrevState == State::Starting )
					uLastTailSample = 0;

				// We're looping back after starting, looping, or waiting until we can stop		
			case State::Looping:
				// If we hit the loopback fade
				if ( uLastHeadSample == uFadeBegin )
				{
					// The target val for looping is (head+tail)[0]
					fTargetVal = m_vAudioBuffer.front();
					if ( uSamplesInTail )
						fTargetVal += m_vAudioBuffer[m_uSamplesInHead];
				}

				break;

			// We're rendering the tail only
			case State::Tail:
			Case_TailPending:
				// If we're in the tail state, manually advance the number 
				// of samples added, since the tail  loop doesn't do that 
				// (shitty, I know, but if we don't then we'll loop forever)
				uSamplesAdded += uLastTailSample - uPosInBuf;

				// If we'll hit the end of the tail
				if ( uLastTailSample == uSamplesInTail )
				{
					// A real tail means we're stopped
					if ( m_eState == State::Tail )
						eNextState = State::Stopped;
					// A pending tail means we're either pending or starting
					else if ( eNextState != State::Starting )
						eNextState = State::Pending;
				}

				// Make sure these loops don't get hit
				uLastHeadSample = 0;
				uLastFadeoutToBegin = 0;
				break;

				// We shouldn't be doing anything
			case State::Stopped:
				return;
		}

		// We'll use the last head idx to know where to start the fade out
		size_t uHeadIdx( uPosInBuf );
		for ( ; uHeadIdx < uLastHeadSample; uHeadIdx++ )
		{
			float fSampleVal = m_fVolume * m_vAudioBuffer[uHeadIdx];
			pMixBuffer[uSamplesAdded++] += fSampleVal;
		}

		// Add the tail samples
		for ( size_t uTailIdx = uPosInBuf; uTailIdx < uLastTailSample; uTailIdx++ )
		{
			float fSampleVal = m_fVolume * m_vAudioBuffer[m_uSamplesInHead + uTailIdx];
			*pFirstTailMixSample++ += fSampleVal;
		}

		// Fade out to target sample
		for ( size_t uFadeoutIdx = uHeadIdx; uFadeoutIdx < uLastFadeoutToBegin; uFadeoutIdx++ )
		{
			// Fade the current sample to the target val
			float fSampleVal = m_fVolume * m_vAudioBuffer[uFadeoutIdx];
			pMixBuffer[uSamplesAdded++] += remap( uFadeoutIdx, uFadeBegin, m_uSamplesInHead, fSampleVal, fTargetVal );
		}

		// Update state
		if ( eNextState != m_eState )
			setState( eNextState );

		// This is more of a debug helper than anything else
		uWhileLoop++;
	}

	return;
}

void Loop::setState(State eState)
{
	m_ePrevState = m_eState;
	m_eState = eState;
}

Loop::State Loop::GetState() const
{
	return m_eState;
}

std::string Loop::GetName() const
{
	return m_strName;
}

void Loop::SetVolume( float fVol )
{
	m_fVolume = fVol;
}

float Loop::GetVolume() const
{
	return m_fVolume;
}

size_t Loop::GetNumSamples( bool bTail /*= false*/ ) const
{
	if ( bTail )
		return m_vAudioBuffer.size();
	return m_uSamplesInHead;
}

// I'm sure these aren't well defined...
void Loop::SetPending( size_t uTriggerRes )
{
	switch ( m_eState )
	{
		// Don't do anything if we're already
		// pending or playing normally
		case State::Pending:
		case State::Starting:
		case State::Looping:
			break;
		case State::Stopping:
			// Ideally this wouldn't happen during a fade...?
			if ( m_ePrevState == State::Looping || m_ePrevState == State::Starting )
				std::swap( m_ePrevState, m_eState );
			break;
		// If we're tailing out and we get set to pending,
		// set to the special TailPending state
		case State::Tail:
			setState( State::TailPending );
			break;
		// If we're stopped, then set to pending
		case State::Stopped:
			setState( State::Pending );
			break;
	}

	// Should we update this no matter what?
	// I'm really not sure
	m_uTriggerResolution = uTriggerRes;
}

void Loop::SetStopping( size_t uTriggerRes )
{
	switch ( m_eState )
	{
		// If we're pending, just stop and get out
		case State::Pending:
			setState( State::Stopped );
			break;
		// If we're TailPending, set to Tail and get out
		case State::TailPending:
			setState( State::Tail );
			break;
		// If we're playing, set state to stopping
		case State::Starting:
		case State::Looping:
			setState( State::Stopping );
			break;
		// Don't do anything if we're tailing or stopped
		case State::Tail:
		case State::Stopped:
			break;
	}

	// Should we update this no matter what?
	// I'm really not sure
	m_uTriggerResolution = uTriggerRes;
}
