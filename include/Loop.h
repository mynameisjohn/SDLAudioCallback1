#pragma once

#include <vector>
#include <string>

// Generic loop class, used to store audio buffers for loops
// The "Head" is the audio for the first playthrough of the loop
// The "Tail" is what you'd hear if the loop stopped playing
// The first playback is head only, every one after that is head+tail
// and tail when the loop stops
class Loop
{
public:
	// My attempt at a state pattern... very much a work in progress
	enum class State : int
	{
		Pending = 0,							// The loop manager will set us to Starting when it's longest loop loops back
		Starting,								// Play the head once and switch to looping on loop back
		Looping,								// Play the head and tail mixed until set to stopping
		Stopping,								// Acts like Starting or Looping, and on loop back plays tail only
		Tail,									// Play the remainder of the tail only before transitioning to stopped
		TailPending,							// We've been set to pending while tailing out
		Stopped									// Renders no samples to buffer
	};
private:
	State m_eState;								// One of the above, determines where samples come from
	State m_ePrevState;							// The previous state, used to control transitions
	float m_fVolume;							// A float multiplier for volume
	size_t m_uSamplesInHead;					// The number of samples in the head
	size_t m_uTriggerResolution;				// The resolution at which we a) start if pending or b) stop/tail if stopping
	size_t m_uStartingPos;
	size_t m_uFadeSamples;						// The target sample for the fade-out when stopping
	std::string m_strName;						// The name of the loop (this is never touched by aud thread)
	std::vector<float> m_vAudioBuffer;			// The vector storing the entire head and tail (with fades baked)

	// Used to control state transitions
	void setState( State eState );
public:
	Loop();
	Loop( const std::string strName,			// The friendly name of the loop
		  const float * const pHeadBuffer,		// The head buffer
		  const size_t uSamplesInHeadBuffer,	// and its sample count
		  const float * const pTailBuffer,		// The tail buffer
		  const size_t uSamplesInTailBuffer,	// and its sample count
		  const size_t uFadeDuration,			// The fade duration
		  const float fVolume );				// The loop's volume

	// Possibly copy uSamplesDesired of float sampels into pMixBuffer
	void GetData( float * const pMixBuffer, const size_t uSamplesDesired, const size_t uSamplePos );

	// Various gets
	float GetVolume() const;
	State GetState() const;
	std::string GetName() const;

	// Various sets
	void SetVolume( float fVol );
	void SetPending( size_t uTriggerRes );
	void SetStopping( size_t uTriggerRes );

	// The sample count of the head buffer
	size_t GetNumSamples( bool bTail = false ) const;

	// PYL stuff
	static const std::string strModuleName;
	static void pylExpose();
};

