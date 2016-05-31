#pragma once

#include <SDL.h>
#include <SDL_audio.h>

#include <map>
#include <mutex>
#include <list>
#include <vector>

#include <pyliason.h>

#include "Loop.h"

// Loopmanager class, owns loops and fields
// requests for what to play. Has a member function
// used as the SDL audo callback (called on a separate thread)
class LoopManager
{
public:
	// Command types used to control loop playback
	enum class Command : int	
	{
		// These commands are sent to the audio thread
		// and control loop playback
		None = 0,
		SetVolume,
		Start,
		StartLoop,
		Pause,
		Stop,
		StopLoop,
////////////////////////////
		// These commands are sent from the audio thread to
		// anyone who cares, used right now to queue up
		// new loops (which is a pain in the ass)
		//LoopLaunched,
		//LongestLoopCompleted
		BufCompleted
	};

	// Generic tasks object used to queue actions
	// until it is convenient to execute them. 
	// Depending on the eCmdID field, the task
	// means different things (hence the union)
	struct Task
	{
		Command eCmdID{ Command::None };
		Loop * pLoop{ nullptr };
		union
		{
			size_t uData;	// Usually for sample positions
			float fData;	// Volume
		} U{ 0 };
	};
	
	// A message sent from python, the int is a Command enum (hopefully)
	using Message = std::tuple<int, pyl::Object>;
private:
	SDL_AudioSpec m_AudioSpec;				// Audio spec, describes loop format
	size_t m_uSamplePos;					// Current sample pos in playback
	size_t m_uMaxSampleCount;				// Sample count of longest loop
	std::map<std::string, Loop> m_mapLoops;	// Loop storage, right now the map is a convenience

	std::mutex m_muAudioMutex;				// Mutex controlling communication between audio and main threads
	std::list<Task> m_liPublicTaskQueue;	// Anyone can put tasks here, will be read by audio thread
	std::list<Task> m_liAudioTaskQueue;		// Audio thread's tasks, only modified by audio thread

	// The actual callback function used to fill audio buffers
	void fill_audio_impl( Uint8 * pStream, int nBytesToFill );

	// Called internally to pass messages between audio and main thread
	void updateTaskQueue();

	// Turn a message into something useful
	Task translateMessage( Message& M );
public:
	LoopManager();
	~LoopManager();

	// Constructor takes audio spec used to load loops
	LoopManager( SDL_AudioSpec sdlAudioSpec );

	// Called periodically to pump python script
	bool GetNumBuffersCompleted( size_t * pNumBufs );

	bool Configure( std::map<std::string, int> mapAudCfg );
	bool Start();

	// Various gets
	size_t GetMaxSampleCount() const;
	size_t GetSampleRate() const;
	size_t GetBufferSize() const;
	Loop * GetLoop( std::string strLoopName ) const;

	// Called from python to add tasks to public queue
	bool SendMessage( Message M );
	bool SendMessages( std::list<Message> liM );

	// Add a loop to storage
	bool AddLoop( std::string strLoopName, std::string strHeadFile, std::string strTailFile, size_t uFadeDurationMS, float fVol );
	
	// Get the audio spec ptr
	const SDL_AudioSpec * GetAudioSpecPtr() const;

	// Static callback function, invokes fill_audio_impl
	static void FillAudio( void * pUserData, Uint8 * pStream, int nSamplesDesired );

	// PYL stuff
	static const std::string strModuleName;
	static bool pylExpose();
};