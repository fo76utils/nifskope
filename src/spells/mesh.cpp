#include "mesh.h"
#include "gl/gltools.h"
#include "qtcompat.h"

#include <QDialog>
#include <QGridLayout>

#include <cfloat>

#include "libfo76utils/src/fp32vec4.hpp"
#include "io/MeshFile.h"
#include "meshlet.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file mesh.cpp
 * \brief Mesh spells
 *
 * All classes here inherit from the Spell class.
 */

//! Find shape data of triangle geometry
static QModelIndex getShape( const NifModel * nif, const QModelIndex & index )
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
			texco << nif->getArray<Vector2>( QModelIndex_child( iUVSets, r ) );

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
			strips << nif->getArray<quint16>( QModelIndex_child( iPoints, r ) );
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
			nif->setArray<quint16>( QModelIndex_child( iPoints, r ), strips[r] );

		nif->set<int>( iData, "Num Vertices", verts.count() );
		nif->updateArraySize( iData, "Vertices" );
		nif->setArray<Vector3>( iData, "Vertices", verts );
		nif->updateArraySize( iData, "Normals" );
		nif->setArray<Vector3>( iData, "Normals", norms );
		nif->updateArraySize( iData, "Vertex Colors" );
		nif->setArray<Color4>( iData, "Vertex Colors", colors );

		for ( int r = 0; r < nif->rowCount( iUVSets ); r++ ) {
			nif->updateArraySize( QModelIndex_child( iUVSets, r ) );
			nif->setArray<Vector2>( QModelIndex_child( iUVSets, r ), texco[r] );
		}

		// process NiSkinData

		QModelIndex iSkinInst = nif->getBlockIndex( nif->getLink( iShape, "Skin Instance" ), "NiSkinInstance" );

		QModelIndex iSkinData = nif->getBlockIndex( nif->getLink( iSkinInst, "Data" ), "NiSkinData" );
		QModelIndex iBones = nif->getIndex( iSkinData, "Bone List" );

		for ( int b = 0; b < nif->rowCount( iBones ); b++ ) {
			QVector<QPair<int, float> > weights;
			QModelIndex iWeights = nif->getIndex( QModelIndex_child( iBones, b ), "Vertex Weights" );

			for ( int w = 0; w < nif->rowCount( iWeights ); w++ ) {
				weights.append( QPair<int, float>( nif->get<int>( QModelIndex_child( iWeights, w ), "Index" ), nif->get<float>( QModelIndex_child( iWeights, w ), "Weight" ) ) );
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

			nif->set<int>( QModelIndex_child( iBones, b ), "Num Vertices", weights.count() );
			nif->updateArraySize( iWeights );

			for ( int w = 0; w < weights.count(); w++ ) {
				nif->set<int>( QModelIndex_child( iWeights, w ), "Index", weights[w].first );
				nif->set<float>( QModelIndex_child( iWeights, w ), "Weight", weights[w].second );
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
		if ( nif->blockInherits( index, "BSTriShape" ) && nif->getIndex( index, "Vertex Data" ).isValid() )
			return true;
		return nif->itemStrType( index ).toLower() == "texcoord" || nif->blockInherits( index, "NiTriBasedGeomData" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex idx = index;

		if ( nif->blockInherits( index, "BSTriShape" ) ) {
			idx = nif->getIndex( index, "Vertex Data" );
		} else if ( nif->itemStrType( index ).toLower() != "texcoord" ) {
			idx = nif->getIndex( nif->getBlockIndex( index ), "UV Sets" );
		}

		QMenu menu;
		static const char * const flipCmds[3] = { "S = 1.0 - S", "T = 1.0 - T", "S <=> T" };

		for ( int c = 0; c < 3; c++ )
			menu.addAction( flipCmds[c] );

		QAction * act = menu.exec( QCursor::pos() );

		if ( act ) {
			for ( int c = 0; c < 3; c++ ) {
				if ( act->text() == flipCmds[c] )
					flip( nif, idx, c );
			}

		}

		return index;
	}

	//! Flips UV data in a model index
	void flip( NifModel * nif, const QModelIndex & index, int f )
	{
		if ( nif->itemStrType( index ) == "BSVertexData" ) {
			// BSTriShape vertex data
			int	n = nif->rowCount( index );
			for ( int i = 0; i < n; i++ ) {
				auto	v = QModelIndex_child( index, i );
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
			QModelIndex idx = QModelIndex_child( index );

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

REGISTER_SPELL( spFlipTexCoords )

//! Flips triangle faces, individually or in the selected array
class spFlipFace final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Flip Face" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( nif->getValue( index ).type() == NifValue::tTriangle )
		       || ( nif->isArray( index ) && nif->getValue( QModelIndex_child( index ) ).type() == NifValue::tTriangle );
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
		return getTriShapeData( nif, index ).isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iData = getTriShapeData( nif, index );

		QVector<Triangle> tris = nif->getArray<Triangle>( iData, "Triangles" );

		for ( int t = 0; t < tris.count(); t++ )
			tris[t].flip();

		nif->setArray<Triangle>( iData, "Triangles", tris );

		return index;
	}
};

REGISTER_SPELL( spFlipAllFaces )

//! Removes redundant triangles from a mesh
class spPruneRedundantTriangles final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Prune Triangles" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->blockInherits( index, "BSTriShape" ) && nif->getIndex( index, "Triangles" ).isValid() )
			return true;
		return getTriShapeData( nif, index ).isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
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

REGISTER_SPELL( spPruneRedundantTriangles )

//! Removes duplicate vertices from a mesh
class spRemoveDuplicateVertices final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Remove Duplicate Vertices" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return getShape( nif, index ).isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		try
		{
			QModelIndex iShape = getShape( nif, index );
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
				texco << nif->getArray<Vector2>( QModelIndex_child( iUVSets, r ) );

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

			// detect the dublicates

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

			QVector<Triangle> tris = nif->getArray<Triangle>( iData, "Triangles" );
			QMutableVectorIterator<Triangle> itri( tris );

			while ( itri.hasNext() ) {
				Triangle & t = itri.next();

				for ( int p = 0; p < 3; p++ ) {
					if ( map.contains( t[p] ) )
						t[p] = map.value( t[p] );
				}

			}

			nif->setArray<Triangle>( iData, "Triangles", tris );

			QModelIndex iPoints = nif->getIndex( iData, "Points" );

			for ( int r = 0; r < nif->rowCount( iPoints ); r++ ) {
				QVector<quint16> strip = nif->getArray<quint16>( QModelIndex_child( iPoints, r ) );
				QMutableVectorIterator<quint16> istrp( strip );

				while ( istrp.hasNext() ) {
					quint16 & p = istrp.next();

					if ( map.contains( p ) )
						p = map.value( p );
				}

				nif->setArray<quint16>( QModelIndex_child( iPoints, r ), strip );
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

REGISTER_SPELL( spRemoveDuplicateVertices )

//! Removes unused vertices
class spRemoveWasteVertices final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Remove Unused Vertices" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif && nif->blockInherits( index, "BSTriShape" ) && nif->getIndex( index, "Vertex Data" ).isValid() )
			return true;
		return getShape( nif, index ).isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->blockInherits( index, "BSTriShape" ) ) {
			removeWasteVertices( nif, index );
		} else {
			QModelIndex iShape = getShape( nif, index );
			QModelIndex iData  = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );

			removeWasteVertices( nif, iData, iShape );
		}

		return index;
	}
};

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
		FloatVector4	v( verts[i][0], verts[i][1], verts[i][2], 0.0f );
		tmpMin.minValues( v );
		tmpMax.maxValues( v );
	}
	bndCenter = ( tmpMin + tmpMax ) * 0.5f;
	bndDims = ( tmpMax - tmpMin ) * 0.5f;

	return true;
}

static void setBoundingBox( NifModel * nif, const QModelIndex & index, FloatVector4 bndCenter, FloatVector4 bndDims )
{
	auto boundMinMax = nif->getIndex( index, "Bound Min Max" );
	if ( !boundMinMax.isValid() )
		return;
	nif->updateArraySize( boundMinMax );
	nif->set<float>( QModelIndex_child( boundMinMax, 0 ), bndCenter[0] );
	nif->set<float>( QModelIndex_child( boundMinMax, 1 ), bndCenter[1] );
	nif->set<float>( QModelIndex_child( boundMinMax, 2 ), bndCenter[2] );
	nif->set<float>( QModelIndex_child( boundMinMax, 3 ), bndDims[0] );
	nif->set<float>( QModelIndex_child( boundMinMax, 4 ), bndDims[1] );
	nif->set<float>( QModelIndex_child( boundMinMax, 5 ), bndDims[2] );
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

//! Updates Bounds of BSTriShape
class spUpdateBounds final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Update Bounds" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->blockInherits( index, "BSGeometry" ) )
			return true;
		return nif->blockInherits( index, "BSTriShape" ) && nif->getIndex( index, "Vertex Data" ).isValid();
	}

	static QModelIndex cast_Starfield( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->blockInherits( index, "BSGeometry" ) )
			return cast_Starfield( nif, index );

		auto vertData = nif->getIndex( index, "Vertex Data" );

		// Retrieve the verts
		QVector<Vector3> verts;
		for ( int i = 0; i < nif->rowCount( vertData ); i++ ) {
			verts << nif->get<Vector3>( QModelIndex_child( vertData, i ), "Vertex" );
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
};

static void updateCullData( NifModel * nif, const QPersistentModelIndex & iMeshData, const MeshFile & meshFile )
{
	int	meshletCount = int( nif->get<quint32>( iMeshData, "Num Meshlets" ) );
	auto	iMeshlets = nif->getIndex( iMeshData, "Meshlets" );
	nif->set<quint32>( iMeshData, "Num Cull Data", quint32(meshletCount) );
	auto	iCullData = nif->getIndex( iMeshData, "Cull Data" );
	nif->updateArraySize( iCullData );
	qsizetype	k = 0;
	for ( int i = 0; i < meshletCount; i++ ) {
		int	triangleCount = int( nif->get<quint32>( QModelIndex_child( iMeshlets, i ), "Triangle Count" ) );
		FloatVector4	bndMin( float(FLT_MAX) );
		FloatVector4	bndMax( float(FLT_MIN) );
		bool	haveBounds = false;
		for ( int j = 0; j < triangleCount; j++, k++ ) {
			if ( k >= meshFile.triangles.size() ) [[unlikely]]
				break;
			Triangle	t = meshFile.triangles.at( k );
			for ( int l = 0; l < 3; l++ ) {
				int	m = t[l];
				if ( !( m >= 0 && m < int(meshFile.positions.size()) ) )
					continue;
				const Vector3 &	v = meshFile.positions.at( m );
				FloatVector4	xyz( v[0], v[1], v[2], 0.0f );
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
		setBoundingBox( nif, QModelIndex_child( iCullData, i ), bndCenter, bndDims );
	}
}

static void updateMeshlets( NifModel * nif, const QPersistentModelIndex & iMeshData, const MeshFile & meshFile )
{
	NifItem *	item = nif->getItem( iMeshData );
	if ( !item )
		return;
	item->invalidateVersionCondition();
	item->invalidateCondition();
	nif->set<quint32>( iMeshData, "Version", 2 );

	std::vector< DirectX::Meshlet >	meshletData;
	std::vector< std::uint16_t >	newIndices;
	if ( DirectX::ComputeMeshlets(
			meshFile.triangles.data(), size_t(meshFile.triangles.size()),
			meshFile.positions.data(), size_t(meshFile.positions.size()), meshletData, newIndices, 96, 128 ) == 0
		&& newIndices.size() == size_t(meshFile.triangles.size() * 3) ) {
		auto	iTriangles = nif->getIndex( iMeshData, "Triangles" );
		int	numTriangles = int( meshFile.triangles.size() );
		for ( int i = 0; i < numTriangles; i++ ) {
			Triangle	t( newIndices[i * 3], newIndices[i * 3 + 1], newIndices[i * 3 + 2] );
			nif->set<Triangle>( QModelIndex_child( iTriangles, i ), t );
		}
	} else {
		meshletData.clear();
		if ( meshFile.triangles.size() > 0 && meshFile.positions.size() > 0 )
			QMessageBox::critical( nullptr, "NifSkope error", QString("Meshlet generation failed") );
	}
	int	meshletCount = int( meshletData.size() );

	nif->set<quint32>( iMeshData, "Num Meshlets", quint32(meshletCount) );
	auto	iMeshlets = nif->getIndex( iMeshData, "Meshlets" );
	nif->updateArraySize( iMeshlets );
	std::uint32_t	vertexOffset = 0;
	std::uint32_t	triangleOffset = 0;
	for ( int i = 0; i < meshletCount; i++ ) {
		std::uint32_t	triangleCount = meshletData[i].PrimCount;
		auto	iMeshlet = QModelIndex_child( iMeshlets, i );
		if ( iMeshlet.isValid() ) {
			nif->set<quint32>( iMeshlet, "Vertex Count", meshletData[i].VertCount );
			nif->set<quint32>( iMeshlet, "Vertex Offset", vertexOffset );
			nif->set<quint32>( iMeshlet, "Triangle Count", triangleCount );
			nif->set<quint32>( iMeshlet, "Triangle Offset", triangleOffset );
		}
		vertexOffset = vertexOffset + meshletData[i].VertCount;
		triangleOffset = ( triangleOffset + ( triangleCount * 3U) + 3U ) & ~3U;
	}
	updateCullData( nif, iMeshData, meshFile );
}

QModelIndex spUpdateBounds::cast_Starfield( NifModel * nif, const QModelIndex & index )
{
	auto meshes = nif->getIndex( index, "Meshes" );
	if ( !meshes.isValid() )
		return index;

	bool	boundsCalculated = false;
	BoundSphere	bounds;
	FloatVector4	bndCenter( 0.0f );
	FloatVector4	bndDims( -1.0f );
	for ( int i = 0; i <= 3; i++ ) {
		auto mesh = QModelIndex_child( meshes, i );
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
			// Creating a bounding sphere and bounding box from the verts
			bounds = BoundSphere( meshFile.positions, true );
			calculateBoundingBox( bndCenter, bndDims, meshFile.positions );
			boundsCalculated = true;
		}
		if ( ( nif->get<quint32>(index, "Flags") & 0x0200 ) == 0 )
			continue;
		auto	meshData = nif->getIndex( mesh, "Mesh Data" );
		// update cull data for version 2 meshlets
		if ( meshData.isValid() && nif->get<quint32>( meshData, "Version" ) >= 2U )
			updateCullData( nif, meshData, meshFile );
	}

	bounds.update( nif, index );
	setBoundingBox( nif, index, bndCenter, bndDims );

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
};

REGISTER_SPELL( spUpdateAllBounds )


//! Generates Starfield meshlets
class spGenerateMeshlets final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Generate Meshlets and Update Bounds" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		if ( !index.isValid() )
			return true;
		return ( nif->blockInherits( index, "BSGeometry" ) && ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

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

	if ( !nif->blockInherits( index, "BSGeometry" ) )
		return index;

	auto	meshes = nif->getIndex( index, "Meshes" );
	if ( meshes.isValid() && ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 ) {
		for ( int i = 0; i <= 3; i++ ) {
			auto mesh = QModelIndex_child( meshes, i );
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
		tris << SkinPartition( nif, QModelIndex_child( iParts, i ) ).getRemappedTriangles();

	nif->set<bool>( iData, "Has Triangles", true );
	nif->set<ushort>( iData, "Num Triangles", tris.size() );
	nif->set<uint>( iData, "Num Triangle Points", tris.size() * 3 );
	nif->updateArraySize( iData, "Triangles" );
	nif->setArray( iData, "Triangles", tris );

	return index;
}

REGISTER_SPELL( spUpdateTrianglesFromSkin )
