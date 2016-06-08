#include "SoundManager.h"
#include "Clip.h"
#include "Voice.h"

#include <algorithm>
#include <iostream>

SoundManager::SoundManager():
	m_bPlaying( false ),
	m_uSamplePos( 0 ),
	m_uMaxSampleCount( 0 ),
	m_uNumBufsCompleted( 0 )
{
}

SoundManager::SoundManager( SDL_AudioSpec sdlAudioSpec ) :
	m_uSamplePos( 0 ),
	m_uMaxSampleCount( 0 ),
	m_AudioSpec( sdlAudioSpec )
{
	m_AudioSpec.userdata = nullptr;
}

SoundManager::~SoundManager()
{
	if ( m_AudioSpec.userdata )
	{
		SDL_CloseAudio();
		memset( &m_AudioSpec, 0, sizeof( SDL_AudioSpec ) );
	}
}

bool SoundManager::RegisterClip( std::string strLoopName, std::string strHeadFile, std::string strTailFile, size_t uFadeDurationMS )
{
	// This shouldn't be happening at a bad time, but just in case
	std::lock_guard<std::mutex> lg( m_muAudioMutex );

	// If we already have this clip stored, return true
	if ( m_mapClips.find( strLoopName ) != m_mapClips.end() )
		return true;

	float * pSoundBuffer( nullptr );
	Uint32 uNumBytesInHead( 0 );
	float * pTailBuffer( nullptr );
	Uint32 uNumBytesInTail( 0 );
	SDL_AudioSpec wavSpec{ 0 }, refSpec = m_AudioSpec;
	auto checkAudioSpec = [refSpec, &wavSpec] ()
	{
		return (refSpec.freq == wavSpec.freq &&
				 refSpec.format == wavSpec.format &&
				 refSpec.channels == wavSpec.channels &&
				 refSpec.samples == wavSpec.samples);
	};
	if ( SDL_LoadWAV( strHeadFile.c_str(), &wavSpec, (Uint8 **) &pSoundBuffer, &uNumBytesInHead ) )
	{
		if ( checkAudioSpec() )
		{
			if ( SDL_LoadWAV( strTailFile.c_str(), &wavSpec, (Uint8 **) &pTailBuffer, &uNumBytesInTail ) )
			{
				if ( checkAudioSpec() == false )
				{
					pTailBuffer = nullptr;
					uNumBytesInTail = 0;
				}
			}

			const size_t uNumSamplesInHead = uNumBytesInHead / sizeof( float );
			const size_t uNumSamplesInTail = uNumBytesInTail / sizeof( float );
			m_uMaxSampleCount = std::max( m_uMaxSampleCount, uNumSamplesInHead );
			m_mapClips[strLoopName] = Clip( strLoopName, pSoundBuffer, uNumSamplesInHead, pTailBuffer, uNumSamplesInTail, uFadeDurationMS );
			return true;
		}
	}

	return false;
}

// Add a message-wrapped task to the queue (locks mutex)
bool SoundManager::SendMessage( Message M )
{
	Command cmd = translateMessage( M );
	if ( cmd.eID == ECommandID::None )
		return false;

	std::lock_guard<std::mutex> lg( m_muAudioMutex );
	m_liPublicCmdQueue.push_back( cmd );

	return true;
}

// Adds several message-wrapped tasks to the queue (locks mutex once)
bool SoundManager::SendMessages( std::list<Message> liM )
{
	if ( liM.empty() )
		return false;

	bool ret( true );
	std::list<Command> liNewTasks;
	for ( auto& m : liM )
	{
		Command cmd = translateMessage( m );
		if ( cmd.eID != ECommandID::None )
			liNewTasks.push_back( cmd );
		else
			ret = false;
	}

	if ( liNewTasks.empty() )
		return false;

	std::lock_guard<std::mutex> lg( m_muAudioMutex );
	m_liPublicCmdQueue.splice( m_liPublicCmdQueue.end(), liNewTasks );

	return ret;
}

SoundManager::Command SoundManager::translateMessage( Message& M )
{
	// The int is the command ID, the data is the second component
	ECommandID eCommandID = (ECommandID) std::get<0>( M );
	pyl::Object& pylObj = std::get<1>( M );
	Command cmd;

	// Get data based on CMD ID
	// If a data conversion fails, the default task is 
	// returned (and its CMD ID is none)
	switch ( eCommandID )
	{
		case ECommandID::SetVolume:
		{
			std::tuple<std::string, int, float> setVolData;
			if ( pylObj.convert( setVolData ) == false )
				return cmd;
			
			cmd.pClip = &m_mapClips[std::get<0>( setVolData )];
			cmd.iData = std::get<1>( setVolData );
			cmd.fData = std::get<2>( setVolData );
			break;
		}
		case ECommandID::Start:
		case ECommandID::Stop:
			if ( pylObj.convert( cmd.uData ) == false )
				return cmd;
			break;

		case ECommandID::StartLoop:
		case ECommandID::StopLoop:
		case ECommandID::OneShot:
		{
			std::tuple<std::string, int, float, size_t> setPendingData;
			if ( pylObj.convert( setPendingData ) == false )
				return cmd;

			cmd.pClip = &m_mapClips[std::get<0>( setPendingData )];
			cmd.iData = std::get<1>( setPendingData );
			cmd.fData = std::get<2>( setPendingData );
			cmd.uData = std::get<3>( setPendingData );
			break;
		}
		default:
			return cmd;
	}

	cmd.eID = eCommandID;

	return cmd;
}

// Called by main thread, locks mutex
void SoundManager::incNumBufsCompleted()
{
	// We gotta lock this while we mess with the public queue
	std::lock_guard<std::mutex> lg( m_muAudioMutex );

	// Get out if empty
	if ( m_liPublicCmdQueue.empty() )
		return;

	// Grab the front, maybe inc buf count and pop
	Command tFront = m_liPublicCmdQueue.front();
	if ( tFront.eID == ECommandID::BufCompleted )
	{
		m_liPublicCmdQueue.pop_front();
		m_uNumBufsCompleted += tFront.uData;
	}
}

// Called by main thread
void SoundManager::Update()
{
	// Just see if the audio thread has left any
	// BufCompleted tasks for us
	incNumBufsCompleted();
}

// Called by audio thread, locks mutex
void SoundManager::updateTaskQueue()
{
	// Take any tasks the main thread has left us
	// and put them into our queue
	{
		std::lock_guard<std::mutex> lg( m_muAudioMutex );

		// We're going to leave the main thread a task indicating how many
		// buffers have completed since the last time it checked the queue
		Command tNumBufsCompleted;

		// Format the taks - we leave it with one buf
		tNumBufsCompleted.eID = ECommandID::BufCompleted;
		tNumBufsCompleted.uData = 1;

		// See if there's anything still in the public queue
		if ( m_liPublicCmdQueue.empty() == false )
		{
			// The first task might be a leftover numBufsCompleted
			// (happens if our buffer size is lower than the refresh rate)
			Command tFront = m_liPublicCmdQueue.front();
			if ( tFront.eID == ECommandID::BufCompleted )
			{
				// If it's a buf completed task, pop it off
				// and add its sample count to the one declared above
				tNumBufsCompleted.uData += tFront.uData;
				m_liPublicCmdQueue.pop_front();
			}

			// Take all other tasks and add them to our queue
			m_liAudioCmdQueue.splice( m_liAudioCmdQueue.end(), m_liPublicCmdQueue );
		}

		// The public queue is empty, leave it with the # of buffers completed
		m_liPublicCmdQueue = { tNumBufsCompleted };
	}

	// Remove any voices that have stopped
	m_liVoices.remove_if( [] ( const Voice& v ) { return v.GetState() == Voice::EState::Stopped; } );

	// Handle each task
	for ( Command cmd : m_liAudioCmdQueue )
	{
		// Find the voice associated with the command's ID - this is dumb, but easy
		auto prFindVoice = [cmd] ( const Voice& v ) { return v.GetID() == cmd.iData; };
		auto itVoice = std::find_if( m_liVoices.begin(), m_liVoices.end(), prFindVoice );

		// Handle the command
		switch ( cmd.eID )
		{
			// Start every loop
			case ECommandID::Start:
				for ( auto& itLoop : m_mapClips )
					m_liVoices.emplace_back( &itLoop.second, cmd.uData, cmd.fData, false );
            break;

			// Stop every loop
			case ECommandID::Stop:
				for ( Voice& v : m_liVoices )
					v.SetStopping( cmd.uData );
            break;

			// Start a specific loop
			case ECommandID::StartLoop:
			case ECommandID::OneShot:
                // If it isn't already there, construct the voice
				if ( itVoice == m_liVoices.end() )
					m_liVoices.emplace_back( cmd );
                // Otherwise try set the voice to pending
                else
                    itVoice->SetPending( cmd.uData, cmd.eID == ECommand::StartLoop );
            break;

			// Stop a specific loop
			case ECommandID::StopLoop:
				if ( itVoice != m_liVoices.end() )
					itVoice->SetStopping( cmd.uData );
            break;

			// Set the volume of a loop
			case ECommandID::SetVolume:
				if ( itVoice != m_liVoices.end() )
					itVoice->SetVolume( cmd.fData );
            break;

			// Uhhh
			case ECommandID::Pause:
			default:
				break;
		}
	}

	// The audio thread doesn't care about anything else
	// so clear the list
	m_liAudioCmdQueue.clear();
}

bool SoundManager::Configure( std::map<std::string, int> mapAudCfg )
{
	try
	{
		m_AudioSpec.freq = mapAudCfg.at( "freq" );
		m_AudioSpec.channels = mapAudCfg.at( "channels" );
		m_AudioSpec.samples = mapAudCfg.at( "bufSize" );

	}
	catch ( std::out_of_range )
	{
		memset( &m_AudioSpec, 0, sizeof( SDL_AudioSpec ) );
		return false;
	}

	m_AudioSpec.format = AUDIO_F32;
	m_AudioSpec.callback = (SDL_AudioCallback) SoundManager::FillAudio;
	m_AudioSpec.userdata = this;

	SDL_AudioSpec received;
	if ( SDL_OpenAudio( &m_AudioSpec, &received ) )
	{
		std::cout << "Error initializing SDL Audio" << std::endl;
		std::cout << SDL_GetError() << std::endl;
		memset( &m_AudioSpec, 0, sizeof( SDL_AudioSpec ) );
		return false;
	}

	m_bPlaying = false;

	return true;
}

// TODO Have this send a message telling all loops to fade to silence
// without blowing out sample position, and actually pause once that's done
bool SoundManager::PlayPause()
{
	// This gets set if configure is successful
	if ( m_AudioSpec.userdata == nullptr )
		return false;

	// Toggle audio playback (and bool)
	m_bPlaying = !m_bPlaying;

	if ( m_bPlaying )
		SDL_PauseAudio( 0 );
	else
		SDL_PauseAudio( 1 );

	return true;
}

size_t SoundManager::GetSampleRate() const
{
	return m_AudioSpec.freq;
}

size_t SoundManager::GetBufferSize() const
{
	return m_AudioSpec.samples;
}

size_t SoundManager::GetMaxSampleCount() const
{
	return m_uMaxSampleCount;
}

size_t SoundManager::GetNumBufsCompleted() const
{
	return m_uNumBufsCompleted;
}

size_t SoundManager::GetNumSamplesInClip( std::string strClipName, bool bTail /*= false*/ ) const
{
	auto it = m_mapClips.find( strClipName );
	if ( it != m_mapClips.end() )
		return it->second.GetNumSamples( bTail );
	return 0;
}

SDL_AudioSpec const * SoundManager::GetAudioSpecPtr() const
{
	return &m_AudioSpec;
}

// Called via the static fill_audi function
void SoundManager::fill_audio_impl( Uint8 * pStream, int nBytesToFill )
{
	// Don't do nothin if they gave us nothin
	if ( pStream == nullptr || nBytesToFill == 0 )
		return;

	// Silence no matter what
	memset( pStream, 0, nBytesToFill );

	// Get tasks from public thread and handle them
	// Also let them know a buffer is about to complete
	updateTaskQueue();

	// Nothing to do
	if ( m_liVoices.empty() )
		return;

	// The number of float samples we want
	const size_t uNumSamplesDesired = nBytesToFill / sizeof( float );

	// Fill audio data for each loop
	for ( Voice& v : m_liVoices )
		v.RenderData( (float *) pStream, uNumSamplesDesired, m_uSamplePos );

	// Update sample counter, reset if we went over
	m_uSamplePos += uNumSamplesDesired;
	if ( m_uSamplePos > m_uMaxSampleCount )
	{
		// Just do a mod
		m_uSamplePos %= m_uMaxSampleCount;
	}
}

// Static SDL audio callback function (each instance sets its own userdata to this, so I guess
// multiple instances are legit)
/*static*/ void SoundManager::FillAudio( void * pUserData, Uint8 * pStream, int nSamplesDesired )
{
	// livin on a prayer
	((SoundManager *) pUserData)->fill_audio_impl( pStream, nSamplesDesired );
}

// Expose the LM class and some functions
const std::string SoundManager::strModuleName = "pylSoundManager";
/*static*/ bool SoundManager::pylExpose()
{
	using pyl::ModuleDef;
	ModuleDef * pSoundManagerModDef = ModuleDef::CreateModuleDef<struct st_LMModule>( SoundManager::strModuleName );
	if ( pSoundManagerModDef == nullptr )
		return false;

	pSoundManagerModDef->RegisterClass<SoundManager>( "SoundManager" );

	AddMemFnToMod( SoundManager, RegisterClip, bool, pSoundManagerModDef, std::string, std::string, std::string, size_t );
	AddMemFnToMod( SoundManager, SendMessages, bool, pSoundManagerModDef, std::list<SoundManager::Message> );
	AddMemFnToMod( SoundManager, SendMessage, bool, pSoundManagerModDef, SoundManager::Message );
	AddMemFnToMod( SoundManager, Update, void, pSoundManagerModDef );
	AddMemFnToMod( SoundManager, GetSampleRate, size_t, pSoundManagerModDef );
	AddMemFnToMod( SoundManager, GetMaxSampleCount, size_t, pSoundManagerModDef );
	AddMemFnToMod( SoundManager, GetBufferSize, size_t, pSoundManagerModDef );
	AddMemFnToMod( SoundManager, GetNumBufsCompleted, size_t, pSoundManagerModDef );
	AddMemFnToMod( SoundManager, GetNumSamplesInClip, size_t, pSoundManagerModDef, std::string, bool );
	AddMemFnToMod( SoundManager, Configure, bool, pSoundManagerModDef, std::map<std::string, int> );
	AddMemFnToMod( SoundManager, PlayPause, bool, pSoundManagerModDef );

	pSoundManagerModDef->SetCustomModuleInit( [] ( pyl::Object obModule )
	{
		// Expose command enums into the module
		obModule.set_attr( "CMDSetVolume",	(int) ECommandID::SetVolume );
		obModule.set_attr( "CMDStart",		(int) ECommandID::Start );
		obModule.set_attr( "CMDStartLoop",	(int) ECommandID::StartLoop );
		obModule.set_attr( "CMDStop",		(int) ECommandID::Stop );
		obModule.set_attr( "CMDStopLoop",	(int) ECommandID::StopLoop );
		obModule.set_attr( "CMDPause",		(int) ECommandID::Pause );
		obModule.set_attr( "CMDOneShot",	(int) ECommandID::OneShot);
	} );

	return true;
}
