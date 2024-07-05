#include "nifskope.h"
#include "data/niftypes.h"
#include "gl/glscene.h"
#include "gl/glnode.h"
#include "gl/gltools.h"
#include "gl/BSMesh.h"
#include "io/MeshFile.h"
#include "model/nifmodel.h"
#include "message.h"
#include "libfo76utils/src/filebuf.hpp"
#include "libfo76utils/src/material.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <cmath>
#include <cctype>

#include <QApplication>
#include <QBuffer>
#include <QVector>
#include <QFileDialog>
#include <QFileInfo>

#define tr( x ) QApplication::tr( x )

struct GltfStore
{
	// Block ID to list of gltfNodeID
	// BSGeometry may have 1-4 associated gltfNodeID to deal with LOD0-LOD3
	// NiNode will only have 1 gltfNodeID
	QMap<int, QVector<int>> nodes;
	// gltfSkinID to BSMesh
	QMap<int, BSMesh*> skins;
	// Material Paths
	std::map< std::string, int > materials;

	QStringList errors;

	bool flatSkeleton = false;
};


static inline void exportFloats( QByteArray & bin, const float * data, size_t n )
{
#if defined(__i386__) || defined(__x86_64__) || defined(__x86_64)
	const char *	buf = reinterpret_cast< const char * >( data );
	qsizetype	nBytes = qsizetype( n * sizeof(float) );
#else
	char	buf[64];
	qsizetype	nBytes = 0;

	for ( ; n > 0; n--, data++, nBytes = nBytes + 4 )
		FileBuffer::writeUInt32Fast( &(buf[nBytes]), std::bit_cast< std::uint32_t >( *data ) );
#endif

	bin.append( buf, nBytes );
}

void exportCreateInverseBoneMatrices(tinygltf::Model& model, QByteArray& bin, const BSMesh* bsmesh, int gltfSkinID, GltfStore& gltf)
{
	(void) gltf;
	auto bufferViewIndex = model.bufferViews.size();
	auto acc = tinygltf::Accessor();
	acc.bufferView = bufferViewIndex;
	acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
	acc.count = bsmesh->boneTransforms.size();
	acc.type = TINYGLTF_TYPE_MAT4;
	model.accessors.push_back(acc);

	tinygltf::BufferView view;
	view.buffer = 0;
	view.byteOffset = bin.size();
	view.byteLength = acc.count * tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
	model.bufferViews.push_back(view);

	model.skins[gltfSkinID].inverseBindMatrices = bufferViewIndex;

	for ( const auto& b : bsmesh->boneTransforms ) {
		exportFloats( bin, b.toMatrix4().data(), 16 );
	}
}

bool exportCreateNodes(const NifModel* nif, const Scene* scene, tinygltf::Model& model, QByteArray& bin, GltfStore& gltf)
{
	int gltfNodeID = 0;
	int gltfSkinID = -1;

	// NODES

	auto& nodes = scene->nodes.list();
	for ( const auto node : nodes ) {
		if ( !node )
			continue;

		auto nodeId = node->id();
		auto iBlock = nif->getBlockIndex(nodeId);
		if ( nif->blockInherits(iBlock, { "NiNode", "BSGeometry" }) ) {
			auto gltfNode = tinygltf::Node();
			auto mesh = static_cast<BSMesh*>(node);
			bool hasGPULODs = false;
			bool isBSGeometry = nif->blockInherits(iBlock, "BSGeometry") && mesh;
			// Create extra nodes for GPU LODs
			int createdNodes = 1;
			if ( isBSGeometry ) {
				if ( !mesh->materialPath.isEmpty() ) {
					std::string matPath( Game::GameManager::get_full_path( mesh->materialPath, "materials/", ".mat" ) );
					if ( gltf.materials.find( matPath ) == gltf.materials.end() ) {
						int materialID = int( gltf.materials.size() );
						gltf.materials.emplace( matPath, materialID );
					}
				}
				hasGPULODs = mesh->gpuLODs.size() > 0;
				createdNodes = mesh->meshCount();
				if ( hasGPULODs )
					createdNodes = mesh->gpuLODs.size() + 1;
			}

			for ( int j = 0; j < createdNodes; j++ ) {
				// Fill nodes map
				gltf.nodes[nodeId].append(gltfNodeID);

				gltfNode.name = node->getName().toStdString();
				if ( isBSGeometry ) {
					gltfNode.name += ":LOD" + std::to_string(j);
					// Skins
					if ( mesh->skinID > -1 && mesh->weightsUNORM.size() > 0 ) {
						if ( !gltf.skins.values().contains(mesh) ) {
							gltfSkinID++;
						}
						gltfNode.skin = gltfSkinID;
						gltf.skins[gltfSkinID] = mesh;
					}
				}

				// Rotate the root NiNode for glTF Y-Up
				Matrix rot;
				auto& trans = node->localTrans();
				if ( gltfNodeID == 0 ) {
					rot = trans.rotation.toYUp();
				} else {
					rot = trans.rotation;
				}
				auto quat = rot.toQuat();
				gltfNode.translation = { trans.translation[0], trans.translation[1], trans.translation[2] };
				gltfNode.rotation = { quat[1], quat[2], quat[3], quat[0] };
				gltfNode.scale = { trans.scale, trans.scale, trans.scale };

				std::map<std::string, tinygltf::Value> extras;
				extras["ID"] = tinygltf::Value(nodeId);
				extras["Parent ID"] = tinygltf::Value((node->parentNode()) ? node->parentNode()->id() : -1);
				auto links = nif->getChildLinks(nif->getBlockNumber(node->index()));
				for ( const auto link : links ) {
					auto idx = nif->getBlockIndex(link);
					if ( nif->blockInherits(idx, "BSShaderProperty") ) {
						extras["Material Path"] = tinygltf::Value(nif->get<QString>(idx, "Name").toStdString());
					} else if ( nif->blockInherits(idx, "NiIntegerExtraData") ) {
						auto key = QString("%1:%2").arg(nif->itemName(idx)).arg(nif->get<QString>(idx, "Name"));
						extras[key.toStdString()] = tinygltf::Value(nif->get<int>(idx, "Integer Data"));
					}
				}

				auto flags = nif->get<int>(iBlock, "Flags");
				extras["Flags"] = tinygltf::Value(flags);
				if ( isBSGeometry )
					extras["Has GPU LODs"] = tinygltf::Value(hasGPULODs);
				gltfNode.extras = tinygltf::Value(extras);

				model.nodes.push_back(gltfNode);
				gltfNodeID++;
			}

		}
	}

	// Add child nodes after first pass
	for ( int i = 0; i < nif->getBlockCount(); i++ ) {
		auto iBlock = nif->getBlockIndex(i);

		if ( nif->blockInherits(iBlock, "NiNode") ) {
			auto children = nif->getChildLinks(i);
			for ( const auto& child : children ) {
				auto nodes = gltf.nodes.value(child, {});
				for ( const auto& node : nodes ) {
					model.nodes[gltf.nodes[i][0]].children.push_back(node);
				}

			}
		}
	}

	// SKINNING

	bool hasSkeleton = false;
	for ( const auto shape : scene->shapes ) {
		if ( !shape )
			continue;
		auto mesh = static_cast<BSMesh*>(shape);
		if ( mesh->boneNames.size() > 0 ) {
			hasSkeleton = true;
			break;
		}
	}
	if ( hasSkeleton ) {
		for ( const auto mesh : gltf.skins ) {
			if ( mesh && mesh->boneNames.size() > 0 ) {
				for ( const auto& name : mesh->boneNames ) {
					auto it = std::find_if(model.nodes.begin(), model.nodes.end(), [&](const tinygltf::Node& n) {
						return n.name == name.toStdString();
					});

					int gltfNodeID = (it != model.nodes.end()) ? it - model.nodes.begin() : -1;
					if ( gltfNodeID == -1 ) {
						gltf.flatSkeleton = true;
					}
				}
			}
		}
	}

	if ( !hasSkeleton )
		return true;

	for ( [[maybe_unused]] const auto skin : gltf.skins ) {
		auto gltfSkin = tinygltf::Skin();
		model.skins.push_back(gltfSkin);
	}

	if ( gltf.flatSkeleton ) {
		gltf.errors << tr("WARNING: Missing bones detected, exporting as a flat skeleton.");

		int skinID = 0;
		for ( const auto mesh : gltf.skins ) {
			if ( mesh && mesh->boneNames.size() > 0 ) {
				auto gltfNode = tinygltf::Node();
				gltfNode.name = mesh->getName().toStdString();
				model.nodes.push_back(gltfNode);
				int skeletonRoot = gltfNodeID++;
				model.skins[skinID].skeleton = skeletonRoot;
				model.skins[skinID].name = gltfNode.name + "_Armature";
				model.nodes[0].children.push_back(skeletonRoot);

				for ( int i = 0; i < mesh->boneNames.size(); i++ ) {
					auto& name = mesh->boneNames.at(i);
					auto trans = mesh->boneTransforms.at(i).toMatrix4().inverted();

					auto gltfNode = tinygltf::Node();
					gltfNode.name = name.toStdString();
					Vector3 translation;
					Matrix rotation;
					Vector3 scale;
					trans.decompose(translation, rotation, scale);

					auto quat = rotation.toQuat();
					gltfNode.translation = { translation[0], translation[1], translation[2] };
					gltfNode.rotation = { quat[1], quat[2], quat[3], quat[0] };
					gltfNode.scale = { scale[0], scale[1], scale[2] };

					std::map<std::string, tinygltf::Value> extras;
					extras["Flat"] = tinygltf::Value(true);
					gltfNode.extras = tinygltf::Value(extras);

					model.skins[skinID].joints.push_back(gltfNodeID);
					model.nodes[skeletonRoot].children.push_back(gltfNodeID);
					model.nodes.push_back(gltfNode);
					gltfNodeID++;
				}

				exportCreateInverseBoneMatrices(model, bin, mesh, skinID, gltf);
			}

			skinID++;
		}
	} else {
		// Find COM or COM_Twin first if available
		auto it = std::find_if(model.nodes.begin(), model.nodes.end(), [&](const tinygltf::Node& n) {
			return n.name == "COM_Twin" || n.name == "COM";
		});

		int skeletonRoot = (it != model.nodes.end()) ? it - model.nodes.begin() : -1;
		int skinID = 0;
		for ( const auto mesh : gltf.skins ) {
			if ( mesh && mesh->boneNames.size() > 0 ) {
				// TODO: 0 should come from BSSkin::Instance Skeleton Root, mapped to gltfNodeID
				// However, non-zero Skeleton Root never happens, at least in Starfield
				model.skins[skinID].skeleton = (skeletonRoot == -1) ? 0 : skeletonRoot;
				for ( const auto& name : mesh->boneNames ) {
					auto it = std::find_if(model.nodes.begin(), model.nodes.end(), [&](const tinygltf::Node& n) {
						return n.name == name.toStdString();
					});

					int gltfNodeID = (it != model.nodes.end()) ? it - model.nodes.begin() : -1;
					if ( gltfNodeID > -1 ) {
						model.skins[skinID].joints.push_back(gltfNodeID);
					} else {
						gltf.errors << tr("ERROR: Missing Skeleton Node: %1").arg(name);
					}
				}

				exportCreateInverseBoneMatrices(model, bin, mesh, skinID, gltf);
			}
			skinID++;
		}
	}

	return true;
}

void exportCreatePrimitive(tinygltf::Model& model, QByteArray& bin, std::shared_ptr<MeshFile> mesh, tinygltf::Primitive& prim, std::string attr,
							int count, int componentType, int type, quint32& attributeIndex, GltfStore& gltf)
{
	(void) gltf;
	if ( count < 1 )
		return;

	auto acc = tinygltf::Accessor();
	acc.bufferView = attributeIndex;
	acc.componentType = componentType;
	acc.count = count;
	acc.type = type;

	// Min/Max bounds
	// TODO: Utility function in niftypes
	if ( attr == "POSITION" ) {
		Q_ASSERT( !mesh->positions.isEmpty() );

		FloatVector4 max( float(-FLT_MAX) );
		FloatVector4 min( float(FLT_MAX) );

		for ( const auto& v : mesh->positions ) {
			FloatVector4 tmp( FloatVector4::convertVector3( &(v[0]) ) );

			max.maxValues( tmp );
			min.minValues( tmp );
		}

		acc.minValues.push_back(min[0]);
		acc.minValues.push_back(min[1]);
		acc.minValues.push_back(min[2]);

		acc.maxValues.push_back(max[0]);
		acc.maxValues.push_back(max[1]);
		acc.maxValues.push_back(max[2]);
	}

	prim.mode = TINYGLTF_MODE_TRIANGLES;
	prim.attributes[attr] = attributeIndex++;

	model.accessors.push_back(acc);

	auto size = tinygltf::GetComponentSizeInBytes(acc.componentType);

	tinygltf::BufferView view;
	view.buffer = 0;

	auto pad = bin.size() % size;
	for ( int i = 0; i < pad; i++ ) {
		bin.append("\xFF");
	}
	view.byteOffset = bin.size();
	view.byteLength = count * size * tinygltf::GetNumComponentsInType(acc.type);
	view.target = TINYGLTF_TARGET_ARRAY_BUFFER;

	bin.reserve(bin.size() + view.byteLength);
	// TODO: Refactoring BSMesh to std::vector for aligned allocators
	// would bring incompatibility with Shape superclass and take a larger refactor.
	// So, do this for now.
	if ( attr == "POSITION" ) {
		for ( const auto& v : mesh->positions ) {
			exportFloats( bin, &(v[0]), 3 );
		}
	} else if ( attr == "NORMAL" ) {
		for ( const auto& v : mesh->normals ) {
			exportFloats( bin, &(v[0]), 3 );
		}
	} else if ( attr == "TANGENT" ) {
		for ( const auto& v : mesh->tangents ) {
			Vector4	tmp( v );
			tmp[3] = mesh->bitangentsBasis.at( qsizetype(&v - mesh->tangents.data()) );
			exportFloats( bin, &(tmp[0]), 4 );
		}
	} else if ( attr == "TEXCOORD_0" ) {
		for ( const auto& v : mesh->coords ) {
			exportFloats( bin, &(v[0]), 2 );
		}
	} else if ( attr == "TEXCOORD_1" && mesh->haveTexCoord2 ) {
		for ( const auto& v : mesh->coords ) {
			exportFloats( bin, &(v[2]), 2 );
		}
	} else if ( attr == "COLOR_0" ) {
		for ( const auto& v : mesh->colors ) {
			exportFloats( bin, &(v[0]), 4 );
		}
	} else if ( attr == "WEIGHTS_0" ) {
		FloatVector4 tmpWeights( 0.0f );
		for ( const auto& v : mesh->weights ) {
			for ( int i = 0; i < 4; i++ ) {
				tmpWeights.shuffleValues( 0x39 );	// 1, 2, 3, 0
				float weight = v.weightsUNORM[i].weight;
				// Fix Bethesda's non-zero weights
				if ( v.weightsUNORM[i].bone != 0 || weight > 0.00005f )
					tmpWeights[3] = weight;
			}
			exportFloats( bin, &(tmpWeights[0]), 4 );
		}
	} else if ( attr == "WEIGHTS_1" ) {
		FloatVector4 tmpWeights( 0.0f );
		for ( const auto& v : mesh->weights ) {
			for ( int i = 4; i < 8; i++ ) {
				tmpWeights.shuffleValues( 0x39 );	// 1, 2, 3, 0
				float weight = v.weightsUNORM[i].weight;
				// Fix Bethesda's non-zero weights
				if ( v.weightsUNORM[i].bone != 0 || weight > 0.00005f )
					tmpWeights[3] = weight;
			}
			exportFloats( bin, &(tmpWeights[0]), 4 );
		}
	} else if ( attr == "JOINTS_0" ) {
		char tmpBones[8];
		for ( const auto& v : mesh->weights ) {
			for ( int i = 0; i < 4; i++ )
				FileBuffer::writeUInt16Fast( &(tmpBones[i << 1]), v.weightsUNORM[i].bone );
			bin.append( tmpBones, 8 );
		}
	} else if ( attr == "JOINTS_1" ) {
		char tmpBones[8];
		for ( const auto& v : mesh->weights ) {
			for ( int i = 0; i < 4; i++ )
				FileBuffer::writeUInt16Fast( &(tmpBones[i << 1]), v.weightsUNORM[i + 4].bone );
			bin.append( tmpBones, 8 );
		}
	}

	model.bufferViews.push_back(view);
}

bool exportCreatePrimitives(tinygltf::Model& model, QByteArray& bin, const BSMesh* bsmesh, tinygltf::Mesh& gltfMesh,
							quint32& attributeIndex, quint32 lodLevel, int materialID, GltfStore& gltf, qint32 meshLodLevel = -1)
{
	if ( int(lodLevel) >= bsmesh->meshes.size() )
		return false;

	auto& mesh = bsmesh->meshes[lodLevel];
	auto prim = tinygltf::Primitive();

	// TODO: Full Materials, create empty Material for now
	prim.material = materialID;

	exportCreatePrimitive(model, bin, mesh, prim, "POSITION", mesh->positions.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, attributeIndex, gltf);
	exportCreatePrimitive(model, bin, mesh, prim, "NORMAL", mesh->normals.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, attributeIndex, gltf);
	exportCreatePrimitive(model, bin, mesh, prim, "TANGENT", mesh->tangents.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex, gltf);
	if ( mesh->coords.size() > 0 ) {
		exportCreatePrimitive(model, bin, mesh, prim, "TEXCOORD_0", mesh->coords.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC2, attributeIndex, gltf);
	}
	if ( mesh->coords.size() > 0 && mesh->haveTexCoord2 ) {
		exportCreatePrimitive(model, bin, mesh, prim, "TEXCOORD_1", mesh->coords.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC2, attributeIndex, gltf);
	}
	exportCreatePrimitive(model, bin, mesh, prim, "COLOR_0", mesh->colors.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex, gltf);

	if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 0 ) {
		exportCreatePrimitive(model, bin, mesh, prim, "WEIGHTS_0", mesh->weights.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex, gltf);
	}

	if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 4 ) {
		exportCreatePrimitive(model, bin, mesh, prim, "WEIGHTS_1", mesh->weights.size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex, gltf);
	}

	if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 0 ) {
		exportCreatePrimitive(model, bin, mesh, prim, "JOINTS_0", mesh->weights.size(), TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, TINYGLTF_TYPE_VEC4, attributeIndex, gltf);
	}

	if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 4 ) {
		exportCreatePrimitive(model, bin, mesh, prim, "JOINTS_1", mesh->weights.size(), TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, TINYGLTF_TYPE_VEC4, attributeIndex, gltf);
	}

	QVector<Triangle>& tris = mesh->triangles;
	if ( meshLodLevel >= 0 ) {
		tris = bsmesh->gpuLODs[meshLodLevel];
	}

	// Triangle Indices
	auto acc = tinygltf::Accessor();
	acc.bufferView = attributeIndex;
	acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
	acc.count = tris.size() * 3;
	acc.type = TINYGLTF_TYPE_SCALAR;

	prim.indices = attributeIndex++;

	model.accessors.push_back(acc);

	tinygltf::BufferView view;
	view.buffer = 0;
	view.byteOffset = bin.size();
	view.byteLength = acc.count * tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
	view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

	bin.reserve(bin.size() + view.byteLength);
	for ( const auto & v : tris ) {
		char tmpTriangles[6];
		FileBuffer::writeUInt16Fast( &(tmpTriangles[0]), v[0] );
		FileBuffer::writeUInt16Fast( &(tmpTriangles[2]), v[1] );
		FileBuffer::writeUInt16Fast( &(tmpTriangles[4]), v[2] );
		bin.append( tmpTriangles, 6 );
	}

	model.bufferViews.push_back(view);

	gltfMesh.primitives.push_back(prim);

	return true;
}

bool exportCreateMeshes(const NifModel* nif, const Scene* scene, tinygltf::Model& model, QByteArray& bin, GltfStore& gltf)
{
	int meshIndex = 0;
	quint32 attributeIndex = model.bufferViews.size();
	auto& nodes = scene->nodes.list();
	for ( const auto node : nodes ) {
		auto nodeId = node->id();
		auto iBlock = nif->getBlockIndex(nodeId);
		if ( nif->blockInherits(iBlock, "BSGeometry") ) {
			if ( gltf.nodes.value(nodeId, {}).size() == 0 )
				continue;

			auto& n = gltf.nodes[nodeId];
			auto mesh = static_cast<BSMesh*>(node);
			if ( mesh ) {
				int createdMeshes = mesh->meshCount();
				int skeletalLodIndex = -1;
				bool hasGPULODs = mesh->gpuLODs.size() > 0;
				if ( hasGPULODs )
					createdMeshes = mesh->gpuLODs.size() + 1;

				for ( int j = 0; j < createdMeshes; j++ ) {
					auto& gltfNode = model.nodes[n[j]];
					tinygltf::Mesh gltfMesh;
					gltfNode.mesh = meshIndex;
					gltfMesh.name = QString("%1%2%3").arg(node->getName()).arg(":LOD").arg(j).toStdString();
					int materialID = 0;
					if ( !mesh->materialPath.isEmpty() )
						materialID = gltf.materials[Game::GameManager::get_full_path( mesh->materialPath, "materials/", ".mat" )];
					int lodLevel = (hasGPULODs) ? 0 : j;
					if ( exportCreatePrimitives(model, bin, mesh, gltfMesh, attributeIndex, lodLevel, materialID, gltf, skeletalLodIndex) ) {
						meshIndex++;
						model.meshes.push_back(gltfMesh);
						if ( hasGPULODs ) {
							skeletalLodIndex++;
						}
					} else {
						gltf.errors << QString("ERROR: %1 creation failed").arg(QString::fromStdString(gltfMesh.name));
						return false;
					}
				}
			}
		}
	}
	return true;
}


void exportGltf(const NifModel* nif, const Scene* scene, const QModelIndex& index)
{
	(void) index;
	QString filename = QFileDialog::getSaveFileName(qApp->activeWindow(), tr("Choose a .glTF file for export"), nif->getFilename(), "glTF (*.gltf)");
	if ( filename.isEmpty() )
		return;

	QString buffName = filename;
	buffName = QString(buffName.remove(".gltf") + ".bin");

	tinygltf::TinyGLTF writer;
	tinygltf::Model model;
	model.asset.generator = "NifSkope glTF 2.0 Exporter v1.1";

	GltfStore gltf;
	gltf.materials.emplace( std::string(), 0 );
	QByteArray buffer;
	bool success = exportCreateNodes(nif, scene, model, buffer, gltf);
	if ( success )
		success = exportCreateMeshes(nif, scene, model, buffer, gltf);
	if ( success ) {
		auto buff = tinygltf::Buffer();
		buff.name = buffName.toStdString();
		buff.data = std::vector<unsigned char>(buffer.cbegin(), buffer.cend());
		model.buffers.push_back(buff);

		CE2MaterialDB *	matDB = nif->getCE2Materials();
		model.materials.resize( gltf.materials.size() );
		for ( const auto& i : gltf.materials ) {
			const std::string &	name = i.first;
			int	materialID = i.second;
			auto &	mat = model.materials[materialID];

			mat.name = ( !name.empty() ? name : std::string("Default") );
			std::map<std::string, tinygltf::Value> extras;
			extras["Material Path"] = tinygltf::Value( mat.name );
			mat.extras = tinygltf::Value(extras);
			for ( size_t n = mat.name.rfind('/'); n != std::string::npos; ) {
				mat.name.erase( 0, n + 1 );
				break;
			}
			if ( mat.name.ends_with(".mat") )
				mat.name.resize( mat.name.length() - 4 );
			for ( size_t j = 0; j < mat.name.length(); j++ ) {
				char	c = mat.name[j];
				if ( std::islower(c) && ( j == 0 || !std::isalpha(mat.name[j - 1]) ) )
					mat.name[j] = std::toupper( c );
			}

			const CE2Material *	material = nullptr;
			if ( matDB && !name.empty() )
				material = matDB->loadMaterial( name );
			if ( !material )
				continue;

			// TODO: implement exporting material data

		}

		writer.WriteGltfSceneToFile(&model, filename.toStdString(), false, false, true, false);
	}

	if ( gltf.errors.size() == 1 ) {
		Message::warning(nullptr, gltf.errors[0]);
	} else if ( gltf.errors.size() > 1 ) {
		for ( const auto& msg : gltf.errors ) {
			Message::append("Warnings/Errors occurred during glTF Export", msg);
		}
	}
}
