#include "io/MeshFile.h"
#include "model/nifmodel.h"
#include "qtcompat.h"

#include <QByteArray>
#include <QDataStream>
#include <QBuffer>
#include "fp32vec4.hpp"

#if 0
// x/32767 matches the min/max bounds in BSGeometry more accurately on average
double snormToDouble(int16_t x) { return x < 0 ? x / double(32768) : x / double(32767); }
#endif

MeshFile::MeshFile( const void * data, size_t size )
{
	update( data, size );
}

MeshFile::MeshFile( const NifModel * nif, const QString & path )
{
	update( nif, path );
}

MeshFile::MeshFile( const NifModel * nif, const QModelIndex & index )
{
	update( nif, index );
}

void MeshFile::clear()
{
	if ( !haveData )
		return;

	positions.clear();
	normals.clear();
	colors.clear();
	tangents.clear();
	bitangentsBasis.clear();
	haveTexCoord2 = false;
	coords.clear();
	weights.clear();
	weightsPerVertex = 0;
	triangles.clear();
	lods.clear();

	haveData = false;
}

void MeshFile::update( const void * data, size_t size )
{
	clear();
	if ( !( data && size > 0 ) )
		return;

	QBuffer	f;
	f.setData( reinterpret_cast< const char * >( data ), qsizetype( size ) );
	if ( !f.open(QIODevice::ReadOnly) )
		return;

	QDataStream	in;
	in.setDevice(&f);
	in.setByteOrder(QDataStream::LittleEndian);
	in.setFloatingPointPrecision(QDataStream::SinglePrecision);

	quint32 magic;
	in >> magic;
	if ( magic > 2U )
		return;

	quint32 indicesSize;
	in >> indicesSize;
	triangles.resize( indicesSize / 3 );
	haveData = true;

	for ( quint32 i = 0; i < indicesSize / 3; i++ ) {
		Triangle tri;
		in >> tri;
		triangles[i] = tri;
	}

	float scale;
	in >> scale;
	if ( scale <= 0.0f ) {
		clear();
		return; // From RE
	}

	quint32 numWeightsPerVertex;
	in >> numWeightsPerVertex;
	weightsPerVertex = numWeightsPerVertex;

	quint32 numPositions;
	in >> numPositions;
	if ( !numPositions ) {
		clear();
		return;
	}
	positions.resize( numPositions );

	for ( int i = 0; i < int(numPositions); i++ ) {
		uint32_t	xy;
		uint16_t	z;

		in >> xy;
		in >> z;
		FloatVector4	xyz(FloatVector4::convertInt16((std::uint64_t(z) << 32) | xy));
		xyz /= 32767.0f;
		xyz *= scale;

		positions[i] = Vector3(xyz[0], xyz[1], xyz[2]);
	}

	quint32 numCoord1;
	in >> numCoord1;
	coords.resize( numCoord1 );

	for ( int i = 0; i < int(numCoord1); i++ ) {
		uint32_t uv;

		in >> uv;

		coords[i] = Vector4(FloatVector4::convertFloat16(uv));
	}

	quint32 numCoord2;
	in >> numCoord2;
	numCoord2 = std::min( numCoord2, numCoord1 );
	haveTexCoord2 = bool( numCoord2 );

	for ( quint32 i = 0; i < numCoord2; i++ ) {
		uint32_t uv;

		in >> uv;
		FloatVector4  uv_f(FloatVector4::convertFloat16(uv));

		coords[i][2] = uv_f[0];
		coords[i][3] = uv_f[1];
	}

	quint32 numColor;
	in >> numColor;
	if ( numColor > 0 ) {
		colors.resize( numColor );
	}
	for ( quint32 i = 0; i < numColor; i++ ) {
		uint32_t	bgra;
		in >> bgra;
		colors[i] = Color4( ( FloatVector4(bgra) / 255.0f ).shuffleValues( 0xC6 ) );	// 2, 1, 0, 3
	}

	quint32 numNormal;
	in >> numNormal;
	if ( numNormal > 0 ) {
		normals.resize( numNormal );
	}
	for ( int i = 0; i < int(numNormal); i++ ) {
		std::uint32_t	n;
		in >> n;
		normals[i] = Vector3( UDecVector4(n) );
	}

	quint32 numTangent;
	in >> numTangent;
	if ( numTangent > 0 ) {
		tangents.resize( numTangent );
		bitangentsBasis.resize( numTangent );
	}
	for ( int i = 0; i < int(numTangent); i++ ) {
		std::uint32_t	n;
		in >> n;
		UDecVector4	v( n );
		tangents[i] = Vector3( v );
		bitangentsBasis[i] = v[3];
	}

	quint32 numWeights;
	in >> numWeights;
	if ( numWeights > 0 && numWeightsPerVertex > 0 ) {
		weights.resize(numWeights / numWeightsPerVertex);
	}
	for ( int i = 0; i < weights.count(); i++ ) {
		QVector<QPair<quint16, quint16>> weightsUNORM;
		for ( quint32 j = 0; j < 8; j++ ) {
			if ( j < numWeightsPerVertex ) {
				quint16 b, w;
				in >> b;
				in >> w;
				weightsUNORM.append({ b, w });
			} else {
				weightsUNORM.append({0, 0});
			}
		}
		weights[i] = BoneWeightsUNorm(weightsUNORM, i);
	}

	if ( magic ) {
		quint32 numLODs;
		in >> numLODs;
		lods.resize(numLODs);
		for ( quint32 i = 0; i < numLODs; i++ ) {
			quint32 indicesSize2;
			in >> indicesSize2;
			lods[i].resize(indicesSize2 / 3);

			for ( quint32 j = 0; j < indicesSize2 / 3; j++ ) {
				Triangle tri;
				in >> tri;
				lods[i][j] = tri;
			}
		}
	}
}

void MeshFile::update( const NifModel * nif, const QString & path )
{
	clear();

	if ( path.isEmpty() || !nif )
		return;

	QByteArray	data;
	if ( nif->getResourceFile( data, path, "geometries", ".mesh" ) )
		update( data.data(), size_t(data.size()) );
	if ( haveData )
		qDebug() << "MeshFile created for" << path;
	else
		qWarning() << "MeshFile creation failed for" << path;
}

void MeshFile::update( const NifModel * nif, const QModelIndex & index )
{
	clear();
	if ( !( nif && index.isValid() ) )
		return;
	auto	meshPath = nif->getIndex( index, "Mesh Path" );
	if ( meshPath.isValid() ) {
		update( nif, nif->get<QString>( meshPath ) );
		return;
	}
	auto	meshData = nif->getIndex( index, "Mesh Data" );
	if ( !meshData.isValid() )
		return;

	quint32 magic = nif->get<quint32>( meshData, "Version" );
	if ( magic > 2U )
		return;

	quint32 indicesSize = nif->get<quint32>( meshData, "Indices Size" );
	auto	trianglesIndex = nif->getIndex( meshData, "Triangles" );
	if ( !trianglesIndex.isValid() )
		indicesSize = 0;
	triangles.resize( indicesSize / 3 );
	haveData = true;

	for ( int i = 0; i < int(indicesSize / 3); i++ )
		triangles[i] = nif->get<Triangle>( QModelIndex_child( trianglesIndex, i ) );

	float	scale = nif->get<float>( meshData, "Scale" );
	if ( scale <= 0.0f ) {
		clear();
		return; // From RE
	}

	quint32	numWeightsPerVertex = nif->get<quint32>( meshData, "Weights Per Vertex" );
	weightsPerVertex = numWeightsPerVertex;

	quint32	numPositions = nif->get<quint32>( meshData, "Num Verts" );
	auto	verticesIndex = nif->getIndex( meshData, "Vertices" );
	if ( !( numPositions && verticesIndex.isValid() ) ) {
		clear();
		return;
	}
	positions.resize( numPositions );

	for ( int i = 0; i < int(numPositions); i++ ) {
		Vector3	xyz = nif->get<Vector3>( QModelIndex_child( verticesIndex, i ) );
		xyz /= 32767.0f;
		xyz *= scale;

		positions[i] = xyz;
	}

	quint32 numCoord1 = nif->get<quint32>( meshData, "Num UVs" );
	auto	uvIndex1 = nif->getIndex( meshData, "UVs" );
	if ( !uvIndex1.isValid() )
		numCoord1 = 0;
	coords.resize( numCoord1 );

	for ( int i = 0; i < int(numCoord1); i++ ) {
		HalfVector2	uv = nif->get<HalfVector2>( QModelIndex_child( uvIndex1, i ) );

		coords[i] = Vector4( uv[0], uv[1], 0.0f, 0.0f );
	}

	quint32 numCoord2 = nif->get<quint32>( meshData, "Num UVs 2" );
	auto	uvIndex2 = nif->getIndex( meshData, "UVs 2" );
	if ( !uvIndex2.isValid() )
		numCoord2 = 0;
	numCoord2 = std::min( numCoord2, numCoord1 );
	haveTexCoord2 = bool( numCoord2 );

	for ( quint32 i = 0; i < numCoord2; i++ ) {
		HalfVector2	uv = nif->get<HalfVector2>( QModelIndex_child( uvIndex2, i ) );

		coords[i][2] = uv[0];
		coords[i][3] = uv[1];
	}

	quint32 numColor = nif->get<quint32>( meshData, "Num Vertex Colors" );
	auto	colorsIndex = nif->getIndex( meshData, "Vertex Colors" );
	if ( !colorsIndex.isValid() )
		numColor = 0;
	if ( numColor > 0 )
		colors.resize( numColor );
	for ( int i = 0; i < int(numColor); i++ )
		colors[i] = nif->get<Color4>( QModelIndex_child( colorsIndex, i ) );

	quint32 numNormal = nif->get<quint32>( meshData, "Num Normals" );
	auto	normalsIndex = nif->getIndex( meshData, "Normals" );
	if ( !normalsIndex.isValid() )
		numNormal = 0;
	if ( numNormal > 0 )
		normals.resize( numNormal );
	for ( int i = 0; i < int(numNormal); i++ )
		normals[i] = Vector3( nif->get<UDecVector4>( QModelIndex_child( normalsIndex, i ) ) );

	quint32 numTangent = nif->get<quint32>( meshData, "Num Tangents" );
	auto	tangentsIndex = nif->getIndex( meshData, "Tangents" );
	if ( !tangentsIndex.isValid() )
		numTangent = 0;
	if ( numTangent > 0 ) {
		tangents.resize( numTangent );
		bitangentsBasis.resize( numTangent );
	}
	for ( int i = 0; i < int(numTangent); i++ ) {
		Vector4	v = nif->get<Vector4>( QModelIndex_child( tangentsIndex, i ) );
		tangents[i] = Vector3( v );
		bitangentsBasis[i] = v[3];
	}

	quint32 numWeights = nif->get<quint32>( meshData, "Num Weights" );
	auto	weightsIndex = nif->getIndex( meshData, "Weights" );
	if ( !weightsIndex.isValid() )
		numWeights = 0;
	if ( numWeights > 0 && numWeightsPerVertex > 0 ) {
		weights.resize(numWeights / numWeightsPerVertex);
	}
	int	k = 0;
	for ( int i = 0; i < weights.count(); i++ ) {
		QVector<QPair<quint16, quint16>> weightsUNORM;
		for ( quint32 j = 0; j < 8; j++ ) {
			if ( j < numWeightsPerVertex ) {
				auto	weightIndex = QModelIndex_child( weightsIndex, k );
				quint16	b = nif->get<quint16>( QModelIndex_child( weightIndex, 0 ) );
				quint16	w = nif->get<quint16>( QModelIndex_child( weightIndex, 1 ) );
				weightsUNORM.append({ b, w });
				k++;
			} else {
				weightsUNORM.append({0, 0});
			}
		}
		weights[i] = BoneWeightsUNorm(weightsUNORM, i);
	}

	quint32	numLODs = 0;
	auto	lodsIndex = nif->getIndex( meshData, "LODs" );
	if ( lodsIndex.isValid() )
		numLODs = nif->get<quint32>( meshData, "Num LODs" );
	lods.resize(numLODs);
	for ( quint32 i = 0; i < numLODs; i++ ) {
		auto	lodIndex = QModelIndex_child( lodsIndex, int(i) );
		if ( !lodIndex.isValid() )
			continue;
		auto	trianglesIndex2 = nif->getIndex( lodIndex, "Triangles" );
		if ( !trianglesIndex2.isValid() )
			continue;
		quint32 indicesSize2 = nif->get<quint32>( lodIndex, "Indices Size" );
		lods[i].resize(indicesSize2 / 3);

		for ( quint32 j = 0; j < indicesSize2 / 3; j++ )
			lods[i][j] = nif->get<Triangle>( QModelIndex_child( trianglesIndex2, int(j) ) );
	}
}

void MeshFile::calculateBitangents( QVector<Vector3> & bitangents ) const
{
	bitangents.clear();
	qsizetype	n = tangents.size();
	bitangents.resize( n );
	const Vector3 *	srcN = normals.data();
	const Vector3 *	srcT = tangents.data();
	const float *	srcB = bitangentsBasis.data();
	Vector3 *	dstB = bitangents.data();
	qsizetype	m = std::min< qsizetype >( n, normals.size() );
	m = std::min< qsizetype >( m, bitangentsBasis.size() );
	qsizetype	i = 0;
	for ( ; (i + 1) < m; i++ ) {
		FloatVector4	t( &(srcT[i][0]) );
		t = FloatVector4( &(srcN[i][0]) ).crossProduct3( t * srcB[i] );
		dstB[i].fromFloatVector4( t );
	}
	for ( ; i < n; i++ ) {
		FloatVector4	t( srcT[i] );
		FloatVector4	normal( 0.0f, 0.0f, 1.0f, 0.0f );
		if ( i < normals.size() )
			normal = FloatVector4( srcN[i] );
		if ( i < bitangentsBasis.size() )
			t *= srcB[i];
		dstB[i].fromFloatVector4( normal.crossProduct3( t ) );
	}
}
