#include "Voice.h"
#include "Clip.h"

#include <algorithm>

// Initializing constructor
Voice::Voice() :
	m_iUniqueID( -1 ),
	m_eState( EState::Stopped ),
	m_ePrevState( EState::Stopped ),
	m_fVolume( 1.f ),
	m_uTriggerRes( 0 ),
	m_uStartingPos( 0 ),
    m_uLastTailSampleAdded( UINT_MAX ),
	m_pClip( nullptr )
{}

// Don't set anything until the pointer and ID are checked
Voice::Voice( const Clip const * pClip, int ID, size_t uTriggerRes, float fVolume, bool bLoop /*= false*/ ) :
    Voice()
{
    // If these are valid, assign members
	if ( pClip && ID >= 0 )
	{
		m_iUniqueID = ID;
		m_pClip = pClip;
		m_uTriggerRes = uTriggerRes;
		m_fVolume = fVolume;
		m_eState = bLoop ? EState::Pending : EState::OneShot;
	}
}

// Construct with a command
Voice::Voice (const SoundManager::Command cmd ) :
    Voice()
{
    // Save some words
    using ECommandID = SoundManager::ECommandID;

    // Check the validity of the command
    if (cmd.eID == ECommandID::StartLoop || cmd.eID == ECommandID::OneShot)
    {
        if (cmd.pClip && cmd.eID != ECommandID::None)
        {
            m_iUniqueID = cmd.iData;
            m_pClip = cmd.pClip;
            m_uTriggerRes = cmd.uData;
            m_fVolume = cmd.fData;
            m_eState = cmd.eID == ECommandID::StartLoop ? EState::Pending : EState::OneShot;
        }
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

// Handle the transition to stopping appropriately
void Voice::SetStopping( const size_t uTriggerRes )
{
	EState eNextState = m_eState;
	switch ( m_eState )
	{
		// If we're pending, just stop and get out
		case EState::Pending:
        case EState::OneShot:
			eNextState = EState::Stopped;
			break;

		// If we're TailPending, set to Tail and get out
		case EState::TailPending:
        case EState::TailOneShot:
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

    // If we made it down here, advance the 
	setState( eNextState );
	m_uTriggerRes = uTriggerRes;
}

void Voice::SetPending(const size_t uTriggerRes, bool bLoop /*= false*/)
{
	EState eNextState = m_eState;
    switch(m_eState){
        // If they are resetting the pending state, 
        // update state and trigger res
        case EState::Pending:
			if ( bLoop == false )
				eNextState = EState::OneShot;
        case EState::TailPending:
			if ( bLoop == false )
				eNextState = EState::TailOneShot;
        case EState::OneShot:
			if ( bLoop )
				eNextState = EState::Pending;
        case EState::TailOneShot:
			if ( bLoop )
				eNextState = EState::TailOneShot;
            break;

        // If we are playing in some capacity, get out
        case EState::Starting:
        case EState::Looping:
		case EState::Stopping:
			//std::cout << "Voice " << m_pClip->GetName() << " " << m_iUniqueID << " is already playing!" << std::endl;
            return;

        // If we're tailing or stopped, set the appropriate pending state
        case EState::Tail:
			eNextState = (bLoop ? EState::TailPending : EState::TailOneShot);
            break;
        case EState::Stopped:
			eNextState = (bLoop ? EState::Pending : EState::OneShot);
            break;
    }

    // If we made it here, update state and
    // trigger res, reset the starting pos to 0
    setState(eNextState);
    m_uTriggerRes = uTriggerRes;
    m_uStartingPos = 0;
}

void Voice::SetVolume( float fVol )
{
	m_fVolume = std::max( 0.f, std::min( fVol, 1.f ) );
}

// Update prevState and assign state
void Voice::setState( EState eNextState )
{
	m_ePrevState = m_eState;
	m_eState = eNextState;
}

// Render audio samples to mix buffer
void Voice::RenderData( float * const pMixBuffer, const size_t uSamplesDesired, const size_t uSamplePos )
{
	// Possible early out
	if ( m_eState == EState::Stopped || pMixBuffer == nullptr || m_pClip == nullptr || m_fVolume <= 0.f )
		return;

	// Get what we need from the clip
    const size_t uTotalSampleCount = m_pClip->GetNumSamples( true );
	const size_t uSamplesInHead = m_pClip->GetNumSamples( false );
	const size_t uSamplesInTail = uTotalSampleCount - uSamplesInHead;
	const size_t uFadeSamples = m_pClip->GetNumFadeSamples();
	const size_t uFadeBegin = uSamplesInHead - uFadeSamples;
	const float const * pAudioData = m_pClip->GetAudioData();

	// Just another early out check
	if ( uSamplesInHead == 0 || pAudioData == nullptr )
		return;

	// While there are samples left to add (and the # of this while loop's iterations)
	size_t uSamplesAdded( 0 );
	while ( uSamplesAdded < uSamplesDesired )
	{
		// The current sample pos, # left to add
		const size_t uCurrentSamplePos = uSamplesAdded + uSamplePos;
		const size_t uSamplesLeftToAdd = uSamplesDesired - uSamplesAdded;

        // Calculate the offset sample position, which is the current sample position
        // offset by whatever our starting position was (for getting pos within buffer)
		size_t uOffsetPos( 0 );
		if ( uCurrentSamplePos < m_uStartingPos )
			uOffsetPos = (uSamplesInHead - m_uStartingPos) + uCurrentSamplePos;
		else
			uOffsetPos = uCurrentSamplePos - m_uStartingPos;

		// Now that we have the offset sample position, 
		// compute the position within our buf
		size_t uFirstHeadSample = uOffsetPos % uSamplesInHead;

		// Where the last sample would be, ignoring the size of our head buffer
		const size_t uTentativeLastSample = uFirstHeadSample + uSamplesLeftToAdd;

		// Boundaries for the head loops, clamped by what we can actually add
		size_t uLastHeadSample = std::min( uTentativeLastSample, uFadeBegin );
		size_t uLastFadeoutToBegin = std::min( uTentativeLastSample, uSamplesInHead );

        // Boundaries for adding the tail
        size_t uFirstTailSample(0), uLastTailSample(0);
        if ( m_uLastTailSampleAdded < uTotalSampleCount )
        {
            // Enter this code path if we have tail samples yet to render
            uFirstTailSample = m_uLastTailSampleAdded;
            uLastTailSample = std::min( uFirstTailSample + uSamplesLeftToAdd, uTotalSampleCount );

			// Update the last tail sample added
			if ( uLastTailSample > uFirstTailSample )
				m_uLastTailSampleAdded += (uLastTailSample - uFirstTailSample);
        }
        else if (m_eState == EState::Looping)
        {
            // Enter this code path if we are looping, which renders tail samples on top of head samples
            uFirstTailSample = std::min( uSamplesInHead + uFirstHeadSample, uTotalSampleCount );
            uLastTailSample = std::min( uSamplesInHead + uTentativeLastSample, uTotalSampleCount );
        }

		// Quantities used for trigger states like pending, stopping (note that we use uCurrentSamplePos)
		const size_t uPosAlongTrigger = m_uTriggerRes ? uCurrentSamplePos % m_uTriggerRes : 0;
		const size_t uSamplesLeftTillTrigger = m_uTriggerRes - uPosAlongTrigger;

		// The state we may switch to after this loop
		EState eNextState = m_eState;

		// The fade target, which depends on the state
		float fTargetVal( 0 );

		// Before adding any head samples, cache a pointer to the destination of the first tail sample
		float * pFirstTailMixSample = &pMixBuffer[uSamplesAdded];

		// We make a note of whether or not the tail and head will overlap
		bool bTailHeadOverlap = false;

		switch ( m_eState )
		{
			// We're pending, and we might also be mixing in the tail
			case EState::OneShot:
            case EState::TailOneShot:
			case EState::Pending:
			case EState::TailPending:
				// If we'll hit the trigger resolution
				if ( uSamplesLeftTillTrigger < uSamplesLeftToAdd )
				{
					// Set the starting position to the current sample idx
					m_uStartingPos = (uCurrentSamplePos + uSamplesLeftTillTrigger) % uSamplesInHead;

					// If we're still in a tail state, there's an overlap
					bTailHeadOverlap = ( m_eState == EState::TailOneShot || m_eState == EState::TailPending );

					if ( bTailHeadOverlap )
					{
						// Update uSamplesAdded/uPosInBuf to the sample before the head and tail intersect
						uSamplesAdded += std::min( uSamplesLeftTillTrigger, uSamplesLeftToAdd );
						uFirstHeadSample = (uFirstHeadSample + uSamplesLeftTillTrigger) % uSamplesInHead;

						// We're going to switch to either starting(pending) or stopping(oneshot)
						eNextState = (m_eState == EState::TailPending ? EState::Starting : EState::Stopping);
					}
					else
					{
						// Otherwise jump to the next iteration, advancing state and sample counter
						uSamplesAdded += uSamplesLeftTillTrigger;

						// We're going to switch to either starting(pending) or stopping(oneshot)
						setState( m_eState == EState::Pending ? EState::Starting : EState::Stopping );
						continue;
					}
				}

				// If we aren't doing any sort of tail, we have nothing to do
				if ( m_eState == EState::Pending || m_eState == EState::OneShot )
					return;

				// If we're currently mixing the tail, and we didn't overlap with the head, jump down
				if ( bTailHeadOverlap == false )
					goto Case_Tail;

				// Otherwise start mixing
				break;

				// Fade up from zero until we hit our fade duration,
				// render only the head until we hit the fade-out,
				// then fade out to (head+tail)[0]
			case EState::Starting:
				// If we're still within the initial fade up
				if ( uFirstHeadSample < uFadeSamples )
				{
					// Fade up from zero (this is the only loop of it's kind, so just do it here
					const size_t uLastFadeFromZero = std::min( uTentativeLastSample, uFadeSamples );
					for ( ; uFirstHeadSample < uLastFadeFromZero; uFirstHeadSample++ )
					{
						float fSampleVal = m_fVolume * pAudioData[uFirstHeadSample];
						pMixBuffer[uSamplesAdded++] += remap( uFirstHeadSample, 0, uFadeSamples, 0.f, fSampleVal );
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
                    {
                        if (uSamplesInTail > 0){
                            eNextState = EState::Tail;
                            m_uLastTailSampleAdded = uSamplesInHead;
                        }
                        else
                            eNextState = EState::Stopped;
                    }

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
            // This extra case is to handle 
            // TailPending and TailOneShot
			Case_Tail:
				if ( bTailHeadOverlap == false )
				{
					// If we're in a pure tail state, manually advance the number 
					// of samples added, since the tail loop doesn't do that 
					// (shitty, I know, but if we don't then we'll loop forever)
					uSamplesAdded += uLastTailSample - uFirstTailSample;

					// If we'll hit the end of the tail
					if ( uLastTailSample == uTotalSampleCount )
					{
						// A real tail means we're stopped
						if ( m_eState == EState::Tail )
							eNextState = EState::Stopped;
						// A pending tail means we're either pending or about to start
						else if ( m_eState == EState::TailPending && eNextState != EState::Starting )
							eNextState = EState::Pending;
						// Similar situation for oneshot
						else if ( m_eState == EState::TailOneShot && eNextState != EState::Stopping )
							eNextState = EState::OneShot;
					}

					// Make sure these loops don't get hit
					uLastHeadSample = 0;
					uLastFadeoutToBegin = 0;
				}

                break;

                // We shouldn't be doing anything
			case EState::Stopped:
				return;
		}

		// Mix in head samples before fade
		for ( size_t uHeadIdx = uFirstHeadSample; uHeadIdx < uLastHeadSample; uHeadIdx++ )
		{
			float fSampleVal = m_fVolume * pAudioData[uHeadIdx];
			pMixBuffer[uSamplesAdded++] += fSampleVal;
		}

		// Fade out to target sample, starting at last added above
		for ( size_t uFadeIdx = uLastHeadSample; uFadeIdx < uLastFadeoutToBegin; uFadeIdx++ )
		{
			// Fade the current sample to the target val
			float fSampleVal = m_fVolume * pAudioData[uFadeIdx];
			pMixBuffer[uSamplesAdded++] += remap( uFadeIdx, uFadeBegin, uSamplesInHead, fSampleVal, fTargetVal );
		}

		// Add the tail samples
        for ( size_t uTailIdx = uFirstTailSample; uTailIdx < uLastTailSample; uTailIdx++)
		{
			float fSampleVal = m_fVolume * pAudioData[uTailIdx];
			*pFirstTailMixSample++ += fSampleVal;
		}

		// Update state
		if ( eNextState != m_eState )
			setState( eNextState );
	}

	return;
}
