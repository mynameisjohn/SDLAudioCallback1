#pragma once

// For file IO and exceptions
#include <stdio.h>
#include <stdexcept>

class IQMFile
{
	// To avoid including stdint
	using uint32_t = unsigned int;
public:
	// Various types of data within an IQM File
	enum class EType : uint32_t
	{
		POSITION = 0,
		TEXCOORD = 1,
		NORMAL = 2,
		TANGENT = 3,
		BLENDINDEXES = 4,
		BLENDWEIGHTS = 5,
		COLOR = 6,
		VARRAY = 7,
		MESH = 8,
		TRI = 9,
		JOINT = 10,
		POSE = 11,
		ANIM = 12,
		FRAME = 13,
		BBOX = 14,
		CUSTOM = 0x10
	};

	// Various structs used in IQM, left public
	struct iqmmesh
	{
		unsigned int name;
		unsigned int material;
		unsigned int first_vertex, num_vertexes;
		unsigned int first_triangle, num_triangles;
	};

	struct iqmjoint
	{
		unsigned int name;
		int parent;
		float translate[3], rotate[4], scale[3];
	};

	struct iqmpose
	{
		int parent;
		unsigned int mask;
		float channeloffset[10];
		float channelscale[10];
	};

	struct iqmanim
	{
		unsigned int name;
		unsigned int first_frame, num_frames;
		float framerate;
		unsigned int flags;
	};

	enum
	{
		IQM_LOOP = 1 << 0
	};

	struct iqmbounds
	{
		float bbmin[3], bbmax[3];
		float xyradius, radius;
	};

	// Indices (triangles)
	struct iqmtriangle { uint32_t a, b, c; };

	// Vertex attributes
	struct iqmposition { float x, y, z; };
	struct iqmtexcoord { float u, v; };
	struct iqmnormal { float nX, nY, nZ; };
	struct iqmtangent { float tX, tY, tZ, tW; };
	struct iqmblendidx { uint8_t bI[4]; };
	struct iqmblendweight { uint8_t bW[4]; };

private:
	// The IQM Header struct, which gives easy access to data
	struct Header
	{	
		char magic[16];
		uint32_t version;
		uint32_t filesize;
		uint32_t flags;
		uint32_t num_text, ofs_text;
		uint32_t num_meshes, ofs_meshes;
		uint32_t num_vertexarrays, num_vertexes, ofs_vertexarrays;
		uint32_t num_triangles, ofs_triangles, ofs_adjacency;
		uint32_t num_joints, ofs_joints;
		uint32_t num_poses, ofs_poses;
		uint32_t num_anims, ofs_anims;
		uint32_t num_frames, num_framechannels, ofs_frames, ofs_bounds;
		uint32_t num_comment, ofs_comment;
		uint32_t num_extensions, ofs_extensions;
	} * m_pHeader;

	// Convenient marker for data within file
	struct Waypoint
	{
		uint32_t num;
		uint32_t ofs;
	};

	// Get waypoint from IQM type code (defined below)
	// Get waypoint from IQM type code 
	Waypoint getWaypoint( EType c ) const noexcept
	{
		switch ( c )
		{
			case EType::MESH:
				return{ m_pHeader->num_meshes, m_pHeader->ofs_meshes };
			case EType::TRI:
				return{ m_pHeader->num_triangles, m_pHeader->ofs_triangles };
			case EType::POSE:
				return{ m_pHeader->num_poses, m_pHeader->ofs_poses };
			case EType::ANIM:
				return{ m_pHeader->num_anims, m_pHeader->ofs_anims };
			case EType::VARRAY:
				return{ m_pHeader->num_vertexarrays, m_pHeader->ofs_vertexarrays };
			case EType::FRAME:
				return{ m_pHeader->num_frames, m_pHeader->ofs_frames };
			case EType::BBOX:
				return{ 1, m_pHeader->ofs_bounds };
			case EType::POSITION:
			case EType::NORMAL:
			case EType::TANGENT:
			case EType::TEXCOORD:
			case EType::BLENDINDEXES:
			case EType::BLENDWEIGHTS:
			{
				auto vArrs = ( (iqmvertexarray *) &reinterpret_cast<char *>(m_pHeader)[m_pHeader->ofs_vertexarrays] );
				for ( uint32_t i = 0; i < m_pHeader->num_vertexarrays; i++ )
				{
					if ( c == (EType) vArrs[i].type )
						return{ m_pHeader->num_vertexes, vArrs[i].offset };
				}
			}
			default:
				break;
		}

		return{ 0,0 };
	}

	// Protected accessor methods
protected:
	// Get number of IQM_T mapped types
	inline uint32_t getNum( EType c ) const noexcept
	{
		return getWaypoint( c ).num;
	}

	// Get ptr to IQM_T mapped types
	template <typename T = uint8_t>
	inline T * getPtr( EType c ) const noexcept
	{
		Waypoint wp = getWaypoint( c );
		return wp.ofs ? (T *) &reinterpret_cast<char *>(m_pHeader)[wp.ofs] : nullptr;
	}

public:
	// Source constructor
	IQMFile( const char * szFilename )
	{
		FILE * fp = nullptr;
		char * pDataBuf = nullptr;

		// Useful lambdas
		auto isLittleEndian = [] ()
		{	// Check if lil endian (not handled yet)
			union { int i; uint8_t b[sizeof( int )]; } conv;
			conv.i = 1;
			return conv.b[0] != 0;
		};
		auto IQMASSERT = [&fp, &pDataBuf] ( bool cond, const char * msg )
		{
			if ( cond == false )
			{
				if ( fp )
				{
					fclose( fp );
					fp = nullptr;
				}
				if ( pDataBuf )
				{
					free( fp );
					pDataBuf = nullptr;
				}
				throw std::runtime_error( msg );
			}
		};

		// Version/Magic at the time of writing this
		const uint32_t IQM_VERSION = 2;
		const char * IQM_MAGIC = "INTERQUAKEMODEL";

		fp = fopen( szFilename, "r" );
		IQMASSERT( fp, "Error: Invalid filename provided to IQM File constructor!" );

		// Check the file size
		fseek( fp, 0, SEEK_END );
		size_t uFileSize = ftell( fp );
		IQMASSERT( uFileSize, "Error: Empty file provided to IQM File constructor!" );

		// Rewind
		fseek( fp, 0, SEEK_SET );

		// Allocate internal buffer
		pDataBuf = (char *) malloc( uFileSize );
		if ( pDataBuf == nullptr )
			throw std::bad_alloc();

		// Read file into buffer, close file
		IQMASSERT( uFileSize == fread( pDataBuf, 1, uFileSize, fp ), "Error: Invalid number of bytes read in from file!" );
		fclose( fp );
		fp = nullptr;

		// Assign header to beginning of buffer
		m_pHeader = (Header *) pDataBuf;

		// Checks
		IQMASSERT( m_pHeader, "No IQM Header loaded" );
		IQMASSERT( m_pHeader->version == IQM_VERSION, "IQM file version incorrect" );
		IQMASSERT( strcmp( m_pHeader->magic, IQM_MAGIC ) == 0, "IQM File contained wrong magic number" );
		IQMASSERT( (size_t) m_pHeader->filesize == uFileSize, "Error: Inconsistency with file sizes reported" );

		// Do a check of the vertex data to catch something odd
		iqmvertexarray * vArrs( (iqmvertexarray *) &pDataBuf[m_pHeader->ofs_vertexarrays] );
		for ( uint32_t i = 0; i < m_pHeader->num_vertexarrays; i++ )
		{	// Check array type, cache info
			iqmvertexarray & va( vArrs[i] );
			EType type = (EType) va.type;
			IQM_P prim = (IQM_P) va.format;
			switch ( type )
			{	// If it's a vertex attribute, make sure it's the right primitive type and cache it
				case EType::POSITION:
				case EType::NORMAL:
				case EType::TANGENT:
				case EType::TEXCOORD:
					IQMASSERT( prim == IQM_P::FLOAT, "Error: Type of vertex attribute incorrect, expected a float" );
					break;
				case EType::BLENDINDEXES:
				case EType::BLENDWEIGHTS:
					IQMASSERT( prim == IQM_P::UBYTE, "Error: Type of vertex attribute incorrect, expected a byte" );
					break;
				default:
					IQMASSERT( false, "Error: Unknown vertex data type encountered in IQM File!" );
			}
		}
	}

	// Destructor, frees buffer
	~IQMFile()
	{
		if ( m_pHeader )
		{
			free( m_pHeader );
			m_pHeader = nullptr;
		}
	}

	// Operators and copy/move constructors
	IQMFile( const IQMFile& other )
	{
		m_pHeader = (Header *) malloc( other.m_pHeader->filesize );
		if ( m_pHeader == nullptr )
			throw std::bad_alloc();
		memcpy( m_pHeader, other.m_pHeader, other.m_pHeader->filesize );
	}

	IQMFile( IQMFile&& other )
	{
		m_pHeader = other.m_pHeader;
		other.m_pHeader = nullptr;
	}

	IQMFile& operator=( const IQMFile& other )
	{
		m_pHeader = (Header *) malloc( other.m_pHeader->filesize );
		if ( m_pHeader == nullptr )
			throw std::bad_alloc();
		memcpy( m_pHeader, other.m_pHeader, other.m_pHeader->filesize );
		return *this;
	}

	IQMFile& operator=( IQMFile&& other )
	{
		m_pHeader = other.m_pHeader;
		other.m_pHeader = nullptr;
		return *this;
	}

	// Get string stored in file
	const char * GetString( uint32_t uStringOffset )
	{
		if ( uStringOffset < m_pHeader->num_text )
			return &((char *) m_pHeader)[m_pHeader->ofs_text];
		return nullptr;
	}

	template <typename N, EType C, typename T = N>
	class IqmAttr
	{
		const IQMFile * m_pFile;
	protected:
		// Only IqmFile can construct
		friend class IQMFile;

		IqmAttr( const IQMFile * pFile )
			: m_pFile( pFile )
		{	// Make sure the data size of T and N work out such that no data is left out
			static_assert(!(sizeof( T ) % sizeof( N )) || !(sizeof( N ) % sizeof( T )), "IQM Error: In order to create an IqmAttr, the Native Type must divide evenly into sizeof(N) / sizeof(T) units, or vice versa.");
		}

	public:
		// The count of this data type within the file
		inline uint32_t count() const noexcept
		{
			size_t ratio = (sizeof( T ) > sizeof( N ) ? sizeof( T ) / sizeof( N ) : sizeof( N ) / sizeof( T ));
			return ratio * m_pFile->getNum( C );
		}
		// The size of the data in bytes
		inline uint32_t numBytes() const noexcept
		{
			return count() * sizeof( T );
		}
		// Type Size
		inline size_t size() const noexcept
		{
			return sizeof( T );
		}
		// Native Size
		inline size_t nativeSize() const noexcept
		{
			return sizeof( N );
		}
		// Pointer to location in file
		inline T * ptr() const noexcept
		{
			return m_pFile->getPtr<T>( C );
		}
		// Array operator
		inline T& operator[]( const uint32_t idx ) const
		{
			return ptr()[idx];
		}
	};
	// Get Attr; can return attr of different type than native, provided the strides work out
	template <typename N, EType C, typename T = N>
	inline IqmAttr<N, C, T> getAttr() const noexcept
	{
		return IqmAttr<N, C, T>( this );
	}


	// structs and enums needed when loading the file
private:
	enum class IQM_P : uint32_t
	{	// Primitive Types
		BYTE = 0,
		UBYTE = 1,
		SHORT = 2,
		USHORT = 3,
		INT = 4,
		UINT = 5,
		HALF = 6,
		FLOAT = 7,
		DOUBLE = 8
	};

	struct iqmvertexarray
	{	// vertex array in the file
		uint32_t type;
		uint32_t flags;
		uint32_t format;
		uint32_t size;
		uint32_t offset;
	};

	// Convenient public access functions
public:
	// I think I can declare these with a template rather than a macro... generates a specific getAttr funcion
#define IQMATTRFNGENMACRO(N,C,fn) template <typename T = N> inline IqmAttr<N, C, T> fn() const noexcept { return getAttr<N, C, T>(); }
	// Returns Position Attr, a float3 type
	IQMATTRFNGENMACRO( iqmposition, EType::POSITION, Positions );
	// Returns Tex Coord Attr, a float2 type
	IQMATTRFNGENMACRO( iqmtexcoord, EType::TEXCOORD, TexCoords );
	// Returns Normals Attr, a float3 type
	IQMATTRFNGENMACRO( iqmnormal, EType::NORMAL, Normals );
	// Returns Tangents Attr, a float4 type
	IQMATTRFNGENMACRO( iqmtangent, EType::TANGENT, Tangents );
	// Returns Blend Indices Attr, a uchar4 type
	IQMATTRFNGENMACRO( iqmblendidx, EType::BLENDINDEXES, BlendIndices );
	// Returns Blend Weights Attr, a uchar4 type
	IQMATTRFNGENMACRO( iqmblendweight, EType::BLENDWEIGHTS, BlendWeights );
	// Returns Meshes
	IQMATTRFNGENMACRO( iqmmesh, EType::MESH, Meshes );
	// Returns Triangles (Geometry Indices)
	IQMATTRFNGENMACRO( iqmtriangle, EType::TRI, Triangles );
	// Returns Joints
	IQMATTRFNGENMACRO( iqmjoint, EType::JOINT, Joints );
	// Returns anims
	IQMATTRFNGENMACRO( iqmanim, EType::ANIM, Anims );
	// Returns frames
	IQMATTRFNGENMACRO( uint16_t, EType::FRAME, Frames );
	// Returns triangles as uint32_t rather than iqmtriangle
	auto Indices()->decltype(Triangles<uint32_t>()) { return Triangles<uint32_t>(); }
	// Get # of frames
	inline uint32_t getNumFrames() { return m_pHeader->num_frames; }
};