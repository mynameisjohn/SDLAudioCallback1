#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_assert.h>

#include "LoopManager.h"
#include <iostream>

int main(int argc, char ** argv)
{
	// Create audio spec and construct loopmanager
	// This could be initialized by python
	SDL_AudioSpec sdlAudioSpec{ 0 };
	sdlAudioSpec.freq = 44100;
	sdlAudioSpec.format = AUDIO_F32;
	sdlAudioSpec.channels = 1;
	sdlAudioSpec.samples = 4096;
	sdlAudioSpec.callback = (SDL_AudioCallback) LoopManager::FillAudio;
	LoopManager lm( sdlAudioSpec );

	// Expose various LoopManager things to python, init python and lm module
	LoopManager::pylExpose();
	pyl::initialize();
	LoopManager::pylInit();
	
	// Load up the driver module and init the loopmanager
	pyl::Object obDriverModule = pyl::Object::from_script( "../scripts/driver.py" );
	obDriverModule.call_function( "InitLoopManager", &lm );

	// Try and init SDL Audio with the audio spec
	if ( SDL_OpenAudio( (SDL_AudioSpec *) lm.GetAudioSpecPtr(), &sdlAudioSpec ) )
	{
		std::cout << "Error initializing SDL Audio" << std::endl;
		std::cout << SDL_GetError() << std::endl;
		return -1;
	}

	// Start the plyaback
	SDL_PauseAudio( 0 );

	// This condition should be event based
	while ( true )
	{
		// Every 10 ms call Update
		SDL_Delay( 10 );
		lm.Update( obDriverModule );
	}

	// Teardown and get out
	SDL_CloseAudio();
	return 0;
}