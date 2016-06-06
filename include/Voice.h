#pragma once

class Clip;

class Voice
{
public:
	// My attempt at a state pattern... very much a work in progress
	enum class EState : int
	{
		Pending = 0,							// The loop manager will set us to Starting when it's longest loop loops back
		OneShot,
		Starting,								// Play the head once and switch to looping on loop back
		Looping,								// Play the head and tail mixed until set to stopping
		Stopping,								// Acts like Starting or Looping, and on loop back plays tail only
		Tail,									// Play the remainder of the tail only before transitioning to stopped
		TailPending,							// We've been set to pending while tailing out
		Stopped									// Renders no samples to buffer
	};

	Voice( const Clip const * pClip, int ID, size_t uTriggerRes, float fVolume, bool bLoop = false );

	// Possibly copy uSamplesDesired of float sampels into pMixBuffer
	void RenderData( float * const pMixBuffer, const size_t uSamplesDesired, const size_t uSamplePos );
	EState GetState() const;
	EState GetPrevState() const;
	float GetVolume() const;
	void SetStopping( const size_t uTriggerRes );
	void SetVolume( const float fVol );
	int GetID() const;
private:
	int m_iUniqueID;
	EState m_eState;								// One of the above, determines where samples come from
	EState m_ePrevState;							// The previous state, used to control transitions
	float m_fVolume;
	size_t m_uTriggerRes;
	size_t m_uStartingPos;
	Clip const * m_pClip;

	void setState ( EState eNextState );
};