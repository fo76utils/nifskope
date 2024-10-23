#include "mesh.h"
#include "gl/gltools.h"

#include <QDialog>
#include <QGridLayout>
#include <QSettings>
#include <cfloat>
#include <unordered_set>

#include "fp32vec4.hpp"
#include "io/MeshFile.h"
#include "meshoptimizer/meshoptimizer.h"
#include "meshlet.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file mesh.cpp
 * \brief Mesh spells
 *
 * All classes here inherit from the Spell class.
 */

//! Find shape data of triangle geometry
QModelIndex spRemoveWasteVertices::getShape( const NifModel * nif, const QModelIndex & index )
{
	QModelIndex iShape = nif->getBlockIndex( index );

	if ( nif->isNiBlock( iShape, "NiTriBasedGeomData" ) )
		iShape = nif->getBlockIndex( nif->getParent( nif->getBlockNumber( iShape ) ) );

	if ( nif->isNiBlock( iShape, { "NiTriShape", "BSLODTriShape", "NiTriStrips" } ) )
		if ( nif->getBlockIndex( nif->getLink( iShape, "Data" ), "NiTriBasedGeomData" ).isValid() )
			return iShape;


	return QModelIndex();
}

//! Find triangle geometry
/*!
 * Subtly different to getShape(); that requires
 * <tt>nif->getBlockIndex( nif->getLink( getShape( nif, index ), "Data" ) );</tt>
 * to return the same result.
 */
static QModelIndex getTriShapeData( const NifModel * nif, const QModelIndex & index )
{
	QModelIndex iData = nif->getBlockIndex( index );

	if ( nif->isNiBlock( index, { "NiTriShape", "BSLODTriShape" } ) )
		iData = nif->getBlockIndex( nif->getLink( index, "Data" ) );

	if ( nif->isNiBlock( iData, "NiTriShapeData" ) )
		return iData;

	return QModelIndex();
}

//! Removes elements of the specified type from an array
template <typename T> static void removeFromArray( QVector<T> & array, QMap<quint16, bool> map )
{
	for ( int x = array.count() - 1; x >= 0; x-- ) {
		if ( !map.contains( x ) )
			array.remove( x );
	}
}

//! Removes waste vertices from the specified data and shape
static void removeWasteVertices( NifModel * nif, const QModelIndex & iData, const QModelIndex & iShape )
{
	try
	{
		// read the data

		QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );

		if ( !verts.count() ) {
			throw QString( Spell::tr( "No vertices" ) );
		}

		QVector<Vector3> norms = nif->getArray<Vector3>( iData, "Normals" );
		QVector<Color4> colors = nif->getArray<Color4>( iData, "Vertex Colors" );
		QList<QVector<Vector2> > texco;
		QModelIndex iUVSets = nif->getIndex( iData, "UV Sets" );

		for ( int r = 0; r < nif->rowCount( iUVSets ); r++ ) {
			texco << nif->getArray<Vector2>( nif->getIndex( iUVSets, r ) );

			if ( texco.last().count() != verts.count() )
				throw QString( Spell::tr( "UV array size differs" ) );
		}

		int numVerts = verts.count();

		if ( numVerts != nif->get<int>( iData, "Num Vertices" )
		     || ( norms.count() && norms.count() != numVerts )
		     || ( colors.count() && colors.count() != numVerts ) )
		{
			throw QString( Spell::tr( "Vertex array size differs" ) );
		}

		// detect unused vertices

		QMap<quint16, bool> used;

		QVector<Triangle> tris = nif->getArray<Triangle>( iData, "Triangles" );
		for ( const Triangle& tri : tris ) {
			for ( int t = 0; t < 3; t++ ) {
				used.insert( tri[t], true );
			}
		}

		QList<QVector<quint16> > strips;
		QModelIndex iPoints = nif->getIndex( iData, "Points" );

		for ( int r = 0; r < nif->rowCount( iPoints ); r++ ) {
			strips << nif->getArray<quint16>( nif->getIndex( iPoints, r ) );
			for ( const auto p : strips.last() ) {
				used.insert( p, true );
			}
		}

		// remove them

		Message::info( nullptr, Spell::tr( "Removed %1 vertices" ).arg( verts.count() - used.count() ) );

		if ( verts.count() == used.count() )
			return;

		removeFromArray( verts, used );
		removeFromArray( norms, used );
		removeFromArray( colors, used );

		for ( int c = 0; c < texco.count(); c++ )
			removeFromArray( texco[c], used );

		// adjust the faces

		QMap<quint16, quint16> map;
		quint16 y = 0;

		for ( quint16 x = 0; x < numVerts; x++ ) {
			if ( used.contains( x ) )
				map.insert( x, y++ );
		}

		QMutableVectorIterator<Triangle> itri( tris );

		while ( itri.hasNext() ) {
			Triangle & tri = itri.next();

			for ( int t = 0; t < 3; t++ ) {
				if ( map.contains( tri[t] ) )
					tri[t] = map[ tri[t] ];
			}

		}

		QMutableListIterator<QVector<quint16> > istrip( strips );

		while ( istrip.hasNext() ) {
			QVector<quint16> & strip = istrip.next();

			for ( int s = 0; s < strip.size(); s++ ) {
				if ( map.contains( strip[s] ) )
					strip[s] = map[ strip[s] ];
			}
		}

		// write back the data

		nif->setArray<Triangle>( iData, "Triangles", tris );

		for ( int r = 0; r < nif->rowCount( iPoints ); r++ )
			nif->setArray<quint16>( nif->getIndex( iPoints, r ), strips[r] );

		nif->set<int>( iData, "Num Vertices", verts.count() );
		nif->updateArraySize( iData, "Vertices" );
		nif->setArray<Vector3>( iData, "Vertices", verts );
		nif->updateArraySize( iData, "Normals" );
		nif->setArray<Vector3>( iData, "Normals", norms );
		nif->updateArraySize( iData, "Vertex Colors" );
		nif->setArray<Color4>( iData, "Vertex Colors", colors );

		for ( int r = 0; r < nif->rowCount( iUVSets ); r++ ) {
			nif->updateArraySize( nif->getIndex( iUVSets, r ) );
			nif->setArray<Vector2>( nif->getIndex( iUVSets, r ), texco[r] );
		}

		// process NiSkinData

		QModelIndex iSkinInst = nif->getBlockIndex( nif->getLink( iShape, "Skin Instance" ), "NiSkinInstance" );

		QModelIndex iSkinData = nif->getBlockIndex( nif->getLink( iSkinInst, "Data" ), "NiSkinData" );
		QModelIndex iBones = nif->getIndex( iSkinData, "Bone List" );

		for ( int b = 0; b < nif->rowCount( iBones ); b++ ) {
			QVector<QPair<int, float> > weights;
			QModelIndex iWeights = nif->getIndex( nif->getIndex( iBones, b ), "Vertex Weights" );

			for ( int w = 0; w < nif->rowCount( iWeights ); w++ ) {
				weights.append( QPair<int, float>( nif->get<int>( nif->getIndex( iWeights, w ), "Index" ), nif->get<float>( nif->getIndex( iWeights, w ), "Weight" ) ) );
			}

			for ( int x = weights.count() - 1; x >= 0; x-- ) {
				if ( !used.contains( weights[x].first ) )
					weights.remove( x );
			}

			QMutableVectorIterator<QPair<int, float> > it( weights );

			while ( it.hasNext() ) {
				QPair<int, float> & w = it.next();

				if ( map.contains( w.first ) )
					w.first = map[ w.first ];
			}

			nif->set<int>( nif->getIndex( iBones, b ), "Num Vertices", weights.count() );
			nif->updateArraySize( iWeights );

			for ( int w = 0; w < weights.count(); w++ ) {
				nif->set<int>( nif->getIndex( iWeights, w ), "Index", weights[w].first );
				nif->set<float>( nif->getIndex( iWeights, w ), "Weight", weights[w].second );
			}
		}

		// process NiSkinPartition

		QModelIndex iSkinPart = nif->getBlockIndex( nif->getLink( iSkinInst, "Skin Partition" ), "NiSkinPartition" );

		if ( !iSkinPart.isValid() )
			iSkinPart = nif->getBlockIndex( nif->getLink( iSkinData, "Skin Partition" ), "NiSkinPartition" );

		if ( iSkinPart.isValid() ) {
			nif->removeNiBlock( nif->getBlockNumber( iSkinPart ) );
			Message::warning( nullptr, Spell::tr( "The skin partition was removed, please regenerate it with the skin partition spell" ) );
		}
	}
	catch ( QString & e )
	{
		Message::warning( nullptr, Spell::tr( "There were errors during the operation" ), e );
	}
}

//! Removes waste vertices from the specified BSTriShape

static void removeWasteVertices( NifModel * nif, const QModelIndex & iShape )
{
	try
	{
		// read the data
		quint32	numTriangles = nif->get<quint32>( iShape, "Num Triangles" );
		quint32	numVertices = nif->get<quint32>( iShape, "Num Vertices" );
		QModelIndex	iVertexData = nif->getIndex( iShape, "Vertex Data" );
		QModelIndex	iTriangleData = nif->getIndex( iShape, "Triangles" );
		if ( !numTriangles || !iTriangleData.isValid() )
			throw QString( Spell::tr( "No triangles" ) );
		if ( !numVertices || !iVertexData.isValid() )
			throw QString( Spell::tr( "No vertices" ) );
		if ( nif->getBlockIndex( nif->getLink( iShape, "Skin" ) ).isValid() )
			throw QString( Spell::tr( "Skinned meshes are not supported yet" ) );
		if ( int(numVertices) != nif->rowCount( iVertexData ) )
			throw QString( Spell::tr( "Vertex array size differs" ) );

		// detect unused vertices

		QMap<quint16, quint16> used;

		QVector<Triangle> tris = nif->getArray<Triangle>( iShape, "Triangles" );
		for ( const Triangle& tri : tris ) {
			for ( int t = 0; t < 3; t++ ) {
				used.insert( tri[t], 0 );
			}
		}

		// remove them

		Message::info( nullptr, Spell::tr( "Removed %1 vertices" ).arg( qsizetype(numVertices) - used.count() ) );

		if ( qsizetype(numVertices) == used.count() )
			return;

		quint16	n = 0;
		for ( auto i = used.begin(); i != used.end(); i++, n++ )
			i.value() = n;

		int	firstRow = 0;
		int	removeCnt = 0;
		for ( size_t i = numVertices; i-- > 0; ) {
			if ( used.contains(quint16(i)) ) {
				if ( removeCnt )
					nif->removeRows( firstRow, removeCnt, iVertexData );
				removeCnt = 0;
			} else {
				firstRow = int(i);
				removeCnt++;
			}
		}
		if ( removeCnt )
			nif->removeRows( firstRow, removeCnt, iVertexData );
		nif->updateArraySize( iVertexData );
		nif->set<quint32>( iShape, "Num Vertices", quint32(used.count()) );

		// adjust the faces

		QMutableVectorIterator<Triangle> itri( tris );

		while ( itri.hasNext() ) {
			Triangle & tri = itri.next();

			for ( int t = 0; t < 3; t++ ) {
				if ( used.contains( tri[t] ) )
					tri[t] = used[ tri[t] ];
			}

		}

		// write back the data

		nif->setArray<Triangle>( iShape, "Triangles", tris );

		// TODO: process NiSkinData

		// TODO: process NiSkinPartition
	}
	catch ( QString & e )
	{
		Message::warning( nullptr, Spell::tr( "There were errors during the operation" ), e );
	}
}

//! Flip texture UV coordinates
class spFlipTexCoords final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Flip UV" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return true;
		if ( nif->blockInherits( index, "BSTriShape" ) && nif->getIndex( index, "Vertex Data" ).isValid() )
			return true;
		return nif->itemStrType( index ).toLower() == "texcoord" || nif->blockInherits( index, "NiTriBasedGeomData" );
	}

	static void flip_Starfield( NifModel * nif, const QModelIndex & index, int f );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex idx = index;

		int	cmdCnt = 4;
		if ( nif->getBSVersion() < 170 ) {
			cmdCnt--;
			if ( nif->blockInherits( index, "BSTriShape" ) ) {
				idx = nif->getIndex( index, "Vertex Data" );
			} else if ( nif->itemStrType( index ).toLower() != "texcoord" ) {
				idx = nif->getIndex( nif->getBlockIndex( index ), "UV Sets" );
			}
		} else if ( !nif->checkInternalGeometry( index ) ) {
			return index;
		}

		QMenu menu;
		static const char * const flipCmds[4] = { "S = 1.0 - S", "T = 1.0 - T", "S <=> T", "ST <=> PQ" };

		for ( int c = 0; c < cmdCnt; c++ )
			menu.addAction( flipCmds[c] );

		QAction * act = menu.exec( QCursor::pos() );

		if ( act ) {
			for ( int c = 0; c < cmdCnt; c++ ) {
				if ( act->text() == flipCmds[c] ) {
					if ( nif->getBSVersion() < 170 )
						flip( nif, idx, c );
					else
						flip_Starfield( nif, idx, c );
				}
			}

		}

		return index;
	}

	//! Flips UV data in a model index
	void flip( NifModel * nif, const QModelIndex & index, int f )
	{
		if ( nif->itemStrType( index ).startsWith( "BSVertexData" ) ) {
			// BSTriShape vertex data
			int	n = nif->rowCount( index );
			for ( int i = 0; i < n; i++ ) {
				auto	v = nif->getIndex( index, i );
				if ( !v.isValid() )
					continue;
				auto	iUV = nif->getIndex( v, "UV" );
				if ( !iUV.isValid() )
					continue;
				HalfVector2	uv = nif->get<HalfVector2>( iUV );
				flip( uv, f );
				nif->set<HalfVector2>( iUV, uv );
			}
		} else if ( nif->isArray( index ) ) {
			QModelIndex idx = nif->getIndex( index, 0 );

			if ( idx.isValid() ) {
				if ( nif->isArray( idx ) )
					flip( nif, idx, f );
				else {
					QVector<Vector2> tc = nif->getArray<Vector2>( index );

					for ( int c = 0; c < tc.count(); c++ )
						flip( tc[c], f );

					nif->setArray<Vector2>( index, tc );
				}
			}
		} else {
			Vector2 v = nif->get<Vector2>( index );
			flip( v, f );
			nif->set<Vector2>( index, v );
		}
	}

	//! Flips UV data in a vector
	void flip( Vector2 & v, int f )
	{
		switch ( f ) {
		case 0:
			v[0] = 1.0 - v[0];
			break;
		case 1:
			v[1] = 1.0 - v[1];
			break;
		default:
			{
				float x = v[0];
				v[0] = v[1];
				v[1] = x;
			}
			break;
		}
	}
};

void spFlipTexCoords::flip_Starfield( NifModel * nif, const QModelIndex & index, int f )
{
	if ( !index.isValid() ) {
		return;
	} else {
		NifItem *	i = nif->getItem( index );
		if ( !i )
			return;
		if ( !i->hasStrType( "BSMeshData" ) ) {
			if ( i->hasStrType( "BSMesh" ) ) {
				flip_Starfield( nif, nif->getIndex( i, "Mesh Data" ), f );
			} else if ( i->hasStrType( "BSMeshArray" ) ) {
				if ( nif->get<bool>( i, "Has Mesh" ) )
					flip_Starfield( nif, nif->getIndex( i, "Mesh" ), f );
			} else if ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>( i, "Flags" ) & 0x0200 ) ) {
				auto	iMeshes = nif->getIndex( i, "Meshes" );
				if ( iMeshes.isValid() && nif->isArray( iMeshes ) ) {
					for ( int n = 0; n <= 3; n++ )
						flip_Starfield( nif, nif->getIndex( iMeshes, n ), f );
				}
			}
			return;
		}
	}

	std::uint32_t	numUVs = nif->get<quint32>( index, "Num UVs" );
	std::uint32_t	numUVs2 = nif->get<quint32>( index, "Num UVs 2" );
	if ( !numUVs || ( f == 3 && numUVs2 != numUVs ) )
		return;

	auto	uvsIndex = nif->getIndex( index, "UVs" );
	if ( !uvsIndex.isValid() )
		return;
	QVector< HalfVector2 >	uvs = nif->getArray< HalfVector2 >( uvsIndex );
	switch ( f ) {
	case 0:
		for ( auto & v : uvs )
			v[0] = 1.0f - v[0];
		break;
	case 1:
		for ( auto & v : uvs )
			v[1] = 1.0f - v[1];
		break;
	case 2:
		for ( auto & v : uvs )
			std::swap( v[0], v[1] );
		break;
	case 3:
		for ( auto uvs2Index = nif->getIndex( index, "UVs 2" ); uvs2Index.isValid(); ) {
			QVector< HalfVector2 >	uvs2 = nif->getArray< HalfVector2 >( uvs2Index );
			nif->setArray< HalfVector2 >( uvs2Index, uvs );
			uvs = uvs2;
			break;
		}
		break;
	}
	nif->setArray< HalfVector2 >( uvsIndex, uvs );
}

REGISTER_SPELL( spFlipTexCoords )

//! Flips triangle faces, individually or in the selected array
class spFlipFace final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Flip Face" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( nif->getValue( index ).type() == NifValue::tTriangle )
				|| ( nif->isArray( index ) && nif->getValue( nif->getIndex( index, 0 ) ).type() == NifValue::tTriangle );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->isArray( index ) ) {
			QVector<Triangle> tris = nif->getArray<Triangle>( index );

			for ( int t = 0; t < tris.count(); t++ )
				tris[t].flip();

			nif->setArray<Triangle>( index, tris );
		} else {
			Triangle t = nif->get<Triangle>( index );
			t.flip();
			nif->set<Triangle>( index, t );
		}

		return index;
	}
};

REGISTER_SPELL( spFlipFace )

//! Flips all faces of a triangle based mesh
class spFlipAllFaces final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Flip Faces" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return true;
		return getTriShapeData( nif, index ).isValid();
	}

	static void cast_Starfield( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->blockInherits( index, "BSGeometry" ) ) {
			if ( nif->checkInternalGeometry( index ) )
				cast_Starfield( nif, index );
			return index;
		}
		QModelIndex iData = getTriShapeData( nif, index );

		QVector<Triangle> tris = nif->getArray<Triangle>( iData, "Triangles" );

		for ( int t = 0; t < tris.count(); t++ )
			tris[t].flip();

		nif->setArray<Triangle>( iData, "Triangles", tris );

		return index;
	}
};

void spFlipAllFaces::cast_Starfield( NifModel * nif, const QModelIndex & index )
{
	if ( !index.isValid() ) {
		return;
	} else {
		NifItem *	i = nif->getItem( index );
		if ( !i )
			return;
		if ( !i->hasStrType( "BSMeshData" ) ) {
			if ( i->hasStrType( "BSMesh" ) ) {
				cast_Starfield( nif, nif->getIndex( i, "Mesh Data" ) );
			} else if ( i->hasStrType( "BSMeshArray" ) ) {
				if ( nif->get<bool>( i, "Has Mesh" ) )
					cast_Starfield( nif, nif->getIndex( i, "Mesh" ) );
			} else if ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>( i, "Flags" ) & 0x0200 ) ) {
				auto	iMeshes = nif->getIndex( i, "Meshes" );
				if ( iMeshes.isValid() && nif->isArray( iMeshes ) ) {
					for ( int n = 0; n <= 3; n++ )
						cast_Starfield( nif, nif->getIndex( iMeshes, n ) );
				}
			}
			return;
		}
	}

	for ( int l = 0; true; l++ ) {
		QModelIndex	lodIndex;
		if ( !l ) {
			lodIndex = index;
		} else if ( l <= int( nif->get<quint32>( index, "Num LODs" ) ) ) {
			lodIndex = nif->getIndex( index, "LODs" );
			if ( lodIndex.isValid() )
				lodIndex = nif->getIndex( lodIndex, l - 1 );
		}
		if ( !lodIndex.isValid() )
			break;

		int	numTriangles = int( nif->get<quint32>( lodIndex, "Indices Size" ) / 3U );
		if ( numTriangles < 1 )
			continue;
		auto	trianglesIndex = nif->getIndex( lodIndex, "Triangles" );
		if ( !trianglesIndex.isValid() )
			continue;

		QVector<Triangle> tris = nif->getArray<Triangle>( trianglesIndex );
		for ( auto & t : tris )
			t.flip();
		nif->setArray<Triangle>( trianglesIndex, tris );
	}
}

REGISTER_SPELL( spFlipAllFaces )

//! Removes redundant triangles from a mesh
class spPruneRedundantTriangles final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Prune Triangles" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return true;
		if ( nif->blockInherits( index, "BSTriShape" ) && nif->getIndex( index, "Triangles" ).isValid() )
			return true;
		return getTriShapeData( nif, index ).isValid();
	}

	static inline std::uint64_t triangleToKey( const Triangle & t )
	{
		return std::uint64_t(t[0]) | ( std::uint64_t(t[1]) << 21 ) | ( std::uint64_t(t[2]) << 42 );
	}
	static inline std::uint64_t rotateVertices( std::uint64_t t )
	{
		return ( t >> 21 ) | ( ( t & 0x001FFFFFUL ) << 42 );
	}
	static void cast_Starfield( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->blockInherits( index, "BSGeometry" ) ) {
			if ( nif->checkInternalGeometry( index ) ) {
				nif->setState( BaseModel::Processing );
				cast_Starfield( nif, index );
				nif->restoreState();
			}
			return index;
		}

		QModelIndex iData;
		bool isBSTriShape = nif->blockInherits( index, "BSTriShape" );
		if ( !isBSTriShape )
			iData = getTriShapeData( nif, index );
		else
			iData = index;
		QList<Triangle> tris = nif->getArray<Triangle>( iData, "Triangles" ).toList();
		int cnt = 0;

		int i = 0;

		while ( i < tris.count() ) {
			const Triangle & t = tris[i];

			if ( t[0] == t[1] || t[1] == t[2] || t[2] == t[0] ) {
				tris.removeAt( i );
				cnt++;
			} else {
				i++;
			}
		}

		i = 0;

		while ( i < tris.count() ) {
			const Triangle & t = tris[i];

			int j = i + 1;

			while ( j < tris.count() ) {
				const Triangle & r = tris[j];

				if ( ( t[0] == r[0] && t[1] == r[1] && t[2] == r[2] )
				     || ( t[0] == r[1] && t[1] == r[2] && t[2] == r[0] )
				     || ( t[0] == r[2] && t[1] == r[0] && t[2] == r[1] ) )
				{
					tris.removeAt( j );
					cnt++;
				} else {
					j++;
				}
			}

			i++;
		}

		if ( cnt > 0 ) {
			Message::info( nullptr, Spell::tr( "Removed %1 triangles" ).arg( cnt ) );
			nif->set<int>( iData, "Num Triangles", tris.count() );
			if ( !isBSTriShape )
				nif->set<int>( iData, "Num Triangle Points", tris.count() * 3 );
			nif->updateArraySize( iData, "Triangles" );
			nif->setArray<Triangle>( iData, "Triangles", tris.toVector() );
		}

		return index;
	}
};

void spPruneRedundantTriangles::cast_Starfield( NifModel * nif, const QModelIndex & index )
{
	if ( !index.isValid() ) {
		return;
	} else {
		NifItem *	i = nif->getItem( index );
		if ( !i )
			return;
		if ( !i->hasStrType( "BSMeshData" ) ) {
			if ( i->hasStrType( "BSMesh" ) ) {
				cast_Starfield( nif, nif->getIndex( i, "Mesh Data" ) );
			} else if ( i->hasStrType( "BSMeshArray" ) ) {
				if ( nif->get<bool>( i, "Has Mesh" ) )
					cast_Starfield( nif, nif->getIndex( i, "Mesh" ) );
			} else if ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>( i, "Flags" ) & 0x0200 ) ) {
				auto	iMeshes = nif->getIndex( i, "Meshes" );
				if ( iMeshes.isValid() && nif->isArray( iMeshes ) ) {
					for ( int n = 0; n <= 3; n++ )
						cast_Starfield( nif, nif->getIndex( iMeshes, n ) );
				}
			}
			return;
		}
	}

	std::uint32_t	numVerts = nif->get<quint32>( index, "Num Verts" );
	size_t	trianglesRemovedCnt = 0;

	for ( int l = 0; true; l++ ) {
		QModelIndex	lodIndex;
		if ( !l ) {
			lodIndex = index;
		} else if ( l <= int( nif->get<quint32>( index, "Num LODs" ) ) ) {
			lodIndex = nif->getIndex( index, "LODs" );
			if ( lodIndex.isValid() )
				lodIndex = nif->getIndex( lodIndex, l - 1 );
		}
		if ( !lodIndex.isValid() )
			break;

		int	numTriangles = int( nif->get<quint32>( lodIndex, "Indices Size" ) / 3U );
		if ( numTriangles < 1 )
			continue;
		NifItem *	trianglesItem = nif->getItem( lodIndex, "Triangles" );
		if ( !trianglesItem )
			continue;

		std::unordered_set< std::uint64_t >	triangleSet;
		std::unordered_set< std::uint64_t >	trianglesRemoved;
		std::unordered_set< std::uint64_t >	invalidTriangles;

		for ( int i = 0; i < numTriangles; i++ ) {
			Triangle	t = nif->get<Triangle>( trianglesItem->child( i ) );
			std::uint32_t	v0 = t[0];
			std::uint32_t	v1 = t[1];
			std::uint32_t	v2 = t[2];
			if ( v0 >= numVerts || v1 >= numVerts || v2 >= numVerts ) {
				invalidTriangles.insert( std::uint64_t(i) );
			} else if ( v0 == v1 || v0 == v2 || v1 == v2 ) {
				trianglesRemoved.insert( std::uint64_t(i) );
			} else {
				std::uint64_t	t0 = triangleToKey( t );
				std::uint64_t	t1 = rotateVertices( t0 );
				if ( !triangleSet.insert( t0 ).second || triangleSet.find( t1 ) != triangleSet.end()
					|| triangleSet.find( rotateVertices( t1 ) ) != triangleSet.end() ) {
					trianglesRemoved.insert( std::uint64_t(i) );
				}
			}
		}

		bool	removeInvalid = false;
		if ( !invalidTriangles.empty() ) {
			removeInvalid = ( QMessageBox::question( nullptr, "NifSkope warning", QString("Remove %1 triangles with invalid indices?").arg(invalidTriangles.size()) ) == QMessageBox::Yes );
		}

		if ( trianglesRemoved.empty() && !removeInvalid )
			continue;

		int	j = 0;
		for ( int i = 0; i < numTriangles; i++ ) {
			if ( trianglesRemoved.find( std::uint64_t(i) ) == trianglesRemoved.end() ) {
				if ( !removeInvalid || invalidTriangles.find( std::uint64_t(i) ) == invalidTriangles.end() ) {
					if ( j < i )
						nif->set<Triangle>( trianglesItem->child(j), nif->get<Triangle>( trianglesItem->child(i) ) );
					j++;
					continue;
				}
			}
		}
		trianglesRemovedCnt += size_t( numTriangles - j );
		numTriangles = j;

		nif->set<quint32>( lodIndex, "Indices Size", quint32(numTriangles) * 3U );
		if ( !l )
			nif->set<quint32>( index.parent(), "Indices Size", quint32(numTriangles) * 3U );
		auto	iTriangles = nif->getIndex( lodIndex, "Triangles" );
		if ( iTriangles.isValid() )
			nif->updateArraySize( iTriangles );
	}

	if ( trianglesRemovedCnt < 1 )
		return;

	// clear meshlets
	spGenerateMeshlets::clearMeshlets( nif, index );

	Message::info( nullptr, Spell::tr( "Removed %1 triangles" ).arg( trianglesRemovedCnt ) );
}

REGISTER_SPELL( spPruneRedundantTriangles )

//! Removes duplicate vertices from a mesh
class spRemoveDuplicateVertices final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Remove Duplicate Vertices" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return true;
		return spRemoveWasteVertices::getShape( nif, index ).isValid();
	}

	static void cast_Starfield( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->blockInherits( index, "BSGeometry" ) ) {
			if ( nif->checkInternalGeometry( index ) ) {
				nif->setState( BaseModel::Processing );
				cast_Starfield( nif, index );
				spRemoveWasteVertices::cast_Starfield( nif, index, false );
				nif->restoreState();
			}
			return index;
		}

		try
		{
			QModelIndex iShape = spRemoveWasteVertices::getShape( nif, index );
			QModelIndex iData  = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );

			// read the data

			QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );

			if ( !verts.count() )
				throw QString( Spell::tr( "No vertices" ) );

			QVector<Vector3> norms = nif->getArray<Vector3>( iData, "Normals" );
			QVector<Color4> colors = nif->getArray<Color4>( iData, "Vertex Colors" );
			QList<QVector<Vector2> > texco;
			QModelIndex iUVSets = nif->getIndex( iData, "UV Sets" );

			for ( int r = 0; r < nif->rowCount( iUVSets ); r++ ) {
				texco << nif->getArray<Vector2>( nif->getIndex( iUVSets, r ) );

				if ( texco.last().count() != verts.count() )
					throw QString( Spell::tr( "UV array size differs" ) );
			}

			int numVerts = verts.count();

			if ( numVerts != nif->get<int>( iData, "Num Vertices" )
			     || ( norms.count() && norms.count() != numVerts )
			     || ( colors.count() && colors.count() != numVerts ) )
			{
				throw QString( Spell::tr( "Vertex array size differs" ) );
			}

			// detect the duplicates

			QMap<quint16, quint16> map;

			for ( int a = 0; a < numVerts; a++ ) {
				Vector3 v = verts[a];

				for ( int b = 0; b < a; b++ ) {
					if ( !( v == verts[b] ) )
						continue;

					if ( norms.count() && !( norms[a] == norms[b] ) )
						continue;

					if ( colors.count() && !( colors[a] == colors[b] ) )
						continue;

					int t = 0;

					for ( t = 0; t < texco.count(); t++ ) {
						if ( !( texco[t][a] == texco[t][b] ) )
							break;
					}

					if ( t < texco.count() )
						continue;

					map.insert( b, a );
				}
			}

			//qDebug() << QString( Spell::tr("detected % duplicates") ).arg( map.count() );

			// adjust the faces

			QModelIndex	iTriangles = nif->getIndex( iData, "Triangles" );
			if ( iTriangles.isValid() ) {
				QVector<Triangle> tris = nif->getArray<Triangle>( iTriangles );
				QMutableVectorIterator<Triangle> itri( tris );

				while ( itri.hasNext() ) {
					Triangle & t = itri.next();

					for ( int p = 0; p < 3; p++ ) {
						if ( map.contains( t[p] ) )
							t[p] = map.value( t[p] );
					}

				}

				nif->setArray<Triangle>( iData, "Triangles", tris );
			}

			QModelIndex iPoints = nif->getIndex( iData, "Points" );
			if ( iPoints.isValid() ) {
				for ( int r = 0; r < nif->rowCount( iPoints ); r++ ) {
					QVector<quint16> strip = nif->getArray<quint16>( nif->getIndex( iPoints, r ) );
					QMutableVectorIterator<quint16> istrp( strip );

					while ( istrp.hasNext() ) {
						quint16 & p = istrp.next();

						if ( map.contains( p ) )
							p = map.value( p );
					}

					nif->setArray<quint16>( nif->getIndex( iPoints, r ), strip );
				}
			}

			// finally, remove the now unused vertices

			removeWasteVertices( nif, iData, iShape );
		}
		catch ( QString & e )
		{
			Message::warning( nullptr, Spell::tr( "There were errors during the operation" ), e );
		}

		return index;
	}
};

struct SFMeshVertexAttributes
{
	std::uint32_t	index;
	float	xyz[3];
	std::uint32_t	normal;
	std::uint32_t	color;
	std::uint64_t	texCoords;
	static inline FloatVector4 clearDenorm( FloatVector4 v, float minVal = 0.0f )
	{
		// convert -0.0 and denormals to 0.0
		FloatVector4	tmp( v * v );
#if ENABLE_X86_64_SIMD
		XMM_UInt32	m = std::bit_cast< XMM_UInt32 >( tmp.v <= FloatVector4( minVal ).v );
		v.v = std::bit_cast< XMM_Float >( std::bit_cast< XMM_UInt32 >( v.v ) & ~m );
#else
		v[0] = ( tmp[0] <= minVal ? 0.0f : v[0] );
		v[1] = ( tmp[1] <= minVal ? 0.0f : v[1] );
		v[2] = ( tmp[2] <= minVal ? 0.0f : v[2] );
		v[3] = ( tmp[3] <= minVal ? 0.0f : v[3] );
#endif
		return v;
	}
	SFMeshVertexAttributes( const MeshFile & meshFile, qsizetype n )
	{
		std::memset( this, 0, sizeof( SFMeshVertexAttributes ) );
		index = std::uint32_t( n );
		if ( n < meshFile.positions.size() )
			clearDenorm( FloatVector4( meshFile.positions.at( n ) ) ).convertToVector3( xyz );
		if ( n < meshFile.normals.size() )
			normal = FloatVector4( meshFile.normals.at( n ) ).convertToX10Y10Z10();
		if ( n < meshFile.colors.size() )
			color = std::uint32_t( FloatVector4( meshFile.colors.at( n ) ) * 255.0f );
		if ( n < meshFile.coords.size() )
			texCoords = clearDenorm( FloatVector4( meshFile.coords.at( n ) ), 1.0e-12f ).convertToFloat16();
		// TODO: should also test weights?
	}
	inline const unsigned char * data() const
	{
		return reinterpret_cast< const unsigned char * >( &( xyz[0] ) );
	}
	inline size_t size() const
	{
		return sizeof( SFMeshVertexAttributes ) - size_t( data() - reinterpret_cast< const unsigned char * >( this ) );
	}
	inline bool operator==( const SFMeshVertexAttributes & r ) const
	{
		return ( std::memcmp( data(), r.data(), size() ) == 0 );
	}
};

template <> class std::hash< SFMeshVertexAttributes >
{
public:
	inline size_t operator()( const SFMeshVertexAttributes & r ) const
	{
		return hashFunctionUInt32( r.data(), r.size() );
	}
};

void spRemoveDuplicateVertices::cast_Starfield( NifModel * nif, const QModelIndex & index )
{
	if ( !index.isValid() ) {
		return;
	} else {
		NifItem *	i = nif->getItem( index );
		if ( !i )
			return;
		if ( !i->hasStrType( "BSMesh" ) ) {
			if ( i->hasStrType( "BSMeshArray" ) ) {
				if ( nif->get<bool>( i, "Has Mesh" ) )
					cast_Starfield( nif, nif->getIndex( i, "Mesh" ) );
			} else if ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>( i, "Flags" ) & 0x0200 ) ) {
				auto	iMeshes = nif->getIndex( i, "Meshes" );
				if ( iMeshes.isValid() && nif->isArray( iMeshes ) ) {
					for ( int n = 0; n <= 3; n++ )
						cast_Starfield( nif, nif->getIndex( iMeshes, n ) );
				}
			}
			return;
		}
	}
	QModelIndex	iMeshData = nif->getIndex( index, "Mesh Data" );
	if ( !iMeshData.isValid() )
		return;

	MeshFile	meshFile( nif, index );
	size_t	numVerts = size_t( meshFile.positions.size() );

	std::unordered_set< SFMeshVertexAttributes >	uniqueVertexSet;
	std::vector< std::uint32_t >	vertexMap( numVerts );
	for ( size_t i = 0; i < numVerts; i++ )
		vertexMap[i] = uniqueVertexSet.emplace( meshFile, qsizetype( i ) ).first->index;
	if ( uniqueVertexSet.size() == numVerts )
		return;

	// remap indices
	for ( int l = 0; true; l++ ) {
		QModelIndex	lodIndex;
		if ( !l ) {
			lodIndex = iMeshData;
		} else if ( l <= int( nif->get<quint32>( iMeshData, "Num LODs" ) ) ) {
			lodIndex = nif->getIndex( iMeshData, "LODs" );
			if ( lodIndex.isValid() )
				lodIndex = nif->getIndex( lodIndex, l - 1 );
		}
		if ( !lodIndex.isValid() )
			break;
		int	numTriangles = int( nif->get<quint32>( lodIndex, "Indices Size" ) / 3U );
		if ( numTriangles > 0 ) {
			NifItem *	trianglesItem = nif->getItem( lodIndex, "Triangles" );
			if ( trianglesItem ) {
				for ( int i = 0; i < numTriangles; i++ ) {
					Triangle	t = nif->get<Triangle>( trianglesItem->child( i ) );
					bool	indicesChanged = false;
					for ( int j = 0; j < 3; j++ ) {
						std::uint32_t	v = t[j];
						if ( v < numVerts && vertexMap[v] != v && vertexMap[v] < numVerts ) {
							t[j] = quint16( vertexMap[v] );
							indicesChanged = true;
						}
					}
					if ( indicesChanged )
						nif->set<Triangle>( trianglesItem->child( i ), t );
				}
			}
		}
	}

	spGenerateMeshlets::clearMeshlets( nif, iMeshData );
}

REGISTER_SPELL( spRemoveDuplicateVertices )

//! Removes unused vertices

void spRemoveWasteVertices::cast_Starfield( NifModel * nif, const QModelIndex & index, bool noMessages )
{
	if ( !index.isValid() ) {
		return;
	} else {
		NifItem *	i = nif->getItem( index );
		if ( !i )
			return;
		if ( !i->hasStrType( "BSMeshData" ) ) {
			if ( i->hasStrType( "BSMesh" ) ) {
				cast_Starfield( nif, nif->getIndex( i, "Mesh Data" ), noMessages );
			} else if ( i->hasStrType( "BSMeshArray" ) ) {
				if ( nif->get<bool>( i, "Has Mesh" ) )
					cast_Starfield( nif, nif->getIndex( i, "Mesh" ), noMessages );
			} else if ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>( i, "Flags" ) & 0x0200 ) ) {
				auto	iMeshes = nif->getIndex( i, "Meshes" );
				if ( iMeshes.isValid() && nif->isArray( iMeshes ) ) {
					for ( int n = 0; n <= 3; n++ )
						cast_Starfield( nif, nif->getIndex( iMeshes, n ), noMessages );
				}
			}
			return;
		}
	}

	std::uint32_t	numVerts = nif->get<quint32>( index, "Num Verts" );
	if ( !numVerts )
		return;
	std::uint32_t	weightsPerVertex = nif->get<quint32>( index, "Weights Per Vertex" );
	std::uint32_t	numUVs = nif->get<quint32>( index, "Num UVs" );
	std::uint32_t	numUVs2 = nif->get<quint32>( index, "Num UVs 2" );
	std::uint32_t	numColors = nif->get<quint32>( index, "Num Vertex Colors" );
	std::uint32_t	numNormals = nif->get<quint32>( index, "Num Normals" );
	std::uint32_t	numTangents = nif->get<quint32>( index, "Num Tangents" );
	std::uint32_t	numWeights = nif->get<quint32>( index, "Num Weights" );
	if ( ( numUVs && numUVs != numVerts ) || ( numUVs2 && numUVs2 != numVerts )
		|| ( numColors && numColors != numVerts ) || ( numNormals && numNormals != numVerts )
		|| ( numTangents && numTangents != numVerts ) || ( numWeights != ( size_t(numVerts) * weightsPerVertex ) ) ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Mesh has inconsistent number of vertex attributes, cannot remove unused vertices") );
		return;
	}

	std::vector< std::uint32_t >	vertexMap( numVerts, 0xFFFFFFFFU );	// value = new index
	size_t	invalidIndices = 0;
	size_t	verticesRemoved = numVerts;

	for ( int l = 0; true; l++ ) {
		QModelIndex	lodIndex;
		if ( !l ) {
			lodIndex = index;
		} else if ( l <= int( nif->get<quint32>( index, "Num LODs" ) ) ) {
			lodIndex = nif->getIndex( index, "LODs" );
			if ( lodIndex.isValid() )
				lodIndex = nif->getIndex( lodIndex, l - 1 );
		}
		if ( !lodIndex.isValid() )
			break;
		int	numTriangles = int( nif->get<quint32>( lodIndex, "Indices Size" ) / 3U );
		if ( numTriangles > 0 ) {
			NifItem *	trianglesItem = nif->getItem( lodIndex, "Triangles" );
			if ( trianglesItem ) {
				for ( int i = 0; i < numTriangles; i++ ) {
					Triangle	t = nif->get<Triangle>( trianglesItem->child( i ) );
					for ( int j = 0; j < 3; j++ ) {
						std::uint32_t	v = t[j];
						if ( v >= numVerts ) {
							invalidIndices++;
						} else if ( vertexMap[v] ) {
							vertexMap[v] = 0;
							verticesRemoved--;
						}
					}
				}
			}
		}
	}
	if ( invalidIndices > 0 )
		QMessageBox::warning( nullptr, "NifSkope warning", QString("Mesh has %1 invalid indices").arg(invalidIndices) );
	if ( verticesRemoved < 1 )
		return;

	std::uint32_t	n = 0;
	for ( auto & v : vertexMap ) {
		if ( !v ) {
			v = n;
			n++;
		}
	}

	// remap indices
	for ( int l = 0; true; l++ ) {
		QModelIndex	lodIndex;
		if ( !l ) {
			lodIndex = index;
		} else if ( l <= int( nif->get<quint32>( index, "Num LODs" ) ) ) {
			lodIndex = nif->getIndex( index, "LODs" );
			if ( lodIndex.isValid() )
				lodIndex = nif->getIndex( lodIndex, l - 1 );
		}
		if ( !lodIndex.isValid() )
			break;
		int	numTriangles = int( nif->get<quint32>( lodIndex, "Indices Size" ) / 3U );
		if ( numTriangles > 0 ) {
			NifItem *	trianglesItem = nif->getItem( lodIndex, "Triangles" );
			if ( trianglesItem ) {
				for ( int i = 0; i < numTriangles; i++ ) {
					Triangle	t = nif->get<Triangle>( trianglesItem->child( i ) );
					bool	indicesChanged = false;
					for ( int j = 0; j < 3; j++ ) {
						std::uint32_t	v = t[j];
						if ( v < numVerts && vertexMap[v] != v && vertexMap[v] < numVerts ) {
							t[j] = quint16( vertexMap[v] );
							indicesChanged = true;
						}
					}
					if ( indicesChanged )
						nif->set<Triangle>( trianglesItem->child( i ), t );
				}
			}
		}
	}

	// remap vertex attributes
	NifItem *	verticesItem = nif->getItem( index, "Vertices" );
	NifItem *	uvsItem = nullptr;
	if ( numUVs )
		uvsItem = nif->getItem( index, "UVs" );
	NifItem *	uvs2Item = nullptr;
	if ( numUVs2 )
		uvs2Item = nif->getItem( index, "UVs 2" );
	NifItem *	colorsItem = nullptr;
	if ( numColors )
		colorsItem = nif->getItem( index, "Vertex Colors" );
	NifItem *	normalsItem = nullptr;
	if ( numNormals )
		normalsItem = nif->getItem( index, "Normals" );
	NifItem *	tangentsItem = nullptr;
	if ( numTangents )
		tangentsItem = nif->getItem( index, "Tangents" );
	NifItem *	weightsItem = nullptr;
	if ( numWeights )
		weightsItem = nif->getItem( index, "Weights" );
	for ( const auto & v : vertexMap ) {
		int	n0 = int( &v - vertexMap.data() );
		int	n1 = int( v );
		if ( n1 < 0 || n1 >= n0 )
			continue;
		if ( verticesItem )
			nif->set<ShortVector3>( verticesItem->child( n1 ), nif->get<ShortVector3>( verticesItem->child( n0 ) ) );
		if ( uvsItem )
			nif->set<HalfVector2>( uvsItem->child( n1 ), nif->get<HalfVector2>( uvsItem->child( n0 ) ) );
		if ( uvs2Item )
			nif->set<HalfVector2>( uvs2Item->child( n1 ), nif->get<HalfVector2>( uvs2Item->child( n0 ) ) );
		if ( colorsItem )
			nif->set<ByteColor4BGRA>( colorsItem->child( n1 ), nif->get<ByteColor4BGRA>( colorsItem->child( n0 ) ) );
		if ( normalsItem )
			nif->set<UDecVector4>( normalsItem->child( n1 ), nif->get<UDecVector4>( normalsItem->child( n0 ) ) );
		if ( tangentsItem )
			nif->set<UDecVector4>( tangentsItem->child( n1 ), nif->get<UDecVector4>( tangentsItem->child( n0 ) ) );
		if ( weightsItem ) {
			for ( std::uint32_t i = 0; i < weightsPerVertex; i++ ) {
				NifItem *	bw0 = weightsItem->child( int( std::uint32_t(n0) * weightsPerVertex + i ) );
				NifItem *	bw1 = weightsItem->child( int( std::uint32_t(n1) * weightsPerVertex + i ) );
				if ( bw0 && bw1 ) {
					nif->set<quint16>( bw1->child( 0 ), nif->get<quint16>( bw0->child( 0 ) ) );
					nif->set<quint16>( bw1->child( 1 ), nif->get<quint16>( bw0->child( 1 ) ) );
				}
			}
		}
	}

	// update array sizes
	for ( auto i = nif->getItem( index ); i; ) {
		i->invalidateVersionCondition();
		i->invalidateCondition();
		break;
	}
	numVerts -= std::uint32_t( verticesRemoved );
	numUVs = std::min( numUVs, numVerts );
	numUVs2 = std::min( numUVs2, numVerts );
	numColors = std::min( numColors, numVerts );
	numNormals = std::min( numNormals, numVerts );
	numTangents = std::min( numTangents, numVerts );
	numWeights = numVerts * weightsPerVertex;
	QModelIndex	i;
	nif->set<quint32>( index, "Num Verts", numVerts );
	nif->set<quint32>( index.parent(), "Num Verts", numVerts );
	if ( ( i = nif->getIndex( index, "Vertices" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( index, "Num UVs", numUVs );
	if ( ( i = nif->getIndex( index, "UVs" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( index, "Num UVs 2", numUVs2 );
	if ( ( i = nif->getIndex( index, "UVs 2" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( index, "Num Vertex Colors", numColors );
	if ( ( i = nif->getIndex( index, "Vertex Colors" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( index, "Num Normals", numNormals );
	if ( ( i = nif->getIndex( index, "Normals" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( index, "Num Tangents", numTangents );
	if ( ( i = nif->getIndex( index, "Tangents" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( index, "Num Weights", numWeights );
	if ( ( i = nif->getIndex( index, "Weights" ) ).isValid() )
		nif->updateArraySize( i );

	if ( !noMessages )
		Message::info( nullptr, Spell::tr( "Removed %1 vertices" ).arg( verticesRemoved ) );
}

QModelIndex spRemoveWasteVertices::cast( NifModel * nif, const QModelIndex & index )
{
	if ( nif->blockInherits( index, "BSGeometry" ) ) {
		if ( nif->checkInternalGeometry( index ) ) {
			nif->setState( BaseModel::Processing );
			cast_Starfield( nif, index );
			nif->restoreState();
		}
	} else if ( nif->blockInherits( index, "BSTriShape" ) ) {
		removeWasteVertices( nif, index );
	} else {
		QModelIndex iShape = getShape( nif, index );
		QModelIndex iData  = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );
		removeWasteVertices( nif, iData, iShape );
	}

	return index;
}

REGISTER_SPELL( spRemoveWasteVertices )

static bool calculateBoundingBox( FloatVector4 & bndCenter, FloatVector4 & bndDims, const QVector< Vector3 > & verts )
{
	qsizetype	n = verts.size();
	if ( n < 1 ) {
		bndCenter = FloatVector4( 0.0f );
		bndDims = FloatVector4( -1.0f );
		return false;
	}

	FloatVector4	tmpMin( float(FLT_MAX) );
	FloatVector4	tmpMax( float(-FLT_MAX) );
	for ( qsizetype i = 0; i < n; i++ ) {
		FloatVector4	v( verts[i] );
		tmpMin.minValues( v );
		tmpMax.maxValues( v );
	}
	bndCenter = ( tmpMin + tmpMax ) * 0.5f;
	bndDims = ( tmpMax - tmpMin ) * 0.5f;

	return true;
}

static void setBoundingBox( NifModel * nif, const QModelIndex & index, FloatVector4 bndCenter, FloatVector4 bndDims )
{
	auto boundingBox = nif->getIndex( index, "Bounding Box" );
	if ( !boundingBox.isValid() )
		return;
	NifItem *	boundsItem = nif->getItem( boundingBox );
	if ( boundsItem ) {
		nif->set<Vector3>( boundsItem->child( 0 ), Vector3( bndCenter ) );
		nif->set<Vector3>( boundsItem->child( 1 ), Vector3( bndDims ) );
	}
}

/*
 * spUpdateCenterRadius
 */
bool spUpdateCenterRadius::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	return nif->getBlockIndex( index, "NiGeometryData" ).isValid();
}

QModelIndex spUpdateCenterRadius::cast( NifModel * nif, const QModelIndex & index )
{
	QModelIndex iData = nif->getBlockIndex( index );

	QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );

	if ( !verts.count() )
		return index;

	Vector3 center;
	float radius = 0.0f;

	/*
	    Oblivion and CT_volatile meshes require a
	    different center algorithm
	*/
	if ( ( ( nif->getVersionNumber() & 0x14000000 ) && ( nif->getUserVersion() == 11 ) )
	     || ( nif->get<ushort>( iData, "Consistency Flags" ) & 0x8000 ) )
	{
		/* is an Oblivion mesh! */
		FloatVector4	bndCenter, bndDims;
		calculateBoundingBox( bndCenter, bndDims, verts );

		center = Vector3( bndCenter[0], bndCenter[1], bndCenter[2] );
	} else {
		for ( const Vector3& v : verts ) {
			center += v;
		}
		center /= verts.count();
	}

	float d;
	for ( const Vector3& v : verts ) {
		if ( ( d = ( center - v ).length() ) > radius )
			radius = d;
	}

	BoundSphere::setBounds( nif, iData, center, radius );

	return index;
}

REGISTER_SPELL( spUpdateCenterRadius )

//! spUpdateBounds: updates Bounds of BSTriShape or BSGeometry

void spUpdateBounds::calculateSFBoneBounds(
	NifModel * nif, const QPersistentModelIndex & iBoneList, int numBones, const MeshFile & meshFile )
{
	std::map< int, std::vector< Vector3 > >	boneVertexMap;
	for ( const auto & w : meshFile.weights ) {
		qsizetype	i = qsizetype( &w - meshFile.weights.data() );
		if ( i >= meshFile.positions.size() ) [[unlikely]]
			break;
		for ( const auto & b : w.weightsUNORM ) {
			if ( (unsigned int) b.bone < (unsigned int) numBones && b.weight > 0.00005f )
				boneVertexMap[int(b.bone)].push_back( meshFile.positions.at(i) );
		}
	}
	for ( int i = 0; i < numBones; i++ ) {
		auto	iBone = nif->getIndex( iBoneList, i );
		if ( !iBone.isValid() )
			continue;
		std::vector< Vector3 > &	vertices = boneVertexMap[i];
		Transform	t( nif, iBone );
		for ( auto & v : vertices )
			v = t * v;
		BoundSphere	bounds;
		if ( vertices.empty() ) {
			bounds.center = Vector3( 0.0f, 0.0f, 0.0f );
			bounds.radius = 0.0f;
		} else {
			bounds = BoundSphere( vertices.data(), qsizetype(vertices.size()), true );
		}
		bounds.update( nif, iBone );
	}
}

static void updateCullData( NifModel * nif, const QPersistentModelIndex & iMeshData, const MeshFile & meshFile )
{
	int	meshletCount = int( nif->get<quint32>( iMeshData, "Num Meshlets" ) );
	auto	iMeshlets = nif->getIndex( iMeshData, "Meshlets" );
	nif->set<quint32>( iMeshData, "Num Cull Data", quint32(meshletCount) );
	auto	iCullData = nif->getIndex( iMeshData, "Cull Data" );
	nif->updateArraySize( iCullData );
	qsizetype	k = 0;
	for ( int i = 0; i < meshletCount; i++ ) {
		int	triangleCount = int( nif->get<quint32>( nif->getIndex( iMeshlets, i ), "Triangle Count" ) );
		FloatVector4	bndMin( float(FLT_MAX) );
		FloatVector4	bndMax( float(-FLT_MAX) );
		bool	haveBounds = false;
		for ( int j = 0; j < triangleCount; j++, k++ ) {
			if ( k >= meshFile.triangles.size() ) [[unlikely]]
				break;
			Triangle	t = meshFile.triangles.at( k );
			for ( int l = 0; l < 3; l++ ) {
				int	m = t[l];
				if ( !( m >= 0 && m < int(meshFile.positions.size()) ) )
					continue;
				FloatVector4	xyz( meshFile.positions.at( m ) );
				bndMin.minValues( xyz );
				bndMax.maxValues( xyz );
				haveBounds = true;
			}
		}
		FloatVector4	bndCenter( 0.0f );
		FloatVector4	bndDims( -1.0f );
		if ( haveBounds ) {
			bndCenter = ( bndMin + bndMax ) * 0.5f;
			bndDims = ( bndMax - bndMin ) * 0.5f;
		}
		setBoundingBox( nif, nif->getIndex( iCullData, i ), bndCenter, bndDims );
	}
}

QModelIndex spUpdateBounds::cast_Starfield( NifModel * nif, const QModelIndex & index )
{
	QModelIndex	iBlock = nif->getBlockIndex( index );
	auto meshes = nif->getIndex( iBlock, "Meshes" );
	if ( !meshes.isValid() )
		return index;

	bool	boundsCalculated = false;
	BoundSphere	bounds;
	FloatVector4	bndCenter( 0.0f );
	FloatVector4	bndDims( -1.0f );
	QModelIndex	iBoneList;
	int	numBones = 0;
	for ( auto iSkin = nif->getBlockIndex( nif->getLink( iBlock, "Skin" ) ); iSkin.isValid(); ) {
		bounds.center = Vector3( 0.0f, 0.0f, 0.0f );
		bounds.radius = 0.0f;
		bndCenter = FloatVector4( float(FLT_MAX) );
		bndDims = FloatVector4( float(FLT_MAX) );
		auto	iBoneData = nif->getBlockIndex( nif->getLink( iSkin, "Data" ) );
		if ( iBoneData.isValid() ) {
			iBoneList = nif->getIndex( iBoneData, "Bone List" );
			if ( iBoneList.isValid() && nif->isArray( iBoneList ) )
				numBones = nif->rowCount( iBoneList );
		}
		break;
	}
	for ( int i = 0; i <= 3; i++ ) {
		auto mesh = nif->getIndex( meshes, i );
		if ( !mesh.isValid() )
			continue;
		auto hasMesh = nif->getIndex( mesh, "Has Mesh" );
		if ( !hasMesh.isValid() || nif->get<quint8>( hasMesh ) == 0 )
			continue;
		mesh = nif->getIndex( mesh, "Mesh" );
		if ( !mesh.isValid() )
			continue;
		MeshFile	meshFile( nif, mesh );
		quint32	indicesSize = 0;
		quint32	numVerts = 0;
		if ( meshFile.isValid() ) {
			indicesSize = quint32( meshFile.triangles.size() * 3 );
			numVerts = quint32( meshFile.positions.size() );
		}
		nif->set<quint32>( mesh, "Indices Size", indicesSize );
		nif->set<quint32>( mesh, "Num Verts", numVerts );
		// FIXME: mesh flags are not updated
		if ( meshFile.isValid() && meshFile.positions.size() > 0 && !boundsCalculated ) {
			if ( numBones > 0 ) {
				calculateSFBoneBounds( nif, iBoneList, numBones, meshFile );
			} else {
				// Creating a bounding sphere and bounding box from the verts
				bounds = BoundSphere( meshFile.positions, true );
				calculateBoundingBox( bndCenter, bndDims, meshFile.positions );
			}
			boundsCalculated = true;
		}
		if ( ( nif->get<quint32>(iBlock, "Flags") & 0x0200 ) == 0 )
			continue;
		auto	meshData = nif->getIndex( mesh, "Mesh Data" );
		// update cull data for version 2 meshlets
		if ( meshData.isValid() && nif->get<quint32>( meshData, "Version" ) >= 2U )
			updateCullData( nif, meshData, meshFile );
	}

	bounds.update( nif, iBlock );
	setBoundingBox( nif, iBlock, bndCenter, bndDims );

	return index;
}

QModelIndex spUpdateBounds::cast( NifModel * nif, const QModelIndex & index )
{
	if ( nif->getBSVersion() >= 170 && nif->blockInherits( index, "BSGeometry" ) )
		return cast_Starfield( nif, index );

	auto vertData = nif->getIndex( index, "Vertex Data" );

	// Retrieve the verts
	QVector<Vector3> verts;
	for ( int i = 0; i < nif->rowCount( vertData ); i++ ) {
		verts << nif->get<Vector3>( nif->getIndex( vertData, i ), "Vertex" );
	}

	if ( verts.isEmpty() )
		return index;

	// Creating a bounding sphere from the verts
	BoundSphere bounds = BoundSphere( verts, true );
	bounds.update( nif, index );

	if ( nif->getBSVersion() >= 151 ) {
		// Fallout 76: update bounding box
		FloatVector4	bndCenter, bndDims;
		calculateBoundingBox( bndCenter, bndDims, verts );
		setBoundingBox( nif, index, bndCenter, bndDims );
	}

	return index;
}

REGISTER_SPELL( spUpdateBounds )


class spUpdateAllBounds final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Update All Bounds" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
	{
		if ( !nif || idx.isValid() )
			return false;

		return ( nif->getBSVersion() >= 130 );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final
	{
		QList<QPersistentModelIndex> indices;

		spUpdateBounds updBounds;

		for ( int n = 0; n < nif->getBlockCount(); n++ ) {
			QModelIndex idx = nif->getBlockIndex( n );

			if ( updBounds.isApplicable( nif, idx ) )
				indices << idx;
		}

		for ( const QPersistentModelIndex& idx : indices ) {
			updBounds.castIfApplicable( nif, idx );
		}

		return QModelIndex();
	}

	static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );
};

QModelIndex spUpdateAllBounds::cast_Static( NifModel * nif, const QModelIndex & index )
{
	spUpdateAllBounds	tmp;
	if ( tmp.isApplicable( nif, index ) )
		return tmp.cast( nif, index );
	return index;
}

REGISTER_SPELL( spUpdateAllBounds )


//! spGenerateMeshlets: generates Starfield meshlets

void spGenerateMeshlets::clearMeshlets( NifModel * nif, const QModelIndex & iMeshData )
{
	for ( auto i = nif->getItem( iMeshData ); i; ) {
		i->invalidateVersionCondition();
		i->invalidateCondition();
		break;
	}
	nif->set<quint32>( iMeshData, "Num Meshlets", 0 );
	QModelIndex	i;
	if ( ( i = nif->getIndex( iMeshData, "Meshlets" ) ).isValid() )
		nif->updateArraySize( i );
	nif->set<quint32>( iMeshData, "Num Cull Data", 0 );
	if ( ( i = nif->getIndex( iMeshData, "Cull Data" ) ).isValid() )
		nif->updateArraySize( i );
}

void spGenerateMeshlets::updateMeshlets(
	NifModel * nif, const QPersistentModelIndex & iMeshData, const MeshFile & meshFile )
{
	int	meshletAlgorithm;
	{
		QSettings	settings;
		meshletAlgorithm = settings.value( "Settings/Nif/Starfield Meshlet Algorithm", 0 ).toInt();
		meshletAlgorithm = std::min< int >( std::max< int >( meshletAlgorithm, 0 ), 4 );
	}

	NifItem *	item = nif->getItem( iMeshData );
	if ( !item )
		return;
	item->invalidateVersionCondition();
	item->invalidateCondition();
	nif->set<quint32>( iMeshData, "Version", 2 );

	std::vector< meshopt_Meshlet >	meshletData;
	if ( meshFile.positions.size() > 0 && meshFile.triangles.size() > 0 ) {
		try {
			size_t	vertexCnt = size_t( meshFile.positions.size() );
			size_t	triangleCnt = size_t( meshFile.triangles.size() );
			auto	iTriangles = nif->getIndex( iMeshData, "Triangles" );
			if ( !iTriangles.isValid() || size_t( nif->rowCount( iTriangles ) ) != triangleCnt )
				throw FO76UtilsError( "invalid triangle data" );
			if ( meshletAlgorithm < 4 ) {
				std::vector< unsigned int >	indices( triangleCnt * 3 );
				size_t	k = 0;
				for ( const auto & t : meshFile.triangles ) {
					if ( t[0] >= vertexCnt || t[1] >= vertexCnt || t[2] >= vertexCnt )
						throw FO76UtilsError( "vertex number is out of range" );
					indices[k] = t[0];
					indices[k + 1] = t[1];
					indices[k + 2] = t[2];
					k = k + 3;
				}
				size_t	maxMeshlets = meshopt_buildMeshletsBound( triangleCnt * 3, 96, 128 );
				meshletData.resize( maxMeshlets );
				std::vector< unsigned int >	meshletVertices( maxMeshlets * 96 );
				std::vector< unsigned char >	meshletTriangles( maxMeshlets * 128 * 3 );
				size_t	meshletCnt;
				if ( meshletAlgorithm & 2 ) {
					std::vector< unsigned int >	indicesOpt( triangleCnt * 3 );
					meshopt_spatialSortTriangles( indicesOpt.data(), indices.data(), triangleCnt * 3,
													&( meshFile.positions.at(0)[0] ), vertexCnt, sizeof( Vector3 ) );
					meshopt_optimizeVertexCache( indices.data(), indicesOpt.data(), triangleCnt * 3, vertexCnt );
					meshletCnt =
						meshopt_buildMeshletsScan( meshletData.data(), meshletVertices.data(), meshletTriangles.data(),
													indices.data(), triangleCnt * 3, vertexCnt, 96, 128 );
				} else {
					meshletCnt =
						meshopt_buildMeshlets( meshletData.data(), meshletVertices.data(), meshletTriangles.data(),
												indices.data(), triangleCnt * 3, &( meshFile.positions.at(0)[0] ),
												vertexCnt, sizeof( Vector3 ), 96, 128, 0.0625f );
				}
				meshletData.resize( meshletCnt );
				if ( meshletAlgorithm & 1 ) {
					for ( const auto & m : meshletData ) {
						meshopt_optimizeMeshlet( meshletVertices.data() + m.vertex_offset,
												meshletTriangles.data() + m.triangle_offset,
												m.triangle_count, m.vertex_count );
					}
				}
				k = 0;
				for ( const auto & m : meshletData ) {
					unsigned int	n = m.triangle_count;
					const unsigned int *	v = meshletVertices.data() + m.vertex_offset;
					const unsigned char *	p = meshletTriangles.data() + m.triangle_offset;
					for ( ; n; n--, k++, p = p + 3 ) {
						auto	iTriangle = nif->getIndex( iTriangles, int(k) );
						if ( !iTriangle.isValid() )
							throw FO76UtilsError( "triangle number is out of range" );
						quint16	v0 = quint16( v[p[0]] );
						quint16	v1 = quint16( v[p[1]] );
						quint16	v2 = quint16( v[p[2]] );
						Triangle	t( v0, v1, v2 );
						nif->set<Triangle>( iTriangle, t );
					}
				}
			} else {
				std::vector< DirectX::Meshlet >	tmpMeshlets;
				std::vector< std::uint16_t >	newIndices;
				int	err = DirectX::ComputeMeshlets( meshFile.triangles.data(), triangleCnt,
													meshFile.positions.data(), vertexCnt,
													tmpMeshlets, newIndices, 96, 128 );
				if ( err ) {
					throw FO76UtilsError( err == ERANGE ? "vertex number is out of range"
														: ( err == ENOMEM ? "std::bad_alloc" : "invalid argument" ) );
				}
				meshletData.resize( tmpMeshlets.size() );
				std::uint32_t	vertexOffset = 0;
				std::uint32_t	triangleOffset = 0;
				for ( const auto & m : tmpMeshlets ) {
					meshopt_Meshlet &	o = meshletData[&m - tmpMeshlets.data()];
					o.vertex_offset = vertexOffset;
					o.triangle_offset = triangleOffset;
					o.vertex_count = m.VertCount;
					vertexOffset = vertexOffset + o.vertex_count;
					o.triangle_count = m.PrimCount;
					triangleOffset = ( triangleOffset + ( o.triangle_count * 3U ) + 3U ) & ~3U;
				}
				for ( int i = 0; i < int(triangleCnt); i++ ) {
					Triangle	t( newIndices[i * 3], newIndices[i * 3 + 1], newIndices[i * 3 + 2] );
					nif->set<Triangle>( nif->getIndex( iTriangles, i ), t );
				}
			}
		} catch ( std::exception & e ) {
			meshletData.clear();
			QMessageBox::critical( nullptr, "NifSkope error", QString("Meshlet generation failed: %1").arg(e.what()) );
		}
	}
	int	meshletCount = int( meshletData.size() );

	nif->set<quint32>( iMeshData, "Num Meshlets", quint32(meshletCount) );
	auto	iMeshlets = nif->getIndex( iMeshData, "Meshlets" );
	nif->updateArraySize( iMeshlets );
	for ( int i = 0; i < meshletCount; i++ ) {
		auto	iMeshlet = nif->getIndex( iMeshlets, i );
		if ( iMeshlet.isValid() ) {
			nif->set<quint32>( iMeshlet, "Vertex Count", meshletData[i].vertex_count );
			nif->set<quint32>( iMeshlet, "Vertex Offset", meshletData[i].vertex_offset );
			nif->set<quint32>( iMeshlet, "Triangle Count", meshletData[i].triangle_count );
			nif->set<quint32>( iMeshlet, "Triangle Offset", meshletData[i].triangle_offset );
		}
	}

	updateCullData( nif, iMeshData, meshFile );
}

QModelIndex spGenerateMeshlets::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return index;
	if ( !index.isValid() ) {
		// process all shapes
		for ( int n = 0; n < nif->getBlockCount(); n++ ) {
			QModelIndex idx = nif->getBlockIndex( n );
			if ( idx.isValid() )
				cast( nif, idx );
		}
		return index;
	}

	if ( !( nif->isNiBlock( index, "BSGeometry" ) && nif->checkInternalGeometry( index ) ) )
		return index;

	auto	meshes = nif->getIndex( index, "Meshes" );
	if ( meshes.isValid() ) {
		for ( int i = 0; i <= 3; i++ ) {
			auto mesh = nif->getIndex( meshes, i );
			if ( !mesh.isValid() )
				continue;
			auto hasMesh = nif->getIndex( mesh, "Has Mesh" );
			if ( !hasMesh.isValid() || nif->get<quint8>( hasMesh ) == 0 )
				continue;
			mesh = nif->getIndex( mesh, "Mesh" );
			if ( !mesh.isValid() )
				continue;
			MeshFile	meshFile( nif, mesh );
			auto	meshData = nif->getIndex( mesh, "Mesh Data" );
			if ( meshData.isValid() )
				updateMeshlets( nif, meshData, meshFile );
		}
	}

	return spUpdateBounds::cast_Starfield( nif, index );
}

QModelIndex spGenerateMeshlets::cast_Static( NifModel * nif, const QModelIndex & index )
{
	spGenerateMeshlets	tmp;
	if ( tmp.isApplicable( nif, index ) )
		return tmp.cast( nif, index );
	return index;
}

REGISTER_SPELL( spGenerateMeshlets )


//! Update Triangles on Data from Skin
bool spUpdateTrianglesFromSkin::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	return nif->isNiBlock( index, "NiTriShape" ) && nif->getLink( index, "Skin Instance" ) != -1;
}

QModelIndex spUpdateTrianglesFromSkin::cast( NifModel * nif, const QModelIndex & index )
{
	auto iData = nif->getBlockIndex( nif->getLink( index, "Data" ) );
	auto iSkin = nif->getBlockIndex( nif->getLink( index, "Skin Instance" ) );
	auto iSkinPart = nif->getBlockIndex( nif->getLink( iSkin, "Skin Partition" ) );
	if ( !iSkinPart.isValid() || !iData.isValid() )
		return QModelIndex();

	QVector<Triangle> tris;
	auto iParts = nif->getIndex( iSkinPart, "Partitions" );
	for ( int i = 0; i < nif->rowCount( iParts ) && iParts.isValid(); i++ )
		tris << SkinPartition( nif, nif->getIndex( iParts, i ) ).getRemappedTriangles();

	nif->set<bool>( iData, "Has Triangles", true );
	nif->set<ushort>( iData, "Num Triangles", tris.size() );
	nif->set<uint>( iData, "Num Triangle Points", tris.size() * 3 );
	nif->updateArraySize( iData, "Triangles" );
	nif->setArray( iData, "Triangles", tris );

	return index;
}

REGISTER_SPELL( spUpdateTrianglesFromSkin )
