#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_assert.h>
#include <SDL_events.h>

#include "LoopManager.h"
#include <iostream>

int main(int argc, char ** argv)
{
	// Create the SDL keys module for python
	pyl::ModuleDef * pSDLKeysModule = pyl::ModuleDef::CreateModuleDef<struct st_SDLK>( "pylSDLKeys" );
	if ( pSDLKeysModule == nullptr )
	{
		std::cout << "Error: Unable to create SDL keycode python module" << std::endl;
		pyl::print_error();
		return -1;
	}

	// Expose various LoopManager things to python, init python and lm module
	LoopManager::pylExpose();
	pyl::initialize();
	LoopManager::pylInit();

	// Add the key codes after initializing... wouldn't it be nice to
	// be able to do this using some sort of callback on init?
	pSDLKeysModule->AsObject().set_attr( "Num1", (int) SDLK_1 );
	pSDLKeysModule->AsObject().set_attr( "Num2", (int) SDLK_2 );
	pSDLKeysModule->AsObject().set_attr( "Num3", (int) SDLK_3 );

	// Create audio spec and construct loopmanager
	// This could be initialized by python
	SDL_AudioSpec sdlAudioSpec{ 0 };
	sdlAudioSpec.freq = 44100;
	sdlAudioSpec.format = AUDIO_F32;
	sdlAudioSpec.channels = 1;
	sdlAudioSpec.samples = 4096;
	sdlAudioSpec.callback = (SDL_AudioCallback) LoopManager::FillAudio;
	LoopManager lm( sdlAudioSpec );
	
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

	// Create Window (only used for keyboard input, as of now)
	SDL_Window * pWindow = SDL_CreateWindow( "3D Test",
								 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
								 800, 600,
								 SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN );
	if ( pWindow == NULL )
	{
		std::cout << "Window could not be created! SDL Error: " << SDL_GetError() << std::endl;
		return -1;
	}

	// Start the playback
	SDL_PauseAudio( 0 );

	// This condition should be event based
	bool bContinue = true;
	while ( bContinue )
	{
		SDL_Event e;
		while ( SDL_PollEvent( &e ) )
		{
			// We only care about the keyboard for now
			if ( e.type == SDL_KEYDOWN || e.type == SDL_KEYUP )
			{
				SDL_Keycode kc = e.key.keysym.sym;
				if ( kc == SDLK_ESCAPE )
				{
					bContinue = false;
					continue;
				}

				// These could be stored and sent in one go
				obDriverModule.call_function( "HandleKey", (int) kc, e.type == SDL_KEYDOWN );
			}
		}

		// Every 10 ms call Update
		SDL_Delay( 10 );
		lm.Update( obDriverModule );
	}

	// Teardown and get out
	SDL_CloseAudio();
	SDL_DestroyWindow( pWindow );

	return 0;
}