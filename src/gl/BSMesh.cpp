#include "BSMesh.h"
#include "message.h"
#include "gl/controllers.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/nifstream.h"
#include "model/nifmodel.h"
#include "qtcompat.h"
#include "glview.h"

#include <QDir>
#include <QBuffer>


BSMesh::BSMesh(Scene* s, const QModelIndex& iBlock) : Shape(s, iBlock)
{
}

void BSMesh::transformShapes()
{
	// TODO: implement this
#if 0
	if ( isHidden() )
		return;
#endif
}

void BSMesh::drawShapes( NodeList * secondPass )
{
	if ( isHidden() || ( !scene->hasOption(Scene::ShowMarkers) && name.contains("EditorMarker") ) )
		return;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add(this);
		return;
	}

	auto nif = NifModel::fromIndex(iBlock);
	if ( lodLevel != scene->lodLevel ) {
		lodLevel = scene->lodLevel;
		updateData(nif);
	}

	glPushMatrix();
	glMultMatrix(viewTrans());

	glEnable(GL_POLYGON_OFFSET_FILL);
	if ( drawInSecondPass )
		glPolygonOffset(0.5f, 1.0f);
	else
		glPolygonOffset(1.0f, 2.0f);


	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, transVerts.constData());

	if ( Node::SELECTING ) {
		if ( scene->isSelModeObject() ) {
			int s_nodeId = ID2COLORKEY(nodeId);
			glColor4ubv((GLubyte*)&s_nodeId);
		} else {
			glColor4f(0, 0, 0, 1);
		}
	}

	if ( !Node::SELECTING ) {
		glEnable(GL_FRAMEBUFFER_SRGB);
		shader = scene->renderer->setupProgram(this, shader);

	} else {
		glDisable(GL_FRAMEBUFFER_SRGB);
	}

	if ( !Node::SELECTING ) {
		if ( transNorms.count() ) {
			glEnableClientState(GL_NORMAL_ARRAY);
			glNormalPointer(GL_FLOAT, 0, transNorms.constData());
		}

		if ( transColors.count() && scene->hasOption(Scene::DoVertexColors) ) {
			glEnableClientState(GL_COLOR_ARRAY);
			glColorPointer(4, GL_FLOAT, 0, transColors.constData());
		} else {
			glColor(Color3(1.0f, 1.0f, 1.0f));
		}
	}

	if ( sortedTriangles.count() )
		glDrawElements(GL_TRIANGLES, sortedTriangles.count() * 3, GL_UNSIGNED_SHORT, sortedTriangles.constData());

	if ( !Node::SELECTING )
		scene->renderer->stopProgram();

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	glDisable(GL_POLYGON_OFFSET_FILL);

	if ( scene->isSelModeVertex() )
		drawVerts();

	glPopMatrix();
}

void BSMesh::drawSelection() const
{
	if ( scene->hasOption(Scene::ShowNodes) )
		Node::drawSelection();

	auto& blk = scene->currentBlock;

	if ( isHidden() || !( scene->isSelModeObject() && blk == iBlock ) )
		return;

	auto& idx = scene->currentIndex;
	auto nif = NifModel::fromValidIndex(blk);

	glDisable(GL_LIGHTING);
	glDisable(GL_COLOR_MATERIAL);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_NORMALIZE);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);

	glDisable(GL_FRAMEBUFFER_SRGB);
	glPushMatrix();
	glMultMatrix(viewTrans());

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -2.0f);

	glPointSize( GLView::Settings::vertexPointSize );
	glLineWidth( GLView::Settings::lineWidthWireframe );
	glNormalColor();

	// Name of this index
	auto n = idx.data( NifSkopeDisplayRole ).toString();
	// Name of this index's parent
	auto p = idx.parent().data( NifSkopeDisplayRole ).toString();

	float	normalScale = std::max< float >( boundSphere.radius / 20.0f, 1.0f / 512.0f );

	// Draw All Verts lambda
	auto allv = [this]( float size ) {
		glPointSize( size );
		glBegin( GL_POINTS );

		for ( int j = 0; j < transVerts.count(); j++ )
			glVertex( transVerts.value( j ) );

		glEnd();
	};

	// Draw Lines lambda
	auto lines = [this, &normalScale, &allv]( const QVector<Vector3> & v, int s, bool isBitangent = false ) {
		glNormalColor();
		if ( !isBitangent ) {
			allv( GLView::Settings::tbnPointSize );

			glLineWidth( GLView::Settings::lineWidthWireframe * 0.78125f );
			glBegin( GL_LINES );
			for ( int j = 0; j < transVerts.count() && j < v.count(); j++ ) {
				glVertex( transVerts.value( j ) );
				glVertex( transVerts.value( j ) + v.value( j ) * normalScale );
				glVertex( transVerts.value( j ) );
				glVertex( transVerts.value( j ) - v.value( j ) * normalScale * 0.25f );
			}
			glEnd();
		}

		if ( s >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			if ( isBitangent ) {
				Color4	c( cfg.highlight );
				glColor4f( 1.0f - c[0], 1.0f - c[1], 1.0f - c[2], c[3] );
			} else {
				glHighlightColor();
			}
			glLineWidth( GLView::Settings::lineWidthHighlight * 1.2f );
			glBegin( GL_LINES );
			glVertex( transVerts.value( s ) );
			glVertex( transVerts.value( s ) + v.value( s ) * normalScale * 2.0f );
			glVertex( transVerts.value( s ) );
			glVertex( transVerts.value( s ) - v.value( s ) * normalScale * 0.5f );
			glEnd();
		}
		glLineWidth( GLView::Settings::lineWidthWireframe );
	};

	if ( n == "Bounding Sphere" ) {
		auto sph = BoundSphere( nif, idx );
		if ( sph.radius > 0.0f ) {
			glColor4f( 1, 1, 1, 0.33f );
			drawSphereSimple( sph.center, sph.radius, 72 );
		}
	} else if ( n == "Bounding Box" ) {
		const NifItem *	boundsItem = nif->getItem( idx );
		Vector3	boundsCenter, boundsDims;
		if ( boundsItem ) {
			boundsCenter = nif->get<Vector3>( boundsItem->child( 0 ) );
			boundsDims = nif->get<Vector3>( boundsItem->child( 1 ) );
		}
		float	minVal = std::min( boundsDims[0], std::min( boundsDims[1], boundsDims[2] ) );
		float	maxVal = std::max( boundsDims[0], std::max( boundsDims[1], boundsDims[2] ) );
		if ( minVal > 0.0f && maxVal < 2.1e9f ) {
			glColor4f( 1, 1, 1, 0.33f );
			drawBox( boundsCenter - boundsDims, boundsCenter + boundsDims );
		}
	} else if ( n == "Vertices" || n == "UVs" || n == "UVs 2" || n == "Vertex Colors" || n == "Weights" ) {
		allv( GLView::Settings::vertexPointSize );

		int	s;
		if ( n == p && ( s = idx.row() ) >= 0 ) {
			if ( n == "Weights" ) {
				int	weightsPerVertex = int( nif->get<quint32>(idx.parent().parent(), "Weights Per Vertex") );
				if ( weightsPerVertex > 1 )
					s /= weightsPerVertex;
			}
			glPointSize( GLView::Settings::vertexPointSizeSelected );
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_POINTS );
			glVertex( transVerts.value( s ) );
			glEnd();
		}
	} else if ( n == "Normals" ) {
		int	s = -1;
		if ( n == p )
			s = idx.row();
		lines( transNorms, s );
	} else if ( n == "Tangents" ) {
		int	s = -1;
		if ( n == p )
			s = idx.row();
		lines( transBitangents, s );
		lines( transTangents, s, true );
	} else if ( n == "Skin" ) {
		auto	iSkin = nif->getBlockIndex( nif->getLink( idx.parent(), "Skin" ) );
		if ( iSkin.isValid() && nif->isNiBlock( iSkin, "BSSkin::Instance" ) ) {
			auto	iBoneData = nif->getBlockIndex( nif->getLink( iSkin, "Data" ) );
			if ( iBoneData.isValid() && nif->isNiBlock( iBoneData, "BSSkin::BoneData" ) ) {
				auto	iBones = nif->getIndex( iBoneData, "Bone List" );
				int	numBones;
				if ( iBones.isValid() && nif->isArray( iBones ) && ( numBones = nif->rowCount( iBones ) ) > 0 ) {
					for ( int i = 0; i < numBones; i++ ) {
						auto	iBone = QModelIndex_child( iBones, i );
						if ( !iBone.isValid() )
							continue;
						BoundSphere	sph( nif, iBone );
						if ( !( sph.radius > 0.0f ) )
							continue;
						Transform	t( nif, iBone );
						sph.center -= t.translation;
						t.rotation = t.rotation.inverted();
						t.translation = Vector3( 0.0f, 0.0f, 0.0f );
						t.scale = 1.0f / t.scale;
						sph.radius *= t.scale;
						sph.center = t * sph.center;
						glColor4f( 1, 1, 1, 0.33f );
						drawSphereSimple( sph.center, sph.radius, 72 );
					}
				}
			}
		}
	} else {
		int	s = -1;
		if ( n == p )
			s = idx.row();

		QModelIndex	iMeshlets;
		if ( s < 0 && n == "Meshlets" && ( iMeshlets = nif->getIndex( idx.parent(), "Meshlets" ) ).isValid() ) {
			// draw all meshlets
			quint32	triangleOffset = 0;
			quint32	triangleCount = 0;
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			int	numMeshlets = nif->rowCount( iMeshlets );
			for ( int i = 0; i < numMeshlets && triangleOffset < quint32(sortedTriangles.size()); i++ ) {
				triangleCount = nif->get<quint32>( QModelIndex_child( iMeshlets, i ), "Triangle Count" );
				std::uint32_t	j = std::uint32_t(i);
				j = ( ( j & 0x0001U ) << 7 ) | ( ( j & 0x0008U ) << 3 ) | ( ( j & 0x0040U ) >> 1 )
					| ( ( j & 0x0200U ) >> 5 ) | ( ( j & 0x1000U ) >> 9 )
					| ( ( j & 0x0002U ) << 14 ) | ( ( j & 0x0010U ) << 10 ) | ( ( j & 0x0080U ) << 6 )
					| ( ( j & 0x0400U ) << 2 ) | ( ( j & 0x2000U ) >> 2 )
					| ( ( j & 0x0004U ) << 21 ) | ( ( j & 0x0020U ) << 17 ) | ( ( j & 0x0100U ) << 13 )
					| ( ( j & 0x0800U ) << 9 ) | ( ( j & 0x4000U ) << 5 );
				j = ~j;
				FloatVector4	meshletColor( j );
				meshletColor /= 255.0f;
				glColor4f( meshletColor[0], meshletColor[1], meshletColor[2], meshletColor[3] );
				glBegin( GL_TRIANGLES );
				for ( ; triangleCount && triangleOffset < quint32(sortedTriangles.size()); triangleCount-- ) {
					Triangle tri = sortedTriangles.value( qsizetype(triangleOffset) );
					glVertex( transVerts.value(tri.v1()) );
					glVertex( transVerts.value(tri.v2()) );
					glVertex( transVerts.value(tri.v3()) );
					triangleOffset++;
				}
				glEnd();
			}
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
			Color4	wireframeColor( cfg.wireframe );
			glColor4f( wireframeColor[0], wireframeColor[1], wireframeColor[2], 0.125f );
			glDepthFunc( GL_LEQUAL );
		}

		// General wireframe
		for ( const Triangle& tri : sortedTriangles ) {
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value(tri.v1()) );
			glVertex( transVerts.value(tri.v2()) );
			glVertex( transVerts.value(tri.v3()) );
			glEnd();
		}

		if ( s >= 0 && ( n == "Triangles" || n == "Meshlets" ) ) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			glHighlightColor();
			glDepthFunc( GL_ALWAYS );

			glBegin( GL_TRIANGLES );
			if ( n == "Triangles" ) {
				// draw selected triangle
				Triangle tri = sortedTriangles.value( s );
				glVertex( transVerts.value(tri.v1()) );
				glVertex( transVerts.value(tri.v2()) );
				glVertex( transVerts.value(tri.v3()) );
			} else if ( ( iMeshlets = nif->getIndex( idx.parent().parent(), "Meshlets" ) ).isValid() ) {
				// draw selected meshlet
				quint32	triangleOffset = 0;
				quint32	triangleCount = 0;
				for ( int i = 0; i <= s; i++ ) {
					triangleOffset += triangleCount;
					triangleCount = nif->get<quint32>( QModelIndex_child( iMeshlets, i ), "Triangle Count" );
				}
				for ( ; triangleCount && triangleOffset < quint32(sortedTriangles.size()); triangleCount-- ) {
					Triangle tri = sortedTriangles.value( qsizetype(triangleOffset) );
					glVertex( transVerts.value(tri.v1()) );
					glVertex( transVerts.value(tri.v2()) );
					glVertex( transVerts.value(tri.v3()) );
					triangleOffset++;
				}
			}
			glEnd();
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
	}

	glDisable(GL_POLYGON_OFFSET_FILL);

#if 0 && !defined(QT_NO_DEBUG)
	drawSphereSimple(boundSphere.center, boundSphere.radius, 72);
#endif

	glPopMatrix();
}

BoundSphere BSMesh::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		if ( transVerts.count() ) {
			boundSphere = BoundSphere(transVerts);
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
	glDisable( GL_LIGHTING );
	glNormalColor();

	glPointSize( GLView::Settings::vertexSelectPointSize );
	glBegin( GL_POINTS );
	for ( int i = 0; i < transVerts.count(); i++ ) {
		if ( Node::SELECTING ) {
			GLubyte	id[4];
			FileBuffer::writeUInt32Fast( id, std::uint32_t( ID2COLORKEY( (shapeNumber << 16) + i ) ) );
			glColor4ubv( id );
		}
		glVertex( transVerts.value(i) );
	}
	glEnd();

	if ( Node::SELECTING || !( scene->currentBlock == iBlock ) )
		return;

	int	vertexSelected = -1;

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
				int	weightsPerVertex;
				if ( nif && ( weightsPerVertex = nif->get<int>( idx.parent().parent(), "Weights Per Vertex" ) ) > 0 )
					vertexSelected /= weightsPerVertex;
				else
					vertexSelected = -1;
			}
		}
		break;
	}

	if ( vertexSelected >= 0 && vertexSelected < transVerts.count() ) {
		glHighlightColor();
		glPointSize( GLView::Settings::vertexPointSizeSelected );
		glBegin( GL_POINTS );
		glVertex( transVerts.value(vertexSelected) );
		glEnd();
	}
}

QModelIndex BSMesh::vertexAt( int c ) const
{
	if ( !( c >= 0 && c < transVerts.count() ) )
		return QModelIndex();

	QModelIndex	iMeshData = iBlock;
	if ( !iMeshData.isValid() )
		return QModelIndex();
	for ( auto nif = NifModel::fromValidIndex( iMeshData ); nif && nif->blockInherits( iMeshData, "BSGeometry" ); ) {
		iMeshData = nif->getIndex( iMeshData, "Meshes" );
		if ( !( iMeshData.isValid() && nif->isArray( iMeshData ) ) )
			break;
		int	l = 0;
		if ( gpuLODs.isEmpty() )
			l = int( lodLevel );
		iMeshData = QModelIndex_child( iMeshData, l );
		if ( !iMeshData.isValid() )
			break;
		iMeshData = nif->getIndex( iMeshData, "Mesh" );
		if ( !iMeshData.isValid() )
			break;
		iMeshData = nif->getIndex( iMeshData, "Mesh Data" );
		if ( !iMeshData.isValid() )
			break;
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
		if ( !( iVerts.isValid() && nif->isArray( iVerts ) ) )
			break;
		int	n = nif->rowCount( iVerts );
		if ( n <= 0 )
			break;
		return QModelIndex_child( iVerts, int( std::int64_t( c ) * n / transVerts.count() ) );
	}
	return QModelIndex();
}

void BSMesh::updateImpl(const NifModel* nif, const QModelIndex& index)
{
	qDebug() << "updateImpl";
	Shape::updateImpl(nif, index);
	if ( index != iBlock )
		return;

	iData = index;
	iMeshes = nif->getIndex(index, "Meshes");
	meshes.clear();
	for ( int i = 0; i < 4; i++ ) {
		auto meshArray = QModelIndex_child( iMeshes, i );
		bool hasMesh = nif->get<bool>( QModelIndex_child( meshArray ) );
		if ( hasMesh ) {
			auto mesh = std::make_shared<MeshFile>( nif, QModelIndex_child( meshArray, 1 ) );
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
	resetSkinning();
	resetVertexData();
	resetSkeletonData();
	gpuLODs.clear();
	boneNames.clear();
	boneTransforms.clear();

	if ( meshes.size() == 0 )
		return;

	bool hasMeshLODs = meshes[0]->lods.size() > 0;
	int lodCount = (hasMeshLODs) ? meshes[0]->lods.size() + 1 : meshes.size();

	if ( hasMeshLODs && meshes.size() > 1 ) {
		qWarning() << "Both static and skeletal mesh LODs exist";
	}

	lodLevel = std::min(scene->lodLevel, Scene::LodLevel(lodCount - 1));

	auto meshIndex = (hasMeshLODs) ? 0 : lodLevel;
	if ( lodCount > int(lodLevel) ) {
		auto& mesh = meshes[meshIndex];
		if ( lodLevel > 0 && int(lodLevel) <= mesh->lods.size() ) {
			sortedTriangles = mesh->lods[lodLevel - 1];
		}
		else {
			sortedTriangles = mesh->triangles;
		}
		transVerts = mesh->positions;
		coords.resize( mesh->haveTexCoord2 ? 2 : 1 );
		coords[0].resize( mesh->coords.size() );
		for ( int i = 0; i < mesh->coords.size(); i++ ) {
			coords[0][i][0] = mesh->coords[i][0];
			coords[0][i][1] = mesh->coords[i][1];
		}
		if ( mesh->haveTexCoord2 ) {
			coords[1].resize( mesh->coords.size() );
			for ( int i = 0; i < mesh->coords.size(); i++ ) {
				coords[1][i][0] = mesh->coords[i][2];
				coords[1][i][1] = mesh->coords[i][3];
			}
		}
		transColors = mesh->colors;
		hasVertexColors = !transColors.empty();
		transNorms = mesh->normals;
		transBitangents = mesh->tangents;
		mesh->calculateBitangents( transTangents );
		weightsUNORM = mesh->weights;
		gpuLODs = mesh->lods;

		boundSphere = BoundSphere(transVerts);
		boundSphere.applyInv(viewTrans());
	}

	auto links = nif->getChildLinks(nif->getBlockNumber(iBlock));
	for ( const auto link : links ) {
		auto idx = nif->getBlockIndex(link);
		if ( nif->blockInherits(idx, "BSSkin::Instance") ) {
			iSkin = idx;
			iSkinData = nif->getBlockIndex(nif->getLink(nif->getIndex(idx, "Data")));
			skinID = nif->getBlockNumber(iSkin);

			auto iBones = nif->getLinkArray(iSkin, "Bones");
			for ( const auto b : iBones ) {
				if ( b == -1 )
					continue;
				auto iBone = nif->getBlockIndex(b);
				boneNames.append(nif->resolveString(iBone, "Name"));
			}

			auto numBones = nif->get<int>(iSkinData, "Num Bones");
			boneTransforms.resize(numBones);
			auto iBoneList = nif->getIndex(iSkinData, "Bone List");
			for ( int i = 0; i < numBones; i++ ) {
				auto iBone = QModelIndex_child( iBoneList, i );
				Transform trans;
				trans.rotation = nif->get<Matrix>(iBone, "Rotation");
				trans.translation = nif->get<Vector3>(iBone, "Translation");
				trans.scale = nif->get<float>(iBone, "Scale");
				boneTransforms[i] = trans;
			}
		}
	}
	// Do after dependent blocks above
	for ( const auto link : links ) {
		auto idx = nif->getBlockIndex(link);
		if ( nif->blockInherits(idx, "SkinAttach") ) {
			boneNames = nif->getArray<QString>(idx, "Bones");
			if ( std::all_of(boneNames.begin(), boneNames.end(), [](const QString& name) { return name.isEmpty(); }) ) {
				boneNames.clear();
				auto iBones = nif->getLinkArray(nif->getIndex(iSkin, "Bones"));
				for ( const auto& b : iBones ) {
					auto iBone = nif->getBlockIndex(b);
					boneNames.append(nif->resolveString(iBone, "Name"));
				}
			}
		}
	}
}
