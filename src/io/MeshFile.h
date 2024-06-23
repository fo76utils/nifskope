#ifndef MESHFILE_H_INCLUDED
#define MESHFILE_H_INCLUDED

#include "data/niftypes.h"
#include "gl/gltools.h"

#include <QVector>

class NifModel;


class MeshFile
{
public:
	MeshFile( const void * data, size_t size );
	MeshFile( const NifModel * nif, const QString & path );
	// construct from BSMesh structure index, can load .mesh file or internal geometry data
	MeshFile( const NifModel * nif, const QModelIndex & index );

	void clear();

	void update( const void * data, size_t size );
	void update( const NifModel * nif, const QString & path );
	void update( const NifModel * nif, const QModelIndex & index );

	void calculateBitangents( QVector<Vector3> & bitangents ) const;

	//! Vertices
	QVector<Vector3> positions;
	//! Normals
	QVector<Vector3> normals;
	//! Vertex colors
	QVector<Color4> colors;
	//! Tangents
	QVector<Vector3> tangents;
	//! Bitangents basis (bitangents[i] = cross(tangents[i] * bitangentsBasis[i], normals[i]))
	QVector<float> bitangentsBasis;
	//! UV coordinate sets
	bool	haveTexCoord2 = false;
	QVector<Vector4> coords;
	//! Weights
	QVector<BoneWeightsUNorm> weights;
	quint8 weightsPerVertex = 0;
	//! Triangles
	QVector<Triangle> triangles;
	//! Skeletal Mesh LOD
	QVector<QVector<Triangle>> lods;

private:
	bool	haveData = false;
public:
	inline bool isValid() const
	{
		return haveData;
	}
};

#endif
