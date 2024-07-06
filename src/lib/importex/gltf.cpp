#include "nifskope.h"
#include "data/niftypes.h"
#include "gl/glscene.h"
#include "gl/glnode.h"
#include "gl/gltools.h"
#include "gl/BSMesh.h"
#include "io/MeshFile.h"
#include "model/nifmodel.h"
#include "message.h"
#include "qtcompat.h"
#include "libfo76utils/src/filebuf.hpp"
#include "libfo76utils/src/material.hpp"
#include "spells/mesh.h"
#include "spells/tangentspace.h"

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
#include <QMessageBox>

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
					if ( j )
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

				Transform trans;
				if ( !j ) {
					trans = node->localTrans();
					// Rotate the root NiNode for glTF Y-Up
					if ( gltfNodeID == 0 )
						trans.rotation = trans.rotation.toYUp();
				}
				auto quat = trans.rotation.toQuat();
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
				auto & gltfNode = model.nodes[gltf.nodes[i][0]];
				for ( qsizetype j = 0; j < nodes.size(); j++ ) {
					if ( j == 0 )
						gltfNode.children.push_back( nodes[j] );
					else
						model.nodes[nodes[0]].lods.push_back( nodes[j] );
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
					if ( !j ) {
						gltfMesh.name = node->getName().toStdString();
					} else {
						gltfMesh.name = QString("%1%2%3").arg(node->getName()).arg(":LOD").arg(j).toStdString();
					}
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


void exportGltf( const NifModel* nif, const Scene* scene, [[maybe_unused]] const QModelIndex& index )
{
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

static bool nodeHasMeshes( const tinygltf::Model & model, int nodeNum )
{
	if ( nodeNum < 0 || size_t(nodeNum) >= model.nodes.size() )
		return false;
	const tinygltf::Node &	node = model.nodes[nodeNum];
	if ( node.mesh >= 0 && size_t(node.mesh) < model.meshes.size() )
		return true;
	for ( int i : node.children ) {
		if ( nodeHasMeshes( model, i ) )
			return true;
	}
	return false;
}

template< typename T >
static bool importGltfLoadBuffer(
	std::vector< T > & outBuf, const tinygltf::Model & model, int accessor, int typeRequired )
{
	if ( accessor < 0 || size_t(accessor) >= model.accessors.size() )
		return false;
	const tinygltf::Accessor &	a = model.accessors[accessor];
	if ( a.bufferView < 0 || size_t(a.bufferView) >= model.bufferViews.size() )
		return false;
	const tinygltf::BufferView &	v = model.bufferViews[a.bufferView];
	if ( v.buffer < 0 || size_t(v.buffer) >= model.buffers.size() )
		return false;
	const tinygltf::Buffer &	b = model.buffers[v.buffer];

	int	componentSize = tinygltf::GetComponentSizeInBytes( std::uint32_t(a.componentType) );
	int	componentCnt = tinygltf::GetNumComponentsInType( std::uint32_t(a.type) );
	if ( a.type != typeRequired || componentSize < 1 || componentCnt < 1 )
		return false;
	int	blockSize = std::max< int >( componentSize * componentCnt, v.byteStride );

	size_t	offset = a.byteOffset + v.byteOffset;
	if ( std::max( std::max( a.byteOffset, v.byteOffset ), std::max( offset, offset + v.byteLength ) ) > b.data.size() )
		return false;
	size_t	blockCnt = v.byteLength / size_t( blockSize );
	if ( blockCnt < 1 )
		return ( v.byteLength < 1 );

	FileBuffer	inBuf( b.data.data() + offset, v.byteLength );
	outBuf.resize( blockCnt * size_t( componentCnt ) );

	size_t	k = 0;
	int	t = a.componentType;
	for ( size_t i = 0; i < blockCnt; i++ ) {
		inBuf.setPosition( i * size_t( blockSize ) );
		const unsigned char *	readPtr = inBuf.getReadPtr();
		for ( int j = 0; j < componentCnt; j++, k++, readPtr = readPtr + componentSize ) {
			switch ( t ) {
			case TINYGLTF_COMPONENT_TYPE_BYTE:
				outBuf[k] = static_cast< T >( *( reinterpret_cast< const signed char * >(readPtr) ) );
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				outBuf[k] = static_cast< T >( *readPtr );
				break;
			case TINYGLTF_COMPONENT_TYPE_SHORT:
				outBuf[k] = static_cast< T >( std::int16_t( FileBuffer::readUInt16Fast(readPtr) ) );
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				outBuf[k] = static_cast< T >( FileBuffer::readUInt16Fast(readPtr) );
				break;
			case TINYGLTF_COMPONENT_TYPE_INT:
				outBuf[k] = static_cast< T >( std::int32_t( FileBuffer::readUInt32Fast(readPtr) ) );
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				outBuf[k] = static_cast< T >( FileBuffer::readUInt32Fast(readPtr) );
				break;
			case TINYGLTF_COMPONENT_TYPE_FLOAT:
#if defined(__i386__) || defined(__x86_64__) || defined(__x86_64)
				outBuf[k] = static_cast< T >( std::bit_cast< float >( FileBuffer::readUInt32Fast(readPtr) ) );
#else
				outBuf[k] = static_cast< T >( inBuf.readFloat() );
#endif
				break;
			case TINYGLTF_COMPONENT_TYPE_DOUBLE:
				outBuf[k] = static_cast< T >( std::bit_cast< double >( FileBuffer::readUInt64Fast(readPtr) ) );
				break;
			default:
				outBuf[k] = static_cast< T >( 0 );
				break;
			}
		}
	}

	return true;
}

// Returns true if tangent space needs to be calculated

static bool importGltfLoadMesh( NifModel * nif, const QPersistentModelIndex & index,
								const tinygltf::Model & model, const tinygltf::Mesh & mesh, int lod, int skin )
{
	const tinygltf::Primitive *	p = nullptr;
	for ( const auto & i : mesh.primitives ) {
		if ( i.mode != TINYGLTF_MODE_TRIANGLES || i.attributes.empty() )
			continue;
		if ( i.indices >= 0 && size_t(i.indices) < model.accessors.size() ) {
			p = &i;
			break;
		}
	}
	if ( !p )
		return false;

	auto	iMeshes = nif->getIndex( index, "Meshes" );
	if ( !( iMeshes.isValid() && nif->isArray( iMeshes ) && nif->rowCount( iMeshes ) > lod ) )
		return false;
	auto	iMesh = QModelIndex_child( iMeshes, lod );
	if ( !iMesh.isValid() )
		return false;
	nif->set<bool>( iMesh, "Has Mesh", true );
	iMesh = nif->getIndex( iMesh, "Mesh" );
	if ( !iMesh.isValid() )
		return false;
	auto	iMeshData = nif->getIndex( iMesh, "Mesh Data" );
	if ( !iMeshData.isValid() )
		return false;
	nif->set<quint32>( iMesh, "Flags", 64 );
	nif->set<quint32>( iMeshData, "Version", 2 );

	std::vector< std::uint16_t >	indices;
	if ( !importGltfLoadBuffer< std::uint16_t >( indices, model, p->indices, TINYGLTF_TYPE_SCALAR ) )
		return false;
	nif->set<quint32>( iMeshData, "Indices Size", quint32(indices.size()) );
	nif->set<quint32>( iMesh, "Indices Size", quint32(indices.size()) );
	auto	iTriangles = nif->getIndex( iMeshData, "Triangles" );
	if ( iTriangles.isValid() ) {
		nif->updateArraySize( iTriangles );
		QVector< Triangle >	triangles;
		triangles.resize( qsizetype(indices.size() / 3) );
		for ( qsizetype i = 0; i < triangles.size(); i++ ) {
			triangles[i][0] = indices[i * 3];
			triangles[i][1] = indices[i * 3 + 1];
			triangles[i][2] = indices[i * 3 + 2];
		}
		nif->setArray<Triangle>( iTriangles, triangles );
	}

	for ( const auto & i : p->attributes ) {
		if ( i.first == "POSITION" ) {
			std::vector< float >	positions;
			if ( !importGltfLoadBuffer< float >( positions, model, i.second, TINYGLTF_TYPE_VEC3 ) )
				continue;
			std::uint32_t	numVerts = std::uint32_t( positions.size() / 3 );
			if ( !numVerts )
				continue;
			float	maxPos = 0.0f;
			for ( float x : positions )
				maxPos = std::max( maxPos, float( std::fabs(x) ) );
			float	scale = 1.0f / 64.0f;
			while ( maxPos > scale && scale < 16777216.0f )
				scale = scale + scale;
			float	invScale = 1.0f / scale;
			nif->set<float>( iMeshData, "Scale", scale );
			nif->set<quint32>( iMeshData, "Num Verts", numVerts );
			nif->set<quint32>( iMesh, "Num Verts", numVerts );
			auto	iVertices = nif->getIndex( iMeshData, "Vertices" );
			if ( iVertices.isValid() ) {
				nif->updateArraySize( iVertices );
				QVector< ShortVector3 >	vertices;
				vertices.resize( qsizetype(numVerts) );
				for ( qsizetype j = 0; j < qsizetype(numVerts); j++ ) {
					vertices[j][0] = positions[j * 3] * invScale;
					vertices[j][1] = positions[j * 3 + 1] * invScale;
					vertices[j][2] = positions[j * 3 + 2] * invScale;
				}
				nif->setArray<ShortVector3>( iVertices, vertices );
			}
		} else if ( i.first == "NORMAL" ) {
			std::vector< float >	normals;
			if ( !importGltfLoadBuffer< float >( normals, model, i.second, TINYGLTF_TYPE_VEC3 ) )
				continue;
			std::uint32_t	numNormals = std::uint32_t( normals.size() / 3 );
			if ( !numNormals )
				continue;
			nif->set<quint32>( iMeshData, "Num Normals", numNormals );
			auto	iNormals = nif->getIndex( iMeshData, "Normals" );
			if ( iNormals.isValid() ) {
				nif->updateArraySize( iNormals );
				QVector< UDecVector4 >	normalsVec4;
				normalsVec4.resize( qsizetype(numNormals) );
				for ( qsizetype j = 0; j < qsizetype(numNormals); j++ ) {
					auto	normal = FloatVector4::convertVector3( normals.data() + (j * 3) );
					float	r = normal.dotProduct3( normal );
					if ( r > 0.0f ) [[likely]] {
						normal /= float( std::sqrt( r ) );
						normal[3] = -1.0f / 3.0f;
					} else {
						normal = FloatVector4( 0.0f, 0.0f, 1.0f, -1.0f / 3.0f );
					}
					normal.convertToFloats( &(normalsVec4[j][0]) );
				}
				nif->setArray<UDecVector4>( iNormals, normalsVec4 );
			}
		} else if ( i.first == "TEXCOORD_0" || i.first == "TEXCOORD_1" ) {
			std::vector< float >	uvs;
			if ( !importGltfLoadBuffer< float >( uvs, model, i.second, TINYGLTF_TYPE_VEC2 ) )
				continue;
			std::uint32_t	numUVs = std::uint32_t( uvs.size() >> 1 );
			if ( !numUVs )
				continue;
			bool	isUV2 = ( i.first.c_str()[9] != '0' );
			nif->set<quint32>( iMeshData, ( !isUV2 ? "Num UVs" : "Num UVs 2" ), numUVs );
			auto	iUVs = nif->getIndex( iMeshData, ( !isUV2 ? "UVs" : "UVs 2" ) );
			if ( iUVs.isValid() ) {
				nif->updateArraySize( iUVs );
				QVector< HalfVector2 >	uvsVec2;
				uvsVec2.resize( qsizetype(numUVs) );
				for ( qsizetype j = 0; j < qsizetype(numUVs); j++ ) {
					uvsVec2[j][0] = uvs[j * 2];
					uvsVec2[j][1] = uvs[j * 2 + 1];
				}
				nif->setArray<HalfVector2>( iUVs, uvsVec2 );
			}
		}
		// TODO: vertex colors and tangents
	}

	// TODO: import skin
	if ( skin >= 0 && size_t(skin) < model.skins.size() ) {

	}

	if ( nif->get<quint32>( iMeshData, "Num Tangents" ) == 0 ) {
		nif->set<quint32>( iMeshData, "Num Tangents", nif->get<quint32>( iMeshData, "Num Verts" ) );
		auto	iTangents = nif->getIndex( iMeshData, "Tangents" );
		if ( iTangents.isValid() ) {
			nif->updateArraySize( iTangents );
			return true;
		}
	}
	return false;
}

static void importGltfLoadNode( NifModel * nif, const QPersistentModelIndex & index,
								const tinygltf::Model & model, int nodeNum )
{
	if ( nodeNum < 0 || size_t(nodeNum) >= model.nodes.size() || !nodeHasMeshes( model, nodeNum ) )
		return;

	const tinygltf::Node &	node = model.nodes[nodeNum];
	bool	haveMesh = ( node.mesh >= 0 && size_t(node.mesh) < model.meshes.size() );
	QPersistentModelIndex	iBlock = nif->insertNiBlock( !haveMesh ? "NiNode" : "BSGeometry" );
	if ( index.isValid() ) {
		NifItem *	parentItem = nif->getItem( index );
		if ( parentItem ) {
			parentItem->invalidateVersionCondition();
			parentItem->invalidateCondition();
		}
		auto	iNumChildren = nif->getIndex( index, "Num Children" );
		if ( iNumChildren.isValid() ) {
			quint32	n = nif->get<quint32>( iNumChildren );
			nif->set<quint32>( iNumChildren, n + 1 );
			auto	iChildren = nif->getIndex( index, "Children" );
			if ( iChildren.isValid() ) {
				nif->updateArraySize( iChildren );
				nif->setLink( QModelIndex_child( iChildren, int(n) ), quint32( nif->getBlockNumber(iBlock) ) );
			}
		}
	}
	nif->set<quint32>( iBlock, "Flags", ( !haveMesh ? 14U : 526U ) );	// enable internal geometry
	nif->set<QString>( iBlock, "Name", QString::fromStdString( node.name ) );
	{
		Transform	t;
		if ( node.rotation.size() >= 4 && node.scale.size() >= 1 && node.translation.size() >= 3 ) {
			Quat	r;
			for ( size_t i = 0; i < 4; i++ )
				r[(i + 1) & 3] = float( node.rotation[i] );
			t.rotation.fromQuat( r );
			t.translation[0] = float( node.translation[0] );
			t.translation[1] = float( node.translation[1] );
			t.translation[2] = float( node.translation[2] );
			double	s = 0.0;
			for ( double i : node.scale )
				s += i;
			t.scale = float( s / double( int(node.scale.size()) ) );
		} else if ( node.matrix.size() >= 16 ) {
			Matrix4	m;
			for ( size_t i = 0; i < 16; i++ )
				const_cast< float * >( m.data() )[i] = float( node.matrix[i] );
			Vector3	tmpScale;
			m.decompose( t.translation, t.rotation, tmpScale );
			t.scale = ( tmpScale[0] + tmpScale[1] + tmpScale[2] ) / 3.0f;
		}
		t.writeBack( nif, iBlock );
	}

	if ( haveMesh ) {
		QPersistentModelIndex	iShaderProperty = nif->insertNiBlock( "BSLightingShaderProperty" );
		nif->setLink( iBlock, "Shader Property", quint32( nif->getBlockNumber(iShaderProperty) ) );

		QPersistentModelIndex	iMaterialID = nif->insertNiBlock( "NiIntegerExtraData" );
		for ( auto iNumExtraData = nif->getIndex( iBlock, "Num Extra Data List" ); iNumExtraData.isValid(); ) {
			quint32	n = nif->get<quint32>( iNumExtraData );
			nif->set<quint32>( iNumExtraData, n + 1 );
			auto	iExtraData = nif->getIndex( iBlock, "Extra Data List" );
			if ( iExtraData.isValid() ) {
				nif->updateArraySize( iExtraData );
				nif->setLink( QModelIndex_child( iExtraData, int(n) ), quint32( nif->getBlockNumber(iMaterialID) ) );
			}
			break;
		}

		std::string	materialPath;
		if ( node.extras.Has( "Material Path" ) )
			materialPath = node.extras.Get( "Material Path" ).Get< std::string >();
		if ( !materialPath.empty() ) {
			auto	matPathTmp = QString::fromStdString( materialPath );
			materialPath = Game::GameManager::get_full_path( matPathTmp, "materials/", ".mat" );
			nif->set<QString>( iShaderProperty, "Name", matPathTmp );
		}
		std::uint32_t	matPathHash = 0;
		for ( char c : materialPath )
			hashFunctionCRC32( matPathHash, (unsigned char) ( c != '/' ? c : '\\' ) );
		nif->set<quint32>( iMaterialID, "Integer Data", matPathHash );

		bool	tangentsNeeded = importGltfLoadMesh( nif, iBlock, model, model.meshes[node.mesh], 0, node.skin );
		for ( int l = 0; l < 3 && size_t(l) < node.lods.size(); l++ ) {
			int	n = node.lods[l];
			if ( n >= 0 && size_t(n) < model.nodes.size() ) {
				int	m = model.nodes[n].mesh;
				if ( m >= 0 && size_t(m) < model.meshes.size() )
					tangentsNeeded |= importGltfLoadMesh( nif, iBlock, model, model.meshes[m], l + 1, -1 );
			}
		}
		if ( tangentsNeeded )
			spTangentSpace::tangentSpaceSFMesh( nif, iBlock );
		spUpdateBounds::cast_Starfield( nif, iBlock );
	}

	for ( int i : node.children )
		importGltfLoadNode( nif, iBlock, model, i );
}

void importGltf( NifModel* nif, const QModelIndex& index )
{
	if ( nif->getBSVersion() < 170 || ( index.isValid() && !nif->blockInherits( index, "NiNode" ) ) ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("glTF import requires selecting a NiNode in a Starfield NIF") );
		return;
	}

	QString filename = QFileDialog::getOpenFileName( qApp->activeWindow(), tr("Choose a .glTF file for import"), nif->getFilename(), "glTF (*.gltf)" );
	if ( filename.isEmpty() )
		return;

	tinygltf::TinyGLTF	reader;
	tinygltf::Model	model;
	std::string	gltfErr;
	std::string	gltfWarn;
	if ( !reader.LoadASCIIFromFile( &model, &gltfErr, &gltfWarn, filename.toStdString() ) ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error importing glTF file: %1").arg(gltfErr.c_str()) );
		return;
	}
	if ( !gltfWarn.empty() )
		QMessageBox::warning( nullptr, "NifSkope warning", QString("glTF import warning: %1").arg(gltfWarn.c_str()) );

	QPersistentModelIndex	iBlock( index );

	if ( model.scenes.empty() ) {
		importGltfLoadNode( nif, iBlock, model, 0 );
	} else {
		for ( const auto & i : model.scenes ) {
			for ( int j : i.nodes )
				importGltfLoadNode( nif, iBlock, model, j );
		}
	}
	nif->updateHeader();
}
