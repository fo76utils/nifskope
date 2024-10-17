#ifndef SP_MESH_H
#define SP_MESH_H

#include "spellbook.h"

class MeshFile;

//! \file mesh.h Mesh spell headers

//! Update center and radius of a mesh
class spUpdateCenterRadius final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Update Bounding Sphere" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Update Triangles on Data from Skin
class spUpdateTrianglesFromSkin final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Update Triangles From Skin" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Updates Bounds of BSTriShape or BSGeometry
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

	static void calculateSFBoneBounds(
		NifModel * nif, const QPersistentModelIndex & iBoneList, int numBones, const MeshFile & meshFile );
	static QModelIndex cast_Starfield( NifModel * nif, const QModelIndex & index );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

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
		return nif->isNiBlock( index, "BSGeometry" );
	}

	static void clearMeshlets( NifModel * nif, const QModelIndex & iMeshData );
	static void updateMeshlets( NifModel * nif, const QPersistentModelIndex & iMeshData, const MeshFile & meshFile );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Removes unused vertices
class spRemoveWasteVertices final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Remove Unused Vertices" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	static QModelIndex getShape( const NifModel * nif, const QModelIndex & index );
	static void cast_Starfield( NifModel * nif, const QModelIndex & index, bool noMessages = false );

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !nif )
			return false;
		if ( nif->getBSVersion() >= 170 && nif->isNiBlock( index, "BSGeometry" ) )
			return true;
		if ( nif->blockInherits( index, "BSTriShape" ) )
			return nif->getIndex( index, "Vertex Data" ).isValid();
		return getShape( nif, index ).isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#endif
