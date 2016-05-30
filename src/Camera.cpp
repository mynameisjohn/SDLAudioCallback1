#include "Camera.h"
#include <glm/gtx/transform.hpp>
#include <pyliason.h>

GLint Camera::s_ProjHandle( -1 );
GLint Camera::s_PosHandle( -1 );
using glm::normalize;

Camera::Camera() :
	m_v2Pos( 0 ),
	m_m4Proj( 1 )
{
}

void Camera::InitOrtho( vec2 X, vec2 Y )
{
	m_m4Proj = glm::ortho( X[0], X[1], Y[0], Y[1], Y[0], Y[1] );
}

// Why am I inverting the translation but not the rotation?
mat4 Camera::GetTransform()
{
	return glm::translate( vec3( -m_v2Pos, 0 ) );
}

mat4 Camera::GetMat()
{
	// The camera is always at the origin.
	// We're really moving and rotating everyone else
	return m_m4Proj * GetTransform();
}

void Camera::Translate( vec2 T )
{
	m_v2Pos += T;
}

void Camera::Reset()
{
	m_v2Pos = vec2( 0 );
}

mat4 Camera::GetProj()
{
	return m_m4Proj;
}

/*static*/ GLint Camera::GetPosHandle()
{
	return s_PosHandle;
}

/*static*/ GLint Camera::GetProjHandle()
{
	return s_ProjHandle;
}

/*static*/ void Camera::SetPosHandle( GLint P )
{
	s_PosHandle = P;
}

/*static*/ void Camera::SetProjHandle( GLint p )
{
	s_ProjHandle = p;
}

/*static*/ const std::string Camera::strModuleName = "pylCamera";
/*static*/ void Camera::pylExpose()
{
	using pyl::ModuleDef;
	ModuleDef * pCameraModDef = ModuleDef::CreateModuleDef<Camera>( Camera::strModuleName );
	if ( pCameraModDef == nullptr )
		throw std::runtime_error( "Error creating Scene pyl module" );

	pCameraModDef->RegisterClass<Camera>( "Camera" );
	
	AddMemFnToMod( Camera, InitOrtho, void, pCameraModDef, vec2, vec2 );
	pCameraModDef->RegisterFunction<struct st_fnCamSetPH>( "SetProjHandle", pyl::make_function( Camera::SetProjHandle ) );
}