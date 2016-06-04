#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_assert.h>
#include <SDL_events.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include <iostream>
#include <stdexcept>
#include "Scene.h"

int main(int argc, char ** argv)
{
	// Try catch so that the scene gets properly destroyed
	try
	{
		// Initalize custom python modules and python
		Scene::pylExpose();
		pyl::initialize();

		// Initialize the scene with the driver script
		pyl::Object obDriverModule = pyl::Object::from_script( "../scripts/driver.py" );
		Scene S( obDriverModule );

		// Loop until the script sets the quit flag
		while ( S.GetQuitFlag() == false )
		{
			// Let python handle events
			SDL_Event e;
			while ( SDL_PollEvent( &e ) && S.GetQuitFlag() == false )
			{
				obDriverModule.call_function( "HandleEvent", &e );
			}

			// Update the scene, draw
			S.Update();
			S.Draw();
		}

		return EXIT_SUCCESS;
	}
	catch ( std::runtime_error )
	{
		// This is meant to catch python errors
		return EXIT_FAILURE;
	}
}