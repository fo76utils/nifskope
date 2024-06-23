#pragma once
#include "glshape.h"
#include "glscene.h"
#include "io/MeshFile.h"

#include <memory>
#include <functional>

#include <QString>

class QByteArray;
class NifModel;

namespace tinygltf {
	class Model;
}

class BSMesh : public Shape
{

public:
	BSMesh(Scene* s, const QModelIndex& iBlock);

	// Node

	void transformShapes() override;

	void drawShapes( NodeList * secondPass = nullptr ) override;
	void drawSelection() const override;

	BoundSphere bounds() const override;

	QString textStats() const override; // TODO (Gavrant): move to Shape

	int meshCount();

	// end Node

	// Shape

	void drawVerts() const override;
	QModelIndex vertexAt(int) const override;

	QVector<std::shared_ptr<MeshFile>> meshes;
	inline const MeshFile * getMeshFile() const
	{
		if ( meshes.size() > 0 ) {
			if ( meshes[0]->lods.size() > 0 )
				return meshes[0].get();
			int	l = std::min( int(scene->lodLevel), int(meshes.size()) - 1 );
			if ( l >= 0 )
				return meshes[l].get();
		}
		return nullptr;
	}

	int materialID = 0;
	QString materialPath;

	int skinID = -1;
	QVector<BoneWeightsUNorm> weightsUNORM;
	QVector<QVector<Triangle>> gpuLODs;
	QVector<QString> boneNames;
	QVector<Transform> boneTransforms;

protected:
	void updateImpl(const NifModel* nif, const QModelIndex& index) override;
	void updateData(const NifModel* nif) override;

	QModelIndex iMeshes;

	BoundSphere dataBound;

	quint32 lodLevel = 0;
};
