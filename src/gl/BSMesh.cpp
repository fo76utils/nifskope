#include "BSMesh.h"
#include "message.h"
#include "gl/controllers.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/nifstream.h"
#include "model/nifmodel.h"
#include "glview.h"

#include <QDir>
#include <QBuffer>


BSMesh::BSMesh(Scene* s, const QModelIndex& iBlock) : Shape(s, iBlock)
{
}

void BSMesh::transformShapes()
{
	if ( isHidden() )
		return;

	if ( !( isSkinned && scene->hasOption(Scene::DoSkinning) ) ) [[likely]] {
		transformRigid = true;
		return;
	}

	updateBoneTransforms();
}

void BSMesh::drawShapes( NodeList * secondPass )
{
	if ( isHidden() || ( !scene->hasOption(Scene::ShowMarkers) && name.contains(QLatin1String("EditorMarker")) ) )
		return;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add(this);
		return;
	}

	auto nif = scene->nifModel;
	if ( !nif )
		return;
	if ( lodLevel != scene->lodLevel ) {
		lodLevel = scene->lodLevel;
		updateData(nif);
	}

	if ( !bindShape() )
		return;

	int	selectionFlags = scene->selecting;
	if ( ( selectionFlags & int(Scene::SelVertex) ) && drawInSecondPass ) [[unlikely]] {
		glDisable( GL_FRAMEBUFFER_SRGB );
		drawVerts();
		return;
	}

	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	auto	context = scene->renderer;

	if ( !selectionFlags ) [[likely]] {
		glEnable( GL_FRAMEBUFFER_SRGB );
		shader = context->setupProgram( this, shader );

	} else if ( auto prog = context->useProgram( "selection.prog" ); prog ) {
		glDisable( GL_FRAMEBUFFER_SRGB );

		setUniforms( prog );
		prog->uni1i( "selectionFlags", selectionFlags & 5 );
		prog->uni1i( "selectionParam", ( !( selectionFlags & int(Scene::SelVertex) ) ? nodeId : -1 ) );
	}

	context->fn->glDrawElements( GL_TRIANGLES, GLsizei( triangles.size() * 3 ), GL_UNSIGNED_SHORT, (void *) 0 );

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( selectionFlags & int( Scene::SelVertex ) ) [[unlikely]]
		drawVerts();
}

void BSMesh::drawSelection() const
{
	const auto &	blk = scene->currentBlock;
	if ( !scene->isSelModeVertex() ) {
		if ( scene->hasOption(Scene::ShowNodes) )
			Node::drawSelection();

		if ( !( scene->isSelModeObject() && blk.isValid() && ( blk == iBlock || blk == iSkinData ) ) )
			return;
	}

	auto &	idx = scene->currentIndex;
	auto	nif = scene->nifModel;
	auto	context = scene->renderer;
	if ( isHidden() || !( nif && context && bindShape() ) )
		return;

	glDepthFunc( GL_LEQUAL );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDisable( GL_CULL_FACE );
	glDisable( GL_FRAMEBUFFER_SRGB );

	if ( scene->isSelModeVertex() ) {
		drawVerts();
		return;
	}

	glEnable( GL_BLEND );
	context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	glEnable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_POLYGON_OFFSET_LINE );
	glEnable( GL_POLYGON_OFFSET_POINT );
	glPolygonOffset( -1.0f, -2.0f );

	// Name of this index
	auto n = idx.data( NifSkopeDisplayRole ).toString();
	// Name of this index's parent
	auto p = idx.parent().data( NifSkopeDisplayRole ).toString();

	qsizetype	numTriangles = triangles.size();

	if ( n == "Bounding Sphere" ) {
		auto sph = BoundSphere( nif, idx );
		if ( sph.radius > 0.0f )
			Shape::drawBoundingSphere( sph, FloatVector4( 1.0f, 1.0f, 1.0f, 0.33f ) );
	} else if ( n == "Bounding Box" ) {
		const NifItem *	boundsItem = nif->getItem( idx );
		Vector3	boundsCenter, boundsDims;
		if ( boundsItem ) {
			boundsCenter = nif->get<Vector3>( boundsItem->child( 0 ) );
			boundsDims = nif->get<Vector3>( boundsItem->child( 1 ) );
		}
		float	minVal = std::min( boundsDims[0], std::min( boundsDims[1], boundsDims[2] ) );
		float	maxVal = std::max( boundsDims[0], std::max( boundsDims[1], boundsDims[2] ) );
		if ( minVal > 0.0f && maxVal < 2.1e9f )
			Shape::drawBoundingBox( boundsCenter, boundsDims, FloatVector4( 1.0f, 1.0f, 1.0f, 0.33f ) );
	} else if ( n == "Vertices" || n == "UVs" || n == "UVs 2" || n == "Vertex Colors" || n == "Weights" ) {
		int	s = -1;
		if ( n == p )
			s = idx.row();
		if ( n == "Weights" ) {
			if ( s >= 0 ) {
				int	weightsPerVertex = int( nif->get<quint32>(idx.parent().parent(), "Weights Per Vertex") );
				if ( weightsPerVertex > 1 )
					s /= weightsPerVertex;
			}
			Shape::drawWeights( s );
		} else {
			Shape::drawVerts( GLView::Settings::vertexPointSize, s );
		}
	} else if ( n == "Normals" || n == "Tangents" ) {
		int	btnMask = ( n == "Normals" ? 0x04 : 0x03 );
		int	s = -1;
		if ( n == p )
			s = idx.row();
		Shape::drawVerts( GLView::Settings::tbnPointSize, s );
		float	normalScale = std::max< float >( boundSphere.radius / 16.0f, 2.5f / 512.0f ) * viewTrans().scale;
		Shape::drawNormals( btnMask, s, normalScale );
	} else if ( n == "Skin" ) {
		auto	iSkin = nif->getBlockIndex( nif->getLink( idx.parent(), "Skin" ) );
		if ( iSkin.isValid() && nif->isNiBlock( iSkin, "BSSkin::Instance" ) ) {
			auto	iBoneData = nif->getBlockIndex( nif->getLink( iSkin, "Data" ) );
			if ( iBoneData.isValid() && nif->isNiBlock( iBoneData, "BSSkin::BoneData" ) ) {
				auto	iBones = nif->getIndex( iBoneData, "Bone List" );
				int	numBones;
				if ( iBones.isValid() && nif->isArray( iBones ) && ( numBones = nif->rowCount( iBones ) ) > 0 ) {
					for ( int i = 0; i < numBones; i++ ) {
						if ( auto iBone = nif->getIndex( iBones, i ); iBone.isValid() )
							boneSphere( nif, iBone );
					}
				}
			}
		}
	} else if ( n == "BSSkin::BoneData" ) {
		// Draw all bones' bounding spheres

		// Get shape block
		if ( nif->getBlockIndex( nif->getParent( nif->getParent( blk ) ) ) == iBlock ) {
			auto iBones = nif->getIndex( blk, "Bone List" );
			int ct = nif->rowCount( iBones );

			for ( int i = 0; i < ct; i++ ) {
				auto b = nif->getIndex( iBones, i );
				boneSphere( nif, b );
			}
		}
	} else if ( n == "Bone List" ) {
		// Draw bone bounding sphere
		if ( nif->isArray( idx ) ) {
			for ( int i = 0; i < nif->rowCount( idx ); i++ )
				boneSphere( nif, nif->getIndex( idx, i ) );
		} else {
			boneSphere( nif, idx );
		}
	} else {
		int	s = -1;
		if ( n == p )
			s = idx.row();

		QModelIndex	iMeshlets;
		if ( s < 0 && n == "Meshlets" && ( iMeshlets = nif->getIndex( idx.parent(), "Meshlets" ) ).isValid() ) {
			// draw all meshlets
			qsizetype	triangleOffset = 0;
			qsizetype	triangleCount = 0;
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			int	numMeshlets = nif->rowCount( iMeshlets );
			for ( int i = 0; i < numMeshlets && triangleOffset < numTriangles; i++ ) {
				triangleCount = nif->get<quint32>( nif->getIndex( iMeshlets, i ), "Triangle Count" );
				triangleCount = std::min< qsizetype >( triangleCount, numTriangles - triangleOffset );
				if ( triangleCount < 1 )
					continue;

				// generate meshlet color from index
				std::uint32_t	j = std::uint32_t( i );
				j = ( j & 0x1249U ) | ( ( j & 0x2492U ) << 7 ) | ( ( j & 0x4924U ) << 14 );
				j = ( ( j & 0x00010101U ) << 7 ) | ( ( j & 0x00080808U ) << 3 )
					| ( ( j & 0x00404040U ) >> 1 ) | ( ( j & 0x02020200U ) >> 5 ) | ( ( j & 0x10101000U ) >> 9 );
				j = ~j;
				Shape::drawTriangles( triangleOffset, triangleCount, FloatVector4( j ) / 255.0f );
				triangleOffset += triangleCount;
			}
			Shape::drawWireframe( FloatVector4( 0.125f ).blendValues( scene->wireframeColor, 0x07 ) );
			glDepthFunc( GL_LEQUAL );
		} else {
			// General wireframe
			Shape::drawWireframe( scene->wireframeColor );
		}

		if ( s >= 0 && ( n == "Triangles" || n == "Meshlets" ) ) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			glDepthFunc( GL_ALWAYS );

			if ( n == "Triangles" ) {
				// draw selected triangle
				Shape::drawTriangles( s, 1, scene->highlightColor );
			} else if ( ( iMeshlets = nif->getIndex( idx.parent().parent(), "Meshlets" ) ).isValid() ) {
				// draw selected meshlet
				qsizetype	triangleOffset = 0;
				qsizetype	triangleCount = 0;
				for ( int i = 0; i <= s; i++ ) {
					triangleOffset += triangleCount;
					triangleCount = nif->get<quint32>( nif->getIndex( iMeshlets, i ), "Triangle Count" );
				}
				Shape::drawTriangles( triangleOffset, triangleCount, scene->highlightColor );
			}
		}
	}

	glDisable( GL_POLYGON_OFFSET_FILL );
	glDisable( GL_POLYGON_OFFSET_LINE );
	glDisable( GL_POLYGON_OFFSET_POINT );

#if 0 && !defined(QT_NO_DEBUG)
	drawSphereSimple(boundSphere.center, boundSphere.radius, 72);
#endif
}

BoundSphere BSMesh::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		if ( verts.size() ) {
			boundSphere = BoundSphere( verts );
		} else {
			boundSphere = dataBound;
		}
	}

	return worldTrans() * boundSphere;
}

QString BSMesh::textStats() const
{
	return QString();
}

int BSMesh::meshCount()
{
	return meshes.size();
}

void BSMesh::drawVerts() const
{
	int	vertexSelected = -1;

	if ( !scene->selecting && scene->currentBlock == iBlock ) {
		for ( const auto & idx = scene->currentIndex; idx.isValid(); ) {
			// Name of this index
			auto n = idx.data( NifSkopeDisplayRole ).toString();
			if ( !( n == "Vertices" || n == "UVs" || n == "UVs 2" || n == "Vertex Colors"
					|| n == "Normals" || n == "Tangents" || n == "Weights" ) ) {
				break;
			}
			// Name of this index's parent
			auto p = idx.parent().data( NifSkopeDisplayRole ).toString();
			if ( n == p ) {
				vertexSelected = idx.row();
				if ( n == "Weights" ) {
					auto	nif = NifModel::fromValidIndex( idx );
					int	weightsPerVertex = 0;
					if ( nif )
						weightsPerVertex = nif->get<int>( idx.parent().parent(), "Weights Per Vertex" );
					if ( weightsPerVertex > 0 )
						vertexSelected /= weightsPerVertex;
					else
						vertexSelected = -1;
				}
			}
			break;
		}
	}

	Shape::drawVerts( GLView::Settings::vertexSelectPointSize, vertexSelected );
}

QModelIndex BSMesh::getMeshDataIndex() const
{
	for ( QModelIndex iMeshData = iBlock; iMeshData.isValid(); ) {
		auto	nif = scene->nifModel;
		if ( !( nif && nif->blockInherits( iMeshData, "BSGeometry" ) ) )
			break;
		iMeshData = nif->getIndex( iMeshData, "Meshes" );
		if ( !( iMeshData.isValid() && nif->isArray( iMeshData ) ) )
			break;
		int	l = 0;
		if ( gpuLODs.isEmpty() )
			l = int( lodLevel );
		iMeshData = nif->getIndex( iMeshData, l );
		if ( !iMeshData.isValid() )
			break;
		iMeshData = nif->getIndex( iMeshData, "Mesh" );
		if ( iMeshData.isValid() )
			return nif->getIndex( iMeshData, "Mesh Data" );
	}
	return QModelIndex();
}

QModelIndex BSMesh::vertexAt( int c ) const
{
	auto	nif = scene->nifModel;
	if ( !( c >= 0 && c < verts.size() && nif ) )
		return QModelIndex();

	QModelIndex	iMeshData = getMeshDataIndex();
	if ( !iMeshData.isValid() )
		return QModelIndex();
	QModelIndex	iVerts;
	const auto &	idx = scene->currentIndex;
	if ( idx.isValid() ) {
		auto	n = idx.data( NifSkopeDisplayRole ).toString();
		if ( n == "UVs" )
			iVerts = nif->getIndex( iMeshData, "UVs" );
		else if ( n == "UVs 2" )
			iVerts = nif->getIndex( iMeshData, "UVs 2" );
		else if ( n == "Vertex Colors" )
			iVerts = nif->getIndex( iMeshData, "Vertex Colors" );
		else if ( n == "Normals" )
			iVerts = nif->getIndex( iMeshData, "Normals" );
		else if ( n == "Tangents" )
			iVerts = nif->getIndex( iMeshData, "Tangents" );
		else if ( n == "Weights" )
			iVerts = nif->getIndex( iMeshData, "Weights" );
	}
	if ( !iVerts.isValid() )
		iVerts = nif->getIndex( iMeshData, "Vertices" );
	int	n;
	if ( iVerts.isValid() && nif->isArray( iVerts ) && ( n = nif->rowCount( iVerts ) ) > 0 )
		return nif->getIndex( iVerts, int( std::int64_t( c ) * n / verts.size() ) );
	return QModelIndex();
}

QModelIndex BSMesh::triangleAt( int c ) const
{
	auto	nif = scene->nifModel;
	if ( !( c >= 0 && c < triangles.size() && nif ) )
		return QModelIndex();

	QModelIndex	iMeshData = getMeshDataIndex();
	if ( !iMeshData.isValid() )
		return QModelIndex();
	QModelIndex	iTriangles = iMeshData;
	quint32	l = lodLevel;
	quint32	n;
	if ( l && ( n = nif->get<quint32>( iMeshData, "Num LODs" ) ) != 0 ) {
		l = std::min( l, n );
		iTriangles = nif->getIndex( nif->getIndex( iMeshData, "LODs" ), int( l - 1U ) );
	}
	if ( iTriangles.isValid() )
		iTriangles = nif->getIndex( iTriangles, "Triangles" );
	return nif->getIndex( iTriangles, c );
}

void BSMesh::updateImpl(const NifModel* nif, const QModelIndex& index)
{
	qDebug() << "updateImpl";
	Shape::updateImpl(nif, index);
	if ( index != iBlock )
		return;

	iData = index;
	auto iMeshes = nif->getIndex(index, "Meshes");
	meshes.clear();
	for ( int i = 0; i < 4; i++ ) {
		auto meshArray = nif->getIndex( iMeshes, i );
		bool hasMesh = nif->get<bool>( nif->getIndex( meshArray, 0 ) );
		if ( hasMesh ) {
			auto mesh = std::make_shared<MeshFile>( nif, nif->getIndex( meshArray, 1 ) );
			if ( mesh->isValid() ) {
				meshes.append(mesh);
				if ( i > 0 || mesh->lods.size() > 0 )
					emit nif->lodSliderChanged(true);
			}
		}
	}
}

void BSMesh::updateData(const NifModel* nif)
{
	qDebug() << "updateData";
	clearHash();
	resetSkinning();
	resetVertexData();
	resetSkeletonData();
	gpuLODs.clear();

	if ( meshes.size() == 0 )
		return;

	bool hasMeshLODs = meshes[0]->lods.size() > 0;
	int lodCount = (hasMeshLODs) ? meshes[0]->lods.size() + 1 : meshes.size();

	if ( hasMeshLODs && meshes.size() > 1 ) {
		qWarning() << "Both static and skeletal mesh LODs exist";
	}

	lodLevel = std::min(scene->lodLevel, Scene::LodLevel(lodCount - 1));

	const std::uint32_t *	weights = nullptr;
	int	weightsPerVertex = 0;
	int	numWeights = 0;
	auto meshIndex = (hasMeshLODs) ? 0 : lodLevel;
	if ( lodCount > int(lodLevel) ) {
		auto& mesh = meshes[meshIndex];
		if ( lodLevel > 0 && int(lodLevel) <= mesh->lods.size() ) {
			triangles = mesh->lods[lodLevel - 1];
		}
		else {
			triangles = mesh->triangles;
		}
		lodTriangleCount = triangles.size();
		verts = mesh->positions;
		removeInvalidIndices();
		coords.resize( mesh->coords2.isEmpty() ? 1 : 2 );
		coords.first() = mesh->coords1;
		if ( !mesh->coords2.isEmpty() )
			coords[1] = mesh->coords2;
		colors = mesh->colors;
		hasVertexColors = !colors.empty();
		norms = mesh->normals;
		bitangents = mesh->tangents;
		mesh->calculateBitangents( tangents );
		boneWeights0.clear();
		boneWeights1.clear();
		if ( mesh->weightsPerVertex > 0 ) {
			weightsPerVertex = mesh->weightsPerVertex;
			numWeights = int( mesh->weights.size() / mesh->weightsPerVertex );
		}
		if ( numWeights > 0 )
			weights = mesh->weights.constData();
		gpuLODs = mesh->lods;

		boundSphere = BoundSphere( verts );
		boundSphere.applyInv( viewTrans() );
	}

	if ( int link = nif->getLink( iBlock, "Skin" ); link >= 0 ) {
		if ( auto idx = nif->getBlockIndex( link ); nif->blockInherits( idx, "BSSkin::Instance" ) ) {
			iSkin = idx;
			iSkinData = nif->getBlockIndex( nif->getLink( nif->getIndex(idx, "Data") ) );

			qsizetype numBones = nif->get<int>( iSkinData, "Num Bones" );
			boneData.fill( BoneData(), numBones );

			bones = nif->getLinkArray( iSkin, "Bones" );
			for ( qsizetype i = 0; i < bones.size(); i++ ) {
				int b = bones.at( i );
				if ( i < numBones )
					boneData[i].bone = b;
			}
			isSkinned = true;

			auto iBoneList = nif->getIndex( iSkinData, "Bone List" );
			for ( int i = 0; i < numBones; i++ )
				boneData[i].setTransform( nif, nif->getIndex( iBoneList, i ) );

			if ( weights ) {
				size_t	n = size_t( numWeights );
				boneWeights0.assign( n, FloatVector4( 0.0f ) );
				for ( size_t i = 0; i < n; i++ ) {
					size_t	k = 0;
					for ( int j = 0; j < weightsPerVertex; j++, weights++ ) {
						std::uint32_t	bw = *weights;
						unsigned int	b = bw >> 16;
						unsigned int	w = bw & 0xFFFFU;
						if ( b < (unsigned int) numBones && b < 256U && w > ( !b ? 3U : 0U ) ) {
							float	tmp = float( int(bw) ) * float( 1.0 / 65536.0 );
							if ( k < 4 ) {
								boneWeights0[i][k] = tmp;
							} else if ( k < 8 ) {
								if ( boneWeights1.size() < n ) [[unlikely]]
									boneWeights1.assign( n, FloatVector4( 0.0f ) );
								boneWeights1[i][k & 3] = tmp;
							}
							k++;
						}
					}
				}
			}
		}
	}
}
