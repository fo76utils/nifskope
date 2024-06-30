#include "spellbook.h"
#include "qtcompat.h"

#include "lib/nvtristripwrapper.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QMessageBox>

// Brief description is deliberately not autolinked to class Spell
/*! \file normals.cpp
 * \brief Vertex normal spells
 *
 * All classes here inherit from the Spell class.
 */

static inline void normalizeUDecVector4( UDecVector4 & n )
{
	FloatVector4	xyzw( &(n[0]) );
	float	r = xyzw.dotProduct3( xyzw );
	if ( r > 0.0f ) [[likely]]
		xyzw /= float( std::sqrt( r ) );
	else
		xyzw = FloatVector4( 0.0f, 0.0f, 1.0f, 0.0f );
	xyzw.convertToVector3( &(n[0]) );
}

//! Recalculates and faces the normals of a mesh
class spFaceNormals final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Face Normals" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	static QModelIndex getShapeData( const NifModel * nif, const QModelIndex & index )
	{
		QModelIndex iData = nif->getBlockIndex( index );

		if ( nif->isNiBlock( index, { "NiTriShape", "BSLODTriShape", "NiTriStrips" } ) )
			iData = nif->getBlockIndex( nif->getLink( index, "Data" ) );

		if ( nif->isNiBlock( iData, { "NiTriShapeData", "NiTriStripsData" } ) )
			return iData;

		if ( nif->isNiBlock( index, { "BSTriShape", "BSMeshLODTriShape", "BSSubIndexTriShape", "BSDynamicTriShape" } ) ) {
			auto vf = nif->get<BSVertexDesc>( index, "Vertex Desc" );
			if ( (vf & VertexFlags::VF_SKINNED) && nif->getBSVersion() == 100 ) {
				// Skinned SSE
				auto skinID = nif->getLink( nif->getIndex( index, "Skin" ) );
				auto partID = nif->getLink( nif->getBlockIndex( skinID, "NiSkinInstance" ), "Skin Partition" );
				auto iPartBlock = nif->getBlockIndex( partID, "NiSkinPartition" );
				if ( iPartBlock.isValid() )
					return nif->getIndex( iPartBlock, "Vertex Data" );
			}

			return nif->getIndex( index, "Vertex Data" );
		}

		return QModelIndex();
	}

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return ( ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 );
		return getShapeData( nif, index ).isValid();
	}

	static void faceNormalsSFMesh( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) ) {
			faceNormalsSFMesh( nif, index );
			return index;
		}

		QModelIndex iData = getShapeData( nif, index );

		auto faceNormals = []( const QVector<Vector3> & verts, const QVector<Triangle> & triangles, QVector<Vector3> & norms ) {
			for ( const Triangle & tri : triangles ) {
				Vector3 a = verts[tri[0]];
				Vector3 b = verts[tri[1]];
				Vector3 c = verts[tri[2]];

				Vector3 fn = Vector3::crossproduct( b - a, c - a );
				norms[tri[0]] += fn;
				norms[tri[1]] += fn;
				norms[tri[2]] += fn;
			}

			for ( int n = 0; n < norms.count(); n++ ) {
				norms[n].normalize();
			}
		};

		if ( nif->getBSVersion() < 100 ) {
			QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
			QVector<Triangle> triangles;
			QModelIndex iPoints = nif->getIndex( iData, "Points" );

			if ( iPoints.isValid() ) {
				QVector<QVector<quint16> > strips;

				for ( int r = 0; r < nif->rowCount( iPoints ); r++ )
					strips.append( nif->getArray<quint16>( QModelIndex_child( iPoints, r ) ) );

				triangles = triangulate( strips );
			} else {
				triangles = nif->getArray<Triangle>( iData, "Triangles" );
			}


			QVector<Vector3> norms( verts.count() );

			faceNormals( verts, triangles, norms );

			nif->set<int>( iData, "Has Normals", 1 );
			nif->updateArraySize( iData, "Normals" );
			nif->setArray<Vector3>( iData, "Normals", norms );
		} else {
			QVector<Triangle> triangles;
			int numVerts;
			auto vf = nif->get<BSVertexDesc>( index, "Vertex Desc" );
			if ( !((vf & VertexFlags::VF_SKINNED) && nif->getBSVersion() == 100) ) {
				numVerts = nif->get<int>( index, "Num Vertices" );
				triangles = nif->getArray<Triangle>( index, "Triangles" );
			} else {
				// Skinned SSE
				auto iPart = iData.parent();
				numVerts = nif->get<uint>( iPart, "Data Size" ) / nif->get<uint>( iPart, "Vertex Size" );

				// Get triangles from all partitions
				auto numParts = nif->get<int>( iPart, "Num Partitions" );
				auto iParts = nif->getIndex( iPart, "Partitions" );
				for ( int i = 0; i < numParts; i++ )
					triangles << nif->getArray<Triangle>( QModelIndex_child( iParts, i ), "Triangles" );
			}

			QVector<Vector3> verts;
			verts.reserve( numVerts );
			QVector<Vector3> norms( numVerts );

			if ( nif->isNiBlock(index, "BSDynamicTriShape") ) {
				auto dynVerts = nif->getArray<Vector4>(index, "Vertices");
				verts.clear();
				verts.reserve(numVerts);
				for ( const auto & v : dynVerts )
					verts << Vector3(v);
			} else {
				for ( int i = 0; i < numVerts; i++ ) {
					auto idx = nif->index(i, 0, iData);

					verts += nif->get<Vector3>(idx, "Vertex");
				}
			}

			faceNormals( verts, triangles, norms );

			// Pause updates between model/view
			nif->setState( BaseModel::Processing );
			for ( int i = 0; i < numVerts; i++ ) {
				nif->set<ByteVector3>( nif->index( i, 0, iData ), "Normal", norms[i] );
			}
			nif->resetState();
		}

		return index;
	}
};

void spFaceNormals::faceNormalsSFMesh( NifModel * nif, const QModelIndex & index )
{
	if ( ( nif->get<quint32>(index, "Flags") & 0x0200 ) == 0 )
		return;

	auto	iMeshes = nif->getIndex( index, "Meshes" );
	if ( !iMeshes.isValid() )
		return;
	for ( int i = 0; i <= 3; i++ ) {
		if ( !nif->get<bool>( QModelIndex_child( iMeshes, i ), "Has Mesh" ) )
			continue;
		QModelIndex	iMesh = nif->getIndex( QModelIndex_child( iMeshes, i ), "Mesh" );
		if ( !iMesh.isValid() )
			continue;
		QModelIndex	iMeshData = nif->getIndex( iMesh, "Mesh Data" );
		if ( !iMeshData.isValid() )
			continue;

		QModelIndex	iTriangles = nif->getIndex( iMeshData, "Triangles" );
		QModelIndex	iVertices = nif->getIndex( iMeshData, "Vertices" );
		QModelIndex	iNormals = nif->getIndex( iMeshData, "Normals" );
		int	numVerts;
		if ( !( iTriangles.isValid() && iVertices.isValid() && iNormals.isValid()
				&& ( numVerts = nif->rowCount( iVertices ) ) > 0 && nif->rowCount( iNormals ) == numVerts ) ) {
			QMessageBox::critical( nullptr, "NifSkope error", QString("Error calculating normals for mesh %1").arg(i) );
			continue;
		}

		QVector< Triangle >	triangles( nif->getArray<Triangle>( iTriangles ) );
		QVector< Vector3 >	vertices( nif->getArray<Vector3>( iVertices ) );
		QVector< UDecVector4 >	normals;
		normals.resize( numVerts );
		for ( auto & n : normals )
			FloatVector4( 0.0f ).convertToFloats( &(n[0]) );
		for ( const auto & t : triangles ) {
			if ( qsizetype(t[0]) >= numVerts || qsizetype(t[1]) >= numVerts || qsizetype(t[2]) >= numVerts )
				continue;
			FloatVector4	v0( vertices.at( t[0] ) );
			FloatVector4	v1( vertices.at( t[1] ) );
			FloatVector4	v2( vertices.at( t[2] ) );
			FloatVector4	normal = ( v1 - v0 ).crossProduct3( v2 - v0 );
			float *	n0 = &( normals[t[0]][0] );
			float *	n1 = &( normals[t[1]][0] );
			float *	n2 = &( normals[t[2]][0] );
			( FloatVector4( n0 ) + normal ).convertToFloats( n0 );
			( FloatVector4( n1 ) + normal ).convertToFloats( n1 );
			( FloatVector4( n2 ) + normal ).convertToFloats( n2 );
		}
		for ( auto & n : normals ) {
			normalizeUDecVector4( n );
			n[3] = -1.0f / 3.0f;
		}
		nif->setArray<UDecVector4>( iNormals, normals );
	}
}

REGISTER_SPELL( spFaceNormals )

//! Flip normals of a mesh, without recalculating them.
class spFlipNormals final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Flip Normals" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return ( ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 );
		QModelIndex iData = spFaceNormals::getShapeData( nif, index );
		return ( iData.isValid() && nif->get<bool>( iData, "Has Normals" ) );
	}

	static void flipNormalsSFMesh( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) ) {
			flipNormalsSFMesh( nif, index );
			return index;
		}

		QModelIndex iData = spFaceNormals::getShapeData( nif, index );

		QVector<Vector3> norms = nif->getArray<Vector3>( iData, "Normals" );

		for ( int n = 0; n < norms.count(); n++ )
			norms[n] = -norms[n];

		nif->setArray<Vector3>( iData, "Normals", norms );

		return index;
	}
};

void spFlipNormals::flipNormalsSFMesh( NifModel * nif, const QModelIndex & index )
{
	if ( ( nif->get<quint32>(index, "Flags") & 0x0200 ) == 0 )
		return;

	auto	iMeshes = nif->getIndex( index, "Meshes" );
	if ( !iMeshes.isValid() )
		return;
	for ( int i = 0; i <= 3; i++ ) {
		if ( !nif->get<bool>( QModelIndex_child( iMeshes, i ), "Has Mesh" ) )
			continue;
		QModelIndex	iMesh = nif->getIndex( QModelIndex_child( iMeshes, i ), "Mesh" );
		if ( !iMesh.isValid() )
			continue;
		QModelIndex	iMeshData = nif->getIndex( iMesh, "Mesh Data" );
		if ( !iMeshData.isValid() )
			continue;

		QModelIndex	iNormals = nif->getIndex( iMeshData, "Normals" );
		if ( !( iNormals.isValid() && nif->rowCount( iNormals ) > 0 ) )
			continue;

		QVector< UDecVector4 >	normals = nif->getArray<UDecVector4>( iNormals );
		for ( auto & n : normals )
			( FloatVector4( &(n[0]) ) * -1.0f ).convertToVector3( &(n[0]) );
		nif->setArray<UDecVector4>( iNormals, normals );
	}
}

REGISTER_SPELL( spFlipNormals )

//! Smooths the normals of a mesh
class spSmoothNormals final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Smooth Normals" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return ( ( nif->get<quint32>(index, "Flags") & 0x0200 ) != 0 );
		return spFaceNormals::getShapeData( nif, index ).isValid();
	}

	static void smoothNormalsSFMesh( NifModel * nif, const QModelIndex & index, float maxa, float maxd );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iData;
		bool isSFMesh = ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) );
		if ( !isSFMesh )
			iData = spFaceNormals::getShapeData( nif, index );
		else if ( ( nif->get<quint32>( index, "Flags" ) & 0x0200 ) == 0 )
			return index;

		QVector<Vector3> verts;
		QVector<Vector3> norms;

		int numVerts = 0;

		if ( nif->getBSVersion() < 100 ) {
			verts = nif->getArray<Vector3>( iData, "Vertices" );
			norms = nif->getArray<Vector3>( iData, "Normals" );
		} else if ( !isSFMesh ) {
			auto vf = nif->get<BSVertexDesc>( index, "Vertex Desc" );
			if ( !((vf & VertexFlags::VF_SKINNED) && nif->getBSVersion() == 100) ) {
				numVerts = nif->get<int>( index, "Num Vertices" );
			} else {
				// Skinned SSE
				// "Num Vertices" does not exist in the partition
				auto iPart = iData.parent();
				numVerts = nif->get<uint>( iPart, "Data Size" ) / nif->get<uint>( iPart, "Vertex Size" );
			}

			verts.reserve( numVerts );
			norms.reserve( numVerts );

			for ( int i = 0; i < numVerts; i++ ) {
				auto idx = nif->index( i, 0, iData );

				verts += nif->get<Vector3>( idx, "Vertex" );
				norms += nif->get<ByteVector3>( idx, "Normal" );
			}
		}

		if ( !isSFMesh ) {
			if ( nif->isNiBlock(index, "BSDynamicTriShape") ) {
				auto dynVerts = nif->getArray<Vector4>(index, "Vertices");
				verts.clear();
				verts.reserve( numVerts );
				for ( const auto & v : dynVerts )
					verts << Vector3(v);
			}

			if ( verts.isEmpty() || verts.count() != norms.count() )
				return index;
		}

		QDialog dlg;
		dlg.setWindowTitle( Spell::tr( "Smooth Normals" ) );

		QGridLayout * grid = new QGridLayout;
		dlg.setLayout( grid );

		QDoubleSpinBox * angle = new QDoubleSpinBox;
		angle->setRange( 0, 180 );
		angle->setValue( 60 );
		angle->setSingleStep( 5 );

		grid->addWidget( new QLabel( Spell::tr( "Max Smooth Angle" ) ), 0, 0 );
		grid->addWidget( angle, 0, 1 );

		QDoubleSpinBox * dist = new QDoubleSpinBox;
		dist->setRange( 0, 1 );
		dist->setDecimals( 4 );
		if ( !isSFMesh ) {
			dist->setSingleStep( 0.01 );
			dist->setValue( 0.03 );
		} else {
			dist->setSingleStep( 0.0001 );
			dist->setValue( 0.001 );
		}

		grid->addWidget( new QLabel( Spell::tr( "Max Vertex Distance" ) ), 1, 0 );
		grid->addWidget( dist, 1, 1 );

		QPushButton * btOk = new QPushButton;
		btOk->setText( Spell::tr( "Smooth" ) );
		QObject::connect( btOk, &QPushButton::clicked, &dlg, &QDialog::accept );

		QPushButton * btCancel = new QPushButton;
		btCancel->setText( Spell::tr( "Cancel" ) );
		QObject::connect( btCancel, &QPushButton::clicked, &dlg, &QDialog::reject );

		grid->addWidget( btOk, 2, 0 );
		grid->addWidget( btCancel, 2, 1 );

		if ( dlg.exec() != QDialog::Accepted )
			return index;


		float maxa = deg2rad( angle->value() );
		float maxd = dist->value();
		maxa = float( std::cos( maxa ) );
		maxd = maxd * maxd;

		if ( isSFMesh ) {
			smoothNormalsSFMesh( nif, index, maxa, maxd );
			return index;
		}

		QVector<Vector3> snorms( norms );

		for ( int i = 0; i < verts.count(); i++ ) {
			const Vector3 & a = verts[i];
			Vector3 an = norms[i];

			for ( int j = i + 1; j < verts.count(); j++ ) {
				const Vector3 & b = verts[j];

				if ( ( a - b ).squaredLength() < maxd ) {
					Vector3 bn = norms[j];

					// NOTE: this assumes normalized normals
					if ( Vector3::dotproduct( an, bn ) > maxa ) {
						snorms[i] += bn;
						snorms[j] += an;
					}
				}
			}
		}

		for ( int i = 0; i < verts.count(); i++ )
			snorms[i].normalize();

		if ( nif->getBSVersion() < 100 ) {
			nif->setArray<Vector3>( iData, "Normals", snorms );
		} else {
			// Pause updates between model/view
			nif->setState( BaseModel::Processing );
			for ( int i = 0; i < numVerts; i++ )
				nif->set<ByteVector3>( nif->index( i, 0, iData ), "Normal", snorms[i] );
			nif->resetState();
		}

		return index;
	}
};

void spSmoothNormals::smoothNormalsSFMesh( NifModel * nif, const QModelIndex & index, float maxa, float maxd )
{
	auto	iMeshes = nif->getIndex( index, "Meshes" );
	if ( !iMeshes.isValid() )
		return;
	for ( int i = 0; i <= 3; i++ ) {
		if ( !nif->get<bool>( QModelIndex_child( iMeshes, i ), "Has Mesh" ) )
			continue;
		QModelIndex	iMesh = nif->getIndex( QModelIndex_child( iMeshes, i ), "Mesh" );
		if ( !iMesh.isValid() )
			continue;
		QModelIndex	iMeshData = nif->getIndex( iMesh, "Mesh Data" );
		if ( !iMeshData.isValid() )
			continue;

		QModelIndex	iVertices = nif->getIndex( iMeshData, "Vertices" );
		QModelIndex	iNormals = nif->getIndex( iMeshData, "Normals" );
		int	numVerts;
		if ( !( iVertices.isValid() && iNormals.isValid()
				&& ( numVerts = nif->rowCount( iVertices ) ) > 0 && nif->rowCount( iNormals ) == numVerts ) ) {
			QMessageBox::critical( nullptr, "NifSkope error", QString("Error calculating normals for mesh %1").arg(i) );
			continue;
		}

		QVector< Vector3 >	vertices( nif->getArray<Vector3>( iVertices ) );
		QVector< UDecVector4 >	normals( nif->getArray<UDecVector4>( iNormals ) );
		QVector< UDecVector4 >	snorms( normals );

		const Vector3 *	aPtr = vertices.constData();
		const Vector3 *	endPtr = aPtr + numVerts;
		const UDecVector4 *	anPtr = normals.constData();
		for ( ; ( aPtr + 1 ) < endPtr; aPtr++, anPtr++ ) {
			FloatVector4	a( FloatVector4::convertVector3( &((*aPtr)[0]) ) );
			FloatVector4	an( &((*anPtr)[0]) );

			const Vector3 *	bPtr = aPtr + 1;
			for ( ; bPtr < endPtr; bPtr++ ) {
				FloatVector4	b( FloatVector4::convertVector3( &((*bPtr)[0]) ) );
				b -= a;

				if ( b.dotProduct3( b ) < maxd ) {
					const UDecVector4 *	bnPtr = anPtr + size_t( bPtr - aPtr );
					FloatVector4	bn( &((*bnPtr)[0]) );

					// NOTE: this assumes normalized normals
					if ( an.dotProduct3( bn ) > maxa ) {
						int	i = int( anPtr - normals.constData() );
						int	j = int( bnPtr - normals.constData() );
						( FloatVector4( &(snorms[i][0]) ) + bn ).convertToFloats( &(snorms[i][0]) );
						( FloatVector4( &(snorms[j][0]) ) + an ).convertToFloats( &(snorms[j][0]) );
					}
				}
			}
		}

		for ( auto & n : snorms ) {
			normalizeUDecVector4( n );
			n[3] = -1.0f / 3.0f;
		}
		nif->setArray<UDecVector4>( iNormals, snorms );
	}
}

REGISTER_SPELL( spSmoothNormals )

//! Normalises any single Vector3 or array.
/**
 * Most used on Normals, Bitangents and Tangents.
 */
class spNormalize final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Normalize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		NifValue::Type	t;
		if ( nif->isArray( index ) )
			t = nif->getValue( QModelIndex_child( index ) ).type();
		else
			t = nif->getValue( index ).type();
		return ( t == NifValue::tVector3 || t == NifValue::tUDecVector4 );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif->isArray( index ) ) {
			if ( nif->getValue( QModelIndex_child( index ) ).type() == NifValue::tUDecVector4 ) {
				QVector<UDecVector4> norms = nif->getArray<UDecVector4>( index );

				for ( auto & n : norms )
					normalizeUDecVector4( n );

				nif->setArray<UDecVector4>( index, norms );
			} else {
				QVector<Vector3> norms = nif->getArray<Vector3>( index );

				for ( int n = 0; n < norms.count(); n++ )
					norms[n].normalize();

				nif->setArray<Vector3>( index, norms );
			}
		} else if ( nif->getValue( index ).type() == NifValue::tUDecVector4 ) {
			UDecVector4	n = nif->get<UDecVector4>( index );
			normalizeUDecVector4( n );
			nif->set<UDecVector4>( index, n );
		} else {
			Vector3 n = nif->get<Vector3>( index );
			n.normalize();
			nif->set<Vector3>( index, n );
		}

		return index;
	}
};

REGISTER_SPELL( spNormalize )

