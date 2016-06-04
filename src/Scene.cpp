#include "Scene.h"

#include <glm/gtc/type_ptr.hpp>

Scene::Scene( pyl::Object obInitScript ) :
	m_bQuitFlag( false ),
	m_GLContext( nullptr ),
	m_pWindow( nullptr ),
	m_obDriverScript( obInitScript )
{
	m_obDriverScript.call_function( "InitScene", this );
}

Scene::~Scene()
{
	if ( m_pWindow )
	{
		SDL_DestroyWindow( m_pWindow );
		m_pWindow = nullptr;
	}
	if ( m_GLContext )
	{
		SDL_GL_DeleteContext( m_GLContext );
		m_GLContext = nullptr;
	}
}

void Scene::Draw()
{
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	auto sBind = m_Shader.ScopeBind();

	GLuint pmvHandle = m_Shader.GetHandle( "u_PMV" );
	GLuint clrHandle = m_Shader.GetHandle( "u_Color" );
	mat4 P = m_Camera.GetMat();

	for ( Drawable& dr : m_vDrawables )
	{
		mat4 PMV = P * dr.GetMV();
		vec4 c = dr.GetColor();
		glUniformMatrix4fv( pmvHandle, 1, GL_FALSE, glm::value_ptr( PMV ) );
		glUniform4fv( clrHandle, 1, glm::value_ptr( c ) );
		dr.Draw();
	}

	SDL_GL_SwapWindow( m_pWindow );
}

/*static*/const std::string Scene::strModuleName = "pylScene";

void Scene::Update()
{
	m_obDriverScript.call_function( "Update", this );
}

bool Scene::InitDisplay( int glMajor, int glMinor, int iScreenW, int iScreenH, vec4 v4ClearColor )
{
	// Create Window (only used for keyboard input, as of now)
	m_pWindow = SDL_CreateWindow( "3D Test",
											 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
											 iScreenW, iScreenH,
											 SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN );
	if ( m_pWindow == NULL )
	{
		std::cout << "Window could not be created! SDL Error: " << SDL_GetError() << std::endl;
		return false;
	}
	
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, glMajor );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, glMinor );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	m_GLContext = SDL_GL_CreateContext( m_pWindow );
	if ( m_GLContext == nullptr )
	{
		std::cout << "Error creating opengl context" << std::endl;
		return false;
	}

	//Initialize GLEW
	glewExperimental = GL_TRUE;
	GLenum glewError = glewInit();
	if ( glewError != GLEW_OK )
	{
		printf( "Error initializing GLEW! %s\n", glewGetErrorString( glewError ) );
		return false;
	}

	//Use Vsync
	if ( SDL_GL_SetSwapInterval( 1 ) < 0 )
	{
		printf( "Warning: Unable to set VSync! SDL Error: %s\n", SDL_GetError() );
	}

	//OpenGL settings
	glClearColor( v4ClearColor.x, v4ClearColor.y, v4ClearColor.z, v4ClearColor.w );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LESS );
	glEnable( GL_MULTISAMPLE_ARB );

	//For debugging
	glLineWidth( 8.f );
}

bool Scene::AddDrawable( std::string strIqmFile, vec2 T, vec2 S, vec4 C )
{
	try
	{
		Drawable D( strIqmFile, C, quatvec( vec3( T, 0 ), fquat() ), S );
		m_vDrawables.push_back( D );
		return true;
	}
	catch ( std::runtime_error )
	{
		return false;
	}
}

LoopManager * Scene::GetLoopManagerPtr() const
{
	return (LoopManager *) &m_LoopManager;
}

Shader * Scene::GetShaderPtr() const
{
	return (Shader *) &m_Shader;
}

Camera * Scene::GetCameraPtr() const
{
	return (Camera *) &m_Camera;
}

void Scene::SetQuitFlag( bool bQuit )
{
	m_bQuitFlag = bQuit;
}

bool Scene::GetQuitFlag() const
{
	return m_bQuitFlag;
}

Drawable * Scene::GetDrawable( const size_t drIdx ) const
{
	if ( drIdx < m_vDrawables.size() )
		return (Drawable *) &m_vDrawables[drIdx];
	return nullptr;
}

/*static*/ void Scene::pylExpose()
{
	LoopManager::pylExpose();
	Shader::pylExpose();
	Camera::pylExpose();
	Drawable::pylExpose();

	using pyl::ModuleDef;
	ModuleDef * pSceneModuleDef = ModuleDef::CreateModuleDef<struct st_ScMod>( Scene::strModuleName );
	if ( pSceneModuleDef == nullptr )
		throw std::runtime_error( "Error creating Scene pyl module" );

	pSceneModuleDef->RegisterClass<Scene>( "Scene" );

	//std::function<bool( Scene *, int, int, int, int, vec4 )> fnScInitDisp = &Scene::InitDisplay;
	//pSceneModuleDef->RegisterMemFunction<Scene, struct st_fnScInitDisp>( "InitDisplay", fnScInitDisp );

	//std::function<bool( Scene *, std::map<std::string, int> mapLoopManagerCfg )> fnScAddDrawable = &Scene::AddDrawable;
	//pSceneModuleDef->RegisterMemFunction<Scene, struct st_fnAddDrawable>( "AddDrawable", fnScAddDrawable );

	AddMemFnToMod( Scene, InitDisplay, bool, pSceneModuleDef, int, int, int, int, vec4 );
	AddMemFnToMod( Scene, AddDrawable, bool, pSceneModuleDef, std::string, vec2, vec2, vec4 );
	AddMemFnToMod( Scene, GetShaderPtr, Shader *, pSceneModuleDef );
	AddMemFnToMod( Scene, GetCameraPtr, Camera *, pSceneModuleDef );
	AddMemFnToMod( Scene, GetLoopManagerPtr, LoopManager *, pSceneModuleDef );
	AddMemFnToMod( Scene, GetDrawable, Drawable *, pSceneModuleDef, size_t );
	AddMemFnToMod( Scene, GetQuitFlag, bool, pSceneModuleDef );
	AddMemFnToMod( Scene, SetQuitFlag, void, pSceneModuleDef, bool );
}
