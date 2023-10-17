#pragma once

#include "data/niftypes.h"
#include "gl/gltools.h"
#include "gamemanager.h"

#include <QByteArray>
#include <QDataStream>
#include <QVector>

#include <string>


class MeshFile
{

public:
	MeshFile(const QString& path);

	static inline bool readBytes(const QString& path, QByteArray& data)
	{
		return Game::GameManager::get_file(data, Game::STARFIELD, path, "geometries", ".mesh");
	}

	bool isValid();

	//! Vertices
	QVector<Vector3> positions;
	//! Normals
	QVector<Vector3> normals;
	//! Vertex colors
	QVector<Color4> colors;
	//! Tangents
	QVector<Vector3> tangents;
	//! Tangents with bitangent basis (1.0, -1.0)
	QVector<Vector4> tangentsBasis;
	//! Bitangents
	QVector<Vector3> bitangents;
	//! UV coordinate sets
	bool	haveTexCoord2 = false;
	QVector<Vector4> coords;
	//! Weights
	QVector<BoneWeightsUNorm> weights;
	quint8 weightsPerVertex;
	//! Triangles
	QVector<Triangle> triangles;
	//! Skeletal Mesh LOD
	QVector<QVector<Triangle>> lods;

	std::string path;

private:
	QByteArray data;
	QDataStream in;
	quint32 readMesh();
};
