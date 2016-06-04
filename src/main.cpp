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
	try
	{
		Scene::pylExpose();
		pyl::initialize();

		pyl::Object obDriverModule = pyl::Object::from_script( "../scripts/driver.py" );
		Scene S( obDriverModule );

		while ( S.GetQuitFlag() == false )
		{
			SDL_Event e;
			while ( SDL_PollEvent( &e ) && S.GetQuitFlag() == false )
			{
				obDriverModule.call_function( "HandleEvent", &e );
			}

			S.Update();
			S.Draw();
		}

		return EXIT_SUCCESS;
	}
	catch ( std::runtime_error )
	{
		return EXIT_FAILURE;
	}
}