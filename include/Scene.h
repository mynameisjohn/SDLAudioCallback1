#pragma once

#include "Camera.h"
#include "Shader.h"
#include "LoopManager.h"
#include "Drawable.h"

#include <pyliason.h>
#include <memory>

class Scene
{
	Shader m_Shader;	
	LoopManager m_LoopManager;
	Camera m_Camera;
	std::vector<Drawable> m_vDrawables;
	SDL_GLContext m_GLContext;
	SDL_Window * m_pWindow;
public:
	Scene( pyl::Object obInitScript );
	~Scene();

	void Draw();
	void Update( pyl::Object obDriverScript );

	Shader * GetShaderPtr() const;
	Camera * GetCameraPtr() const;
	LoopManager * GetLoopManagerPtr() const;

	bool InitDisplay( int glMajor, int glMinor, int iScreenW, int iScreenH, vec4 v4ClearColor );
	bool AddDrawable( std::string strIqmFile, vec2 T, vec2 S, vec4 C );

	static void pylExpose();

	// PYL stuff
	static const std::string strModuleName;
};