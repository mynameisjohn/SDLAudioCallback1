#pragma once

#include "GL_Includes.h"

// You should make copying / moving safe
#include <string>
#include <map>
#include <memory>

class Shader
{
	// Private initializer
	//int compileAndLink();

	// private constructor
	Shader( std::string vSrc, std::string fSrc );

public:
	Shader();
	// Factory methods instead of constructors
	static Shader FromSource( std::string vSrc, std::string fSrc );
	static Shader FromFiles( std::string vSrcFile, std::string fSrcFile );

	bool SetSrcFiles( std::string vSrcFile, std::string fSrcFile );
	int CompileAndLink();

	~Shader();

	void PrintHandles();

	// Bound status
	bool Bind();
	bool Unbind();
	bool IsBound() const;

	// Logging functions
	int PrintLog_V() const;
	int PrintLog_F() const;
	int PrintSrc_V() const;
	int PrintSrc_F() const;
	int PrintLog_P() const;

	// Public Accessors
	GLint GetHandle( const std::string idx );
	GLint operator[]( const std::string idx );

private:
	// Bound status, program/shaders, source, handles
	bool m_bIsBound;
	GLuint m_Program;
	GLuint m_hVertShader;
	GLuint m_hFragShader;
	std::string m_VertShaderSrc, m_FragShaderSrc;

	using HandleMap = std::map<std::string, GLint>;
	HandleMap m_Handles;

	// Public scoped bind class
	// binds shader for as long as it lives
public:
	class ScopedBind
	{
		friend class Shader;
	protected:
		Shader& m_Shader;
		ScopedBind( Shader& s ) : m_Shader( s ) { m_Shader.Bind(); }
	public:
		~ScopedBind() { m_Shader.Unbind(); }
	};
	inline ScopedBind ScopeBind() { return ScopedBind( *this ); }

	// Why not?
	inline HandleMap getHandleMap() { return m_Handles; }

	static const std::string strModuleName;
	static void pylExpose();
};

