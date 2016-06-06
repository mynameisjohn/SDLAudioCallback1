#include "Voice.h"
#include "Clip.h"

#include <algorithm>

Voice::Voice( const Clip const * pClip, int ID, size_t uTriggerRes, float fVolume, bool bLoop /*= false*/ ) :
	m_iUniqueID( -1 ),
	m_eState( EState::Stopped ),
	m_ePrevState( EState::Stopped ),
	m_fVolume( 1.f ),
	m_uTriggerRes( 0 ),
	m_uStartingPos( 0 ),
	m_pClip( nullptr )
{
	if ( pClip && ID >= 0 )
	{
		m_iUniqueID = ID;
		m_pClip = pClip;
		m_uTriggerRes = uTriggerRes;
		m_fVolume = fVolume;
		m_eState = bLoop ? EState::Pending : EState::OneShot;
	}
}

Voice::EState Voice::GetState() const
{
	return m_eState;
}

Voice::EState Voice::GetPrevState() const
{
	return m_ePrevState;
}

float Voice::GetVolume() const
{
	return m_fVolume;
}

int Voice::GetID() const
{
	return m_iUniqueID;
}

void Voice::SetStopping( const size_t uTriggerRes )
{
	EState eNextState = m_eState;
	switch ( m_eState )
	{
		// If we're pending, just stop and get out
		case EState::Pending:
			eNextState = EState::Stopped;
			break;
			// If we're TailPending, set to Tail and get out
		case EState::TailPending:
			eNextState = EState::Tail;
			break;
			// If we're playing, set state to stopping
		case EState::Starting:
		case EState::Looping:
			eNextState = EState::Stopping;
			break;
			// Don't do anything if we're tailing or stopped
		case EState::Tail:
		case EState::Stopped:
			return;
	}

	setState( eNextState );
	m_uTriggerRes = uTriggerRes;
}

void Voice::SetVolume( float fVol )
{
	m_fVolume = std::max( 0.f, std::min( fVol, 1.f ) );
}

void Voice::setState( EState eNextState )
{
	m_ePrevState = m_eState;
	m_eState = eNextState;
}

void Voice::RenderData( float * const pMixBuffer, const size_t uSamplesDesired, const size_t uSamplePos )
{
	// Possible early out
	if ( m_eState == EState::Stopped || pMixBuffer == nullptr || m_pClip == nullptr || m_fVolume <= 0.f )
		return;

	// Get what we need from the clip
	const size_t uSamplesInHead = m_pClip->GetNumSamples( false );
	const size_t uSamplesInTail = m_pClip->GetNumSamples( true ) - uSamplesInHead;
	const size_t uFadeSamples = m_pClip->GetNumFadeSamples();
	const size_t uFadeBegin = uSamplesInHead - uFadeSamples;
	const float const * pAudioData = m_pClip->GetAudioData();

	// Just another check
	if ( uSamplesInHead == 0 || pAudioData == nullptr )
		return;

	// While there are samples left to add (and the # of this while loop's iterations)
	size_t uSamplesAdded( 0 ), uWhileLoop( 0 );
	while ( uSamplesAdded < uSamplesDesired )
	{
		// The current sample pos, # left to add
		const size_t uCurrentSamplePos = uSamplesAdded + uSamplePos;
		const size_t uSamplesLeftToAdd = uSamplesDesired - uSamplesAdded;

		// We need to compute the offset sample position, which is basically transforming
		// the global sample position into the position it would be if we had started
		// at sample 0 (which we need to get the correct position within our buffer)
		size_t uOffsetPos( 0 );
		if ( uCurrentSamplePos < m_uStartingPos )
			uOffsetPos = (uSamplesInHead - m_uStartingPos) + uCurrentSamplePos;
		else
			uOffsetPos = uCurrentSamplePos - m_uStartingPos;

		// Now that we have the offset sample position, 
		// compute the position within our buf
		size_t uPosInBuf = uOffsetPos % uSamplesInHead;

		// Where the last sample would be, ignoring the size of our head buffer
		const size_t uTentativeLastSample = uPosInBuf + uSamplesLeftToAdd;

		// Boundaries for the three loops, clamped by what we can actually add
		size_t uLastHeadSample = std::min( uTentativeLastSample, uFadeBegin );
		size_t uLastTailSample = std::min( uTentativeLastSample, uSamplesInTail );
		size_t uLastFadeoutToBegin = std::min( uTentativeLastSample, uSamplesInHead );

		// Quantities used for trigger states like pending, stopping (note that we use uCurrentSamplePos)
		const size_t uPosAlongTrigger = m_uTriggerRes ? uCurrentSamplePos % m_uTriggerRes : 0;
		const size_t uSamplesLeftTillTrigger = m_uTriggerRes - uPosAlongTrigger;

		// The state we may switch to after this loop
		EState eNextState = m_eState;

		// The fade target, which depends on the state
		float fTargetVal( 0 );

		// Before adding any head samples, cache a pointer to the first tail sample
		float * pFirstTailMixSample = &pMixBuffer[uSamplesAdded];

		switch ( m_eState )
		{
			// We're pending, and we might also be mixing in the tail
			case EState::OneShot:
			case EState::TailPending:
			case EState::Pending:
				// If we'll hit the trigger resolution
				if ( uSamplesLeftTillTrigger < uSamplesLeftToAdd )
				{
					// Advance the number of samples added to the remainder 
					// of the way to the trigger resoution
					uSamplesAdded += uSamplesLeftTillTrigger;

					// Set the starting position to the current sample idx
					m_uStartingPos = (uCurrentSamplePos + uSamplesLeftTillTrigger) % uSamplesInHead;

					// If we're pending and we hit the end of the trigger res,
					// advance state and continue, otherwise postpone state change
					if ( m_eState == EState::Pending )
					{
						setState( EState::Starting );
						continue;
					}
					else if (m_eState == EState::OneShot )
					{
						setState( EState::Stopping );
						continue;
					}
					else
						eNextState = EState::Starting;
				}

				// If we're currently mixing in the tail, jump down
				if ( m_eState == EState::TailPending )
					goto Case_TailPending;

				// We're pending and we won't play this iteration, get out
				return;

				// Fade up from zero until we hit our fade duration,
				// render only the head until we hit the fade-out,
				// then fade out to (head+tail)[0]
			case EState::Starting:
				// If we're still within the initial fade up
				if ( uPosInBuf < uFadeSamples )
				{
					// Fade up from zero (this is the only loop of it's kind, so just do it here
					const size_t uLastFadeFromZero = std::min( uTentativeLastSample, uFadeSamples );
					for ( ; uPosInBuf < uLastFadeFromZero; uPosInBuf++ )
					{
						float fSampleVal = m_fVolume * pAudioData[uPosInBuf];
						pMixBuffer[uSamplesAdded++] += remap( uPosInBuf, 0, uFadeSamples, 0.f, fSampleVal );
					}

					// Continue here to get fade-in out of the way
					continue;
				}

				// If we'll be fading
				if ( uLastHeadSample == uFadeBegin )
				{
					// Compute the target value (head+tail)[0]
					fTargetVal = *pAudioData;
					if ( uSamplesInTail )
						fTargetVal += pAudioData[uSamplesInHead];
				}

				// If we'll hit the end of the buffer, we'll be looping afterwards
				if ( uLastFadeoutToBegin == uSamplesInHead )
					eNextState = EState::Looping;

				// If starting, don't add tail
				uLastTailSample = 0;
				break;

				// We're fading out to a tail if there is one, zero otherwise,
				// once we hit the trigger resolution
			case EState::Stopping:
				// If we'll be fading, and if the samples till trigger is less
				// than our head size (meaning we'll hit it this loop iteration)
				// then set the fadeout sample to either 0 or the first tail sample
				if ( uLastHeadSample == uFadeBegin && uSamplesLeftTillTrigger < uSamplesInHead )
				{
					// Only assign if there are tail samples; it's already 0
					if ( uSamplesInTail )
						fTargetVal = pAudioData[uSamplesInHead];

					// If we'll hit the end of the buffer, advance to either Tail or Stopped
					if ( uLastFadeoutToBegin == uSamplesInHead )
						eNextState = uSamplesInTail ? EState::Tail : EState::Stopped;

					// break if we're fading for the trigger
					break;
				}

				// Otherwise we're going to treat ourselves as either starting or looping;
				// the only difference between starting and looping is that looping involves
				// mixing the tail back in, so if we were starting then turn off that loop
				if ( m_ePrevState == EState::Starting )
					uLastTailSample = 0;

				// We're looping back after starting, looping, or waiting until we can stop		
			case EState::Looping:
				// If we hit the loopback fade
				if ( uLastHeadSample == uFadeBegin )
				{
					// The target val for looping is (head+tail)[0]
					fTargetVal = *pAudioData;
					if ( uSamplesInTail )
						fTargetVal += pAudioData[uSamplesInHead];
				}

				break;

				// We're rendering the tail only
			case EState::Tail:
				Case_TailPending:
					// If we're in the tail state, manually advance the number 
					// of samples added, since the tail  loop doesn't do that 
					// (shitty, I know, but if we don't then we'll loop forever)
					uSamplesAdded += uLastTailSample - uPosInBuf;

					// If we'll hit the end of the tail
					if ( uLastTailSample == uSamplesInTail )
					{
						// A real tail means we're stopped
						if ( m_eState == EState::Tail )
							eNextState = EState::Stopped;
						// A pending tail means we're either pending or starting
						else if ( eNextState != EState::Starting )
							eNextState = EState::Pending;
					}

					// Make sure these loops don't get hit
					uLastHeadSample = 0;
					uLastFadeoutToBegin = 0;
					break;

					// We shouldn't be doing anything
			case EState::Stopped:
				return;
		}

		// We'll use the last head idx to know where to start the fade out
		size_t uHeadIdx( uPosInBuf );
		for ( ; uHeadIdx < uLastHeadSample; uHeadIdx++ )
		{
			float fSampleVal = m_fVolume * pAudioData[uHeadIdx];
			pMixBuffer[uSamplesAdded++] += fSampleVal;
		}

		// Add the tail samples
		for ( size_t uTailIdx = uPosInBuf; uTailIdx < uLastTailSample; uTailIdx++ )
		{
			float fSampleVal = m_fVolume * pAudioData[uSamplesInHead + uTailIdx];
			*pFirstTailMixSample++ += fSampleVal;
		}

		// Fade out to target sample
		for ( size_t uFadeoutIdx = uHeadIdx; uFadeoutIdx < uLastFadeoutToBegin; uFadeoutIdx++ )
		{
			// Fade the current sample to the target val
			float fSampleVal = m_fVolume * pAudioData[uFadeoutIdx];
			pMixBuffer[uSamplesAdded++] += remap( uFadeoutIdx, uFadeBegin, uSamplesInHead, fSampleVal, fTargetVal );
		}

		// Update state
		if ( eNextState != m_eState )
			setState( eNextState );

		// This is more of a debug helper than anything else
		uWhileLoop++;
	}

	return;
}