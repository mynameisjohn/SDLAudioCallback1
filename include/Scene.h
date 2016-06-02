#pragma once

#include "Camera.h"
#include "Shader.h"
#include "LoopManager.h"
#include "Drawable.h"

#include <pyliason.h>
#include <memory>

class Scene
{
	bool m_bQuitFlag;
	Shader m_Shader;	
	LoopManager m_LoopManager;
	Camera m_Camera;
	std::vector<Drawable> m_vDrawables;
	SDL_GLContext m_GLContext;
	SDL_Window * m_pWindow;
	pyl::Object m_obDriverScript;
public:
	Scene( pyl::Object obInitScript );
	~Scene();

	void Draw();
	void Update();

	void SetQuitFlag( bool bQuit );
	bool GetQuitFlag() const;

	Shader * GetShaderPtr() const;
	Camera * GetCameraPtr() const;
	LoopManager * GetLoopManagerPtr() const;
	Drawable * GetDrawable( size_t drIdx ) const;

	bool InitDisplay( int glMajor, int glMinor, int iScreenW, int iScreenH, vec4 v4ClearColor );
	bool AddDrawable( std::string strIqmFile, vec2 T, vec2 S, vec4 C );

	static void pylExpose();

	// PYL stuff
	static const std::string strModuleName;
};