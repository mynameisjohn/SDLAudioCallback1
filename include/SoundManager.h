#pragma once

#include <SDL.h>
#include <SDL_audio.h>

#include <pyliason.h>

#include <string>
#include <map>
#include <list>
#include <mutex>
#include <stdint.h>

class Clip;
class Voice;
struct SDL_AudioSpec;

class SoundManager
{
public:
	enum class ECommandID : int
	{
		// These commands are sent to the audio thread
		None = 0,
		SetVolume,
		Start,
		StartLoop,
		Pause,
		Stop,
		StopLoop,
		OneShot,
		////////////////////////////
		// These commands are sent from the audio thread
		BufCompleted
	};
	
	struct Command
	{
		ECommandID eID{ ECommandID::None };
		Clip * pClip{ nullptr };
		int iData{ -1 };
		float fData{ 1.f };
		size_t uData{ 0 };
	};
	const int x = sizeof( Command );
	SoundManager();
	~SoundManager();

	// Constructor takes audio spec used to load loops
	SoundManager( SDL_AudioSpec sdlAudioSpec );

	// Called periodically to pump python script
	void Update();

	// Configure the audio device
	bool Configure( std::map<std::string, int> mapAudCfg );

	// Play / Pause the audio device
	bool PlayPause();

	// Various gets
	size_t GetMaxSampleCount() const;
	size_t GetSampleRate() const;
	size_t GetBufferSize() const;
	size_t GetNumBufsCompleted() const;
	size_t GetNumSamplesInClip( std::string strClipName, bool bTail ) const;
	SDL_AudioSpec const * GetAudioSpecPtr() const;

	// Add a clip to storage
	bool RegisterClip( std::string strClipName, std::string strHeadFile, std::string strTailFile, size_t uFadeDurationMS );

	// SDL Audio callback
	static void FillAudio( void * pUserData, Uint8 * pStream, int nSamplesDesired );

	// PYL stuff
	static const std::string strModuleName;
	static bool pylExpose();

	// A message sent from python, the int is a Command enum (hopefully)
	using Message = std::tuple<int, pyl::Object>;

	// Called from python to add tasks to public queue
	bool SendMessage( Message M );
	bool SendMessages( std::list<Message> liM );

private:
	bool m_bPlaying;						// Whether or not we are filling buffers of audio
	size_t m_uMaxSampleCount;				// Sample count of longest loop
	size_t m_uNumBufsCompleted;             // The number of buffers filled by the audio thread
	SDL_AudioSpec m_AudioSpec;				// Audio spec, describes loop format

	std::mutex m_muAudioMutex;				// Mutex controlling communication between audio and main threads
	size_t m_uSamplePos;					// Current sample pos in playback
	std::list<Command> m_liPublicCmdQueue;	// Anyone can put tasks here, will be read by audio thread
	std::list<Command> m_liAudioCmdQueue;	// Audio thread's tasks, only modified by audio thread
	std::map<std::string, Clip> m_mapClips;	// Clip storage, right now the map is a convenience
	std::list<Voice> m_liVoices;

	// The actual callback function used to fill audio buffers
	void fill_audio_impl( Uint8 * pStream, int nBytesToFill );

	// Called by audio thread to get messages from main thread
	void updateTaskQueue();

	// Called from Update to find out if we've filled some buffers
	void incNumBufsCompleted();

	// Turn a message into something useful
	Command translateMessage( Message& M );
};