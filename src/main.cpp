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
	pylExpose();
	pyl::Object obDriverModule = pyl::Object::from_script( "../scripts/driver.py" );

	std::unique_ptr<Scene> pScene;
	try
	{
		pScene = std::unique_ptr<Scene>( new Scene( obDriverModule ) );


	// This condition should be event based
	bool bContinue = true;
	while ( bContinue )
	{
		SDL_Event e;
		while ( SDL_PollEvent( &e ) )
		{
			if ( e.type == SDL_QUIT )
				bContinue = false;

			// We only care about the keyboard for now
			else if ( e.type == SDL_KEYDOWN || e.type == SDL_KEYUP )
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
		pScene->Update();
		pScene->Draw();
	}

	return 0;
	}
	catch ( std::runtime_error )
	{
		return -1;
	}
}