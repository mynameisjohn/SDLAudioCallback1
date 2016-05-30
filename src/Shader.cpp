#include <fstream>
#include <string>
#include <iostream>
using namespace std;

#include <pyliason.h>

#include "Shader.h"

// TODO
// Set up constructors so things don't get messed up if someone
// decides to copy this around or move it

// Basics
Shader::Shader() :
	m_bIsBound( false ),
	m_Program( 0 ),
	m_hVertShader( 0 ),
	m_hFragShader( 0 )
{
}
Shader::Shader( std::string vSrc, std::string fSrc ) :
	Shader()
{
	m_VertShaderSrc = vSrc;
	m_FragShaderSrc = fSrc;
}

bool Shader::SetSrcFiles( std::string vSrcFile, std::string fSrcFile )
{
	ifstream vIn( vSrcFile ), fIn( fSrcFile );
	std::string vSrc( (istreambuf_iterator<char>( vIn )), istreambuf_iterator<char>() );
	std::string fSrc( (istreambuf_iterator<char>( fIn )), istreambuf_iterator<char>() );

	if ( vSrc.empty() || fSrc.empty() )
		return false;

	m_VertShaderSrc = vSrc;
	m_FragShaderSrc = fSrc;

	return true;
}

/*static*/ Shader Shader::FromSource( std::string vSrc, std::string fSrc )
{
	if ( vSrc.empty() || fSrc.empty() )
		throw std::runtime_error( "Invalid shader source files provided" );

	Shader ret( vSrc, fSrc );
	if ( ret.CompileAndLink() )
		throw std::runtime_error( "Error compiling shader program" );

	return ret;
}

/*static*/ Shader Shader::FromFiles( std::string vSrcFile, std::string fSrcFile )
{
	ifstream vIn( vSrcFile ), fIn( fSrcFile );
	std::string vSrc( (istreambuf_iterator<char>( vIn )), istreambuf_iterator<char>() );
	std::string fSrc( (istreambuf_iterator<char>( fIn )), istreambuf_iterator<char>() );

	return FromSource( vSrc, fSrc );
}

Shader::~Shader()
{
	Unbind();
}

// Managing bound state
bool Shader::Bind() {
	if (!m_bIsBound) {
		glUseProgram(m_Program);
		m_bIsBound = true;
	}
	return m_bIsBound == true;
}

bool Shader::Unbind() {
	if (m_bIsBound) {
		glUseProgram(0);
		m_bIsBound = false;
	}
	return m_bIsBound == false;
}

bool Shader::IsBound() const {
	return m_bIsBound;
}

int Shader::CompileAndLink() {
	// Check if the shader op went ok
	auto check = [](GLuint id, GLuint type) {
		GLint status(GL_FALSE);
		if (type == GL_COMPILE_STATUS)
			glGetShaderiv(id, type, &status);
		if (type == GL_LINK_STATUS)
			glGetProgramiv(id, type, &status);
		return status == GL_TRUE;
	};

	// Error codes to return
	const int ERR_N(0), ERR_V(1), ERR_F(2), ERR_L(4);

	// Weird thing that helps with glShaderSource
	const GLchar * shaderSrc[] = { m_VertShaderSrc.c_str(), m_FragShaderSrc.c_str() };

	// Compile Vertex Shader
	m_hVertShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(m_hVertShader, 1, &(shaderSrc[0]), 0);
	glCompileShader(m_hVertShader);
	if (!check(m_hVertShader, GL_COMPILE_STATUS)) {
		cout << "Unable to compile vertex shader." << endl;
		PrintLog_V();
		return ERR_V;
	}

	// Compile Frag Shader
	m_hFragShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(m_hFragShader, 1, &(shaderSrc[1]), 0);
	glCompileShader(m_hFragShader);
	if (!check(m_hFragShader, GL_COMPILE_STATUS)) {
		cout << "Unable to compile fragment shader." << endl;
		PrintLog_F();
		return ERR_F;
	}

	// Create and Link Program
	m_Program = glCreateProgram();
	glAttachShader(m_Program, m_hVertShader);
	glAttachShader(m_Program, m_hFragShader);
	glLinkProgram(m_Program);
	if (!check(m_Program, GL_LINK_STATUS)) {
		PrintLog_P();
		cout << "Unable to link shader program." << endl;
		return ERR_L;
	}

	return ERR_N;
}

GLint Shader::operator[](const string idx) {
	return GetHandle(idx);
}

// Accessor for shader handles
GLint Shader::GetHandle(const string idx) {
	// If we have the handle, return it
	if (m_Handles.find(idx) != m_Handles.end())
		return m_Handles.find(idx)->second;

	// Otherwise we have to find it, so client must bind
	if (!m_bIsBound) {
		cout << "Error: shader queried for untested variable " << idx
			<< " while shader was not bound" << endl;
		return -1;
	}

	// Try and get handle, first as attr then as uniform
	GLint handle = glGetAttribLocation(m_Program, idx.c_str());
	if (handle < 0)
		handle = glGetUniformLocation(m_Program, idx.c_str());

	// valid handles begin at 0
	if (handle >= 0)
		m_Handles[idx] = handle;
	else {
		// We queried something bad, print the log
		cout << "Invalid variable " << idx << " queried in shader" << endl;
		PrintLog_V();
		PrintLog_F();
	}

	return handle;
}

void Shader::PrintHandles() {
	for (auto& h : m_Handles)
		cout << h.first << ": " << h.second << "\n";
	cout << endl;
}

// Print Logs
int Shader::PrintLog_V() const {
	const int max(1024);
	int len(0);
	char log[max];
	glGetShaderInfoLog(m_hVertShader, max, &len, log);
	cout << "Vertex Shader Log: \n\n" << log << "\n\n" << endl;

	return len;
}

int Shader::PrintLog_F() const {
	const int max(1024);
	int len(0);
	char log[max];
	glGetShaderInfoLog(m_hFragShader, max, &len, log);
	cout << "Fragment Shader Log: \n\n" << log << "\n\n" << endl;

	return len;
}

int Shader::PrintLog_P() const {
	const int max(1024);
	int len(0);
	char log[max]{ 0 };
	glGetShaderInfoLog(m_Program, max, &len, log);
	cout << "Fragment Shader Log: \n\n" << log << "\n\n" << endl;

	return len;
}

// Print Source
int Shader::PrintSrc_V() const {
	cout << "Vertex Shader Source: \n\n" << m_VertShaderSrc << "\n\n" << endl;
	return m_VertShaderSrc.length();
}

int Shader::PrintSrc_F() const {
	cout << "Fragment Shader Source: \n\n" << m_FragShaderSrc << "\n\n" << endl;
	return m_FragShaderSrc.length();
}

/*static*/ const std::string Shader::strModuleName = "pylShader";
/*static*/ void Shader::pylExpose()
{
	using pyl::ModuleDef;
	ModuleDef * pShaderModDef = ModuleDef::CreateModuleDef<struct Shader>( Shader::strModuleName );
	if ( pShaderModDef == nullptr )
		throw std::runtime_error( "Error creating Scene pyl module" );

	pShaderModDef->RegisterClass<Shader>( "Shader" );

	AddMemFnToMod( Shader, SetSrcFiles, bool, pShaderModDef, std::string, std::string );
	AddMemFnToMod( Shader, CompileAndLink, int, pShaderModDef );
	AddMemFnToMod( Shader, Bind, bool, pShaderModDef );
	AddMemFnToMod( Shader, Unbind, bool, pShaderModDef );
	AddMemFnToMod( Shader, GetHandle, GLint, pShaderModDef, std::string );
}