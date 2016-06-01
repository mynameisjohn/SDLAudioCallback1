#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_assert.h>
#include <SDL_events.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include <glm/gtc/type_ptr.hpp>

#include "LoopManager.h"
#include <iostream>
#include "Drawable.h"
#include "Camera.h"
#include "Shader.h"
#include "Scene.h"
#include <memory>

bool pylExpose()
{
	// Create the SDL keys module for python
	pyl::ModuleDef * pSDLKeysModule = pyl::ModuleDef::CreateModuleDef<struct st_SDLK>( "pylSDLKeys" );
	if ( pSDLKeysModule == nullptr )
	{
		std::cout << "Error: Unable to create SDL keycode python module" << std::endl;
		pyl::print_error();
		return false;
	}

	pSDLKeysModule->SetCustomModuleInit( [] ( pyl::Object obModule )
	{
		obModule.set_attr( "Escape", (int) SDLK_ESCAPE );
		obModule.set_attr( "Space", (int) SDLK_SPACE );
		obModule.set_attr( "Num1", (int) SDLK_1 );
		obModule.set_attr( "Num2", (int) SDLK_2 );
		obModule.set_attr( "Num3", (int) SDLK_3 );
	} );

	Scene::pylExpose();
	pyl::initialize();

	return true;
}

int main(int argc, char ** argv)
{
	// Load up the driver module and init the loopmanager
	if ( pylExpose() == false )
		return EXIT_FAILURE;

	try
	{
		pyl::Object obDriverModule = pyl::Object::from_script( "../scripts/driver.py" );
		Scene S( obDriverModule );

		// This condition should be event based
		bool bContinue = true;
		while ( bContinue )
		{
			SDL_Event e;
			while ( SDL_PollEvent( &e ) && bContinue )
			{
				obDriverModule.call_function( "HandleEvent", &e ).convert( bContinue );
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