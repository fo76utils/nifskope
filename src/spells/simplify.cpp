#include "mesh.h"
#include "gl/gltools.h"
#include "qtcompat.h"

#include <QSettings>
#include <cfloat>
#include <unordered_set>

#include "libfo76utils/src/fp32vec4.hpp"
#include "meshoptimizer/meshoptimizer.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file simplify.cpp
 * \brief LOD generation spell
 *
 * All classes here inherit from the Spell class.
 */

//! Simplifies Starfield meshes
class spSimplifySFMesh final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Generate LODs" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		if ( !index.isValid() )
			return true;
		return ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 );
	}

	static void simplifyMesh( NifModel * nif, const QModelIndex & index, bool noMessages = false );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

void spSimplifySFMesh::simplifyMesh( NifModel * nif, const QModelIndex & index, bool noMessages )
{
	if ( !( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 ) )
		return;

	float	targetCnts[3];
	float	targetErrs[3];
	int	minTriCnts[3];
	{
		QSettings	settings;
		for ( int i = 0; i < 3; i++ ) {
			float	x = 0.2f / float( 1 << i );
			x = settings.value( QString("Settings/Nif/Sf LOD Gen Target Cnt %1").arg(i + 1), x ).toFloat();
			targetCnts[i] = std::min( std::max( x, 0.0f ), 1.0f );
			x = 0.005f * float( 1 << i );
			x = settings.value( QString("Settings/Nif/Sf LOD Gen Target Err %1").arg(i + 1), x ).toFloat();
			targetErrs[i] = std::min( std::max( x, 0.0f ), 1.0f );
			int	n = 200 >> i;
			n = settings.value( QString("Settings/Nif/Sf LOD Gen Min Tri Cnt %1").arg(i + 1), n ).toInt();
			minTriCnts[i] = std::min< int >( std::max< int >( n, 0 ), 1000000 );
		}
	}

	QModelIndex	iMeshData = nif->getIndex( index, "Meshes" );
	if ( iMeshData.isValid() )
		iMeshData = QModelIndex_child( iMeshData, 0 );
	if ( iMeshData.isValid() && nif->get<bool>( iMeshData, "Has Mesh" ) )
		iMeshData = nif->getIndex( iMeshData, "Mesh" );
	else
		return;
	if ( iMeshData.isValid() )
		iMeshData = nif->getIndex( iMeshData, "Mesh Data" );
	if ( !iMeshData.isValid() )
		return;

	std::uint32_t	numVerts = nif->get<quint32>( iMeshData, "Num Verts" );
	std::uint32_t	numTriangles = nif->get<quint32>( iMeshData, "Indices Size" ) / 3U;
	if ( !numVerts || !numTriangles )
		return;
	std::uint32_t	weightsPerVertex = nif->get<quint32>( iMeshData, "Weights Per Vertex" );
	std::uint32_t	numUVs = nif->get<quint32>( iMeshData, "Num UVs" );
	std::uint32_t	numUVs2 = nif->get<quint32>( iMeshData, "Num UVs 2" );
	std::uint32_t	numColors = nif->get<quint32>( iMeshData, "Num Vertex Colors" );
	std::uint32_t	numNormals = nif->get<quint32>( iMeshData, "Num Normals" );
	std::uint32_t	numTangents = nif->get<quint32>( iMeshData, "Num Tangents" );
	std::uint32_t	numWeights = nif->get<quint32>( iMeshData, "Num Weights" );
	if ( ( numUVs && numUVs != numVerts ) || ( numUVs2 && numUVs2 != numVerts )
		|| ( numColors && numColors != numVerts ) || ( numNormals && numNormals != numVerts )
		|| ( numTangents && numTangents != numVerts ) || ( numWeights != ( size_t(numVerts) * weightsPerVertex ) ) ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Mesh has inconsistent number of vertex attributes, cannot generate LODs") );
		return;
	}

	std::vector< float >	positions( size_t(numVerts) * 3 );
	for ( auto i = nif->getItem( iMeshData, "Vertices" ); i; ) {
		for ( size_t j = 0; j < numVerts; j++ ) {
			Vector3	tmp = nif->get<Vector3>( i->child( int(j) ) );
			positions[j * 3] = tmp[0];
			positions[j * 3 + 1] = tmp[1];
			positions[j * 3 + 2] = tmp[2];
		}
		break;
	}

	size_t	indicesCnt = size_t(numTriangles) * 3;
	std::vector< unsigned int >	indices( indicesCnt );
	for ( auto i = nif->getItem( iMeshData, "Triangles" ); i; ) {
		for ( size_t j = 0; j < numTriangles; j++ ) {
			Triangle	tmp = nif->get<Triangle>( i->child( int(j) ) );
			if ( tmp[0] >= numVerts || tmp[1] >= numVerts || tmp[2] >= numVerts ) {
				QMessageBox::critical( nullptr, "NifSkope error", QString("Mesh has invalid indices, cannot generate LODs") );
				return;
			}
			indices[j * 3] = tmp[0];
			indices[j * 3 + 1] = tmp[1];
			indices[j * 3 + 2] = tmp[2];
		}
		break;
	}

	for ( auto i = nif->getItem( index ); i; ) {
		i->invalidateVersionCondition();
		i->invalidateCondition();
		break;
	}
	nif->set<quint32>( iMeshData, "Num LODs", 0 );
	for ( auto i = nif->getIndex( iMeshData, "LODs" ); i.isValid(); ) {
		nif->updateArraySize( i );
		break;
	}

	std::vector< unsigned int >	newIndices( indicesCnt );
	QVector< Triangle >	newTriangles;
	bool	isSkinned = ( weightsPerVertex != 0 || nif->getBlockIndex( nif->getLink( index, "Skin" ) ).isValid() );
	bool	lastLevel = false;
	Vector3	err;
	for ( int l = 0; l < 3; l++ ) {
		size_t	newIndicesCnt = 0;
		if ( targetCnts[l] >= 0.0005f && targetErrs[l] < 0.99995f && !lastLevel ) {
			int	targetCnt = std::max< int >( roundFloat( float( int(numTriangles) ) * targetCnts[l] ), minTriCnts[l] );
			if ( std::uint32_t(targetCnt) >= numTriangles ) {
				newIndicesCnt = indicesCnt;
				std::memcpy( newIndices.data(), indices.data(), indicesCnt * sizeof(unsigned int) );
			} else {
				newIndicesCnt = meshopt_simplify( newIndices.data(), indices.data(), indicesCnt,
													positions.data(), numVerts, sizeof(float) * 3,
													size_t(targetCnt) * 3, targetErrs[l], 0U, &(err[l]) );
			}
		} else {
			lastLevel = true;
		}

		newTriangles.resize( qsizetype( newIndicesCnt / 3 ) );
		for ( auto & t : newTriangles ) {
			size_t	i = size_t( &t - newTriangles.data() ) * 3;
			t[0] = quint16( newIndices[i] );
			t[1] = quint16( newIndices[i + 1] );
			t[2] = quint16( newIndices[i + 2] );
		}

		if ( !isSkinned ) {
			QModelIndex	iMesh = nif->getIndex( index, "Meshes" );
			if ( !iMesh.isValid() )
				continue;
			iMesh = QModelIndex_child( iMesh, l + 1 );
			if ( !iMesh.isValid() )
				continue;
			nif->set<bool>( iMesh, "Has Mesh", bool(newIndicesCnt) );
			if ( !newIndicesCnt )
				continue;
			iMesh = nif->getIndex( iMesh, "Mesh" );
			if ( !iMesh.isValid() )
				continue;
			nif->set<quint32>( iMesh, "Indices Size", quint32(newIndicesCnt) );
			nif->set<quint32>( iMesh, "Num Verts", numVerts );
			nif->set<quint32>( iMesh, "Flags", 64 );
			iMesh = nif->getIndex( iMesh, "Mesh Data" );
			if ( !iMesh.isValid() )
				continue;
			nif->set<quint32>( iMesh, "Version", 2 );
			nif->set<quint32>( iMesh, "Indices Size", quint32(newIndicesCnt) );
			QModelIndex	i;
			if ( ( i = nif->getIndex( iMesh, "Triangles" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<Triangle>( i, newTriangles );
			}
			nif->set<float>( iMesh, "Scale", nif->get<float>( iMeshData, "Scale" ) );
			nif->set<quint32>( iMesh, "Weights Per Vertex", weightsPerVertex );
			nif->set<quint32>( iMesh, "Num Verts", numVerts );
			if ( ( i = nif->getIndex( iMesh, "Vertices" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<ShortVector3>( i, nif->getArray<ShortVector3>( iMeshData, "Vertices" ) );
			}
			nif->set<quint32>( iMesh, "Num UVs", numUVs );
			if ( ( i = nif->getIndex( iMesh, "UVs" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<HalfVector2>( i, nif->getArray<HalfVector2>( iMeshData, "UVs" ) );
			}
			nif->set<quint32>( iMesh, "Num UVs 2", numUVs2 );
			if ( ( i = nif->getIndex( iMesh, "UVs 2" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<HalfVector2>( i, nif->getArray<HalfVector2>( iMeshData, "UVs 2" ) );
			}
			nif->set<quint32>( iMesh, "Num Vertex Colors", numColors );
			if ( ( i = nif->getIndex( iMesh, "Vertex Colors" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<ByteColor4BGRA>( i, nif->getArray<ByteColor4BGRA>( iMeshData, "Vertex Colors" ) );
			}
			nif->set<quint32>( iMesh, "Num Normals", numNormals );
			if ( ( i = nif->getIndex( iMesh, "Normals" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<UDecVector4>( i, nif->getArray<UDecVector4>( iMeshData, "Normals" ) );
			}
			nif->set<quint32>( iMesh, "Num Tangents", numTangents );
			if ( ( i = nif->getIndex( iMesh, "Tangents" ) ).isValid() ) {
				nif->updateArraySize( i );
				nif->setArray<UDecVector4>( i, nif->getArray<UDecVector4>( iMeshData, "Tangents" ) );
			}
			nif->set<quint32>( iMesh, "Num Weights", 0 );
			nif->set<quint32>( iMesh, "Num LODs", 0 );
			nif->set<quint32>( iMesh, "Num Meshlets", 0 );
			nif->set<quint32>( iMesh, "Num Cull Data", 0 );
		} else if ( newIndicesCnt ) {
			NifItem *	i = nif->getItem( iMeshData );
			if ( i ) {
				i->invalidateVersionCondition();
				i->invalidateCondition();
			}
			nif->set<quint32>( iMeshData, "Num LODs", quint32(l + 1) );
			auto	lodsIndex = nif->getIndex( iMeshData, "LODs" );
			if ( lodsIndex.isValid() ) {
				nif->updateArraySize( lodsIndex );
				auto	lodIndex = QModelIndex_child( lodsIndex, l );
				if ( lodIndex.isValid() ) {
					nif->set<quint32>( lodIndex, "Indices Size", quint32(newIndicesCnt) );
					QModelIndex	lodTrianglesIndex;
					if ( ( lodTrianglesIndex = nif->getIndex( lodIndex, "Triangles" ) ).isValid() ) {
						nif->updateArraySize( lodTrianglesIndex );
						nif->setArray<Triangle>( lodTrianglesIndex, newTriangles );
					}
				}
			}
		}
	}

	spRemoveWasteVertices::cast_Starfield( nif, index, noMessages );
}

QModelIndex spSimplifySFMesh::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return index;

	nif->setState( BaseModel::Processing );
	if ( index.isValid() ) {
		simplifyMesh( nif, index );
	} else {
		for ( int b = 0; b < nif->getBlockCount(); b++ )
			simplifyMesh( nif, nif->getBlockIndex( quint32(b) ), true );
	}
	nif->restoreState();

	return index;
}

REGISTER_SPELL( spSimplifySFMesh )

