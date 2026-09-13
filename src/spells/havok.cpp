#include "spellbook.h"

#include "gl/glshape.h"
#include "gl/gltools.h"
#include "glview.h"
#include "nifskope.h"
#include "spells/blocks.h"

#include "lib/coacd.h"
#include "lib/nvtristripwrapper.h"
#include "lib/qhull.h"
#include "meshoptimizer/src/meshoptimizer.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

// Brief description is deliberately not autolinked to class Spell
/*! \file havok.cpp
 * \brief Havok spells
 *
 * All classes here inherit from the Spell class.
 */

//! For Havok coordinate transforms
static const float havokConst = 7.0f;

//! Creates a convex hull using Qhull
class spCreateCVS final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Create Convex Shapes" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	static bool hasGeometryData( const Node * node )
	{
		if ( !node )
			return false;
		if ( const Shape * s = dynamic_cast< const Shape * >( node ); s )
			return !( s->verts.isEmpty() || s->triangles.isEmpty() );
		const auto &	c = node->getChildren().list();
		for ( auto n : c ) {
			if ( hasGeometryData( n ) )
				return true;
		}
		return false;
	}

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() > 0 && index.isValid() ) )
			return false;
		if ( !nif->blockInherits( index, { "BSGeometry", "BSTriShape", "NiNode", "NiTriBasedGeom" } ) )
			return false;
		Scene * scene = nullptr;
		if ( NifSkope * w = dynamic_cast< NifSkope * >( const_cast< NifModel * >( nif )->getWindow() ); w ) {
			if ( GLView * ogl = w->getGLView(); ogl )
				scene = ogl->getScene();
		}
		if ( !scene && scene->renderer )
			return false;
		QModelIndex iBlock = nif->getBlockIndex( index );
		if ( auto node = scene->getNode( nif, iBlock ); node )
			return hasGeometryData( node );
		return false;
	}

	struct MeshData {
		QVector<Vector3> verts;
		QVector<unsigned int> indices;
		void removeDuplicateVertices();
	};

	static float getHavokScale( const NifModel * nif );
	static void getShapeData( QVector<MeshData> & meshes, NifModel * nif, const Node * node, int rootNode );
	static void createConvexShapes( QVector<MeshData> & meshes, CoACD & coacd );
	static void addLabel( QBoxLayout * parent, const QString & l );
	static QDoubleSpinBox * addSpinBox( QBoxLayout * parent, const QString & l,
										double v, double minVal, double maxVal, int nDigits );
	static QSpinBox * addSpinBox( QBoxLayout * parent, const QString & l, int v, int minVal, int maxVal );
	static QCheckBox * addCheckBox( QBoxLayout * parent, const QString & l, bool v );
	static QComboBox * addComboBox( QBoxLayout * parent, const QString & l, int v, const QStringList & itemList );
	static bool settingsDialog( CoACD & coacd, float & precision, float & radius, float & simplifyMaxError,
								bool & replaceShape, bool & enableCoACD );
	
	// sort and remove duplicates from a QVector of Vector4
	QVector<Vector4> SortUnique( QVector<Vector4> input )
	{
		QMap<uint, Vector4> hashmap;
		QMap<Vector4, bool> vectormap;
		QVector<Vector4> result;
		for ( Vector4 v : input )
		{
			hashmap.insert( qHash( v ), v );
		}
		for ( auto i = hashmap.constBegin(); i != hashmap.constEnd(); i++ )
			vectormap.insert( i.value(), false );
		for ( auto i = vectormap.constBegin(); i != vectormap.constEnd(); i++ )
			result.append( i.key() );
		return result;
	}
	
	// sort and remove duplicates from a QVector of Vector4 in place
	void SortUniqueInPlace( QVector<Vector4> & vector )
	{
		QMap<uint, Vector4> hashmap;
		QMap<Vector4, bool> vectormap;
		for ( Vector4 v : vector )
		{
			hashmap.insert( qHash( v ), v );
		}
		for ( auto i = hashmap.constBegin(); i != hashmap.constEnd(); i++ )
			vectormap.insert( i.value(), false );
		vector.clear();
		for ( auto i = vectormap.constBegin(); i != vectormap.constEnd(); i++ )
			vector.append( i.key() );
	}
	
	// find the mean of a QVector of Vector4
	Vector4 mean( QVector<Vector4> vector )
	{
		int len = vector.size();
		float xsum = 0, ysum = 0, zsum = 0, wsum = 0;
		
		for ( int i = 0; i < len; i++ )
		{
			xsum += vector[i][0];
			ysum += vector[i][1];
			zsum += vector[i][2];
			wsum += vector[i][3];
		}
		
		float x = 0, y = 0, z = 0, w = 0;
		x = xsum / len;
		y = ysum / len;
		z = zsum / len;
		w = wsum / len;
		
		return Vector4(x,y,z,w);
	}
	
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex	iBlock = nif->getBlockIndex( index );
		QModelIndex	iParent = iBlock;
		if ( !nif->blockInherits( iParent, "NiNode" ) ) {
			iParent = nif->getBlockIndex( nif->getParent( iBlock ) );
			if ( !( iParent.isValid() && nif->blockInherits( iParent, "NiNode" ) ) )
				return index;
		}
		QVector<MeshData>	meshes;
		{
			Scene * scene = nullptr;
			if ( NifSkope * w = dynamic_cast< NifSkope * >( nif->getWindow() ); w ) {
				if ( GLView * ogl = w->getGLView(); ogl )
					scene = ogl->getScene();
			}
			if ( !scene && scene->renderer )
				return index;
			if ( auto node = scene->getNode( nif, iBlock ); node )
				getShapeData( meshes, nif, node, nif->getBlockNumber( iParent ) );
		}
		if ( meshes.isEmpty() )
			return index;
		meshes.first().removeDuplicateVertices();
		if ( meshes.constFirst().verts.isEmpty() || meshes.constFirst().indices.isEmpty() )
			return index;

		CoACD	coacd;
		float	precision = 0.25f;
		float	radius = 0.05f;
		float	simplifyMaxError = 0.0f;
		bool	replaceShape = false;
		bool	enableCoACD = false;
		if ( !settingsDialog( coacd, precision, radius, simplifyMaxError, replaceShape, enableCoACD ) )
			return index;

		if ( simplifyMaxError >= 0.00005f ) {
			auto &	m = meshes.first();
			QVector<unsigned int>	newIndices;
			newIndices.resize( m.indices.size() );
			size_t	n = meshopt_simplify( newIndices.data(), m.indices.constData(), size_t( m.indices.size() ),
											&( m.verts.constFirst()[0] ), size_t( m.verts.size() ), sizeof( Vector3 ),
											12, simplifyMaxError, meshopt_SimplifyLockBorder, nullptr );
			newIndices.resize( qsizetype( n ) );
			m.indices = newIndices;
			m.removeDuplicateVertices();
		}

		if ( enableCoACD )
			createConvexShapes( meshes, coacd );

		if ( meshes.size() > 1 )
			nif->setState( BaseModel::Processing );
		QModelIndex	rigidBody = index;
		// For each shape: make a convex hull or box shape from it
		for ( const MeshData & m : meshes ) {
			QModelIndex	iCVS;
			qsizetype	cvsVertCount = 0;
			qsizetype	cvsNormCount = 0;
			if ( enableCoACD && coacd.apxMode == 1 && m.verts.size() == 8 ) {	// CoACD approximation mode = box
				FloatVector4	boundsMin = FloatVector4( m.verts.constFirst() );
				FloatVector4	boundsMax = boundsMin;
				for ( qsizetype i = 1; i < 8; i++ ) {
					FloatVector4	v = FloatVector4( m.verts.at( i ) );
					boundsMin.minValues( v );
					boundsMax.maxValues( v );
				}
				FloatVector4	boundsCenter( ( boundsMin + boundsMax ) * 0.5f );
				FloatVector4	boundsDims( ( boundsMax - boundsMin ) * 0.5f );
				iCVS = nif->insertNiBlock( "bhkBoxShape" );
				nif->set<Vector3>( iCVS, "Dimensions", Vector3( boundsDims ) );
				// radius is always 0.1?
				// TODO: Figure out if radius is not arbitrarily set in vanilla NIFs
				nif->set<float>( iCVS, "Radius", radius );
				{
					QModelIndex	i = nif->insertNiBlock( "bhkTransformShape" );
					nif->setLink( i, "Shape", nif->getBlockNumber( iCVS ) );
					iCVS = i;
				}
				nif->set<Matrix4>( iCVS, "Transform", Transform( Vector3( boundsCenter ), 1.0f ).toMatrix4() );

			} else {									// CoACD disabled or approximation mode = convex hull
				// to store results of initial convex hull
				QVector<Vector4> hullVerts, hullNorms;
				
				// for intermediate steps
				QVector<QVector<Vector4>> hullTris;
				QVector<Vector4> hullVertsShrunk, hullPlanes, hullPlanesShrunk;
				
				// to store final convex shape data
				QVector<Vector4> convex_verts, convex_norms;

				compute_convex_hull( m.verts, hullVerts, hullNorms, precision / getHavokScale( nif ) );

				{
					QVector<Vector4> tri(3);
					for ( int i = 0; i < hullVerts.size(); i += 3 )
					{
						tri[0] = hullVerts[i];
						tri[1] = hullVerts[i + 1];
						tri[2] = hullVerts[i + 2];
						hullTris.append( tri );
					}
				}
				
				SortUniqueInPlace( hullVerts );
				int hullVertsInitialCount = hullVerts.size();
				
				Vector3 center = Vector3( mean( hullVerts ) );

#if 1
				// hullNorms seem a bit off sometimes, so construct them ourselves
				for ( int i = 0; i < hullTris.size(); i++ )
				{
					Vector3 v01 = Vector3( hullTris[i][1] ) - Vector3( hullTris[i][0] );
					Vector3 v12 = Vector3( hullTris[i][2] ) - Vector3( hullTris[i][1] );
					Vector3 normal = Vector3::crossproduct( v01, v12 );
					normal.normalize();
					float d = Vector3::dotproduct( normal, Vector3( hullTris[i][0] ) );
					
					// check that the center is on the inside
					float h = Vector3::dotproduct( normal, center ) - d;
					if ( h > 0 )
					{
						normal = -normal;
						d = -d;
					}
					
					hullPlanes.append( Vector4( normal, d ) );
				}
#endif

#if 0
				for ( int i = 0; i < hullNorms.size(); i++ )
				{
					hullPlanes.append( Vector4( Vector3( hullNorms[i] ), -hullNorms[i][3] ) );
				}
#endif	
		
				if ( radius != 0 )
				{
					// shift planes toward center by distance radius
					for ( int i = 0; i < hullTris.size(); i++ )
					{
						Vector3 normal = Vector3( hullPlanes[i] );
						float d = hullPlanes[i][3];
						float h = Vector3::dotproduct( normal, center ) - d;
						int hsign;
						if ( h < 0 )
							hsign = -1;
						else
							hsign = 1;
						
						float d_act = abs( h );
						float d_new;
						// if the distance from center to plane is zero (or -zero !) don't change it.
						// otherwise, check to make sure it isn't too close to center, and if it is,
						// move it 99% of the way there instead
						if ( d != 0 )
						{
							if ( d_act < radius )
							{
								d_new = d + 0.99 * d_act * hsign;
							}
							else
							{
								d_new = d + radius * hsign;
							}
							
							hullPlanesShrunk.append( Vector4( normal, d_new ) );
						}
						else
						{
							hullPlanesShrunk.append( Vector4( normal, d ) );
						}
					}
					// sort and remove duplicate normals
					SortUniqueInPlace( hullPlanesShrunk );
					
					// calculate new points of intersection from the shrunk planes
					for ( int i = 0; i < hullPlanesShrunk.size() - 2; i++ )
					{
						Vector4 plane1 = hullPlanesShrunk[i];
						Vector3 normal1 = Vector3( plane1 );
						float a1 = plane1[0];
						float b1 = plane1[1];
						float c1 = plane1[2];
						float d1 = plane1[3];
						
						for ( int j = i + 1 ; j < hullPlanesShrunk.size() - 1; j++ )
						{
							Vector4 plane2 = hullPlanesShrunk[j];
							Vector3 normal2 = Vector3( plane2 );
							float a2 = plane2[0];
							float b2 = plane2[1];
							float c2 = plane2[2];
							float d2 = plane2[3];
							
							// check for parallel planes
							Vector3 D = Vector3::crossproduct( normal1, normal2 ); // cross product of two normals is a normal orthogonal to the two
							float mag_D = Vector3::dotproduct( D, D ); // vector dot product of itself is its magnitude
							// zero means the planes are parallel
							// one means ???
							// vector dot product of itself cannot be negative so don't bother to do abs()
							if ( mag_D < 0.000001 )
								continue;
							
							for ( int k = j + 1 ; k < hullPlanesShrunk.size(); k++ )
							{
								Vector4 plane3 = hullPlanesShrunk[k];
								Vector3 normal3 = Vector3( plane3 );
								float a3 = plane3[0];
								float b3 = plane3[1];
								float c3 = plane3[2];
								float d3 = plane3[3];
								
								// check for parallel planes
								Vector3 D1 = Vector3::crossproduct( normal1, normal3 );
								Vector3 D2 = Vector3::crossproduct( normal2, normal3 );
								float mag_D1 = Vector3::dotproduct( D1, D1 );
								float mag_D2 = Vector3::dotproduct( D2, D2 );
								if ( ( mag_D1 < 0.000001 ) || ( mag_D2 < 0.000001 ) )
									continue;
								
								// divide by zero problems here
								float z = ( (d3-d2*a3/a2) - (b3-b2*a3/a2)*(d1-d2*a1/a2)/(b1-b2*a1/a2) ) / ((c3-c2*a3/a2) - (b3-b2*a3/a2)*(c1-c2*a1/a2)/(b1-b2*a1/a2));
								float y = ( (d3-d2*a3/a2) - (c3-c2*a3/a2)*z ) / (b3-b2*a3/a2);
								float x = (d1-b1*y-c1*z) / a1;
								Vector3 P = Vector3( x, y, z );
								
								// check if point is on the correct side of all other planes
								QVector<Vector4> idx = hullPlanesShrunk;
								idx.removeAt( k );
								idx.removeAt( j );
								idx.removeAt( i );
								bool isValid = true;
								for ( int m = 0; m < idx.size(); m++ )
								{
									Vector3 normal = Vector3( idx[m] );
									float d = idx[m][3];
									float h = Vector3::dotproduct( normal, P ) - d;
									if ( h > 0 )
									{
										isValid = false;
										break;
									}
								}
								
								if ( isValid )
									hullVertsShrunk.append( Vector4( Vector3( P ), 0 ) );
							}
						}
					}
					
					// sort and remove duplicate normals and vertices
					SortUniqueInPlace( hullPlanesShrunk );
					SortUniqueInPlace( hullVertsShrunk );
					
					// combine nearby vertices.
					// find the distance between every vertex and every other vertex.
						
					// combine the two nearest until the total vertices remaining matches that
					// of the original hull.
						
					// keep track of the number of times a vertex has been combined, so that a
					// weighted average can be used if it is combed additional times.
					
					QVector<int> avg_weights( hullVertsShrunk.size() );
					
					while ( hullVertsShrunk.size() > hullVertsInitialCount )
					{
						// find the smallest distance between any two vertices by checking every
						// pair. if the current checked pair is smallest found so far, record
						// that distance and the indices of the two vertices.
						int i_merge, j_merge;						
						float d_merge = MAXFLOAT;
						
						for ( int i = 0; i < hullVertsShrunk.size() - 1; i++ )
						{
							Vector3 v1 = Vector3( hullVertsShrunk[i] );
							for ( int j = i + 1; hullVertsShrunk.size(); j++ )
							{
								Vector3 v2 = Vector3( hullVertsShrunk[j] );
								float d = Vector3::distance( v1, v2 );
								if ( d < d_merge )
								{
									i_merge = i;
									j_merge = j;
									d_merge = d;
								}
							}
						}
						
						// combine two vertices with the smallest distance apart
						Vector3 v1 = Vector3( hullVertsShrunk[i_merge] );
						int w1 = avg_weights[i_merge];
						Vector3 v2 = Vector3( hullVertsShrunk[j_merge] );
						int w2 = avg_weights[j_merge];
						
						// weighted average
						Vector3 v_merge = ( v1 * w1 + v2 * w2 ) / ( w1 + w2 );
						int w_merge = w1 + w2;
						
						// record the merged vertex by overwriting the first vertex
						hullVertsShrunk[i_merge] = v_merge;
						avg_weights[i_merge] = w_merge;
						
						// and erase the second vertex
						hullVertsShrunk.removeAt( j_merge );
						avg_weights.removeAt( j_merge );
					}
					
					// sort and remove duplicate normals and vertices
					convex_norms = SortUnique( hullPlanesShrunk );
					convex_verts = SortUnique( hullVertsShrunk );
				}
				else
				{
					// sort and remove duplicate normals and vertices
					convex_norms = SortUnique( hullPlanes );
					convex_verts = SortUnique( hullVerts );
				}
				cvsNormCount = convex_norms.size();
				if ( cvsVertCount = convex_verts.size(); cvsVertCount < 4 )
					continue;
				
				for ( int i = 0; i < convex_norms.size(); i++ )
				{
					convex_norms[i][3] = convex_norms[i][3] * -1 - radius;
				}

				/* create the CVS block */
				iCVS = nif->insertNiBlock( "bhkConvexVerticesShape" );

				/* set CVS verts */
				nif->set<uint>( iCVS, "Num Vertices", convex_verts.count() );
				nif->updateArraySize( iCVS, "Vertices" );
				nif->setArray<Vector4>( iCVS, "Vertices", convex_verts );

				/* set CVS norms */
				nif->set<uint>( iCVS, "Num Normals", convex_norms.count() );
				nif->updateArraySize( iCVS, "Normals" );
				nif->setArray<Vector4>( iCVS, "Normals", convex_norms );
			}

			// radius is always 0.1?
			// TODO: Figure out if radius is not arbitrarily set in vanilla NIFs
			nif->set<float>( iCVS, "Radius", radius );

			QModelIndex collisionLink = nif->getIndex( iParent, "Collision Object" );
			QModelIndex collisionObject = nif->getBlockIndex( nif->getLink( collisionLink ) );

			// create bhkCollisionObject
			if ( !collisionObject.isValid() ) {
				collisionObject = nif->insertNiBlock( "bhkCollisionObject" );

				nif->setLink( collisionLink, nif->getBlockNumber( collisionObject ) );
				nif->setLink( collisionObject, "Target", nif->getBlockNumber( iParent ) );
			}

			QModelIndex rigidBodyLink = nif->getIndex( collisionObject, "Body" );
			rigidBody = nif->getBlockIndex( nif->getLink( rigidBodyLink ) );

			// create bhkRigidBody
			if ( !rigidBody.isValid() ) {
				rigidBody = nif->insertNiBlock( "bhkRigidBody" );

				nif->setLink( rigidBodyLink, nif->getBlockNumber( rigidBody ) );
			}

			QPersistentModelIndex shapeLink = nif->getIndex( rigidBody, "Shape" );
			QPersistentModelIndex shape = nif->getBlockIndex( nif->getLink( shapeLink ) );

			if ( replaceShape && shape.isValid() ) {
				replaceShape = false;
				nif->setLink( shapeLink, -1 );
				// Remove all old shapes
				spRemoveBranch().castIfApplicable( nif, shape );
				shape = QModelIndex();
			}

			QVector<qint32> shapeLinks;
			bool replace = true;
			if ( shape.isValid() ) {
				shapeLinks = { nif->getBlockNumber( shape ) };

				QString questionTitle = tr( "Create List Shape" );
				QString questionBody = tr( "This collision object already has a shape. Combine into a list shape? 'No' will replace the shape." );

				bool isListShape = false;
				if ( nif->blockInherits( shape, "bhkListShape" ) ) {
					isListShape = true;
					questionTitle = tr( "Add to List Shape" );
					questionBody = tr( "This collision object already has a list shape. Add to list shape? 'No' will replace the list shape." );
					shapeLinks = nif->getLinkArray( shape, "Sub Shapes" );
				}

				int response = QMessageBox::Yes;
				if ( meshes.size() < 2 ) {
					response = QMessageBox::question( nullptr, questionTitle, questionBody,
														QMessageBox::Yes, QMessageBox::No );
				}
				if ( response == QMessageBox::Yes ) {
					QModelIndex iListShape = shape;
					if ( !isListShape ) {
						iListShape = nif->insertNiBlock( "bhkListShape" );
						nif->setLink( shapeLink, nif->getBlockNumber( iListShape ) );
					}

					shapeLinks << nif->getBlockNumber( iCVS );
					nif->set<uint>( iListShape, "Num Sub Shapes", shapeLinks.size() );
					nif->updateArraySize( iListShape, "Sub Shapes" );
					nif->setLinkArray( iListShape, "Sub Shapes", shapeLinks );
					nif->set<uint>( iListShape, "Num Filters", shapeLinks.size() );
					nif->updateArraySize( iListShape, "Filters" );
					replace = false;
				}
			}

			if ( replace ) {
				// Replace link
				nif->setLink( shapeLink, nif->getBlockNumber( iCVS ) );
				// Remove all old shapes
				spRemoveBranch().castIfApplicable( nif, shape );
			}

			if ( cvsVertCount > 0 ) {
				Message::append( nullptr, Spell::tr( "Create Convex Shapes" ),
									Spell::tr( "Created hull with %1 vertices, %2 normals" )
									.arg( cvsVertCount ).arg( cvsNormCount ), QMessageBox::Information );
			}
		}

		if ( meshes.size() > 1 )
			nif->restoreState();

		return rigidBody;
	}
};

void spCreateCVS::MeshData::removeDuplicateVertices()
{
	QMap<Vector3, unsigned int>	vertSet;
	QVector<unsigned int>	vertMap;
	vertMap.resize( verts.size() );
	unsigned int	n = 0;
	for ( const Vector3 & v : verts ) {
		qsizetype	j = qsizetype( &v - &(verts.constFirst()) );
		if ( auto i = vertSet.constFind( v ); i != vertSet.constEnd() ) {
			vertMap[j] = i.value();
			continue;
		}
		vertSet.insert( v, n );
		vertMap[j] = n;
		verts[n] = v;
		n++;
	}
	verts.resize( qsizetype( n ) );
	QVector<unsigned int>	newIndices;
	newIndices.reserve( indices.size() );
	for ( qsizetype i = 0; ( i + 3 ) <= indices.size(); i = i + 3 ) {
		unsigned int	v0 = indices.at( i );
		unsigned int	v1 = indices.at( i + 1 );
		unsigned int	v2 = indices.at( i + 2 );
		if ( qsizetype( std::max( std::max( v0, v1 ), v2 ) ) >= vertMap.size() )
			continue;
		v0 = vertMap.at( v0 );
		v1 = vertMap.at( v1 );
		v2 = vertMap.at( v2 );
		if ( v0 == v1 || v0 == v2 || v1 == v2 )
			continue;
		newIndices.append( v0 );
		newIndices.append( v1 );
		newIndices.append( v2 );
	}
	indices = newIndices;
}

float spCreateCVS::getHavokScale( const NifModel * nif )
{
	if ( nif->getBSVersion() >= 170 )
		return 1.0f;
	if ( nif->checkVersion( 0x14020007, 0x14020007 ) && nif->getUserVersion() >= 12 )
		return havokConst * 10.0f;
	return havokConst;
}

void spCreateCVS::getShapeData( QVector<MeshData> & meshes, NifModel * nif, const Node * node, int rootNode )
{
	if ( !node )
		return;
	const Shape *	s = dynamic_cast< const Shape * >( node );
	if ( !s ) {
		const auto &	c = node->getChildren().list();
		for ( auto n : c )
			getShapeData( meshes, nif, n, rootNode );
		return;
	}

	if ( s->verts.isEmpty() || s->triangles.isEmpty() )
		return;

	if ( meshes.isEmpty() )
		meshes.resize( 1 );
	MeshData &	m = meshes.last();
	qsizetype	vertexOffs = m.verts.size();
	qsizetype	indicesOffs = m.indices.size();
	m.verts.resize( vertexOffs + s->verts.size() );
	const Transform &	trans = s->localTrans( rootNode );
	Vector3 *	vp = m.verts.data() + vertexOffs;
	float	havokScale = getHavokScale( nif );
	for ( const Vector3 & v : s->verts ) {
		*vp = trans * v / havokScale;
		vp++;
	}
	m.indices.resize( indicesOffs + ( s->triangles.size() * 3 ) );
	unsigned int *	tp = m.indices.data() + indicesOffs;
	for ( const Triangle & t : s->triangles ) {
		tp[0] = (unsigned int) vertexOffs + t[0];
		tp[1] = (unsigned int) vertexOffs + t[1];
		tp[2] = (unsigned int) vertexOffs + t[2];
		tp = tp + 3;
	}
}

void spCreateCVS::createConvexShapes( QVector<MeshData> & meshes, CoACD & coacd )
{
	if ( meshes.isEmpty() )
		return;
	const MeshData &	m = meshes.constFirst();
	if ( m.verts.isEmpty() || m.indices.isEmpty() )
		return;

	CoACD::Mesh	coacdInput;
	qsizetype	numVerts = m.verts.size();
	qsizetype	numIndices = m.indices.size();
	qsizetype	numTriangles = numIndices / 3;
	coacdInput.vertices.resize( size_t( numVerts ) );
	coacdInput.indices.resize( size_t( numTriangles ) );
	for ( qsizetype i = 0; i < numVerts; i++ ) {
		coacdInput.vertices[i][0] = m.verts.at( i )[0];
		coacdInput.vertices[i][1] = m.verts.at( i )[1];
		coacdInput.vertices[i][2] = m.verts.at( i )[2];
	}
	for ( qsizetype i = 0; i < numTriangles; i++ ) {
		coacdInput.indices[i][0] = int( m.indices.at( i * 3 ) );
		coacdInput.indices[i][1] = int( m.indices.at( i * 3 + 1 ) );
		coacdInput.indices[i][2] = int( m.indices.at( i * 3 + 2 ) );
	}

	std::vector< CoACD::Mesh >	coacdOutput = coacd.processMesh( coacdInput );
	if ( coacdOutput.empty() )
		return;

	meshes.clear();
	meshes.resize( qsizetype( coacdOutput.size() ) );
	for ( size_t j = 0; j < coacdOutput.size(); j++ ) {
		const CoACD::Mesh &	o = coacdOutput[j];
		MeshData &	p = meshes[j];
		numVerts = qsizetype( o.vertices.size() );
		numTriangles = qsizetype( o.indices.size() );
		numIndices = numTriangles * 3;
		p.verts.resize( numVerts );
		p.indices.resize( numIndices );
		Vector3 *	vp = p.verts.data();
		for ( qsizetype i = 0; i < numVerts; i++ ) {
			*vp = Vector3( float( o.vertices[i][0] ), float( o.vertices[i][1] ), float( o.vertices[i][2] ) );
			vp++;
		}
		unsigned int *	tp = p.indices.data();
		for ( qsizetype i = 0; i < numTriangles; i++ ) {
			tp[0] = (unsigned int) o.indices[i][0];
			tp[1] = (unsigned int) o.indices[i][1];
			tp[2] = (unsigned int) o.indices[i][2];
			tp = tp + 3;
		}
	}
}

void spCreateCVS::addLabel( QBoxLayout * parent, const QString & l )
{
	parent->addWidget( new QLabel( l ) );
}

QDoubleSpinBox * spCreateCVS::addSpinBox( QBoxLayout * parent, const QString & l,
											double v, double minVal, double maxVal, int nDigits )
{
	QHBoxLayout *	hbox = new QHBoxLayout;
	parent->addLayout( hbox );
	QDoubleSpinBox *	o = new QDoubleSpinBox;
	o->setAccelerated( true );
	o->setRange( minVal, maxVal );
	o->setDecimals( nDigits );
	o->setSingleStep( std::pow( 10.0, double( 1 - nDigits ) ) );
	o->setValue( v );
	hbox->addWidget( new QLabel( l ) );
	hbox->addWidget( o );
	return o;
}

QSpinBox * spCreateCVS::addSpinBox( QBoxLayout * parent, const QString & l, int v, int minVal, int maxVal )
{
	QHBoxLayout *	hbox = new QHBoxLayout;
	parent->addLayout( hbox );
	QSpinBox *	o = new QSpinBox;
	o->setAccelerated( true );
	o->setRange( minVal, maxVal );
	o->setSingleStep( 1 );
	o->setValue( v );
	hbox->addWidget( new QLabel( l ) );
	hbox->addWidget( o );
	return o;
}

QCheckBox * spCreateCVS::addCheckBox( QBoxLayout * parent, const QString & l, bool v )
{
	QCheckBox *	o = new QCheckBox( l );
	o->setChecked( v );
	parent->addWidget( o );
	return o;
}

QComboBox * spCreateCVS::addComboBox( QBoxLayout * parent, const QString & l, int v, const QStringList & itemList )
{
	QHBoxLayout *	hbox = new QHBoxLayout;
	parent->addLayout( hbox );
	QComboBox *	o = new QComboBox;
	o->addItems( itemList );
	o->setCurrentIndex( v );
	hbox->addWidget( new QLabel( l ) );
	hbox->addWidget( o );
	return o;
}

bool spCreateCVS::settingsDialog( CoACD & coacd, float & precision, float & radius, float & simplifyMaxError,
									bool & replaceShape, bool & enableCoACD )
{
	{
		QSettings	settings;
		settings.beginGroup( "Spells/Havok/Create Convex Shapes" );
		precision = settings.value( "Precision", 0.25f ).toFloat();
		radius = settings.value( "Radius", 0.05f ).toFloat();
		simplifyMaxError = settings.value( "Simplify Max Error", 0.0f ).toFloat();
		replaceShape = settings.value( "Replace Shape", false ).toBool();
		enableCoACD = settings.value( "Enable CoACD", false ).toBool();
		coacd.loadSettings( settings );
		settings.endGroup();
	}

	// ask for precision
	QDialog dlg;
	QVBoxLayout * vbox = new QVBoxLayout;
	dlg.setLayout( vbox );

	addLabel( vbox, Spell::tr( "Enter the maximum Qhull roundoff error to use (in NIF units)" ) );
	addLabel( vbox, Spell::tr( "Larger values will give a less precise but better performing hull" ) );
	auto precSpin = addSpinBox( vbox, QString(), precision, 0.0, 5.0, 3 );
	auto spnSimplifyMaxError = addSpinBox( vbox, Spell::tr( "Pre-Simplify Max Error (relative to mesh size)" ),
											simplifyMaxError, 0.0, 0.5, 4 );
	auto chkReplaceShape = addCheckBox( vbox, Spell::tr( "Replace Existing Collision Shapes" ), replaceShape );
	auto spnRadius = addSpinBox( vbox, Spell::tr( "Collision Radius (in Havok units)" ), radius, 0.0, 0.5, 4 );

	addLabel( vbox, QString() );

	auto chkEnableCoACD = addCheckBox( vbox, Spell::tr( "Enable CoACD" ), enableCoACD );

	auto spnThreshold = addSpinBox( vbox, Spell::tr( "Threshold" ), coacd.threshold, 0.01, 1.0, 3 );
	auto spnMaxConvexHull = addSpinBox( vbox, Spell::tr( "Max Convex Hull" ), coacd.maxConvexHull, -1, 256 );
	auto chkPreprocessMode = addComboBox( vbox, Spell::tr( "Preprocess Mode" ), coacd.preprocessMode,
											{ Spell::tr( "Auto" ), Spell::tr( "On" ), Spell::tr( "Off" ) } );
	auto spnPrepResolution = addSpinBox( vbox, Spell::tr( "Preprocess Resolution" ), coacd.prepResolution, 10, 1000 );
	auto spnSampleResolution = addSpinBox( vbox, Spell::tr( "Sample Resolution" ), coacd.sampleResolution, 100, 20000 );
	auto spnMCTSNodes = addSpinBox( vbox, Spell::tr( "MCTS Nodes" ), coacd.mctsNodes, 5, 100 );
	auto spnMCTSIteration = addSpinBox( vbox, Spell::tr( "MCTS Iteration" ), coacd.mctsIteration, 10, 1000 );
	auto spnMCTSMaxDepth = addSpinBox( vbox, Spell::tr( "MCTS Max Depth" ), coacd.mctsMaxDepth, 1, 5 );
	auto chkPCA = addCheckBox( vbox, Spell::tr( "Use PCA" ), coacd.pca );
	auto spnMaxCHVertex = addSpinBox( vbox, Spell::tr( "Max Convex Hull Vertex" ), coacd.maxCHVertex, -1, 4096 );
	auto spnExtrudeMargin = addSpinBox( vbox, Spell::tr( "Extrude Margin (in Havok units)" ), coacd.extrudeMargin,
										0.0, 1.0, 4 );
	auto chkApxMode = addComboBox( vbox, Spell::tr( "Approximation Mode" ), coacd.apxMode,
									{ Spell::tr( "Convex Hull" ), Spell::tr( "Box" ) } );
	auto spnSeed = addSpinBox( vbox, Spell::tr( "Random Seed" ), coacd.seed, 0, 0x7FFFFFFF );

	addLabel( vbox, QString() );

	QHBoxLayout * hbox = new QHBoxLayout;
	vbox->addLayout( hbox );

	QPushButton * ok = new QPushButton;
	ok->setText( Spell::tr( "Ok" ) );
	hbox->addWidget( ok );

	QPushButton * cancel = new QPushButton;
	cancel->setText( Spell::tr( "Cancel" ) );
	hbox->addWidget( cancel );

	QObject::connect( ok, &QPushButton::clicked, &dlg, &QDialog::accept );
	QObject::connect( cancel, &QPushButton::clicked, &dlg, &QDialog::reject );

	if ( dlg.exec() != QDialog::Accepted ) {
		return false;
	}

	precision = float( precSpin->value() );
	radius = float( spnRadius->value() );
	simplifyMaxError = float( spnSimplifyMaxError->value() );
	replaceShape = chkReplaceShape->isChecked();
	enableCoACD = chkEnableCoACD->isChecked();

	QSettings	settings;
	settings.beginGroup( "Spells/Havok/Create Convex Shapes" );
	settings.setValue( "Precision", QVariant( precision ) );
	settings.setValue( "Radius", QVariant( radius ) );
	settings.setValue( "Simplify Max Error", QVariant( simplifyMaxError ) );
	settings.setValue( "Replace Shape", QVariant( replaceShape ) );
	settings.setValue( "Enable CoACD", QVariant( enableCoACD ) );
	if ( enableCoACD ) {
		coacd.threshold = float( spnThreshold->value() );
		coacd.maxConvexHull = spnMaxConvexHull->value();
		coacd.preprocessMode = chkPreprocessMode->currentIndex();
		coacd.prepResolution = spnPrepResolution->value();
		coacd.sampleResolution = spnSampleResolution->value();
		coacd.mctsNodes = spnMCTSNodes->value();
		coacd.mctsIteration = spnMCTSIteration->value();
		coacd.mctsMaxDepth = spnMCTSMaxDepth->value();
		coacd.pca = chkPCA->isChecked();
		coacd.merge = ( coacd.maxConvexHull > 0 );
		coacd.maxCHVertex = spnMaxCHVertex->value();
		coacd.decimate = ( coacd.maxCHVertex > 0 );
		coacd.extrudeMargin = float( spnExtrudeMargin->value() );
		coacd.extrude = ( coacd.extrudeMargin >= 0.00005f );
		coacd.apxMode = chkApxMode->currentIndex();
		coacd.seed = spnSeed->value();
		coacd.saveSettings( settings );
	}
	settings.endGroup();

	return true;
}

REGISTER_SPELL( spCreateCVS )


//! Transforms Havok constraints
class spConstraintHelper final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "A -> B" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		static QStringList blockNames = {
				  "bhkMalleableConstraint",
				  "bhkBreakableConstraint",
				  "bhkRagdollConstraint",
				  "bhkLimitedHingeConstraint",
				  "bhkHingeConstraint",
				  "bhkPrismaticConstraint"
		};
		return nif && nif->isNiBlock( nif->getBlockIndex( index ), blockNames );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iConstraint = nif->getBlockIndex( index );
		QString name = nif->itemName( iConstraint );

		if ( name == "bhkMalleableConstraint" || name == "bhkBreakableConstraint" ) {
			if ( nif->getIndex( iConstraint, "Ragdoll" ).isValid() ) {
				name = "bhkRagdollConstraint";
			} else if ( nif->getIndex( iConstraint, "Limited Hinge" ).isValid() ) {
				name = "bhkLimitedHingeConstraint";
			} else if ( nif->getIndex( iConstraint, "Hinge" ).isValid() ) {
				name = "bhkHingeConstraint";
			}
		}

		QModelIndex iBodyA = nif->getBlockIndex( nif->getLink( bhkGetEntity( nif, iConstraint, "Entity A" ) ), "bhkRigidBody" );
		QModelIndex iBodyB = nif->getBlockIndex( nif->getLink( bhkGetEntity( nif, iConstraint, "Entity B" ) ), "bhkRigidBody" );

		if ( !iBodyA.isValid() || !iBodyB.isValid() ) {
			Message::warning( nullptr, Spell::tr( "Couldn't find the bodies for this constraint." ) );
			return index;
		}

		Transform transA = bhkBodyTrans( nif, iBodyA );
		Transform transB = bhkBodyTrans( nif, iBodyB );

		QModelIndex iConstraintData;
		if ( name == "bhkLimitedHingeConstraint" ) {
			iConstraintData = nif->getIndex( iConstraint, "Limited Hinge" );
			if ( !iConstraintData.isValid() )
				iConstraintData = iConstraint;
		} else if ( name == "bhkRagdollConstraint" ) {
			iConstraintData = nif->getIndex( iConstraint, "Ragdoll" );
			if ( !iConstraintData.isValid() )
				iConstraintData = iConstraint;
		} else if ( name == "bhkHingeConstraint" ) {
			iConstraintData = nif->getIndex( iConstraint, "Hinge" );
			if ( !iConstraintData.isValid() )
				iConstraintData = iConstraint;
		}

		if ( !iConstraintData.isValid() )
			return index;

		Matrix r1 = transA.rotation;
		Matrix r2 = transB.rotation.inverted();

		Vector3 pivot = Vector3( nif->get<Vector4>( iConstraintData, "Pivot A" ) );
		pivot = transA * pivot;
		pivot = r2 * ( pivot - transB.translation ) / transB.scale;
		nif->set<Vector4>( iConstraintData, "Pivot B", Vector4( pivot ) );

		const char * axisA = nullptr, * axisB = nullptr, * twistA = nullptr, * twistB = nullptr;
		const char * twistA2 = nullptr, * twistB2 = nullptr, * motorA = nullptr, * motorB = nullptr;
		if ( name.endsWith( QLatin1StringView("HingeConstraint") ) ) {
			axisA = "Axis A";
			axisB = "Axis B";
			twistA = "Perp Axis In A1";
			twistB = "Perp Axis In B1";
			twistA2 = "Perp Axis In A2";
			twistB2 = "Perp Axis In B2";
		} else if ( name == "bhkRagdollConstraint" ) {
			axisA = "Plane A";
			axisB = "Plane B";
			twistA = "Twist A";
			twistB = "Twist B";
			motorA = "Motor A";
			motorB = "Motor B";
		}

		if ( !( axisA && axisB && twistA && twistB ) )
			return index;

		auto axis = FloatVector4( nif->get<Vector4>( iConstraintData, axisA ) );
		auto twist = FloatVector4( nif->get<Vector4>( iConstraintData, twistA ) );

		if ( motorA )
			nif->set<Vector4>( iConstraintData, motorA, Vector4( twist.crossProduct3( axis ) ) );

		axis = FloatVector4( r2 * ( r1 * Vector3( axis ) ) );
		nif->set<Vector4>( iConstraintData, axisB, Vector4( axis ) );

		twist = FloatVector4( r2 * ( r1 * Vector3( twist ) ) );
		nif->set<Vector4>( iConstraintData, twistB, Vector4( twist ) );

		if ( motorB )
			nif->set<Vector4>( iConstraintData, motorB, Vector4( twist.crossProduct3( axis ) ) );

		if ( twistA2 && twistB2 ) {
			twist = FloatVector4( r2 * ( r1 * Vector3( nif->get<Vector4>( iConstraintData, twistA2 ) ) ) );
			nif->set<Vector4>( iConstraintData, twistB2, Vector4( twist ) );
		}

		return index;
	}
};

REGISTER_SPELL( spConstraintHelper )

//! Calculates Havok spring lengths
class spStiffSpringHelper final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Calculate Spring Length" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
	{
		return nif && nif->isNiBlock( nif->getBlockIndex( idx ), "bhkStiffSpringConstraint" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & idx ) override final
	{
		QModelIndex iConstraint = nif->getBlockIndex( idx );
		QModelIndex iSpring = nif->getIndex( iConstraint, "Stiff Spring" );
		if ( !iSpring.isValid() )
			iSpring = iConstraint;

		QModelIndex iBodyA = nif->getBlockIndex( nif->getLink( bhkGetEntity( nif, iConstraint, "Entity A" ) ), "bhkRigidBody" );
		QModelIndex iBodyB = nif->getBlockIndex( nif->getLink( bhkGetEntity( nif, iConstraint, "Entity B" ) ), "bhkRigidBody" );

		if ( !iBodyA.isValid() || !iBodyB.isValid() ) {
			Message::warning( nullptr, Spell::tr( "Couldn't find the bodies for this constraint" ) );
			return idx;
		}

		Transform transA = bhkBodyTrans( nif, iBodyA );
		Transform transB = bhkBodyTrans( nif, iBodyB );

		Vector3 pivotA( nif->get<Vector4>( iSpring, "Pivot A" ) );
		Vector3 pivotB( nif->get<Vector4>( iSpring, "Pivot B" ) );

		float length = ( transA * pivotA - transB * pivotB ).length();

		nif->set<float>( iSpring, "Length", length );

		return nif->getIndex( iSpring, "Length" );
	}
};

REGISTER_SPELL( spStiffSpringHelper )

//! Packs Havok strips
class spPackHavokStrips final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Pack Strips" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
	{
		return nif->isNiBlock( idx, "bhkNiTriStripsShape" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & iBlock ) override final
	{
		QPersistentModelIndex iShape( iBlock );

		QVector<Vector3> vertices;
		QVector<Triangle> triangles;
		QVector<Vector3> normals;

		for ( const auto lData : nif->getLinkArray( iShape, "Strips Data" ) ) {
			QModelIndex iData = nif->getBlockIndex( lData, "NiTriStripsData" );

			if ( iData.isValid() ) {
				QVector<Vector3> vrts = nif->getArray<Vector3>( iData, "Vertices" );
				QVector<Triangle> tris;
				QVector<Vector3> nrms;

				QModelIndex iPoints = nif->getIndex( iData, "Points" );

				for ( int x = 0; x < nif->rowCount( iPoints ); x++ ) {
					tris += triangulate( nif->getArray<quint16>( nif->getIndex( iPoints, x ) ) );
				}

				QMutableVectorIterator<Triangle> it( tris );

				while ( it.hasNext() ) {
					Triangle & tri = it.next();

					Vector3 a = vrts.value( tri[0] );
					Vector3 b = vrts.value( tri[1] );
					Vector3 c = vrts.value( tri[2] );

					nrms << Vector3::crossproduct( b - a, c - a ).normalize();

					tri[0] += vertices.count();
					tri[1] += vertices.count();
					tri[2] += vertices.count();
				}

				float scale = ( nif->getBSVersion() < 47 ? 0.142875f : 0.0142875f );
				for ( const Vector3& v : vrts ) {
					vertices += v * scale;
				}
				triangles += tris;
				normals += nrms;
			}
		}

		if ( vertices.isEmpty() || triangles.isEmpty() ) {
			Message::warning( nullptr, Spell::tr( "No mesh data was found." ) );
			return iShape;
		}

		QPersistentModelIndex iPackedShape = nif->insertNiBlock( "bhkPackedNiTriStripsShape", nif->getBlockNumber( iShape ) );

		if ( nif->getVersionNumber() <= 0x14000005 ) {	// until 20.0.0.5
			nif->set<int>( iPackedShape, "Num Sub Shapes", 1 );
			QModelIndex iSubShapes = nif->getIndex( iPackedShape, "Sub Shapes" );
			nif->updateArraySize( iSubShapes );
			QModelIndex iSubShape = nif->getIndex( iSubShapes, 0 );
			nif->set<int>( nif->getIndex( iSubShape, "Havok Filter" ), "Layer", 1 );
			nif->set<int>( iSubShape, "Num Vertices", vertices.count() );
			nif->set<int>( iSubShape, "Material", nif->get<int>( iShape, "Material" ) );
		}
		nif->set<Vector4>( iPackedShape, "Scale", FloatVector4( 1.0f ) );
		nif->set<Vector4>( iPackedShape, "Scale Copy", FloatVector4( 1.0f ) );

		QModelIndex iPackedData = nif->insertNiBlock( "hkPackedNiTriStripsData", nif->getBlockNumber( iPackedShape ) );
		nif->setLink( iPackedShape, "Data", nif->getBlockNumber( iPackedData ) );

		nif->set<int>( iPackedData, "Num Triangles", triangles.count() );
		QModelIndex iTriangles = nif->getIndex( iPackedData, "Triangles" );
		nif->updateArraySize( iTriangles );

		for ( int t = 0; t < triangles.size(); t++ ) {
			nif->set<Triangle>( nif->getIndex( iTriangles, t ), "Triangle", triangles[ t ] );
			if ( nif->getVersionNumber() <= 0x14000005 )
				nif->set<Vector3>( nif->getIndex( iTriangles, t ), "Normal", normals.value( t ) );
		}

		nif->set<int>( iPackedData, "Num Vertices", vertices.count() );
		QModelIndex iVertices = nif->getIndex( iPackedData, "Vertices" );
		nif->updateArraySize( iVertices );
		nif->setArray<Vector3>( iVertices, vertices );

		QMap<qint32, qint32> lnkmap;
		lnkmap.insert( nif->getBlockNumber( iShape ), nif->getBlockNumber( iPackedShape ) );
		nif->mapLinks( lnkmap );

		// *** THIS SOMETIMES CRASHES NIFSKOPE        ***
		// *** UNCOMMENT WHEN BRANCH REMOVER IS FIXED ***
		// See issue #2508255
		spRemoveBranch BranchRemover;
		BranchRemover.castIfApplicable( nif, iShape );

		return iPackedShape;
	}
};

REGISTER_SPELL( spPackHavokStrips )

//! Converts bhkListShape to bhkConvexListShape for FO3
class spConvertListShape final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Convert to bhkConvexListShape" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
	{
		return nif->isNiBlock( idx, "bhkListShape" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & iBlock ) override final
	{
		QPersistentModelIndex iShape( iBlock );
		QPersistentModelIndex iRigidBody = nif->getBlockIndex( nif->getParent( iShape ) );
		if ( !iRigidBody.isValid() )
			return {};

		auto iCLS = nif->insertNiBlock( "bhkConvexListShape" );

		nif->set<uint>( iCLS, "Num Sub Shapes", nif->get<uint>( iShape, "Num Sub Shapes" ) );
		nif->set<uint>( iCLS, "Material", nif->get<uint>( iShape, "Material" ) );
		nif->updateArraySize( iCLS, "Sub Shapes" );

		nif->setLinkArray( iCLS, "Sub Shapes", nif->getLinkArray( iShape, "Sub Shapes" ) );
		nif->setLinkArray( iShape, "Sub Shapes", {} );
		nif->removeNiBlock( nif->getBlockNumber( iShape ) );

		nif->setLink( iRigidBody, "Shape", nif->getBlockNumber( iCLS ) );

		return iCLS;
	}
};

REGISTER_SPELL( spConvertListShape )

//! Converts bhkConvexListShape to bhkListShape for FNV
class spConvertConvexListShape final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Convert to bhkListShape" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final
	{
		return nif->isNiBlock( idx, "bhkConvexListShape" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & iBlock ) override final
	{
		QPersistentModelIndex iShape( iBlock );
		QPersistentModelIndex iRigidBody = nif->getBlockIndex( nif->getParent( iShape ) );
		if ( !iRigidBody.isValid() )
			return {};

		auto iLS = nif->insertNiBlock( "bhkListShape" );

		nif->set<uint>( iLS, "Num Sub Shapes", nif->get<uint>( iShape, "Num Sub Shapes" ) );
		nif->set<uint>( iLS, "Num Filters", nif->get<uint>( iShape, "Num Sub Shapes" ) );
		nif->set<uint>( iLS, "Material", nif->get<uint>( iShape, "Material" ) );
		nif->updateArraySize( iLS, "Sub Shapes" );
		nif->updateArraySize( iLS, "Filters" );

		nif->setLinkArray( iLS, "Sub Shapes", nif->getLinkArray( iShape, "Sub Shapes" ) );
		nif->setLinkArray( iShape, "Sub Shapes", {} );
		nif->removeNiBlock( nif->getBlockNumber( iShape ) );

		nif->setLink( iRigidBody, "Shape", nif->getBlockNumber( iLS ) );

		return iLS;
	}
};

REGISTER_SPELL( spConvertConvexListShape )
