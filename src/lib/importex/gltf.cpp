#include "nifskope.h"
#include "gl/glscene.h"
#include "gl/BSMesh.h"
#include "model/nifmodel.h"
#include "message.h"
#include "ddstxt16.hpp"
#include "libfo76utils/src/material.hpp"
#include "spells/mesh.h"
#include "spells/tangentspace.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE	1
#define TINYGLTF_NO_STB_IMAGE_WRITE	1
#include <tiny_gltf.h>

#include <cctype>

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QSettings>
#include <QMessageBox>

#define tr( x ) QApplication::tr( x )

static bool	gltfEnableLOD = false;

class GltfStore
{
protected:
	tinygltf::Model &	model;
	NifModel *	nif;
	CE2MaterialDB *	materialDB;
	int	mipLevel;
	std::set< std::string >	materialSet;
	std::map< std::string, int >	textureMap;
	DDSTexture16 * loadTexture( const std::string_view & txtPath );
	int addGltfTexture( const std::string & textureMapKey, const QByteArray & imageBuf,
						int width, int height, int channels, unsigned char texCoordModeS, unsigned char texCoordModeT );
	// n = 0: albedo
	// n = 1: normal
	// n = 2: PBR
	// n = 3: occlusion
	// n = 4: emissive
	void getTexture( tinygltf::Material & mat, int n, const std::string & txtPath1, const std::string & txtPath2,
					const CE2Material::UVStream * uvStream );
	// for Skyrim SE, Fallout 4 and Fallout 76
	void getTexture( tinygltf::Material & mat, int n, const Shape * mesh );
	static std::string getTexturePath( const CE2Material::TextureSet * txtSet, int n, std::uint32_t defaultColor = 0 );
public:
	GltfStore( NifModel * nifModel, tinygltf::Model & gltfModel, int textureMipLevel )
		: model( gltfModel ), nif( nifModel ), materialDB( nullptr ), mipLevel( textureMipLevel )
	{
		if ( nif->getBSVersion() >= 170 )
			materialDB = nif->getCE2Materials();
	}
	void exportMaterial( tinygltf::Material & mat, const std::string & matPath, const Shape * mesh = nullptr );

	// Block ID to list of gltfNodeID
	// BSGeometry may have 1-4 associated gltfNodeID to deal with LOD0-LOD3
	// NiNode will only have 1 gltfNodeID
	QMap<int, QVector<int>> nodes;
	// gltfSkinID to Shape
	QMap<int, Shape*> skins;
	// Material Paths
	std::map< std::string, std::pair< int, const Shape * > > materials;

	QStringList errors;

	bool flatSkeleton = false;

	static inline void exportFloats( QByteArray & bin, const float * data, size_t n );
	static inline Vector3 toMeters( const Vector3 & v );
	QStringList getBoneNames( const Shape * shape ) const;
	void createInverseBoneMatrices( QByteArray & bin, const Shape * bsmesh, int gltfSkinID ) const;
	std::string getMaterialName( const QModelIndex & index ) const;
	std::string getMaterialPath( const QModelIndex & index ) const;
	bool createNodes( const Scene * scene, QByteArray & bin );
	void createPrimitive( QByteArray & bin, const MeshFile * mesh, tinygltf::Primitive & prim,
							std::string attr, int count, int componentType, int type, quint32 & attributeIndex );
	static void meshFileFromShape( MeshFile & mesh, const Shape * shape );
	bool createPrimitives( QByteArray & bin, const Shape * bsmesh, tinygltf::Mesh & gltfMesh,
							quint32 & attributeIndex, quint32 lodLevel, int materialID, qint32 meshLodLevel = -1 );
	bool createMeshes( const Scene * scene, QByteArray & bin );
};


inline void GltfStore::exportFloats( QByteArray & bin, const float * data, size_t n )
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

inline Vector3 GltfStore::toMeters( const Vector3 & v )
{
	return Vector3( float( double( v[0] ) * ( 0.9144 / 64.0 ) ), float( double( v[1] ) * ( 0.9144 / 64.0 ) ),
					float( double( v[2] ) * ( 0.9144 / 64.0 ) ) );
}

QStringList GltfStore::getBoneNames( const Shape * shape ) const
{
	QStringList	boneNames;

	if ( shape && !shape->bones.isEmpty() ) {
		for ( int i : shape->bones ) {
			QString	boneName;
			if ( auto iNode = nif->getBlockIndex( i ); iNode.isValid() )
				boneName = nif->get<QString>( iNode, "Name" );
			boneNames.append( boneName );
		}
	}
	if ( shape ) {
		int	blockID = shape->id();
		auto	links = nif->getChildLinks( blockID );

		for ( const auto link : links ) {
			if ( auto idx = nif->getBlockIndex( link ); nif->blockInherits( idx, "SkinAttach" ) ) {
				auto	boneNames2 = nif->getArray<QString>( idx, "Bones" );
				for ( qsizetype i = 0; i < boneNames2.size(); i++ ) {
					const QString &	boneName = boneNames2.at( i );
					if ( boneName.isEmpty() )
						continue;
					while ( boneNames.size() <= i )
						boneNames.append( QString() );
					boneNames[i] = boneName;
				}
			}
		}
	}

	return boneNames;
}

void GltfStore::createInverseBoneMatrices( QByteArray & bin, const Shape * bsmesh, int gltfSkinID ) const
{
	auto bufferViewIndex = model.bufferViews.size();
	auto acc = tinygltf::Accessor();
	acc.bufferView = bufferViewIndex;
	acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
	acc.count = bsmesh->boneData.size();
	acc.type = TINYGLTF_TYPE_MAT4;
	model.accessors.push_back(acc);

	tinygltf::BufferView view;
	view.buffer = 0;
	view.byteOffset = bin.size();
	view.byteLength = acc.count * tinygltf::GetComponentSizeInBytes(acc.componentType)
						* tinygltf::GetNumComponentsInType(acc.type);
	model.bufferViews.push_back(view);

	model.skins[gltfSkinID].inverseBindMatrices = bufferViewIndex;

	bool scalePositions = ( bsmesh->scene && bsmesh->scene->nifModel && bsmesh->scene->nifModel->getBSVersion() < 170 );
	for ( const auto & b : bsmesh->boneData ) {
		Matrix4	m = b.trans.toMatrix4();
		if ( scalePositions ) {		// convert to meters
			Vector3	tmp( m( 3, 0 ), m( 3, 1 ), m( 3, 2 ) );
			tmp = toMeters( tmp );
			m( 3, 0 ) = tmp[0];
			m( 3, 1 ) = tmp[1];
			m( 3, 2 ) = tmp[2];
		}
		exportFloats( bin, m.data(), 16 );
	}
}

std::string GltfStore::getMaterialName( const QModelIndex & index ) const
{
	auto	iSPBlock = nif->getBlockIndex( nif->getLink( index, "Shader Property" ) );
	std::string	matPath;
	if ( nif->getBSVersion() >= 130 && iSPBlock.isValid() )
		matPath = nif->get<QString>( iSPBlock, "Name" ).toStdString();
	if ( matPath.empty() && nif->getBSVersion() < 170 ) {
		int	blockNum = 0;
		if ( iSPBlock.isValid() )
			blockNum = nif->getBlockNumber( iSPBlock );
		else if ( index.isValid() )
			blockNum = nif->getBlockNumber( index );
		printToString( matPath, "Material_%05d", blockNum );
	}
	return matPath;
}

std::string GltfStore::getMaterialPath( const QModelIndex & index ) const
{
	if ( nif->getBSVersion() >= 130 ) {
		if ( auto iSPBlock = nif->getBlockIndex( nif->getLink( index, "Shader Property" ) ); iSPBlock.isValid() ) {
			if ( QString matPath = nif->get<QString>( iSPBlock, "Name" ); !matPath.isEmpty() ) {
				const char *	extStr = ".mat";
				if ( nif->getBSVersion() < 170 )
					extStr = ( nif->isNiBlock( iSPBlock, "BSEffectShaderProperty" ) ? ".bgem" : ".bgsm" );
				return Game::GameManager::get_full_path( matPath, "materials/", extStr );
			}
		}
	}
	return getMaterialName( index );
}

bool GltfStore::createNodes( const Scene * scene, QByteArray & bin )
{
	int gltfNodeID = 0;
	int gltfSkinID = -1;

	// NODES

	auto& sceneNodes = scene->nodes.list();
	for ( const auto node : sceneNodes ) {
		if ( !node )
			continue;

		auto nodeId = node->id();
		auto iBlock = nif->getBlockIndex(nodeId);
		if ( !nif->blockInherits(iBlock, { "NiNode", "BSGeometry", "BSTriShape" }) )
			continue;

		auto gltfNode = tinygltf::Node();
		auto mesh = dynamic_cast<Shape *>(node);
		auto sfMesh = dynamic_cast<BSMesh *>(node);
		bool hasGPULODs = false;

		// Create extra nodes for GPU LODs
		int createdNodes = 1;
		std::string	matPath;
		std::uint32_t	matPathCRC = 0U;
		if ( mesh ) {
			matPath = getMaterialPath( iBlock );
			if ( !matPath.empty() ) {
				for ( char c : matPath )
					hashFunctionCRC32( matPathCRC, (unsigned char) ( c != '/' ? c : '\\' ) );
				if ( materials.find( matPath ) == materials.end() ) {
					int materialID = int( materials.size() );
					materials.emplace( matPath, std::pair< int, const Shape * >( materialID, mesh ) );
				}
			}
			if ( sfMesh ) {
				hasGPULODs = sfMesh->gpuLODs.size() > 0;
				createdNodes = sfMesh->meshCount();
				if ( hasGPULODs )
					createdNodes = sfMesh->gpuLODs.size() + 1;
				if ( !gltfEnableLOD )
					createdNodes = std::min< int >( createdNodes, 1 );
			}
		}

		for ( int j = 0; j < createdNodes; j++ ) {
			// Fill nodes map
			nodes[nodeId].append(gltfNodeID);

			gltfNode.name = node->getName().toStdString();
			if ( mesh ) {
				if ( j )
					gltfNode.name += ":LOD" + std::to_string(j);
				// Skins
				if ( mesh->iSkin.isValid() && !mesh->bones.isEmpty() ) {
					if ( !skins.values().contains(mesh) ) {
						gltfSkinID++;
					}
					gltfNode.skin = gltfSkinID;
					skins[gltfSkinID] = mesh;
				}
			}

			if ( !j ) {
				Transform trans = node->localTrans();
				if ( nif->getBSVersion() < 170 )
					trans.translation = toMeters( trans.translation );		// convert to meters
				// Rotate the root NiNode for glTF Y-Up
				if ( gltfNodeID == 0 ) {
					trans.rotation = trans.rotation.toYUp();
					trans.translation = Vector3( trans.translation[0], trans.translation[2], -(trans.translation[1]) );
				}
				auto quat = trans.rotation.toQuat();
				gltfNode.translation = { trans.translation[0], trans.translation[1], trans.translation[2] };
				gltfNode.rotation = { quat[1], quat[2], quat[3], quat[0] };
				gltfNode.scale = { trans.scale, trans.scale, trans.scale };
			}

			std::map<std::string, tinygltf::Value> extras;
			extras["ID"] = tinygltf::Value(nodeId);
			extras["Parent ID"] = tinygltf::Value((node->parentNode()) ? node->parentNode()->id() : -1);
			if ( mesh && nif->getBSVersion() >= 130 ) {
				extras["Material Path"] = tinygltf::Value( getMaterialName( iBlock ) );
				if ( sfMesh )
					extras["NiIntegerExtraData:MaterialID"] = tinygltf::Value( int(matPathCRC) );
			}

			auto flags = nif->get<int>(iBlock, "Flags");
			extras["Flags"] = tinygltf::Value(flags);
			if ( sfMesh )
				extras["Has GPU LODs"] = tinygltf::Value(hasGPULODs);
			gltfNode.extras = tinygltf::Value(extras);

			model.nodes.push_back(gltfNode);
			gltfNodeID++;
		}
	}

	// Add child nodes after first pass
	for ( int i = 0; i < nif->getBlockCount(); i++ ) {
		auto iBlock = nif->getBlockIndex(i);

		if ( nif->blockInherits(iBlock, "NiNode") ) {
			auto children = nif->getChildLinks(i);
			for ( const auto& child : children ) {
				auto nodeList = nodes.value(child, {});
				auto & gltfNode = model.nodes[nodes[i][0]];
				for ( qsizetype j = 0; j < nodeList.size(); j++ ) {
					if ( j == 0 )
						gltfNode.children.push_back( nodeList[j] );
					else if ( gltfEnableLOD )
						model.nodes[nodeList[0]].lods.push_back( nodeList[j] );
				}
			}
		}
	}

	// SKINNING

	bool hasSkeleton = false;
	for ( const auto shape : scene->shapes ) {
		if ( shape && !shape->bones.isEmpty() ) {
			hasSkeleton = true;
			break;
		}
	}
	if ( hasSkeleton ) {
		for ( const auto mesh : skins ) {
			if ( auto boneNames = getBoneNames( mesh ); !boneNames.isEmpty() ) {
				for ( const auto & name : boneNames ) {
					auto nameStr = name.toStdString();
					auto it = std::find_if(model.nodes.begin(), model.nodes.end(), [&](const tinygltf::Node& n) {
						return n.name == nameStr;
					});

					int gltfNodeID = (it != model.nodes.end()) ? it - model.nodes.begin() : -1;
					if ( gltfNodeID == -1 ) {
						flatSkeleton = true;
					}
				}
			}
		}
	}

	if ( !hasSkeleton )
		return true;

	for ( [[maybe_unused]] const auto skin : skins ) {
		auto gltfSkin = tinygltf::Skin();
		model.skins.push_back(gltfSkin);
	}

	if ( flatSkeleton ) {
		errors << tr("WARNING: Missing bones detected, exporting as a flat skeleton.");

		int skinID = 0;
		for ( const auto mesh : skins ) {
			if ( mesh && mesh->bones.size() > 0 ) {
				auto gltfNode = tinygltf::Node();
				gltfNode.name = mesh->getName().toStdString();
				model.nodes.push_back(gltfNode);
				int skeletonRoot = gltfNodeID++;
				model.skins[skinID].skeleton = skeletonRoot;
				model.skins[skinID].name = gltfNode.name + "_Armature";
				model.nodes[0].children.push_back(skeletonRoot);

				auto	boneNames = getBoneNames( mesh );
				for ( int i = 0; i < mesh->bones.size(); i++ ) {
					Matrix4	trans;
					if ( i < mesh->boneData.size() )
						trans = mesh->boneData.at( i ).trans.toMatrix4().inverted();

					auto gltfNode = tinygltf::Node();
					gltfNode.name = boneNames.value( i ).toStdString();
					Vector3 translation;
					Matrix rotation;
					Vector3 scale;
					trans.decompose( translation, rotation, scale );
					if ( nif->getBSVersion() < 170 )
						translation = toMeters( translation );		// convert to meters

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

				createInverseBoneMatrices( bin, mesh, skinID );
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
		for ( const auto mesh : skins ) {
			if ( mesh && mesh->bones.size() > 0 ) {
				// TODO: 0 should come from BSSkin::Instance Skeleton Root, mapped to gltfNodeID
				// However, non-zero Skeleton Root never happens, at least in Starfield
				model.skins[skinID].skeleton = (skeletonRoot == -1) ? 0 : skeletonRoot;
				auto	boneNames = getBoneNames( mesh );
				for ( const auto & name : boneNames ) {
					auto nameStr = name.toStdString();
					auto it = std::find_if(model.nodes.begin(), model.nodes.end(), [&](const tinygltf::Node& n) {
						return n.name == nameStr;
					});

					int gltfNodeID = (it != model.nodes.end()) ? it - model.nodes.begin() : -1;
					if ( gltfNodeID > -1 ) {
						model.skins[skinID].joints.push_back(gltfNodeID);
					} else {
						errors << tr("ERROR: Missing Skeleton Node: %1").arg( name );
					}
				}

				createInverseBoneMatrices( bin, mesh, skinID );
			}
			skinID++;
		}
	}

	return true;
}

void GltfStore::createPrimitive(
	QByteArray & bin, const MeshFile * mesh, tinygltf::Primitive & prim,
	std::string attr, int count, int componentType, int type, quint32 & attributeIndex )
{
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
			auto	tmp = FloatVector4::convertVector3( &(v[0]) );

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
			tmp[3] = mesh->bitangentsBasis.at( qsizetype(&v - mesh->tangents.data()) ) * -1.0f;
			exportFloats( bin, &(tmp[0]), 4 );
		}
	} else if ( attr == "TEXCOORD_0" ) {
		for ( const auto& v : mesh->coords1 ) {
			exportFloats( bin, &(v[0]), 2 );
		}
	} else if ( attr == "TEXCOORD_1" ) {
		for ( const auto& v : mesh->coords2 ) {
			exportFloats( bin, &(v[0]), 2 );
		}
	} else if ( attr == "COLOR_0" ) {
		for ( const auto& v : mesh->colors ) {
			exportFloats( bin, &(v[0]), 4 );
		}
	} else if ( attr == "WEIGHTS_0" || attr == "WEIGHTS_1" || attr == "JOINTS_0" || attr == "JOINTS_1" ) {
		std::int32_t	tmpWeights[4] = { 0, 0, 0, 0 };
		int	i = 0;
		int	j = int( attr.back() ) & 1;
		unsigned char	k = ( attr[0] == 'W' ? 0 : 16 );
		for ( std::uint32_t v : mesh->weights ) {
			if ( ( (i >> 2) & 1 ) == j ) {
				// Fix Bethesda's non-zero weights
				if ( v > 3U )
					tmpWeights[i & 3] = std::int32_t( ( v >> k ) & 0xFFFFU );
			}
			if ( ++i < mesh->weightsPerVertex )
				continue;
			if ( !k ) {
				FloatVector4	w = FloatVector4::convertInt32( tmpWeights ) / 65535.0f;
				exportFloats( bin, &(w[0]), 4 );
			} else {
				char tmpBones[8];
				for ( i = 0; i < 4; i++ )
					FileBuffer::writeUInt16Fast( &(tmpBones[i << 1]), std::uint16_t(tmpWeights[i]) );
				bin.append( tmpBones, 8 );
			}
			std::memset( tmpWeights, 0, sizeof( tmpWeights ) );
			i = 0;
		}
	}

	model.bufferViews.push_back(view);
}

void GltfStore::meshFileFromShape( MeshFile & mesh, const Shape * shape )
{
	mesh.clear();
	if ( !shape )
		return;

	mesh.positions = shape->verts;
	for ( auto & v : mesh.positions )
		v = toMeters( v );		// convert to meters
	mesh.normals = shape->norms;
	mesh.colors = shape->colors;
	mesh.tangents = shape->bitangents;
	if ( qsizetype n = shape->bitangents.size(); n > 0 ) {
		mesh.bitangentsBasis.resize( n );
		for ( qsizetype i = 0; i < n; i++ ) {
			FloatVector4	normal( 0.0f, 0.0f, 1.0f, 0.0f );
			FloatVector4	bitangent( 0.0f, -1.0f, 0.0f, 0.0f );
			if ( i < shape->norms.size() )
				normal = FloatVector4( shape->norms.at(i) );
			if ( i < shape->tangents.size() )
				bitangent = FloatVector4( shape->tangents.at(i) );
			float	s = normal.crossProduct3( FloatVector4( shape->bitangents.at(i) ) ).dotProduct3( bitangent );
			mesh.bitangentsBasis[i] = ( s >= 0.0f ? 1.0f : -1.0f );
		}
	}
	if ( shape->coords.size() > 0 )
		mesh.coords1 = shape->coords.at( 0 );
	if ( shape->coords.size() > 1 )
		mesh.coords2 = shape->coords.at( 1 );
	if ( shape->boneWeights0.size() > 0 ) {
		mesh.weightsPerVertex = 4;
		mesh.weights.resize( shape->boneWeights0.size() * 4 );
		for ( size_t i = 0; i < shape->boneWeights0.size(); i++ ) {
			FloatVector4	tmp( shape->boneWeights0[i] );
			// TODO: make sure that weights are normalized
			( tmp * 65536.0f ).convertToInt32( reinterpret_cast< std::int32_t * >( mesh.weights.data() + (i * 4) ) );
		}
	}
	mesh.triangles = shape->triangles;
}

bool GltfStore::createPrimitives(
	QByteArray & bin, const Shape * bsmesh, tinygltf::Mesh & gltfMesh,
	quint32 & attributeIndex, quint32 lodLevel, int materialID, qint32 meshLodLevel )
{
	MeshFile	tmpMeshFile( nullptr, 0 );
	const MeshFile *	mesh = &tmpMeshFile;
	const BSMesh *	sfMesh = dynamic_cast< const BSMesh * >( bsmesh );
	if ( sfMesh ) {
		if ( int(lodLevel) >= sfMesh->meshes.size() )
			return false;

		mesh = sfMesh->meshes[lodLevel].get();
	} else {
		meshFileFromShape( tmpMeshFile, bsmesh );
	}

	auto prim = tinygltf::Primitive();
	prim.material = materialID;

	if ( meshLodLevel >= 0 && !model.meshes.empty() && !model.meshes.back().primitives.empty() ) {
		prim = model.meshes.back().primitives.front();
	} else {
		createPrimitive( bin, mesh, prim, "POSITION", mesh->positions.size(),
							TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, attributeIndex );
		createPrimitive( bin, mesh, prim, "NORMAL", mesh->normals.size(),
							TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, attributeIndex );
		createPrimitive( bin, mesh, prim, "TANGENT", mesh->tangents.size(),
							TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex );
		if ( mesh->coords1.size() > 0 ) {
			createPrimitive( bin, mesh, prim, "TEXCOORD_0", mesh->coords1.size(),
								TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC2, attributeIndex );
		}
		if ( mesh->coords2.size() > 0 ) {
			createPrimitive( bin, mesh, prim, "TEXCOORD_1", mesh->coords2.size(),
								TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC2, attributeIndex );
		}
		if ( bsmesh->hasVertexColors && mesh->colors.size() > 0 ) {
			createPrimitive( bin, mesh, prim, "COLOR_0", mesh->colors.size(),
								TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex );
		}

		if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 0 ) {
			createPrimitive( bin, mesh, prim, "WEIGHTS_0", mesh->weights.size() / mesh->weightsPerVertex,
								TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex );
		}

		if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 4 ) {
			createPrimitive( bin, mesh, prim, "WEIGHTS_1", mesh->weights.size() / mesh->weightsPerVertex,
								TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, attributeIndex );
		}

		if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 0 ) {
			createPrimitive( bin, mesh, prim, "JOINTS_0", mesh->weights.size() / mesh->weightsPerVertex,
								TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, TINYGLTF_TYPE_VEC4, attributeIndex );
		}

		if ( mesh->weights.size() > 0 && mesh->weightsPerVertex > 4 ) {
			createPrimitive( bin, mesh, prim, "JOINTS_1", mesh->weights.size() / mesh->weightsPerVertex,
								TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, TINYGLTF_TYPE_VEC4, attributeIndex );
		}
	}

	const QVector<Triangle> * tris = &( mesh->triangles );
	if ( meshLodLevel >= 0 && sfMesh ) {
		tris = &( sfMesh->gpuLODs.at(meshLodLevel) );
	}

	// Triangle Indices
	auto acc = tinygltf::Accessor();
	acc.bufferView = attributeIndex;
	acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
	acc.count = tris->size() * 3;
	acc.type = TINYGLTF_TYPE_SCALAR;

	prim.indices = attributeIndex++;

	model.accessors.push_back(acc);

	tinygltf::BufferView view;
	view.buffer = 0;
	view.byteOffset = bin.size();
	view.byteLength = acc.count * tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
	view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

	bin.reserve(bin.size() + view.byteLength);
	for ( const auto & v : *tris ) {
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

bool GltfStore::createMeshes( const Scene * scene, QByteArray & bin )
{
	int meshIndex = 0;
	quint32 attributeIndex = model.bufferViews.size();
	auto& sceneNodes = scene->nodes.list();
	for ( const auto node : sceneNodes ) {
		auto nodeId = node->id();
		auto iBlock = nif->getBlockIndex(nodeId);
		auto mesh = dynamic_cast<Shape *>(node);
		if ( mesh ) {
			if ( nodes.value(nodeId, {}).size() == 0 )
				continue;

			auto& n = nodes[nodeId];
			int createdMeshes = 1;
			bool hasGPULODs = false;
			auto sfMesh = dynamic_cast<BSMesh *>(node);
			if ( sfMesh ) {
				createdMeshes = sfMesh->meshCount();
				hasGPULODs = sfMesh->gpuLODs.size() > 0;
				if ( hasGPULODs )
					createdMeshes = sfMesh->gpuLODs.size() + 1;
				if ( !gltfEnableLOD )
					createdMeshes = std::min< int >( createdMeshes, 1 );
			}

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
				std::string	materialPath = getMaterialPath( iBlock );
				if ( !materialPath.empty() )
					materialID = materials[materialPath].first;
				int	lodLevel = (hasGPULODs) ? 0 : j;
				int	skeletalLodIndex = ( hasGPULODs ? j : 0 ) - 1;
				if ( createPrimitives( bin, mesh, gltfMesh, attributeIndex, lodLevel, materialID, skeletalLodIndex ) ) {
					meshIndex++;
					model.meshes.push_back(gltfMesh);
				} else {
					errors << QString("ERROR: %1 creation failed").arg(QString::fromStdString(gltfMesh.name));
					return false;
				}
			}
		}
	}
	return true;
}


// material export

DDSTexture16 * GltfStore::loadTexture( const std::string_view & txtPath )
{
	if ( txtPath.length() == 9 && txtPath[0] == '#' ) {
		std::uint32_t	c = 0;
		for ( size_t i = 1; i < 9; i++ ) {
			std::uint32_t	b = std::uint32_t( txtPath[i] );
			if ( b & 0x40 )
				b = b + 9;
			c = ( c << 4 ) | ( b & 0x0F );
		}
		return new DDSTexture16( FloatVector4( c ) / 255.0f );
	}

	DDSTexture16 *	t = nullptr;
	try {
		QByteArray	buf;
		if ( mipLevel < 0 || !nif->getResourceFile( buf, txtPath ) )
			return nullptr;
		t = new DDSTexture16(
				reinterpret_cast< const unsigned char * >( buf.constData() ), size_t( buf.size() ), mipLevel, true );
	} catch ( NifSkopeError & ) {
		delete t;
		t = nullptr;
	}
	return t;
}

int GltfStore::addGltfTexture(
	const std::string & textureMapKey, const QByteArray & imageBuf,
	int width, int height, int channels, unsigned char texCoordModeS, unsigned char texCoordModeT )
{
	if ( auto i = textureMap.find( textureMapKey ); i != textureMap.end() )
		return i->second;

	int	textureID = -1;

	if ( !imageBuf.isEmpty() ) {
		int	bufView = -1;
		if ( !imageBuf.isEmpty() && !model.buffers.empty() ) {
			bufView = int( model.bufferViews.size() );
			tinygltf::BufferView &	v = model.bufferViews.emplace_back();
			v.buffer = int( model.buffers.size() - 1 );
			std::vector< unsigned char > &	buf = model.buffers.back().data;
			v.byteOffset = buf.size();
			v.byteLength = size_t( imageBuf.size() );
			buf.resize( v.byteOffset + v.byteLength );
			std::memcpy( buf.data() + v.byteOffset, imageBuf.data(), v.byteLength );
		}

		if ( bufView >= 0 ) {
			tinygltf::Image &	img = model.images.emplace_back();
			img.width = width;
			img.height = height;
			img.component = channels;
			img.bits = 8;
			img.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
			img.bufferView = bufView;
			img.mimeType = "image/png";
			textureID = int( model.textures.size() );
			model.textures.emplace_back().source = int( model.images.size() - 1 );
			if ( texCoordModeS || texCoordModeT ) {
				static const int wrapModeTable[4] = {
					TINYGLTF_TEXTURE_WRAP_REPEAT, TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE,
					TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT, TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE
				};
				int	wrapModeS = wrapModeTable[texCoordModeS & 3];
				int	wrapModeT = wrapModeTable[texCoordModeT & 3];
				for ( const auto & j : model.samplers ) {
					if ( j.wrapS == wrapModeS && j.wrapT == wrapModeT ) {
						model.textures.back().sampler = int( &j - model.samplers.data() );
						break;
					}
				}
				if ( model.textures.back().sampler < 0 ) {
					model.samplers.emplace_back().wrapS = wrapModeS;
					model.samplers.back().wrapT = wrapModeT;
					model.textures.back().sampler = int( model.samplers.size() - 1 );
				}
			}
		}
	}

	return textureMap.emplace( textureMapKey, textureID ).first->second;
}

void GltfStore::getTexture( tinygltf::Material & mat, int n, const std::string & txtPath1, const std::string & txtPath2,
							const CE2Material::UVStream * uvStream )
{
	if ( txtPath1.empty() && txtPath2.empty() )
		return;

	std::string	textureMapKey;
	printToString( textureMapKey, "%s\n%s\n%d", txtPath1.c_str(), txtPath2.c_str(), n );

	unsigned char	texCoordMode = 0;	// "Wrap"
	unsigned char	texCoordChannel = 0;
	if ( uvStream ) {
		texCoordMode = uvStream->textureAddressMode & 3;
		texCoordChannel = (unsigned char) ( uvStream->channel > 1 );
	}

	int	i;
	if ( auto j = textureMap.find( textureMapKey ); j != textureMap.end() ) {
		i = j->second;
	} else {
		// load texture(s) and convert to glTF compatible PNG format
		QByteArray	imageBuf;
		int	width = 1;
		int	height = 1;
		int	channels = 0;
		DDSTexture16 *	t1 = nullptr;
		DDSTexture16 *	t2 = nullptr;
		try {
			if ( !txtPath1.empty() && ( t1 = loadTexture( txtPath1 ) ) != nullptr ) {
				width = t1->getWidth();
				height = t1->getHeight();
			}
			if ( !txtPath2.empty() && ( t2 = loadTexture( txtPath2 ) ) != nullptr ) {
				width = std::max< int >( width, t2->getWidth() );
				height = std::max< int >( height, t2->getHeight() );
			}
			if ( t1 || t2 )
				channels = ( n == 0 ? ( !t2 ? 3 : 4 ) : ( n == 3 ? 1 : 3 ) );
			QImage::Format	fmt =
				( channels <= 1 ? QImage::Format_Grayscale8
									: ( channels == 3 ? QImage::Format_RGB888 : QImage::Format_RGBA8888 ) );
			QImage	img( width, height, fmt );
			size_t	lineBytes = size_t( img.bytesPerLine() );
			float	xScale = 1.0f / float( width );
			float	xOffset = xScale * 0.5f;
			float	yScale = 1.0f / float( height );
			float	yOffset = yScale * 0.5f;
			bool	f1 = ( t1 && ( t1->getWidth() != width || t1->getHeight() != height ) );
			bool	f2 = ( t2 && ( t2->getWidth() != width || t2->getHeight() != height ) );
			for ( int y = 0; channels > 0 && y < height; y++ ) {
				unsigned char *	imgPtr = reinterpret_cast< unsigned char * >( img.bits() ) + ( size_t(y) * lineBytes );
				for ( int x = 0; x < width; x++, imgPtr = imgPtr + channels ) {
					FloatVector4	a( 0.0f, 0.0f, 0.0f, 1.0f );
					FloatVector4	b( 0.0f, 0.0f, 0.0f, 1.0f );
					float	xf = float( x ) * xScale + xOffset;
					float	yf = float( y ) * yScale + yOffset;
					if ( t1 )
						a = ( !f1 ? FloatVector4::convertFloat16( t1->getPixelN(x, y, 0) ) : t1->getPixelB(xf, yf, 0) );
					if ( t2 )
						b = ( !f2 ? FloatVector4::convertFloat16( t2->getPixelN(x, y, 0) ) : t2->getPixelB(xf, yf, 0) );
					switch ( n ) {
					case 0:
						// albedo: add alpha channel from opacity texture
						a[3] = b[0];
						break;
					case 1:
						// normal map: calculate Z (blue) channel and convert to unsigned format
						a[2] = float( std::sqrt( std::max( 1.0f - a.dotProduct2(a), 0.0f ) ) );
						a = a * FloatVector4( 0.5f, -0.5f, 0.5f, 0.5f ) + 0.5f;	// invert green channel
						break;
					case 2:
						// PBR map: G = roughness, B = metalness
						a = FloatVector4( 0.0f, a[0], b[0], 0.0f );
						break;
					}
					std::uint32_t	c = std::uint32_t( a * 255.0f );
					if ( channels == 3 ) {
						FileBuffer::writeUInt16Fast( imgPtr, std::uint16_t( c ) );
						imgPtr[2] = std::uint8_t( c >> 16 );
					} else if ( channels == 4 ) {
						FileBuffer::writeUInt32Fast( imgPtr, c );
					} else {
						*imgPtr = std::uint8_t( c );
					}
				}
			}
			delete t1;
			t1 = nullptr;
			delete t2;
			t2 = nullptr;

			if ( channels ) {
				QBuffer	tmpBuf( &imageBuf );
				tmpBuf.open( QIODevice::WriteOnly );
				img.save( &tmpBuf, "PNG", 89 );
			}
		} catch ( ... ) {
			delete t1;
			delete t2;
			throw;
		}

		// if a valid image has been created, add it as a glTF buffer view
		i = addGltfTexture( textureMapKey, imageBuf, width, height, channels, texCoordMode, texCoordMode );
	}

	switch ( n ) {
	case 0:
		mat.pbrMetallicRoughness.baseColorTexture.index = i;
		mat.pbrMetallicRoughness.baseColorTexture.texCoord = texCoordChannel;
		break;
	case 1:
		mat.normalTexture.index = i;
		mat.normalTexture.texCoord = texCoordChannel;
		break;
	case 2:
		mat.pbrMetallicRoughness.metallicRoughnessTexture.index = i;
		mat.pbrMetallicRoughness.metallicRoughnessTexture.texCoord = texCoordChannel;
		break;
	case 3:
		mat.occlusionTexture.index = i;
		mat.occlusionTexture.texCoord = texCoordChannel;
		break;
	case 4:
		mat.emissiveTexture.index = i;
		mat.emissiveTexture.texCoord = texCoordChannel;
		break;
	}
}

void GltfStore::getTexture( tinygltf::Material & mat, int n, const Shape * mesh )
{
	if ( !( mesh && mesh->bssp && ( mesh->bslsp || mesh->bsesp ) ) )
		return;

	std::string	texturePaths[3];
	int	textureSlots[3] = { -1, -1, -1 };
	auto	bsVersion = nif->getBSVersion();
	switch ( n ) {
	case 0:						// albedo (TODO: greyscale to palette mapping)
		textureSlots[0] = 0;
		if ( bsVersion >= 151 )
			textureSlots[1] = ( mesh->bsesp ? 6 : 8 );
		break;
	case 1:						// normal
		textureSlots[0] = ( mesh->bsesp ? 3 : 1 );
		break;
	case 2:						// PBR
		if ( bsVersion >= 151 ) {
			textureSlots[0] = 0;
			textureSlots[1] = ( mesh->bsesp ? 6 : 8 );
			textureSlots[2] = ( mesh->bsesp ? 7 : 9 );
		} else if ( bsVersion >= 130 && !mesh->bsesp ) {
			textureSlots[0] = 7;
			textureSlots[1] = 5;
		}
		break;
	case 3:						// occlusion
		if ( bsVersion >= 151 )
			textureSlots[0] = ( mesh->bsesp ? 7 : 9 );
		break;
	case 4:						// emissive
		if ( !mesh->bsesp )
			textureSlots[0] = 2;
		if ( bsVersion >= 151 )
			textureSlots[1] = ( mesh->bsesp ? 7 : 9 );
		break;
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( textureSlots[i] >= 0 ) {
			if ( QString fileName = mesh->bssp->fileName( textureSlots[i] ); !fileName.isEmpty() )
				texturePaths[i] = Game::GameManager::get_full_path( fileName, "textures/", ".dds" );
		}
	}

	std::string	textureMapKey;
	printToString( textureMapKey, "%s\n%s\n%s\n%d",
					texturePaths[0].c_str(), texturePaths[1].c_str(), texturePaths[2].c_str(), n );

	unsigned char	texCoordModeS = 0;	// "Wrap"
	unsigned char	texCoordModeT = 0;
	switch ( mesh->bssp->clampMode ) {
	case TexClampMode::CLAMP_S_WRAP_T:
		texCoordModeS = 1;
		break;
	case TexClampMode::WRAP_S_CLAMP_T:
		texCoordModeT = 1;
		break;
	case TexClampMode::CLAMP_S_CLAMP_T:
		texCoordModeS = 1;
		texCoordModeT = 1;
		break;
	default:
		break;
	}

	int	i;
	if ( auto j = textureMap.find( textureMapKey ); j != textureMap.end() ) {
		i = j->second;
	} else {
		// load texture(s) and convert to glTF compatible PNG format
		QByteArray	imageBuf;
		int	width = 1;
		int	height = 1;
		int	channels = 0;
		DDSTexture16 *	t[3] = { nullptr, nullptr, nullptr };
		try {
			for ( int j = 0; j < 3; j++ ) {
				if ( !texturePaths[j].empty() && ( t[j] = loadTexture( texturePaths[j] ) ) != nullptr ) {
					width = std::max< int >( width, t[j]->getWidth() );
					height = std::max< int >( height, t[j]->getHeight() );
				}
			}
			if ( t[0] || t[1] || t[2] )
				channels = ( n == 0 ? 4 : ( n == 3 ? 1 : 3 ) );
			if ( channels == 4 && !( mesh->bsesp || ( mesh->alphaProperty
														&& ( mesh->alphaProperty->hasAlphaBlend()
															|| mesh->alphaProperty->hasAlphaTest() ) ) ) ) {
				channels = 3;
			}
			QImage::Format	fmt =
				( channels <= 1 ? QImage::Format_Grayscale8
									: ( channels == 3 ? QImage::Format_RGB888 : QImage::Format_RGBA8888 ) );
			QImage	img( width, height, fmt );
			size_t	lineBytes = size_t( img.bytesPerLine() );
			float	xScale = 1.0f / float( width );
			float	xOffset = xScale * 0.5f;
			float	yScale = 1.0f / float( height );
			float	yOffset = yScale * 0.5f;
			bool	f[3];
			for ( int j = 0; j < 3; j++ )
				f[j] = ( t[j] && ( t[j]->getWidth() != width || t[j]->getHeight() != height ) );
			for ( int y = 0; channels > 0 && y < height; y++ ) {
				unsigned char *	imgPtr = reinterpret_cast< unsigned char * >( img.bits() ) + ( size_t(y) * lineBytes );
				for ( int x = 0; x < width; x++, imgPtr = imgPtr + channels ) {
					FloatVector4	c[3];
					float	xf = float( x ) * xScale + xOffset;
					float	yf = float( y ) * yScale + yOffset;
					for ( int j = 0; j < 3; j++ ) {
						if ( !t[j] )
							c[j] = FloatVector4( 0.0f, 0.0f, 0.0f, 1.0f );
						else if ( !f[j] )
							c[j] = FloatVector4::convertFloat16( t[j]->getPixelN( x, y, 0 ) );
						else
							c[j] = t[j]->getPixelB( xf, yf, 0 );
					}
					switch ( n ) {
					case 0:
						// albedo: combine with Fallout 76 reflectance texture
						if ( t[1] ) {
							FloatVector4	diffuse = DDSTexture16::srgbExpand( c[0] );
							FloatVector4	specular = ( DDSTexture16::srgbExpand( c[1] ) - 0.04f ) * ( 1.0f / 0.96f );
							specular.maxValues( FloatVector4( 0.0f ) );
							FloatVector4	albedo = ( diffuse + specular ).minValues( FloatVector4( 1.0f ) );
							c[0].blendValues( DDSTexture16::srgbCompress( albedo ), 0x07 );
						}
						break;
					case 1:
						// normal map: calculate Z (blue) channel and convert to unsigned format
						if ( t[0] && bsVersion < 151 )
							c[0] = c[0] * 2.0f - 1.0f;
						c[0][2] = float( std::sqrt( std::max( 1.0f - c[0].dotProduct2(c[0]), 0.0f ) ) );
						c[0] = c[0] * FloatVector4( 0.5f, -0.5f, 0.5f, 0.5f ) + 0.5f;	// invert green channel
						break;
					case 2:
						// PBR map: G = roughness, B = metalness
						if ( bsVersion >= 151 ) {
							FloatVector4	diffuse = DDSTexture16::srgbExpand( c[0] );
							FloatVector4	specular = ( DDSTexture16::srgbExpand( c[1] ) - 0.04f ) * ( 1.0f / 0.96f );
							specular.maxValues( FloatVector4( 0.0f ) );
							FloatVector4	albedo = ( diffuse + specular ).minValues( FloatVector4( 1.0f ) );
							c[0][0] = 0.0f;
							c[0][1] = 1.0f - c[2][0];
							c[0][2] = specular.dotProduct3( 1.0f ) / std::max( albedo.dotProduct3( 1.0f ), 0.001f );
							c[0][3] = 1.0f;
						} else if ( bsVersion >= 130 && t[0] ) {
							c[0][0] = 0.0f;
							c[0][1] = 1.0f - c[0][1];
							c[0][2] = c[1][0];
							c[0][3] = 1.0f;
						}
						break;
					case 3:
						// occlusion
						if ( !t[0] )
							c[0] = FloatVector4( 1.0f );
						else
							c[0].shuffleValues( 0xD5 );
						break;
					case 4:
						// emissive
						if ( t[1] )
							c[0] = DDSTexture16::srgbCompress( DDSTexture16::srgbExpand( c[0] ) * c[1][3] );
						break;
					}
					std::uint32_t	b = std::uint32_t( c[0] * 255.0f );
					if ( channels == 3 ) {
						FileBuffer::writeUInt16Fast( imgPtr, std::uint16_t( b ) );
						imgPtr[2] = std::uint8_t( b >> 16 );
					} else if ( channels == 4 ) {
						FileBuffer::writeUInt32Fast( imgPtr, b );
					} else {
						*imgPtr = std::uint8_t( b );
					}
				}
			}
			for ( int j = 0; j < 3; j++ ) {
				delete t[j];
				t[j] = nullptr;
			}

			if ( channels ) {
				QBuffer	tmpBuf( &imageBuf );
				tmpBuf.open( QIODevice::WriteOnly );
				img.save( &tmpBuf, "PNG", 89 );
			}
		} catch ( ... ) {
			for ( int j = 0; j < 3; j++ )
				delete t[j];
			throw;
		}

		// if a valid image has been created, add it as a glTF buffer view
		i = addGltfTexture( textureMapKey, imageBuf, width, height, channels, texCoordModeS, texCoordModeT );
	}

	switch ( n ) {
	case 0:
		mat.pbrMetallicRoughness.baseColorTexture.index = i;
		break;
	case 1:
		mat.normalTexture.index = i;
		break;
	case 2:
		mat.pbrMetallicRoughness.metallicRoughnessTexture.index = i;
		break;
	case 3:
		mat.occlusionTexture.index = i;
		break;
	case 4:
		mat.emissiveTexture.index = i;
		break;
	}
}

std::string GltfStore::getTexturePath(
	const CE2Material::TextureSet * txtSet, int n, std::uint32_t defaultColor )
{
	if ( txtSet->texturePathMask & ( 1U << n ) )
		return std::string( *( txtSet->texturePaths[n] ) );
	std::string	tmp;
	if ( txtSet->textureReplacementMask & ( 1U << n ) )
		printToString( tmp, "#%08X", (unsigned int) ( txtSet->textureReplacements[n] ^ ( n != 1 ? 0U : 0x8080U ) ) );
	else if ( defaultColor )
		printToString( tmp, "#%08X", (unsigned int) defaultColor );
	return tmp;
}

void GltfStore::exportMaterial( tinygltf::Material & mat, const std::string & matPath, const Shape * mesh )
{
	if ( matPath.empty() || !( ( nif->getBSVersion() >= 170 && materialDB ) || ( nif->getBSVersion() < 170 && mesh ) ) )
		return;
	if ( !materialSet.insert( matPath ).second )
		return;

	FloatVector4	emissiveFactor( 0.0f );
	if ( nif->getBSVersion() < 170 && mesh && mesh->bssp && ( mesh->bslsp || mesh->bsesp ) ) {
		// Skyrim, Fallout 4 or 76 shader property
		getTexture( mat, 0, mesh );
		getTexture( mat, 1, mesh );
		if ( nif->getBSVersion() >= 130 )
			getTexture( mat, 2, mesh );
		if ( nif->getBSVersion() >= 151 )
			getTexture( mat, 3, mesh );
		if ( mesh->bslsp && mesh->bslsp->hasGlowMap )
			getTexture( mat, 4, mesh );

		if ( mesh->bsesp || ( mesh->alphaProperty && mesh->alphaProperty->hasAlphaBlend() ) ) {
			mat.alphaMode = "BLEND";
		} else if ( mesh->alphaProperty && mesh->alphaProperty->hasAlphaTest() ) {
			mat.alphaMode = "MASK";
			mat.alphaCutoff = mesh->alphaProperty->alphaThreshold;
		} else {
			mat.alphaMode = "OPAQUE";
		}
		mat.doubleSided = mesh->bssp->isDoubleSided;
		mat.normalTexture.scale = 1.0f;
		mat.pbrMetallicRoughness.baseColorFactor[0] = 1.0f;
		mat.pbrMetallicRoughness.baseColorFactor[1] = 1.0f;
		mat.pbrMetallicRoughness.baseColorFactor[2] = 1.0f;
		if ( mesh->bslsp && mesh->bslsp->hasGlowMap ) {
			if ( nif->getBSVersion() < 151 )
				emissiveFactor = FloatVector4( Color4( mesh->bslsp->emissiveColor ) ) * mesh->bslsp->emissiveMult;
			else
				emissiveFactor = FloatVector4( 1.0f );
		}
	} else if ( materialDB ) {
		// Starfield material
		const CE2Material *	material = nullptr;
		try {
			material = materialDB->loadMaterial( matPath );
		} catch ( NifSkopeError & ) {
		}
		if ( !material )
			return;

		const CE2Material::Layer *	layer = nullptr;
		if ( int i = std::countr_zero< std::uint32_t >( material->layerMask ); i < CE2Material::maxLayers )
			layer = material->layers[i];
		if ( !( layer && layer->material && layer->material->textureSet ) )
			return;

		const CE2Material::Material *	m = layer->material;
		const CE2Material::TextureSet *	txtSet = m->textureSet;

		getTexture( mat, 0, getTexturePath( txtSet, 0, 0xFFFFFFFFU ), getTexturePath( txtSet, 2 ), layer->uvStream );
		getTexture( mat, 1, getTexturePath( txtSet, 1 ), std::string(), layer->uvStream );
		getTexture( mat, 2, getTexturePath( txtSet, 3 ), getTexturePath( txtSet, 4 ), layer->uvStream );
		getTexture( mat, 3, getTexturePath( txtSet, 5 ), std::string(), layer->uvStream );
		getTexture( mat, 4, getTexturePath( txtSet, 7 ), std::string(), layer->uvStream );

		if ( material->shaderRoute == 1 ) {	// effect
			mat.alphaMode = "BLEND";
		} else if ( material->flags & CE2Material::Flag_HasOpacity ) {
			mat.alphaMode = "MASK";
			mat.alphaCutoff = material->alphaThreshold;
		} else {
			mat.alphaMode = "OPAQUE";
		}
		mat.doubleSided = bool( material->flags & CE2Material::Flag_TwoSided );
		mat.normalTexture.scale = txtSet->floatParam;
		mat.pbrMetallicRoughness.baseColorFactor[0] = m->color[0];
		mat.pbrMetallicRoughness.baseColorFactor[1] = m->color[1];
		mat.pbrMetallicRoughness.baseColorFactor[2] = m->color[2];
		if ( material->emissiveSettings && material->emissiveSettings->isEnabled )
			emissiveFactor = material->emissiveSettings->emissiveTint;
		else if ( material->layeredEmissiveSettings && material->layeredEmissiveSettings->isEnabled )
			emissiveFactor = FloatVector4( material->layeredEmissiveSettings->layer1Tint ) / 255.0f;
	}
	mat.emissiveFactor[0] = emissiveFactor[0];
	mat.emissiveFactor[1] = emissiveFactor[1];
	mat.emissiveFactor[2] = emissiveFactor[2];
}


static QString getGltfFolder( const NifModel * nif )
{
	QString	dirName = nif->getFolder();
	if ( !( dirName.isEmpty() || dirName.contains( ".ba2/", Qt::CaseInsensitive ) || dirName.contains( ".bsa/", Qt::CaseInsensitive ) ) )
		return dirName;
	QSettings	settings;
	return settings.value( "Spells//Extract File/Last File Path", QString() ).toString();
}

void exportGltf( const NifModel * nif, const Scene * scene, [[maybe_unused]] const QModelIndex & index )
{
	QString	filename = getGltfFolder( nif );
	if ( auto w = qobject_cast< const NifSkope * >( nif->getWindow() ); w ) {
		if ( auto nifPath = w->getCurrentFile(); !nifPath.isEmpty() ) {
			if ( nifPath.endsWith( QLatin1String(".nif"), Qt::CaseInsensitive )
				|| nifPath.endsWith( QLatin1String(".bto"), Qt::CaseInsensitive )
				|| nifPath.endsWith( QLatin1String(".btr"), Qt::CaseInsensitive ) ) {
				nifPath.chop( 4 );
			}
#ifdef Q_OS_WIN32
			nifPath.replace( QChar('\\'), QChar('/') );
#endif
			nifPath.remove( 0, nifPath.lastIndexOf( QChar('/') ) + 1 );
			if ( !nifPath.isEmpty() ) {
				if ( !filename.isEmpty() && !filename.endsWith( QChar('/') ) )
					filename.append( QChar('/') );
				filename.append( nifPath );
				filename.append( QLatin1String(".gltf") );
			}
		}
	}
	filename = QFileDialog::getSaveFileName(qApp->activeWindow(), tr("Choose a .glTF file for export"), filename, "glTF (*.gltf)");
	bool	useFullMatPaths;
	int	textureMipLevel;
	if ( filename.isEmpty() ) {
		return;
	} else {
		QSettings	settings;
		if ( nif->getBSVersion() < 170 )
			gltfEnableLOD = false;
		else
			gltfEnableLOD = settings.value( "Settings/Nif/Enable LOD", false ).toBool();
		useFullMatPaths = settings.value( "Settings/Nif/Export full material paths", true ).toBool();
		textureMipLevel = settings.value( "Settings/Nif/Gl TF Export Mip Level", 1 ).toInt();
		textureMipLevel = std::min< int >( std::max< int >( textureMipLevel, -1 ), 15 );
	}
	if ( !filename.endsWith( ".gltf", Qt::CaseInsensitive ) )
		filename.append( ".gltf" );

	QString buffName = filename.left( filename.length() - 5 ) + ".bin";

	tinygltf::TinyGLTF writer;
	tinygltf::Model model;
	model.asset.generator = "NifSkope glTF 2.0 Exporter v1.2";

	GltfStore gltf( const_cast< NifModel * >( nif ), model, textureMipLevel );
	gltf.materials.emplace( std::string(), std::pair< int, const Shape * >( 0, nullptr ) );
	QByteArray buffer;
	bool success = gltf.createNodes( scene, buffer );
	if ( success )
		success = gltf.createMeshes( scene, buffer );
	if ( success ) {
		auto buff = tinygltf::Buffer();
		buff.name = buffName.mid( QDir::fromNativeSeparators( buffName ).lastIndexOf( QChar('/') ) + 1 ).toStdString();
		buff.data = std::vector<unsigned char>(buffer.cbegin(), buffer.cend());
		model.buffers.push_back(buff);

		model.materials.resize( gltf.materials.size() );
		for ( const auto & i : gltf.materials ) {
			const std::string &	name = i.first;
			int	materialID = i.second.first;
			auto &	mat = model.materials[materialID];

			mat.name = ( !name.empty() ? name : std::string("Default") );
			std::map<std::string, tinygltf::Value> extras;
			extras["Material Path"] = tinygltf::Value( mat.name );
			mat.extras = tinygltf::Value(extras);
			if ( !useFullMatPaths ) {
				if ( size_t n = mat.name.rfind('/'); n != std::string::npos )
					mat.name.erase( 0, n + 1 );
			}
			if ( nif->getBSVersion() >= 170 && mat.name.ends_with(".mat") )
				mat.name.resize( mat.name.length() - 4 );
			else if ( nif->getBSVersion() >= 130 && ( mat.name.ends_with(".bgsm") || mat.name.ends_with(".bgem") ) )
				mat.name.resize( mat.name.length() - 5 );
			for ( size_t j = 0; j < mat.name.length(); j++ ) {
				char	c = mat.name[j];
				if ( std::islower(c) && ( j == 0 || !std::isalpha(mat.name[j - 1]) ) )
					mat.name[j] = std::toupper( c );
			}

			gltf.exportMaterial( mat, name, i.second.second );
		}

		writer.WriteGltfSceneToFile( &model, filename.toStdString(), false, false, true, false );
	}

	if ( gltf.errors.size() == 1 ) {
		Message::warning(nullptr, gltf.errors[0]);
	} else if ( gltf.errors.size() > 1 ) {
		for ( const auto& msg : gltf.errors ) {
			Message::append("Warnings/Errors occurred during glTF Export", msg);
		}
	}
}

// =================================================== glTF import ====================================================

class ImportGltf {
protected:
	struct BoneWeights {
		std::uint16_t	joints[8];
		std::uint16_t	weights[8];
		BoneWeights()
		{
			for ( int i = 0; i < 8; i++ ) {
				joints[i] = 0;
				weights[i] = 0;
			}
		}
	};
	const tinygltf::Model &	model;
	NifModel *	nif;
	bool	lodEnabled;
	bool	scaleWarningFlag;
	std::vector< int >	nodeMap;
	static inline Vector3 fromMeters( const Vector3 & v );
	bool nodeHasMeshes( const tinygltf::Node & node, int d = 0 ) const;
	static void normalizeFloats( float * p, size_t n, int dataType );
	template< typename T > bool loadBuffer( std::vector< T > & outBuf, int accessor, int typeRequired );
	void applyXYZScale( Transform & t, const Vector3 & scale );
	QModelIndex insertNiBlock( const char * blockType );
	void loadSkin( const QPersistentModelIndex & index, const tinygltf::Skin & skin );
	int loadTriangles( const QModelIndex & index, const tinygltf::Primitive & p );
	void loadSkinnedLODMesh( const QPersistentModelIndex & index, const tinygltf::Primitive & p, int lod );
	// Returns true if tangent space needs to be calculated
	bool loadMesh(
		const QPersistentModelIndex & index, std::string & materialPath, const tinygltf::Primitive & p,
		int lod, int skin );
	bool loadMeshCE1( const QPersistentModelIndex & index, std::string & materialPath, const tinygltf::Primitive & p,
						int skin );
	void loadNode( const QPersistentModelIndex & index, int nodeNum, bool isRoot );
public:
	ImportGltf( NifModel * nifModel, const tinygltf::Model & gltfModel, bool enableLOD )
		: model( gltfModel ), nif( nifModel ), lodEnabled( enableLOD ), scaleWarningFlag( false )
	{
	}
	void importModel( const QPersistentModelIndex & iBlock );
};

inline Vector3 ImportGltf::fromMeters( const Vector3 & v )
{
	return Vector3( float( double( v[0] ) * ( 64.0 / 0.9144 ) ), float( double( v[1] ) * ( 64.0 / 0.9144 ) ),
					float( double( v[2] ) * ( 64.0 / 0.9144 ) ) );
}

bool ImportGltf::nodeHasMeshes( const tinygltf::Node & node, int d ) const
{
	if ( node.mesh >= 0 && size_t(node.mesh) < model.meshes.size() )
		return true;
	if ( d >= 1024 )
		return false;
	for ( int i : node.children ) {
		if ( i >= 0 && size_t(i) < model.nodes.size() && nodeHasMeshes( model.nodes[i], d + 1 ) )
			return true;
	}
	if ( node.extras.Has( "Flat" ) && node.extras.Get( "Flat" ).Get< bool >() )
		return false;
	qsizetype	k = qsizetype( &node - model.nodes.data() );
	for ( const auto & i : model.skins ) {
		for ( int j : i.joints ) {
			if ( j == k )
				return true;
		}
	}
	return false;
}

void ImportGltf::normalizeFloats( float * p, size_t n, int dataType )
{
	float	scale;
	switch ( dataType ) {
	case TINYGLTF_COMPONENT_TYPE_BYTE:
		scale = 127.0f;
		break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		scale = 255.0f;
		break;
	case TINYGLTF_COMPONENT_TYPE_SHORT:
		scale = 32767.0f;
		break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		scale = 65535.0f;
		break;
	case TINYGLTF_COMPONENT_TYPE_INT:
		scale = float( 2147483647.0 );
		break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		scale = float( 4294967295.0 );
		break;
	default:
		return;
	}
	for ( ; n >= 8; p = p + 8, n = n - 8 )
		( FloatVector8( p ) / scale ).convertToFloats( p );
	for ( ; n > 0; p++, n-- )
		*p = *p / scale;
}

template< typename T > bool ImportGltf::loadBuffer( std::vector< T > & outBuf, int accessor, int typeRequired )
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
	if ( sizeof(T) == sizeof(float) && T(0.1f) != T(0.0f) )
		normalizeFloats( reinterpret_cast< float * >(outBuf.data()), outBuf.size(), a.componentType );

	return true;
}

void ImportGltf::applyXYZScale( Transform & t, const Vector3 & scale )
{
	float	avgScale = ( scale[0] + scale[1] + scale[2] ) / 3.0f;
	t.scale = avgScale;
	FloatVector4	tmp = FloatVector4( scale ) / avgScale;
	if ( ( ( ( tmp - 1.0f ).absValues() - 0.000001f ).getSignMask() & 0x07 ) == 0x07 )
		return;
	if ( !scaleWarningFlag ) {
		scaleWarningFlag = true;
		QMessageBox::warning( nullptr, "NifSkope warning",
								tr( "glTF model uses anisotropic scaling, use Transform/Apply to fix transforms, "
									"and recalculate normals and tangents" ) );
	}
	t.rotation( 0, 0 ) *= tmp[0];
	t.rotation( 1, 0 ) *= tmp[0];
	t.rotation( 2, 0 ) *= tmp[0];
	t.rotation( 0, 1 ) *= tmp[1];
	t.rotation( 1, 1 ) *= tmp[1];
	t.rotation( 2, 1 ) *= tmp[1];
	t.rotation( 0, 2 ) *= tmp[2];
	t.rotation( 1, 2 ) *= tmp[2];
	t.rotation( 2, 2 ) *= tmp[2];
}

QModelIndex ImportGltf::insertNiBlock( const char * blockType )
{
	QModelIndex	index = nif->insertNiBlock( blockType );
	nif->updateChildArraySizes( nif->getItem( index ) );
	return index;
}

void ImportGltf::loadSkin( const QPersistentModelIndex & index, const tinygltf::Skin & skin )
{
	QPersistentModelIndex	iSkinBMP;
	quint32	bsVersion = nif->getBSVersion();
	if ( bsVersion >= 170 ) {
		iSkinBMP = insertNiBlock( "SkinAttach" );
		nif->set<QString>( iSkinBMP, "Name", "SkinBMP" );
		if ( auto iNumExtraData = nif->getIndex( index, "Num Extra Data List" ); iNumExtraData.isValid() ) {
			quint32	n = nif->get<quint32>( iNumExtraData );
			nif->set<quint32>( iNumExtraData, n + 1 );
			auto	iExtraData = nif->getIndex( index, "Extra Data List" );
			if ( iExtraData.isValid() ) {
				nif->updateArraySize( iExtraData );
				nif->setLink( nif->getIndex( iExtraData, int(n) ), qint32( nif->getBlockNumber(iSkinBMP) ) );
			}
		}
	}

	// TODO: implement support for Skyrim skin partitions
	QPersistentModelIndex	iSkin = insertNiBlock( bsVersion < 130 ? "NiSkinInstance" : "BSSkin::Instance" );
	nif->setLink( index, "Skin", qint32( nif->getBlockNumber(iSkin) ) );
	nif->setLink( iSkin, "Skeleton Root", qint32( 0 ) );

	QPersistentModelIndex	iBoneData = insertNiBlock( bsVersion < 130 ? "NiSkinData" : "BSSkin::BoneData" );
	nif->setLink( iSkin, "Data", qint32( nif->getBlockNumber(iBoneData) ) );

	size_t	numBones = skin.joints.size();
	if ( bsVersion >= 170 ) {
		nif->set<quint32>( iSkinBMP, "Num Bones", quint32(numBones) );
		iSkinBMP = nif->getIndex( iSkinBMP, "Bones" );
		if ( iSkinBMP.isValid() )
			nif->updateArraySize( iSkinBMP );
	}

	nif->set<quint32>( iSkin, "Num Bones", quint32(numBones) );
	if ( auto iBones = nif->getIndex( iSkin, "Bones" ); iBones.isValid() ) {
		nif->updateArraySize( iBones );
		for ( size_t i = 0; i < numBones; i++ ) {
			const tinygltf::Node *	boneNode = nullptr;
			int	boneBlockNum = -1;
			// FIXME: this works only if bone nodes are loaded before the skin
			if ( skin.joints[i] >= 0 && size_t(skin.joints[i]) < nodeMap.size() ) {
				boneNode = model.nodes.data() + skin.joints[i];
				boneBlockNum = nodeMap[skin.joints[i]];
			}
			if ( boneBlockNum < 0 ) {
				if ( boneNode && iSkinBMP.isValid() )
					nif->set<QString>( nif->getIndex( iSkinBMP, int(i) ), QString::fromStdString( boneNode->name ) );
			} else {
				nif->setLink( nif->getIndex( iBones, int(i) ), qint32(boneBlockNum) );
			}
		}
	}

	nif->set<quint32>( iBoneData, "Num Bones", quint32(numBones) );
	if ( auto iBones = nif->getIndex( iBoneData, "Bone List" ); iBones.isValid() ) {
		nif->updateArraySize( iBones );
		std::vector< float >	boneTransforms;
		(void) loadBuffer< float >( boneTransforms, skin.inverseBindMatrices, TINYGLTF_TYPE_MAT4 );
		for ( size_t i = 0; i < numBones; i++ ) {
			Matrix4	m;
			if ( ( (i + 1) << 4 ) <= boneTransforms.size() ) {
				// use inverse bind matrices if available
				std::memcpy( const_cast< float * >(m.data()), boneTransforms.data() + (i << 4), sizeof(float) << 4 );
			} else if ( skin.joints[i] >= 0 && size_t(skin.joints[i]) < model.nodes.size() ) {
				const tinygltf::Node &	boneNode = model.nodes[skin.joints[i]];
				if ( boneNode.matrix.size() >= 16 ) {
					for ( size_t j = 0; j < 16; j++ )
						const_cast< float * >(m.data())[j] = float( boneNode.matrix[j] );
				} else {
					Vector3	tmpTranslation( 0.0f, 0.0f, 0.0f );
					Matrix	tmpRotation;
					Vector3	tmpScale( 1.0f, 1.0f, 1.0f );
					if ( boneNode.rotation.size() >= 4 ) {
						Quat	q;
						for ( int j = 0; j < 4; j++ )
							q[(j + 1) & 3] = float( boneNode.rotation[j] );
						tmpRotation.fromQuat( q );
					}
					if ( boneNode.scale.size() >= 3 ) {
						tmpScale[0] = float( boneNode.scale[0] );
						tmpScale[1] = float( boneNode.scale[1] );
						tmpScale[2] = float( boneNode.scale[2] );
					}
					if ( boneNode.translation.size() >= 3 ) {
						tmpTranslation[0] = float( boneNode.translation[0] );
						tmpTranslation[1] = float( boneNode.translation[1] );
						tmpTranslation[2] = float( boneNode.translation[2] );
					}
					m.compose( tmpTranslation, tmpRotation, tmpScale );
				}
				m = m.inverted();
			}
			Transform	t;
			Vector3	tmpScale;
			m.decompose( t.translation, t.rotation, tmpScale );
			applyXYZScale( t, tmpScale );
			if ( bsVersion < 170 )
				t.translation = fromMeters( t.translation );		// convert from meters
			QModelIndex	iBone = nif->getIndex( iBones, int(i) );
			if ( iBone.isValid() )
				t.writeBack( nif, iBone );
		}
	}
}

int ImportGltf::loadTriangles( const QModelIndex & index, const tinygltf::Primitive & p )
{
	std::vector< std::uint16_t >	indices;
	if ( !loadBuffer< std::uint16_t >( indices, p.indices, TINYGLTF_TYPE_SCALAR ) )
		return -1;

	int	numTriangles = int( indices.size() / 3 );
	if ( nif->getBSVersion() >= 170 ) {
		nif->set<quint32>( index, "Indices Size", quint32(indices.size()) );
	} else if ( nif->getBSVersion() >= 130 ) {
		nif->set<quint32>( index, "Num Triangles", quint32(numTriangles) );
	} else {
		numTriangles = std::min< int >( numTriangles, 65535 );
		nif->set<quint16>( index, "Num Triangles", quint16(numTriangles) );
	}
	auto	iTriangles = nif->getIndex( index, "Triangles" );
	if ( iTriangles.isValid() ) {
		nif->updateArraySize( iTriangles );
		QVector< Triangle >	triangles;
		triangles.resize( numTriangles );
		for ( qsizetype i = 0; i < numTriangles; i++ ) {
			triangles[i][0] = indices[i * 3];
			triangles[i][1] = indices[i * 3 + 1];
			triangles[i][2] = indices[i * 3 + 2];
		}
		nif->setArray<Triangle>( iTriangles, triangles );
	}

	return numTriangles;
}

void ImportGltf::loadSkinnedLODMesh( const QPersistentModelIndex & index, const tinygltf::Primitive & p, int lod )
{
	auto	iMeshes = nif->getIndex( index, "Meshes" );
	if ( !iMeshes.isValid() )
		return;
	auto	iMesh = nif->getIndex( iMeshes, 0 );
	if ( iMesh.isValid() )
		iMesh = nif->getIndex( iMesh, "Mesh" );
	if ( !iMesh.isValid() )
		return;
	auto	iMeshData = nif->getIndex( iMesh, "Mesh Data" );
	if ( !iMeshData.isValid() )
		return;

	std::uint32_t	numVerts = nif->get<quint32>( iMeshData, "Num Verts" );
	bool	invalidAttrSize = false;
	for ( const auto & i : p.attributes ) {
		std::vector< float >	attrBuf;
		if ( i.first == "POSITION" || i.first == "NORMAL" ) {
			if ( !loadBuffer< float >( attrBuf, i.second, TINYGLTF_TYPE_VEC3 ) )
				continue;
			if ( !attrBuf.empty() && attrBuf.size() != ( size_t(numVerts) * 3 ) ) {
				invalidAttrSize = true;
				break;
			}
		} else if ( i.first == "TEXCOORD_0" || i.first == "TEXCOORD_1" ) {
			if ( !loadBuffer< float >( attrBuf, i.second, TINYGLTF_TYPE_VEC2 ) )
				continue;
			if ( !attrBuf.empty() && attrBuf.size() != ( size_t(numVerts) << 1 ) ) {
				invalidAttrSize = true;
				break;
			}
		} else if ( i.first == "TANGENT" || i.first == "COLOR_0" ) {
			if ( !loadBuffer< float >( attrBuf, i.second, TINYGLTF_TYPE_VEC4 ) )
				continue;
			if ( !attrBuf.empty() && attrBuf.size() != ( size_t(numVerts) << 2 ) ) {
				invalidAttrSize = true;
				break;
			}
		}
	}
	if ( invalidAttrSize ) {
		QMessageBox::warning( nullptr, "NifSkope warning", QString("LOD%1 mesh has inconsistent vertex count with LOD0").arg(lod) );
		return;
	}

	if ( auto i = nif->getItem( iMeshData ); i ) {
		i->invalidateVersionCondition();
		i->invalidateCondition();
	}
	nif->set<quint32>( iMeshData, "Num LODs", quint32(lod) );
	QModelIndex	iLODMesh = nif->getIndex( iMeshData, "LODs" );
	if ( !iLODMesh.isValid() )
		return;
	nif->updateArraySize( iLODMesh );
	iLODMesh = nif->getIndex( iLODMesh, lod - 1 );
	if ( iLODMesh.isValid() )
		(void) loadTriangles( iLODMesh, p );
}

bool ImportGltf::loadMesh(
	const QPersistentModelIndex & index, std::string & materialPath, const tinygltf::Primitive & p, int lod, int skin )
{
	if ( nif->getBSVersion() < 170 )
		return loadMeshCE1( index, materialPath, p, skin );

	if ( lod > 0 && skin >= 0 && size_t(skin) < model.skins.size() ) {
		loadSkinnedLODMesh( index, p, lod );
		return false;
	}

	if ( materialPath.empty() && p.material >= 0 && size_t(p.material) < model.materials.size() ) {
		const std::string &	matName = model.materials[p.material].name;
		std::string	matNameL = matName;
		for ( auto & c : matNameL ) {
			if ( std::isupper(c) )
				c = std::tolower(c);
			else if ( c == '\\' )
				c = '/';
		}
		if ( matNameL.find('/') != std::string::npos ) {
			if ( matNameL.ends_with(".mat") || matNameL.starts_with("materials/") )
				materialPath = matName;
		}
	}

	auto	iMeshes = nif->getIndex( index, "Meshes" );
	if ( !( iMeshes.isValid() && nif->isArray( iMeshes ) && nif->rowCount( iMeshes ) > lod ) )
		return false;
	auto	iMesh = nif->getIndex( iMeshes, lod );
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

	int	numTriangles = loadTriangles( iMeshData, p );
	if ( numTriangles < 0 )
		return false;
	nif->set<quint32>( iMesh, "Indices Size", quint32(numTriangles) * 3U );

	if ( skin >= 0 && size_t(skin) < model.skins.size() )
		loadSkin( index, model.skins[skin] );

	std::vector< BoneWeights >	boneWeights;

	for ( const auto & i : p.attributes ) {
		if ( i.first == "POSITION" ) {
			std::vector< float >	positions;
			if ( !loadBuffer< float >( positions, i.second, TINYGLTF_TYPE_VEC3 ) )
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
		} else if ( i.first == "TEXCOORD_0" || i.first == "TEXCOORD_1" ) {
			std::vector< float >	uvs;
			if ( !loadBuffer< float >( uvs, i.second, TINYGLTF_TYPE_VEC2 ) )
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
		} else if ( i.first == "NORMAL" ) {
			std::vector< float >	normals;
			if ( !loadBuffer< float >( normals, i.second, TINYGLTF_TYPE_VEC3 ) )
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
		} else if ( i.first == "TANGENT" ) {
			std::vector< float >	tangents;
			if ( !loadBuffer< float >( tangents, i.second, TINYGLTF_TYPE_VEC4 ) )
				continue;
			std::uint32_t	numTangents = std::uint32_t( tangents.size() >> 2 );
			if ( !numTangents )
				continue;
			nif->set<quint32>( iMeshData, "Num Tangents", numTangents );
			auto	iTangents = nif->getIndex( iMeshData, "Tangents" );
			if ( iTangents.isValid() ) {
				nif->updateArraySize( iTangents );
				QVector< UDecVector4 >	tangentsVec4;
				tangentsVec4.resize( qsizetype(numTangents) );
				for ( qsizetype j = 0; j < qsizetype(numTangents); j++ ) {
					FloatVector4	tangent( tangents.data() + (j * 4) );
					float	r = tangent.dotProduct3( tangent );
					if ( r > 0.0f ) [[likely]] {
						tangent /= float( std::sqrt( r ) );
						tangent[3] = ( tangent[3] < 0.0f ? 1.0f : -1.0f );
					} else {
						tangent = FloatVector4( 1.0f, 0.0f, 0.0f, -1.0f );
					}
					tangent.convertToFloats( &(tangentsVec4[j][0]) );
				}
				nif->setArray<UDecVector4>( iTangents, tangentsVec4 );
			}
		} else if ( i.first == "COLOR_0" ) {
			std::vector< float >	colors;
			bool	haveAlpha = loadBuffer< float >( colors, i.second, TINYGLTF_TYPE_VEC4 );
			if ( !haveAlpha && !loadBuffer< float >( colors, i.second, TINYGLTF_TYPE_VEC3 ) )
				continue;
			size_t	componentCnt = ( !haveAlpha ? 3 : 4 );
			std::uint32_t	numColors = std::uint32_t( colors.size() / componentCnt );
			if ( !numColors )
				continue;
			nif->set<quint32>( iMeshData, "Num Vertex Colors", numColors );
			auto	iColors = nif->getIndex( iMeshData, "Vertex Colors" );
			if ( iColors.isValid() ) {
				nif->updateArraySize( iColors );
				QVector< ByteColor4BGRA >	colorsVec4;
				colorsVec4.resize( qsizetype(numColors) );
				const float *	q = colors.data();
				for ( size_t j = 0; j < numColors; j++, q = q + componentCnt ) {
					FloatVector4	color;
					if ( !haveAlpha )
						color = FloatVector4::convertVector3( q ).blendValues( FloatVector4(1.0f), 0x08 );
					else
						color = FloatVector4( q );
					color.maxValues( FloatVector4(0.0f) ).minValues( FloatVector4(1.0f) );
					color.convertToFloats( &(colorsVec4[qsizetype(j)][0]) );
				}
				nif->setArray<ByteColor4BGRA>( iColors, colorsVec4 );
			}
		} else if ( i.first == "WEIGHTS_0" || i.first == "WEIGHTS_1" ) {
			std::vector< float >	weights;
			if ( !loadBuffer< float >( weights, i.second, TINYGLTF_TYPE_VEC4 ) )
				continue;
			size_t	numWeights = weights.size() >> 2;
			if ( numWeights > boneWeights.size() )
				boneWeights.resize( numWeights );
			size_t	offs = ( i.first.c_str()[8] == '0' ? 0 : 4 );
			for ( size_t j = 0; j < numWeights; j++ ) {
				FloatVector4	w( weights.data() + (j * 4) );
				w.maxValues( FloatVector4(0.0f) ).minValues( FloatVector4(1.0f) );
				w *= 65535.0f;
				w.roundValues();
				boneWeights[j].weights[offs] = std::uint16_t( w[0] );
				boneWeights[j].weights[offs + 1] = std::uint16_t( w[1] );
				boneWeights[j].weights[offs + 2] = std::uint16_t( w[2] );
				boneWeights[j].weights[offs + 3] = std::uint16_t( w[3] );
			}
		} else if ( i.first == "JOINTS_0" || i.first == "JOINTS_1" ) {
			std::vector< std::uint16_t >	joints;
			if ( !loadBuffer< std::uint16_t >( joints, i.second, TINYGLTF_TYPE_VEC4 ) )
				continue;
			size_t	numJoints = joints.size() >> 2;
			if ( numJoints > boneWeights.size() )
				boneWeights.resize( numJoints );
			size_t	offs = ( i.first.c_str()[7] == '0' ? 0 : 4 );
			for ( size_t j = 0; j < numJoints; j++ ) {
				for ( size_t k = 0; k < 4; k++ )
					boneWeights[j].joints[offs + k] = joints[j * 4 + k];
			}
		}
	}

	if ( !boneWeights.empty() ) {
		size_t	weightsPerVertex = 0;
		for ( const auto & bw : boneWeights ) {
			for ( size_t i = 8; i > 0; i-- ) {
				if ( bw.weights[i - 1] != 0 ) {
					weightsPerVertex = std::max( weightsPerVertex, i );
					break;
				}
			}
		}
		if ( weightsPerVertex > 0 ) {
			size_t	numWeights = boneWeights.size() * weightsPerVertex;
			nif->set<quint32>( iMeshData, "Weights Per Vertex", quint32(weightsPerVertex) );
			nif->set<quint32>( iMeshData, "Num Weights", quint32(numWeights) );
			auto	iWeights = nif->getIndex( iMeshData, "Weights" );
			if ( iWeights.isValid() ) {
				nif->updateArraySize( iWeights );
				NifItem *	weightsItem = nif->getItem( iWeights );
				for ( size_t i = 0; weightsItem && i < boneWeights.size(); i++ ) {
					for ( size_t j = 0; j < weightsPerVertex; j++ ) {
						auto	weightItem = weightsItem->child( int(i * weightsPerVertex + j) );
						if ( weightItem ) {
							nif->set<quint16>( weightItem->child( 0 ), boneWeights[i].joints[j] );
							nif->set<quint16>( weightItem->child( 1 ), boneWeights[i].weights[j] );
						}
					}
				}
			}
		}
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

bool ImportGltf::loadMeshCE1(
	const QPersistentModelIndex & index, std::string & materialPath, const tinygltf::Primitive & p, int skin )
{
	if ( !materialPath.empty() || ( p.material >= 0 && size_t(p.material) < model.materials.size() ) ) {
		const std::string &	matName = ( !materialPath.empty() ? materialPath : model.materials[p.material].name );
		std::string	matNameL = matName;
		for ( auto & c : matNameL ) {
			if ( std::isupper(c) )
				c = std::tolower(c);
			else if ( c == '\\' )
				c = '/';
		}
		if ( nif->getBSVersion() >= 130
			&& !( matNameL.find('/') != std::string::npos
					&& ( matNameL.starts_with("materials/")
						|| matNameL.ends_with(".bgsm") || matNameL.ends_with(".bgem") ) ) ) {
			materialPath.clear();
		} else if ( materialPath.empty() ) {
			materialPath = model.materials[p.material].name;
		}
	}

	std::vector< float >	positions;
	std::vector< float >	uvs;
	std::vector< float >	normals;
	std::vector< float >	tangents;
	std::vector< float >	colors;
	std::vector< float >	weights;
	std::vector< std::uint16_t >	joints;
	bool	haveTexCoord1 = false;
	bool	haveVertexColors = false;
	bool	haveAlpha = false;
	for ( const auto & i : p.attributes ) {
		if ( i.first == "POSITION" ) {
			(void) loadBuffer< float >( positions, i.second, TINYGLTF_TYPE_VEC3 );
		} else if ( i.first == "TEXCOORD_0" ) {
			(void) loadBuffer< float >( uvs, i.second, TINYGLTF_TYPE_VEC2 );
		} else if ( i.first == "TEXCOORD_1" ) {
			haveTexCoord1 = true;
		} else if ( i.first == "NORMAL" ) {
			(void) loadBuffer< float >( normals, i.second, TINYGLTF_TYPE_VEC3 );
		} else if ( i.first == "TANGENT" ) {
			(void) loadBuffer< float >( tangents, i.second, TINYGLTF_TYPE_VEC4 );
		} else if ( i.first == "COLOR_0" ) {
			haveVertexColors = true;
			haveAlpha = loadBuffer< float >( colors, i.second, TINYGLTF_TYPE_VEC4 );
			if ( !haveAlpha )
				haveVertexColors = loadBuffer< float >( colors, i.second, TINYGLTF_TYPE_VEC3 );
		} else if ( i.first == "WEIGHTS_0" ) {
			(void) loadBuffer< float >( weights, i.second, TINYGLTF_TYPE_VEC4 );
		} else if ( i.first == "JOINTS_0" ) {
			(void) loadBuffer< std::uint16_t >( joints, i.second, TINYGLTF_TYPE_VEC4 );
		}
	}
	bool	haveWeights = ( weights.size() >= 4 && joints.size() >= 4 );
	std::uint64_t	vertexDesc = 0x0001B00000000405ULL;
	if ( haveTexCoord1 )
		vertexDesc = ( vertexDesc + 1U ) | ( ( vertexDesc & 0x0FU ) << 12 ) | 0x0000400000000000ULL;
	vertexDesc = ( vertexDesc + 1U ) | ( ( vertexDesc & 0x0FU ) << 16 );		// normals
	vertexDesc = ( vertexDesc + 1U ) | ( ( vertexDesc & 0x0FU ) << 20 );		// tangents
	if ( haveVertexColors )
		vertexDesc = ( vertexDesc + 1U ) | ( ( vertexDesc & 0x0FU ) << 24 ) | 0x0002000000000000ULL;
	if ( haveWeights )
		vertexDesc = ( vertexDesc + 3U ) | ( ( vertexDesc & 0x0FU ) << 28 ) | 0x0004000000000000ULL;
	if ( nif->getBSVersion() >= 130 )
		vertexDesc = vertexDesc | 0x0040000000000000ULL;		// default to full precision
	nif->set<BSVertexDesc>( index, "Vertex Desc", vertexDesc );

	if ( positions.size() > ( 65535 * 3 ) )
		positions.resize( 65535 * 3 );			// TODO: warning or error on too many vertices or triangles
	int	numVerts = int( positions.size() / 3 );
	nif->set<quint16>( index, "Num Vertices", quint16( numVerts ) );
	size_t	dataSize = size_t( numVerts ) * ( vertexDesc & 0x0FU ) * 4;
	nif->set<quint32>( index, "Data Size", quint32( dataSize ) );

	int	numTriangles = std::max< int >( loadTriangles( index, p ), 0 );
	dataSize += size_t( numTriangles ) * 6;
	nif->set<quint32>( index, "Data Size", quint32( dataSize ) );

	if ( skin >= 0 && size_t(skin) < model.skins.size() )
		loadSkin( index, model.skins[skin] );

	QModelIndex	iVertexData = nif->getIndex( index, "Vertex Data" );
	if ( !iVertexData.isValid() )
		return false;
	nif->updateArraySize( iVertexData );

	for ( size_t i = 0; i < size_t( numVerts ); i++ ) {
		if ( QModelIndex iVertex = nif->getIndex( iVertexData, int( i ) ); iVertex.isValid() ) {
			Vector3	v( positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2] );
			nif->set<Vector3>( iVertex, "Vertex", fromMeters( v ) );	// convert from meters

			HalfVector2	uv;
			if ( ( i * 2 + 2 ) <= uvs.size() )
				uv = HalfVector2( uvs[i * 2], uvs[i * 2 + 1] );
			nif->set<HalfVector2>( iVertex, "UV", uv );

			FloatVector4	n( 0.0f, 0.0f, 1.0f, 0.0f );
			if ( ( i * 3 + 3 ) <= normals.size() )
				n = FloatVector4::convertVector3( normals.data() + ( i * 3 ) );
			nif->set<ByteVector3>( iVertex, "Normal", ByteVector3( n[0], n[1], n[2] ) );

			FloatVector4	t( 0.0f, -1.0f, 0.0f, 0.0f );
			FloatVector4	b( 1.0f, 0.0f, 0.0f, 0.0f );
			if ( ( i * 4 + 4 ) <= tangents.size() ) {
				b = FloatVector4( tangents.data() + ( i * 4 ) );
				t = b.crossProduct3( n ) * b[3];
			}
			nif->set<ByteVector3>( iVertex, "Tangent", ByteVector3( t[0], t[1], t[2] ) );
			nif->set<float>( iVertex, "Bitangent X", b[0] );
			nif->set<float>( iVertex, "Bitangent Y", b[1] );
			nif->set<float>( iVertex, "Bitangent Z", b[2] );

			if ( haveVertexColors ) {
				FloatVector4	c( 1.0f );
				size_t	componentCnt = ( !haveAlpha ? 3 : 4 );
				if ( ( i * componentCnt + componentCnt ) <= colors.size() ) {
					if ( !haveAlpha )
						c.blendValues( FloatVector4::convertVector3( colors.data() + ( i * componentCnt ) ), 0x07 );
					else
						c = FloatVector4( colors.data() + ( i * componentCnt ) );
				}
				nif->set<ByteColor4>( iVertex, "Vertex Colors", ByteColor4( c ) );
			}

			if ( haveWeights ) {
				FloatVector4	w( 0.0f );
				if ( ( i * 4 + 4 ) <= weights.size() )
					w = FloatVector4( weights.data() + ( i * 4 ) );
				if ( QModelIndex iWeights = nif->getIndex( iVertex, "Bone Weights" ); iWeights.isValid() ) {
					nif->updateArraySize( iWeights );
					for ( int k = 0; k < 4; k++ )
						nif->set<float>( nif->getIndex( iWeights, k ), w[k] );
				}
				std::uint16_t	j[4] = { 0, 0, 0, 0 };
				if ( ( i * 4 + 4 ) <= joints.size() )
					std::memcpy( j, joints.data() + ( i * 4 ), sizeof( std::uint16_t ) * 4 );
				if ( QModelIndex iBones = nif->getIndex( iVertex, "Bone Indices" ); iBones.isValid() ) {
					nif->updateArraySize( iBones );
					for ( int k = 0; k < 4; k++ )
						nif->set<quint8>( nif->getIndex( iBones, k ), quint8( j[k] ) );
				}
			}
		}
	}

	return ( normals.size() < 3 || ( tangents.size() * 3 ) < ( normals.size() * 4 ) );
}

void ImportGltf::loadNode( const QPersistentModelIndex & index, int nodeNum, bool isRoot )
{
	quint32	bsVersion = nif->getBSVersion();
	if ( nodeNum < 0 || size_t(nodeNum) >= model.nodes.size() || nodeMap[nodeNum] >= 0 )
		return;
	const tinygltf::Node &	node = model.nodes[nodeNum];
	if ( !nodeHasMeshes( node ) )
		return;

	bool	haveMesh = ( node.mesh >= 0 && size_t(node.mesh) < model.meshes.size() );
	size_t	primCnt = 0;
	if ( haveMesh )
		primCnt = model.meshes[node.mesh].primitives.size();
	size_t	p = 0;
	do {
		const tinygltf::Primitive *	meshPrim = nullptr;
		if ( haveMesh ) {
			if ( !primCnt )
				break;
			meshPrim = model.meshes[node.mesh].primitives.data() + p;
			if ( meshPrim->mode != TINYGLTF_MODE_TRIANGLES || meshPrim->attributes.empty() )
				continue;
			if ( meshPrim->indices < 0 || size_t(meshPrim->indices) >= model.accessors.size() )
				continue;
		}

		QPersistentModelIndex	iBlock =
			insertNiBlock( !haveMesh ? "NiNode" : ( bsVersion < 170 ? "BSTriShape" : "BSGeometry" ) );
		if ( !haveMesh )
			nodeMap[nodeNum] = nif->getBlockNumber( iBlock );
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
					nif->setLink( nif->getIndex( iChildren, int(n) ), qint32( nif->getBlockNumber(iBlock) ) );
				}
			}
		}
		// enable internal geometry for Starfield meshes
		nif->set<quint32>( iBlock, "Flags", ( !( haveMesh && bsVersion >= 170 ) ? 14U : 526U ) );
		nif->set<QString>( iBlock, "Name", QString::fromStdString( node.name ) );

		Transform	t;
		if ( node.matrix.size() >= 16 ) {
			Matrix4	m;
			for ( size_t i = 0; i < 16; i++ )
				const_cast< float * >( m.data() )[i] = float( node.matrix[i] );
			Vector3	tmpScale;
			m.decompose( t.translation, t.rotation, tmpScale );
			applyXYZScale( t, tmpScale );
		} else {
			if ( node.rotation.size() >= 4 ) {
				Quat	r;
				for ( size_t i = 0; i < 4; i++ )
					r[(i + 1) & 3] = float( node.rotation[i] );
				t.rotation.fromQuat( r );
			}
			if ( node.scale.size() >= 3 ) {
				Vector3	tmpScale( float( node.scale[0] ), float( node.scale[1] ), float( node.scale[2] ) );
				applyXYZScale( t, tmpScale );
			} else if ( node.scale.size() >= 1 ) {
				t.scale = float( node.scale[0] );
			}
			if ( node.translation.size() >= 3 ) {
				t.translation[0] = float( node.translation[0] );
				t.translation[1] = float( node.translation[1] );
				t.translation[2] = float( node.translation[2] );
			}
		}
		if ( isRoot ) {
			t.rotation = t.rotation.toZUp();
			t.translation = Vector3( t.translation[0], -(t.translation[2]), t.translation[1] );
		}
		if ( bsVersion < 170 )
			t.translation = fromMeters( t.translation );		// convert from meters
		t.writeBack( nif, iBlock );

		if ( meshPrim ) {
			QPersistentModelIndex	iMaterialID;
			if ( bsVersion >= 170 ) {
				iMaterialID = insertNiBlock( "NiIntegerExtraData" );
				nif->set<QString>( iMaterialID, "Name", "MaterialID" );
				if ( auto iNumExtraData = nif->getIndex( iBlock, "Num Extra Data List" ); iNumExtraData.isValid() ) {
					quint32	n = nif->get<quint32>( iNumExtraData );
					nif->set<quint32>( iNumExtraData, n + 1 );
					auto	iExtraData = nif->getIndex( iBlock, "Extra Data List" );
					if ( iExtraData.isValid() ) {
						nif->updateArraySize( iExtraData );
						nif->setLink( nif->getIndex( iExtraData, int(n) ), qint32( nif->getBlockNumber(iMaterialID) ) );
					}
				}
			}

			std::string	materialPath;
			if ( node.extras.Has( "Material Path" ) )
				materialPath = node.extras.Get( "Material Path" ).Get< std::string >();

			bool	tangentsNeeded = loadMesh( iBlock, materialPath, *meshPrim, 0, node.skin );
			for ( int l = 0; size_t(l) < node.lods.size() && l < 3 && gltfEnableLOD; l++ ) {
				int	n = node.lods[l];
				if ( n >= 0 && size_t(n) < model.nodes.size() ) {
					int	m = model.nodes[n].mesh;
					if ( m >= 0 && size_t(m) < model.meshes.size() )
						tangentsNeeded |= loadMesh( iBlock, materialPath, *meshPrim, l + 1, node.skin );
				}
			}

			bool	isEffect = false;
			if ( bsVersion < 170 ) {
				size_t	l = materialPath.length();
				if ( bsVersion >= 130 && l >= 5 && materialPath[l - 5] == '.'
					&& ( FileBuffer::readUInt32Fast( materialPath.c_str() + ( l - 4 ) ) | 0x20202020 ) == 0x6D656762 ) {
					isEffect = true;			// "bgem"
				}
			}
			QPersistentModelIndex	iShaderProperty =
				insertNiBlock( isEffect ? "BSEffectShaderProperty" : "BSLightingShaderProperty" );
			nif->setLink( iBlock, "Shader Property", qint32( nif->getBlockNumber(iShaderProperty) ) );

			if ( !materialPath.empty() ) {
				auto	matPathTmp = QString::fromStdString( materialPath );
				const char *	extStr = ( bsVersion < 170 ? ( isEffect ? ".bgem" : ".bgsm" ) : ".mat" );
				materialPath = Game::GameManager::get_full_path( matPathTmp, "materials/", extStr );
				nif->set<QString>( iShaderProperty, "Name", matPathTmp );
			}
			if ( bsVersion >= 170 ) {
				std::uint32_t	matPathHash = 0;
				for ( char c : materialPath )
					hashFunctionCRC32( matPathHash, (unsigned char) ( c != '/' ? c : '\\' ) );
				nif->set<quint32>( iMaterialID, "Integer Data", matPathHash );
			} else if ( !isEffect && ( bsVersion < 151 || materialPath.empty() ) ) {
				QModelIndex	iTextureSet = insertNiBlock( "BSShaderTextureSet" );
				QModelIndex	iShaderPropertyData = nif->getIndex( iShaderProperty, "Shader Property Data" );
				if ( !iShaderPropertyData.isValid() )
					iShaderPropertyData = iShaderProperty;
				nif->setLink( iShaderPropertyData, "Texture Set", qint32( nif->getBlockNumber(iTextureSet) ) );
				quint32	numTextures = ( bsVersion < 130 ? 9 : ( bsVersion < 151 ? 10 : 15 ) );
				nif->set<quint32>( iTextureSet, "Num Textures", numTextures );
				nif->updateArraySize( nif->getIndex( iTextureSet, "Textures" ) );
			}

			if ( tangentsNeeded ) {
				spTangentSpace	sp;
				sp.cast( nif, iBlock );
			}
			{
				spUpdateBounds	sp;
				sp.cast( nif, iBlock );
			}
		} else {
			for ( int i : node.children )
				loadNode( iBlock, i, false );
		}
	} while ( ++p < primCnt );
}

void ImportGltf::importModel( const QPersistentModelIndex & iBlock )
{
	nodeMap.assign( model.nodes.size(), -1 );
	nif->setState( BaseModel::Loading );
	if ( model.scenes.empty() ) {
		loadNode( iBlock, 0, true );
	} else {
		for ( const auto & i : model.scenes ) {
			for ( int j : i.nodes )
				loadNode( iBlock, j, true );
		}
	}
	nif->restoreState();
	nif->updateModel();
}

static bool dummyImageLoadFunction(
	[[maybe_unused]] tinygltf::Image * image, [[maybe_unused]] const int image_idx, [[maybe_unused]] std::string * err,
	[[maybe_unused]] std::string * warn, [[maybe_unused]] int req_width, [[maybe_unused]] int req_height,
	[[maybe_unused]] const unsigned char * bytes, [[maybe_unused]] int size, [[maybe_unused]] void * data )
{
	return true;
}

void importGltf( NifModel * nif, const QModelIndex & index )
{
	if ( nif->getBSVersion() < 100 || ( index.isValid() && !nif->blockInherits( index, "NiNode" ) ) ) {
		QMessageBox::critical( nullptr, "NifSkope error", tr( "glTF import requires selecting a NiNode" ) );
		return;
	}

	QString filename = QFileDialog::getOpenFileName( qApp->activeWindow(), tr("Choose a .glTF file for import"), getGltfFolder(nif), "glTF (*.gltf)" );
	if ( filename.isEmpty() ) {
		return;
	} else if ( nif->getBSVersion() < 170 ) {
		gltfEnableLOD = false;
	} else {
		QSettings	settings;
		gltfEnableLOD = settings.value( "Settings/Nif/Enable LOD", false ).toBool();
	}

	tinygltf::TinyGLTF	reader;
	tinygltf::Model	model;
	std::string	gltfErr;
	std::string	gltfWarn;
	reader.SetImageLoader( dummyImageLoadFunction, nullptr );
	if ( !reader.LoadASCIIFromFile( &model, &gltfErr, &gltfWarn, filename.toStdString() ) ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error importing glTF file: %1").arg(gltfErr.c_str()) );
		return;
	}
	if ( !gltfWarn.empty() )
		QMessageBox::warning( nullptr, "NifSkope warning", QString("glTF import warning: %1").arg(gltfWarn.c_str()) );

	ImportGltf( nif, model, gltfEnableLOD ).importModel( index );
	NifSkope *	w = dynamic_cast< NifSkope * >( nif->getWindow() );
	if ( w )
		w->on_aViewCenter_triggered();
}
