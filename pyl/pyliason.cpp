/*      This program is free software; you can redistribute it and/or modify
*      it under the terms of the GNU General Public License as published by
*      the Free Software Foundation; either version 3 of the License, or
*      (at your option) any later version.
*
*      This program is distributed in the hope that it will be useful,
*      but WITHOUT ANY WARRANTY; without even the implied warranty of
*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*      GNU General Public License for more details.
*
*      You should have received a copy of the GNU General Public License
*      along with this program; if not, write to the Free Software
*      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
*      MA 02110-1301, USA.
*
*      Author:
*      John Joseph
*
*/

#include <algorithm>
#include <fstream>

#include "pyliason.h"

#include "pyl_module.h"
#include "pyl_misc.h"

namespace pyl 
{
	using std::runtime_error;
	using std::string;

	Object::Object() {

	}

	Object::Object(PyObject *obj) : py_obj(make_pyshared(obj)) {
	}

	pyl::Object::pyshared_ptr Object::make_pyshared(PyObject *obj) {
		Py_XINCREF(obj);
		return pyshared_ptr(obj, [](PyObject *obj) { Py_XDECREF(obj); });
	}

	Object Object::from_script(const string &script_path) {
		// Get the directory path and file name
		string base_path("."), file_path;
		size_t last_slash(script_path.rfind("/"));
		if (last_slash != string::npos) {
			if (last_slash >= script_path.size() - 2)
				throw runtime_error("Invalid script path");
			base_path = script_path.substr(0, last_slash);
			file_path = script_path.substr(last_slash + 1);
		}
		else
			file_path = script_path;
		if (file_path.rfind(".py") == file_path.size() - 3)
			file_path = file_path.substr(0, file_path.size() - 3);

		// Try loading just the file name
		pyl::Object py_ptr((PyImport_ImportModule(file_path.c_str())));
		//PyObject *py_ptr;
		if (py_ptr.get() != nullptr)
			return std::move(py_ptr);

		// If we didn't get it, see if the dir path is in PyPath
		char arr[] = "path";
		pyl::Object path(PySys_GetObject(arr));
		std::vector<std::string> curPath;
		pyl::convert(path.get(), curPath);

		// If it isn't add it to the path and try again
		if (std::find(curPath.begin(), curPath.end(), base_path) == curPath.end()) {
			pyunique_ptr pwd(PyUnicode_FromString(base_path.c_str()));
			PyList_Append(path.get(), pwd.get());
			return from_script(script_path);
		}

		// If it was in the path and we still couldn't load, there's a problem
		print_error();
		throw runtime_error("Failed to load script");
	}

	PyObject *Object::load_function(const std::string &name) {
		PyObject *obj(PyObject_GetAttrString(py_obj.get(), name.c_str()));
		if (!obj)
			throw std::runtime_error("Failed to find function");
		return obj;
	}

	Object Object::call_function(const std::string &name) {
		pyunique_ptr func(load_function(name));
		PyObject *ret(PyObject_CallObject(func.get(), 0));
		if (!ret)
			throw std::runtime_error("Failed to call function");
		return{ ret };
	}

	Object Object::get_attr(const std::string &name) {
		PyObject *obj(PyObject_GetAttrString(py_obj.get(), name.c_str()));
		if (!obj)
			throw std::runtime_error("Unable to find attribute '" + name + '\'');
		return{ obj };
	}

	bool Object::has_attr(const std::string &name) {
		try {
			get_attr(name);
			return true;
		}
		catch (std::runtime_error&) {
			return false;
		}
	}

	void Object::Reset()
	{
		py_obj.reset();
	}

	void initialize() {
		// Finalize any previous stuff
		Py_Finalize();

		ModuleDef::InitAllModules();

		// Startup python
		Py_Initialize();
	}

	void finalize() {
		Py_Finalize();
	}

	void clear_error() {
		PyErr_Clear();
	}

	void print_error() {
		PyErr_Print();
	}

	void print_object(PyObject *obj) {
		PyObject_Print(obj, stdout, 0);
	}

	// Allocation methods

	PyObject *alloc_pyobject(const std::string &str) {
		return PyBytes_FromString(str.c_str());
	}

	PyObject *alloc_pyobject(const std::vector<char> &val, size_t sz) {
		return PyByteArray_FromStringAndSize(val.data(), sz);
	}

	PyObject *alloc_pyobject(const std::vector<char> &val) {
		return alloc_pyobject(val, val.size());
	}

	PyObject *alloc_pyobject(const char *cstr) {
		return PyBytes_FromString(cstr);
	}

	PyObject *alloc_pyobject(bool value) {
		return PyBool_FromLong(value);
	}

	PyObject *alloc_pyobject(double num) {
		return PyFloat_FromDouble(num);
	}

	PyObject *alloc_pyobject(float num) {
		double d_num(num);
		return PyFloat_FromDouble(d_num);
	}

	bool is_py_int(PyObject *obj) {
		return PyLong_Check(obj);
	}

	bool is_py_float(PyObject *obj) {
		return PyFloat_Check(obj);
	}

	bool convert(PyObject *obj, std::string &val) {
		if (PyBytes_Check(obj)) {
			val = PyBytes_AsString(obj);
			return true;
		}
		else if (PyUnicode_Check(obj)) {
			val = PyUnicode_AsUTF8(obj);
			return true;
		}
		return false;
	}

	bool convert(PyObject *obj, std::vector<char> &val) {
		if (!PyByteArray_Check(obj))
			return false;
		if (val.size() < (size_t)PyByteArray_Size(obj))
			val.resize(PyByteArray_Size(obj));
		std::copy(PyByteArray_AsString(obj),
			PyByteArray_AsString(obj) + PyByteArray_Size(obj),
			val.begin());
		return true;
	}

	bool convert(PyObject *obj, bool &value) {
		if (obj == Py_False)
			value = false;
		else if (obj == Py_True)
			value = true;
		else
			return false;
		return true;
	}

	bool convert(PyObject *obj, double &val) {
		return generic_convert<double>(obj, is_py_float, PyFloat_AsDouble, val);
	}

	// It's unforunate that this takes so long
	bool convert(PyObject *obj, float &val) {
		double d(0);
		bool ret = generic_convert<double>(obj, is_py_float, PyFloat_AsDouble, d);
		val = float(d);
		return ret;
	}
	
	// If the client knows what to do, let 'em deal with it
	bool convert(PyObject * obj, pyl::Object& pyObj){
		pyObj = pyl::Object(obj);
		// I noticed that the incref is needed... not sure why?
		if (auto ptr = pyObj.get()) {
			Py_INCREF(ptr);
			return true;
		}
		return false;
	}

	// We only need one instance of the above, shared by exposed objects
	static int PyClsInit (PyObject * self, PyObject * args, PyObject * kwds) 
	{
		// In the example the first arg isn't a PyObject *, but... idk man
		GenericPyClass * realPtr = static_cast<GenericPyClass *>((void *)self);
		// The first argument is the capsule object
		PyObject * c = PyTuple_GetItem(args, 0);
		
		if (c)
		{// Or at least it better be
			if (PyCapsule_CheckExact(c))
			{
				Py_INCREF(c);
				realPtr->capsule = c;

				return 0;
			}
		}

		return -1;
	};
	
	// The () operator just returns the capsule object
	static PyObject * PyClsCall(PyObject * co, PyObject * args, PyObject * kw)
	{
		auto realPtr = static_cast<GenericPyClass *>((voidptr_t)co);
		Py_INCREF(realPtr->capsule);
		return realPtr->capsule;
	}

	// Constructor for exposed classes, sets up type object
	ExposedClass::ExposedClass(std::string n) : PyClassName(n)
	{
		// Take care of this now
        memset(&m_TypeObject, 0, sizeof(PyTypeObject));
		m_TypeObject.ob_base = PyVarObject_HEAD_INIT(NULL, 0)
		m_TypeObject.tp_name = PyClassName.c_str();
		m_TypeObject.tp_init = (initproc)PyClsInit;
		m_TypeObject.tp_call = (ternaryfunc)PyClsCall;
		m_TypeObject.tp_new = PyType_GenericNew;
		m_TypeObject.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
		m_TypeObject.tp_basicsize = sizeof(GenericPyClass);
	}

	// This has to happen at a time when these
	// definitions will no longer move
	void ExposedClass::Prepare()
	{
		m_TypeObject.tp_members = m_MemberDef.Ptr();
		m_TypeObject.tp_methods = m_MethodDef.Ptr();
	}

	// Add a method, preserving the null terminator and storing strings where they won't be destroyed
	void MethodDefinitions::AddMethod(std::string name, PyCFunction fnPtr, int flags, std::string docs)
	{
		// If a method with this name has already been declared, throw an error
		if (std::find(MethodNames.begin(), MethodNames.end(), name) != MethodNames.end())
		{
			// Alternatively, this could actually overwrite the pre-existing method. 
			throw runtime_error("Error: Attempting to overwrite exisiting exposed python function");
		}

		// We need the names in a list so their references stay valid
		MethodNames.push_back(name);
		const char * namePtr = MethodNames.back().c_str();

		PyMethodDef method{ 0 };

		if (docs.empty())
			method = { namePtr, fnPtr, flags, NULL };
		else {
			MethodDocs.push_back(std::string(docs));
			method = { namePtr, fnPtr, flags, MethodDocs.back().c_str() };
		}

		_insert(method);
	}

	// These by default get the c_ptr capsule object
	MemberDefinitions::MemberDefinitions()
		: _NullTermBuf()
	{
		MemberNames.push_back("c_ptr");
		MemberDocs.push_back("pointer to a c object");
		PyMemberDef d = { (char *)MemberNames.back().c_str(), T_OBJECT_EX, offsetof(GenericPyClass, capsule), 0, (char *)MemberDocs.back().c_str() };
		_insert(d);
	}

	// Add a member, preserving the null terminator and storing strings where they won't be destroyed
	void MemberDefinitions::AddMember(std::string name, int type, int offset, int flags, std::string docs)
	{
		// If a member with this name has already been declared, throw an error
		if (std::find(MemberNames.begin(), MemberNames.end(), name) != MemberNames.end())
		{
			// Alternatively, this could actually overwrite the pre-existing member. 
			throw runtime_error("Error: Attempting to overwrite exisiting exposed python class member");
		}

		// We need the names in a list so their references stay valid
		MemberNames.push_back(name);
		char * namePtr = (char *)MemberNames.back().c_str();

		PyMemberDef member{ 0 };
		
		if (docs.empty())
			member = { namePtr, type, offset, flags, NULL };
		else {
			MemberDocs.push_back(std::string(docs));
			member = { namePtr, type, offset, flags, (char *)MemberDocs.back().c_str() };
		}

		_insert(member);
	}

	int RunCmd( std::string cmd )
	{
		return PyRun_SimpleString( cmd.c_str() );
	}

	int RunFile(std::string file)
	{
		std::ifstream in(file);
		return RunCmd({ (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>() });
	}

	int GetTotalRefCount()
	{
		PyObject* refCount = PyObject_CallObject(PySys_GetObject((char*)"gettotalrefcount"), NULL);
		if (!refCount) return -1;
		int ret = _PyLong_AsInt(refCount);
		Py_DECREF(refCount);
		return ret;
	}

	// Static module map map declaration
	std::map<std::string, ModuleDef> ModuleDef::s_mapPyModules;

	// This should never be called
	ModuleDef::ModuleDef()
	{
		//assert( false && "Please don't construct Modules on your own, they won't be visible to the interpreter" );
	}

	// Name and doc constructor
	ModuleDef::ModuleDef( const std::string& moduleName, const std::string& moduleDocs ) :
		m_strModDocs( moduleDocs ),
		m_strModName( moduleName ),
		m_fnCustomInit( [] ( Object o ) {} )
	{
	}

	// This is implemented here just to avoid putting these STL calls in the header
	void ModuleDef::registerClass_impl( const std::type_index T, const std::string& className )
	{
		// If we've already exposed this, don't bother
		auto it = m_mapExposedClasses.find( T );
		if ( it != m_mapExposedClasses.end() )
			return;

		// Add the type info to the map
		m_mapExposedClasses.emplace( T, className );
	}

	// Implementation of expose object function that doesn't need to be in this header file
	int ModuleDef::exposeObject_impl( const std::type_index T, const voidptr_t instance, const std::string& name, PyObject * mod )
	{
		// If we haven't declared the class, we can't expose it
		auto it = m_mapExposedClasses.find( T );
		if ( it == m_mapExposedClasses.end() )
			return -1;

		// Make ref to expose class object
		ExposedClass& expCls = it->second;

		// If a module wasn't specified, just do main
		mod = mod ? mod : PyImport_ImportModule( "__main__" );
		if ( mod == nullptr )
			return -1;

		// Allocate a new object instance given the PyTypeObject
		PyObject* newPyObject = _PyObject_New( &expCls.m_TypeObject );

		// Make a PyCapsule from the void * to the instance (I'd give it a name, but why?
		PyObject* capsule = PyCapsule_New( instance, NULL, NULL );

		// Set the c_ptr member variable (which better exist) to the capsule
		static_cast<GenericPyClass *>((voidptr_t) newPyObject)->capsule = capsule;

		// Make a variable in the module out of the new py object
		int success = PyObject_SetAttrString( mod, name.c_str(), newPyObject );
		if ( success != 0 )
		{
			// TODO make some kind of exception that this can raise
			return success;
		}

		// decref and return
		Py_DECREF( mod );
		Py_DECREF( newPyObject );

		return success;
	}

	// Create the function object invoked when this module is imported
	void ModuleDef::createFnObject()
	{
		// Declare the init function, which gets called on import and returns a PyObject *
		// that represent the module itself (I hate capture this, but it felt necessary)
		m_fnModInit = [this] ()
		{
			// The MethodDef contains all functions defined in C++ code,
			// including those called into by exposed classes
			m_pyModDef = PyModuleDef
			{
				PyModuleDef_HEAD_INIT,
				m_strModName.c_str(),
				m_strModDocs.c_str(),
				-1,
				m_vMethodDef.Ptr()
			};

			// Create the module if possible
			if ( PyObject * mod = PyModule_Create( &m_pyModDef ) )
			{
				// Declare all exposed classes within the module
				for ( auto& exp_class : m_mapExposedClasses )
				{
					// Get the classes PyTypeObject
					auto pTypeObj = (PyTypeObject *) &(exp_class.second.m_TypeObject);
					if ( PyType_Ready( pTypeObj ) < 0 )
						assert( false );

					// Again, we are acting under the assumption that these pointers remain valid
					const char * className = exp_class.second.PyClassName.c_str();
					PyObject * typeObj = (PyObject *) &exp_class.second.m_TypeObject;

					// Add the type to the module
					PyModule_AddObject( mod, className, typeObj );
				}

				// Call the init function once the module is created
				m_fnCustomInit( { mod } );

				// Return the created module
				return mod;
			}

			// If creating the module failed for whatever reason
			return (PyObject *)nullptr;
		};
	}

	// This function locks down any exposed class definitions
	void ModuleDef::prepareClasses()
	{
		// Lock down any definitions
		for ( auto& e_Class : m_mapExposedClasses )
			e_Class.second.Prepare();
	}

	/*static*/ ModuleDef * ModuleDef::GetModuleDef( const std::string moduleName )
	{
		// Return nullptr if we don't have this module
		auto it = s_mapPyModules.find( moduleName );
		if ( it == s_mapPyModules.end() )
			return nullptr;

		// Otherwise return the address of the definition
		return &it->second;
	}

	Object ModuleDef::AsObject() const
	{
		auto it = s_mapPyModules.find( m_strModName );
		if ( it == s_mapPyModules.end() )
			return nullptr;

		PyObject * plMod = PyImport_ImportModule( m_strModName.c_str() );

		return plMod;
	}

	/*static*/ int ModuleDef::InitAllModules()
	{
		for ( auto& module : s_mapPyModules )
		{
			module.second.prepareClasses();
		}
		return 0;
	}

	const char * ModuleDef::getNameBuf() const
	{
		return m_strModName.c_str();
	}

	void ModuleDef::SetCustomModuleInit( std::function<void( Object )> fnCustomInit )
	{
		m_fnCustomInit = fnCustomInit;
	}

	Object GetModule( std::string modName )
	{
		PyObject * pModule = PyImport_ImportModule( modName.c_str() );
		if ( pModule )
			return{ pModule };

		return{ nullptr };
	}

	Object GetMainModule()
	{
		return GetModule( "__main__" );
	}
}
