#include "LoopManager.h"
#include <algorithm>
#include <iostream>

// Predicate used to determine whether or not a task is coming from
// the audio thread (based on the enum...)
struct prIsTaskFromAudioThread
{
	// I know all this is unnecessary, but I get off on the functional stuff
	inline bool operator()( const LoopManager::Task T ) const
	{
		switch ( T.eCmdID )
		{
			case LoopManager::Command::BufCompleted:
				return true;

		}
		return false;
	}
	// used by std::not1
	using argument_type = LoopManager::Task;
};

LoopManager::LoopManager() :
	m_bPlaying( false ),
	m_uSamplePos( 0 ),
	m_uMaxSampleCount( 0 ),
	m_uNumBufsCompleted( 0 )
{
}

LoopManager::LoopManager( SDL_AudioSpec sdlAudioSpec ) :
	m_uSamplePos( 0 ),
	m_uMaxSampleCount( 0 ),
	m_AudioSpec( sdlAudioSpec )
{
	m_AudioSpec.userdata = nullptr;
}

LoopManager::~LoopManager()
{
	if ( m_AudioSpec.userdata )
	{
		SDL_CloseAudio();
		memset( &m_AudioSpec, 0, sizeof( SDL_AudioSpec ) );
	}
}

bool LoopManager::AddLoop( std::string strLoopName, std::string strHeadFile, std::string strTailFile, size_t uFadeDurationMS, float fVol )
{
	// This shouldn't be happening at a bad time, but just in case
	std::lock_guard<std::mutex> lg( m_muAudioMutex );

	if ( m_mapLoops.find( strLoopName ) != m_mapLoops.end() )
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
			m_mapLoops.emplace( strLoopName, Loop( strLoopName, pSoundBuffer, uNumSamplesInHead, pTailBuffer, uNumSamplesInTail, uFadeDurationMS, fVol ) );
			return true;
		}
	}

	return false;
}

// Add a message-wrapped task to the queue (locks mutex)
bool LoopManager::SendMessage( Message M )
{
	Task T = translateMessage( M );
	if ( T.eCmdID != Command::None )
	{
		std::lock_guard<std::mutex> lg( m_muAudioMutex );
		m_liPublicTaskQueue.push_back( T );
		return true;
	}

	return false;
}

// Adds several message-wrapped tasks to the queue (locks mutex once)
bool LoopManager::SendMessages( std::list<Message> liM )
{
	if ( liM.empty() )
		return false;

	bool ret( true );
	std::list<Task> liNewTasks;
	for ( auto& m : liM )
	{
		Task T = translateMessage( m );
		if ( T.eCmdID != Command::None )
			liNewTasks.push_back( T );
		else
			ret = false;
	}

	if ( liNewTasks.empty() )
		return false;
	else
	{
		std::lock_guard<std::mutex> lg( m_muAudioMutex );
		m_liPublicTaskQueue.splice( m_liPublicTaskQueue.end(), liNewTasks );
	}
			
	return ret;
}

// Unpack a Message tuple and its data, based on the int CMD id
LoopManager::Task LoopManager::translateMessage( Message& M )
{
	// The int is the command ID, the data is the second component
	Command eCommandID = (Command) std::get<0>( M );
	pyl::Object& pylObj = std::get<1>( M );
	Task T;

	// Get data based on CMD ID
	// If a data conversion fails, the default task is 
	// returned (and its CMD ID is none)
	switch ( eCommandID )
	{
		case Command::SetVolume:
		{
			std::tuple<std::string, float> setVolTask;
			if ( pylObj.convert( setVolTask ) == false )
				return T;
			T.pLoop = &m_mapLoops[std::get<0>( setVolTask )];
			T.U.fData = std::get<1>( setVolTask );
		}
			break;

		case Command::Start:
		case Command::Stop:
			if ( pylObj.convert( T.U.uData ) == false )
				return T;
			break;

		case Command::StartLoop:
		case Command::StopLoop:
		{
			std::tuple<std::string, size_t> setPendingTask;
			if ( pylObj.convert( setPendingTask ) == false )
				return T;

			T.pLoop = &m_mapLoops[std::get<0>( setPendingTask )];
			T.U.uData = std::get<1>( setPendingTask );
		}
			break;

		default:
			return T;
	}

	T.eCmdID = eCommandID;
	
	return T;
}

// Called by main thread, locks mutex
void LoopManager::incNumBufsCompleted(){
    // We gotta lock this while we mess with the public queue
    std::lock_guard<std::mutex> lg( m_muAudioMutex );

    // Get out if empty
    if ( m_liPublicTaskQueue.empty() )
        return;

    // Grab the front, maybe inc buf count and pop
    Task tFront = m_liPublicTaskQueue.front();
    if (tFront.eCmdID == Command::BufCompleted){
        m_liPublicTaskQueue.pop_front();
        m_uNumBufsCompleted += tFront.U.uData;
    }
}

// Called by main thread
void LoopManager::Update()
{
    // Just see if the audio thread has left any
    // BufCompleted tasks for us
    incNumBufsCompleted();
}

// Called by audio thread, locks mutex
void LoopManager::updateTaskQueue()
{
	// Take any tasks the main thread has left us
	// and put them into our queue
	{
		std::lock_guard<std::mutex> lg( m_muAudioMutex );

        // We're going to leave the main thread a task indicating how many
        // buffers have completed since the last time it checked the queue
        Task tNumBufsCompleted;

        // Format the taks - we leave it with one buf
        tNumBufsCompleted.eCmdID = Command::BufCompleted;
        tNumBufsCompleted.U.uData = 1;

        // See if there's anything still in the public queue
		if ( m_liPublicTaskQueue.empty() == false )
        {
            // The first task might be a leftover numBufsCompleted
            // (happens if our buffer size is lower than the refresh rate)
            Task tFront = m_liPublicTaskQueue.front();
            if (tFront.eCmdID == Command::BufCompleted)
            {
                // If it's a buf completed task, pop it off
                // and add its sample count to the one declared above
                tNumBufsCompleted.U.uData += tFront.U.uData;
                m_liPublicTaskQueue.pop_front();
            }

            // Take all other tasks and add them to our queue
            m_liAudioTaskQueue.splice( m_liAudioTaskQueue.end(), m_liPublicTaskQueue );
        }

        // The public queue is empty, leave it with the # of buffers completed
        m_liPublicTaskQueue = {tNumBufsCompleted};
    }

	// Handle each task
	for ( Task T : m_liAudioTaskQueue )
	{
		switch ( T.eCmdID )
		{
			// Start every loop
			case Command::Start:
				for ( auto& itLoop : m_mapLoops )
					itLoop.second.SetPending( T.U.uData );
				break;
			// Stop every loop
			case Command::Stop:
				for ( auto& itLoop : m_mapLoops )
					itLoop.second.SetStopping( T.U.uData );
				break;
			// Start a specific loop
			case Command::StartLoop:
				T.pLoop->SetPending( T.U.uData );
				break;
			// Stop a specific loop
			case Command::StopLoop:
				T.pLoop->SetStopping( T.U.uData );
				break;
			// Set the volume of a loop
			case Command::SetVolume:
				T.pLoop->SetVolume( T.U.fData );
				break;
			// Uhhh
			case Command::Pause:
			default:
				break;
		}
	}

	// The audio thread doesn't care about anything else
	// so clear the list
	m_liAudioTaskQueue.clear();
}

// Called via the static fill_audi function
void LoopManager::fill_audio_impl( Uint8 * pStream, int nBytesToFill )
{
	// Don't do nothin if they gave us nothin
	if ( pStream == nullptr || nBytesToFill == 0 )
		return;

	// Silence no matter what
	memset( pStream, 0, nBytesToFill );

	// Nothing to do
	if ( m_mapLoops.empty() )
		return;

	// Get tasks from public thread and handle them
    // Also let them know a buffer is about to complete
	updateTaskQueue();

	// The number of float samples we want
	const size_t uNumSamplesDesired = nBytesToFill / sizeof( float );

	// Fill audio data for each loop
	for ( auto& itLoop : m_mapLoops )
		itLoop.second.GetData( (float *) pStream, uNumSamplesDesired, m_uSamplePos );

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
/*static*/ void LoopManager::FillAudio( void * pUserData, Uint8 * pStream, int nSamplesDesired )
{
	// livin on a prayer
	((LoopManager *) pUserData)->fill_audio_impl( pStream, nSamplesDesired );
}

const SDL_AudioSpec * LoopManager::GetAudioSpecPtr() const
{
	return (SDL_AudioSpec *) &m_AudioSpec;
}

size_t LoopManager::GetSampleRate() const
{
	return m_AudioSpec.freq;
}

size_t LoopManager::GetBufferSize() const
{
	return m_AudioSpec.samples;
}

size_t LoopManager::GetMaxSampleCount() const
{
	return m_uMaxSampleCount;
}

size_t LoopManager::GetNumBufsCompleted() const
{
    return m_uNumBufsCompleted;
}

Loop * LoopManager::GetLoop( std::string strLoopName ) const
{
	auto it = m_mapLoops.find( strLoopName );
	if ( it != m_mapLoops.end() )
		return (Loop *) &it->second;
	return nullptr;
}

bool LoopManager::Configure( std::map<std::string, int> mapAudCfg )
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
	m_AudioSpec.callback = (SDL_AudioCallback) LoopManager::FillAudio;
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
bool LoopManager::PlayPause()
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

// Expose the LM class and some functions
const std::string LoopManager::strModuleName = "pylLoopManager";
/*static*/ bool LoopManager::pylExpose()
{
	using pyl::ModuleDef;
	ModuleDef * pLoopManagerDef = ModuleDef::CreateModuleDef<struct st_LMModule>( LoopManager::strModuleName );
	if ( pLoopManagerDef == nullptr )
		return false;

	pLoopManagerDef->RegisterClass<LoopManager>( "LoopManager" );

	AddMemFnToMod( LoopManager, AddLoop, bool, pLoopManagerDef, std::string, std::string, std::string, size_t, float );
	AddMemFnToMod( LoopManager, SendMessages, bool, pLoopManagerDef, std::list<LoopManager::Message> );
	AddMemFnToMod( LoopManager, SendMessage, bool, pLoopManagerDef, LoopManager::Message );
	AddMemFnToMod( LoopManager, Update, void, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetSampleRate, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetMaxSampleCount, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetBufferSize, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetNumBufsCompleted, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetLoop, Loop *, pLoopManagerDef, std::string );
	AddMemFnToMod( LoopManager, Configure, bool, pLoopManagerDef, std::map<std::string, int> );
	AddMemFnToMod( LoopManager, PlayPause, bool, pLoopManagerDef );

	pLoopManagerDef->SetCustomModuleInit( [] ( pyl::Object obModule )
	{
		// Expose command enums into the module
		obModule.set_attr( "CMDSetVolume", (int) Command::SetVolume );
		obModule.set_attr( "CMDStart", (int) Command::Start );
		obModule.set_attr( "CMDStartLoop", (int) Command::StartLoop );
		obModule.set_attr( "CMDStop", (int) Command::Stop );
		obModule.set_attr( "CMDStopLoop", (int) Command::StopLoop );
		obModule.set_attr( "CMDPause", (int) Command::Pause );
	} );

	Loop::pylExpose();

	return true;
}
