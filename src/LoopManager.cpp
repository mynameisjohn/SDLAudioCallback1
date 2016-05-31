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
	m_uSamplePos( 0 ),
	m_uMaxSampleCount( 0 )
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

// Kind of an expensive function at the moment; 
// Takes a reference to the driver script
bool LoopManager::GetNumBuffersCompleted( size_t * pNumBufs )
{
	if ( pNumBufs == nullptr )
		return false;

	// We gotta lock this while we mess with the public queue
	std::lock_guard<std::mutex> lg( m_muAudioMutex );

	// Get out if empty
	if ( m_liPublicTaskQueue.empty() )
		return false;

	bool bRet = false;

	*pNumBufs += std::count_if( m_liPublicTaskQueue.begin(), m_liPublicTaskQueue.end(), [] ( const Task T )
	{
		return T.eCmdID == Command::BufCompleted;
	} );

	m_liPublicTaskQueue.erase( std::remove_if( m_liPublicTaskQueue.begin(), m_liPublicTaskQueue.end(), prIsTaskFromAudioThread() ), m_liPublicTaskQueue.end() );

	return *pNumBufs > 0;

	//// Take any tasks the audio thread has left us
	//// and put them in a new list on the stack
	//std::list<Task> liNewTaskList;
	//{


	//	// This is actually kind of expensive... but whatever
	//	auto itFromAudThread = std::remove_if( m_liPublicTaskQueue.begin(), m_liPublicTaskQueue.end(), prIsTaskFromAudioThread() );
	//	liNewTaskList.splice( liNewTaskList.end(), m_liPublicTaskQueue, itFromAudThread, m_liPublicTaskQueue.end() );
	//}

	//// This could be a class member, but I feel like that's python's job
	//// The only concern is if it's out of date... anyway
	//// Send python the number of loops that have occurred (based on LongestLoopCompleted tasks)
	//// As well as a set of loops that have just started
	//// I think that's pretty dumb, what I'd like to do is send current sample pos and figure out
	//// what to queue up based on that, but right now this was easy
	//size_t uLoopsCompleted( 0 );
	//std::set<std::string> setLoopsStarted;
	//
	//// Handle each new task
	//for ( auto T : liNewTaskList )
	//{
	//	switch ( T.eCmdID )
	//	{
	//		case Command::LoopLaunched:
	//			if ( setLoopsStarted.count( T.pLoop->GetName() ) )
	//				throw std::runtime_error( "More than one loop passed!" );
	//			setLoopsStarted.insert( T.pLoop->GetName() );
	//			break;
	//		case Command::LongestLoopCompleted:
	//			uLoopsCompleted++;
	//			break;
	//		default:
	//			break;
	//	}
	//}

	//// Let the python script know what's going on
	//obDriverScript.call_function( "Update", this, uLoopsCompleted, setLoopsStarted );
}

// Called by audio thread
void LoopManager::updateTaskQueue()
{
	// Take any tasks the main thread has left us
	// and put them into our queue
	{
		std::lock_guard<std::mutex> lg( m_muAudioMutex );
		
		// What we're left with should be meant for us, no?
		if ( m_liPublicTaskQueue.empty() == false )
		{
			// Move all messages from main thread to itFromMainThread and splice (this is O(n), btw...)
			auto itFromMainThread = std::remove_if( m_liPublicTaskQueue.begin(), m_liPublicTaskQueue.end(), std::not1( prIsTaskFromAudioThread() ) );
			m_liAudioTaskQueue.splice( m_liAudioTaskQueue.end(), m_liPublicTaskQueue, itFromMainThread, m_liPublicTaskQueue.end() );
		}

		// Take any tasks meant for the main thread and put them
		// into the public queue
		if ( m_liAudioTaskQueue.empty() == false )
		{
			auto itFromAudThread = std::remove_if( m_liAudioTaskQueue.begin(), m_liAudioTaskQueue.end(), prIsTaskFromAudioThread() );
			m_liPublicTaskQueue.splice( m_liPublicTaskQueue.end(), m_liAudioTaskQueue, itFromAudThread, m_liAudioTaskQueue.end() );
		}

		// Task indicating that buffer completed
		Task bufCompletedTask;
		bufCompletedTask.eCmdID = Command::BufCompleted;
		m_liPublicTaskQueue.push_back( bufCompletedTask );
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
	updateTaskQueue();

	// The number of float samples we want
	const size_t uNumSamplesDesired = nBytesToFill / sizeof( float );

	// For every loop
	for ( auto& itLoop : m_mapLoops )
	{
		// Get audio data, 
		// detect if any loops are going from pending to starting this iteration
		Loop& l = itLoop.second;
		l.GetData( (float *) pStream, uNumSamplesDesired, m_uSamplePos );

		//// We may want to post a message indicating that this loop has started over - first check boundaries
		//bool bPostMessage = ((m_uSamplePos % l.GetNumSamples()) + uNumSamplesDesired > l.GetNumSamples());

		//Loop::State eInitialState = l.GetState();
		//l.GetData( (float *) pStream, uNumSamplesDesired, m_uSamplePos );
		//Loop::State eFinalState = l.GetState();

		//// We'd also like to post a message if it just went from pending to starting, but not if it's stopped or tailing now
		//bPostMessage = bPostMessage || (eInitialState == Loop::State::Pending && eFinalState == Loop::State::Starting);
		//bPostMessage = bPostMessage && (l.GetState() != Loop::State::Stopped && l.GetState() != Loop::State::Tail);

		//// Create the task, it will make it back to the public queue next call
		//if ( bPostMessage )
		//{
		//	Task T;
		//	T.pLoop = &l;
		//	T.eCmdID = Command::LoopLaunched;
		//	m_liAudioTaskQueue.push_back( T );
		//}
	}

	// Update sample counter, reset if we went over
	m_uSamplePos += uNumSamplesDesired;
	if ( m_uSamplePos > m_uMaxSampleCount )
	{
		// Just do a mod
		m_uSamplePos %= m_uMaxSampleCount;

		// We also want to post a message indicating that the 
		// longest loop has completed... unfortunately this won't be posted
		// until the next call to updateTaskQueue
		//Task T;
		//T.eCmdID = Command::LongestLoopCompleted;
		//m_liAudioTaskQueue.push_back( T );
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

	return true;
}

bool LoopManager::Start()
{
	if ( m_AudioSpec.userdata == nullptr )
		return false;

	SDL_AudioSpec received;
	if ( SDL_OpenAudio( &m_AudioSpec, &received ) )
	{
		std::cout << "Error initializing SDL Audio" << std::endl;
		std::cout << SDL_GetError() << std::endl;
		return false;
	}

	SDL_PauseAudio( 0 );

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
	AddMemFnToMod( LoopManager, GetSampleRate, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetMaxSampleCount, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetBufferSize, size_t, pLoopManagerDef );
	AddMemFnToMod( LoopManager, GetLoop, Loop *, pLoopManagerDef, std::string );
	AddMemFnToMod( LoopManager, Configure, bool, pLoopManagerDef, std::map<std::string, int> );
	AddMemFnToMod( LoopManager, Start, bool, pLoopManagerDef );

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