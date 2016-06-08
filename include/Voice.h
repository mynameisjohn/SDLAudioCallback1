#pragma once

class Clip;

// I include this because I want to be able to construct
// from a command
#include "SoundManager.h"

class Voice
{
    // Initializing constructor is private
    Voice();

public:
	// My attempt at a state pattern... very much a work in progress
	enum class EState : int
	{
		Pending = 0,							// Start playing when the trigger res is hit
		OneShot,                                // Play once when the trigger res is hit
		Starting,								// Play the head once and switch to looping on loop back
		Looping,								// Play the head and tail mixed until set to stopping
		Stopping,								// Acts like Starting or Looping, and on loop back plays tail only
		Tail,									// Play the tail only before transitioning to stopped
		TailPending,							// We've been set to start once the tail ends
        TailOneShot,                            // Same as above, but switch to oneshot
		Stopped									// Renders no samples to buffer
	};

    // Construct with pointer to actual audio clip, trigger res, initial volume, and loop bool
	Voice( const Clip const * pClip, int ID, size_t uTriggerRes, float fVolume, bool bLoop = false );

    // Construct with soundmanager command
    Voice( const SoundManager::Command cmd );

	// Possibly copy uSamplesDesired of float sampels into pMixBuffer
	void RenderData( float * const pMixBuffer, const size_t uSamplesDesired, const size_t uSamplePos );

    // Various gets
	EState GetState() const;
	EState GetPrevState() const;
	float GetVolume() const;
	int GetID() const;

    // Set the voice to start/stop at the trigger res
	void SetStopping( const size_t uTriggerRes );
    void SetPending( const size_t uTriggerRes, bool bLoop = false );

    // Set the volume
	void SetVolume( const float fVol );

private:
	int m_iUniqueID;                                // The voice identifier
	EState m_eState;								// One of the above, determines where samples come from
	EState m_ePrevState;							// The previous state, used to control transitions
	float m_fVolume;                                // Volume
	size_t m_uTriggerRes;                           // When actions like starting and stopping occur
	size_t m_uStartingPos;                          // Cached sample pos of when we started
    size_t m_uLastTailSampleAdded;                  // Cached pos of the last tail sample added
	Clip const * m_pClip;                           // Pointer to the clip

    // Internal function to set the state/prevState
	void setState ( EState eNextState );
};
