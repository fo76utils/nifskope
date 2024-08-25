#include "bsshape.h"

#include "gl/glnode.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/material.h"
#include "model/nifmodel.h"
#include "qtcompat.h"
#include "glview.h"

void BSShape::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Shape::updateImpl( nif, index );

	if ( index == iBlock ) {
		isLOD = nif->isNiBlock( iBlock, "BSMeshLODTriShape" );
		if ( isLOD )
			emit nif->lodSliderChanged(true);
	}
}

void BSShape::updateData( const NifModel * nif )
{
	auto vertexFlags = nif->get<BSVertexDesc>(iBlock, "Vertex Desc");

	isDynamic = nif->blockInherits(iBlock, "BSDynamicTriShape");

	hasVertexColors = vertexFlags.HasFlag(VertexAttribute::VA_COLOR);

	dataBound = BoundSphere(nif, iBlock);

	// Is the shape skinned?
	resetSkinning();
	if ( vertexFlags.HasFlag(VertexAttribute::VA_SKINNING) ) {
		isSkinned = true;

		QString skinInstName, skinDataName;
		if ( nif->getBSVersion() >= 130 ) {
			skinInstName = "BSSkin::Instance";
			skinDataName = "BSSkin::BoneData";
		} else {
			skinInstName = "NiSkinInstance";
			skinDataName = "NiSkinData";
		}

		iSkin = nif->getBlockIndex( nif->getLink( nif->getIndex( iBlock, "Skin" ) ), skinInstName );
		if ( iSkin.isValid() ) {
			iSkinData = nif->getBlockIndex( nif->getLink( iSkin, "Data" ), skinDataName );
			if ( nif->getBSVersion() == 100 )
				iSkinPart = nif->getBlockIndex( nif->getLink( iSkin, "Skin Partition" ), "NiSkinPartition" );
		}
	}

	// Fill vertex data
	resetVertexData();
	numVerts = 0;
	if ( isSkinned && iSkinPart.isValid() ) {
		// For skinned geometry, the vertex data is stored in the NiSkinPartition
		// The triangles are split up among the partitions
		iData = nif->getIndex( iSkinPart, "Vertex Data" );
		int dataSize = nif->get<int>( iSkinPart, "Data Size" );
		int vertexSize = nif->get<int>( iSkinPart, "Vertex Size" );
		if ( iData.isValid() && dataSize > 0 && vertexSize > 0 )
			numVerts = dataSize / vertexSize;
	} else {
		iData = nif->getIndex( iBlock, "Vertex Data" );
		if ( iData.isValid() )
			numVerts = nif->rowCount( iData );
	}

	TexCoords coordset; // For compatibility with coords list

	QVector<Vector4> dynVerts;
	if ( isDynamic ) {
		dynVerts = nif->getArray<Vector4>( iBlock, "Vertices" );
		int nDynVerts = dynVerts.count();
		if ( nDynVerts < numVerts )
			numVerts = nDynVerts;
	}

	for ( int i = 0; i < numVerts; i++ ) {
		auto idx = nif->index( i, 0, iData );
		float bitX;

		if ( isDynamic ) {
			auto& dynv = dynVerts.at(i);
			verts << Vector3( dynv );
			bitX = dynv[3];
		} else {
			verts << nif->get<Vector3>( idx, "Vertex" );
			bitX = nif->get<float>( idx, "Bitangent X" );
		}

		// Bitangent Y/Z
		auto bitY = nif->get<float>( idx, "Bitangent Y" );
		auto bitZ = nif->get<float>( idx, "Bitangent Z" );

		coordset << nif->get<HalfVector2>( idx, "UV" );
		norms += nif->get<ByteVector3>( idx, "Normal" );
		tangents += nif->get<ByteVector3>( idx, "Tangent" );
		bitangents += Vector3( bitX, bitY, bitZ );

		auto vcIdx = nif->getIndex( idx, "Vertex Colors" );
		colors += vcIdx.isValid() ? nif->get<ByteColor4>( vcIdx ) : Color4(0, 0, 0, 1);
	}

	// Add coords as the first set of QList
	coords.append( coordset );

	numVerts = verts.count();

	// Fill triangle data
	if ( isSkinned && iSkinPart.isValid() ) {
		auto iPartitions = nif->getIndex( iSkinPart, "Partitions" );
		if ( iPartitions.isValid() ) {
			int n = nif->rowCount( iPartitions );
			for ( int i = 0; i < n; i++ )
				triangles << nif->getArray<Triangle>( nif->index( i, 0, iPartitions ), "Triangles" );
		}
	} else {
		auto iTriData = nif->getIndex( iBlock, "Triangles" );
		if ( iTriData.isValid() )
			triangles = nif->getArray<Triangle>( iTriData );
	}
	// TODO (Gavrant): validate triangles' vertex indices, throw out triangles with the wrong ones

	// Fill skeleton data
	resetSkeletonData();
	if ( isSkinned && iSkin.isValid() ) {
		skeletonRoot = nif->getLink( iSkin, "Skeleton Root" );
		if ( nif->getBSVersion() < 130 )
			skeletonTrans = Transform( nif, iSkinData );

		bones = nif->getLinkArray( iSkin, "Bones" );
		auto nTotalBones = bones.count();

		weights.fill( BoneWeights(), nTotalBones );
		for ( int i = 0; i < nTotalBones; i++ )
			weights[i].bone = bones[i];
		auto nTotalWeights = weights.count();

		for ( int i = 0; i < numVerts; i++ ) {
			auto idx = nif->index( i, 0, iData );
			auto wts = nif->getArray<float>( idx, "Bone Weights" );
			auto bns = nif->getArray<quint8>( idx, "Bone Indices" );
			if ( wts.count() < 4 || bns.count() < 4 )
				continue;

			for ( int j = 0; j < 4; j++ ) {
				if ( bns[j] >= nTotalWeights )
					continue;

				if ( wts[j] > 0.0 )
					weights[bns[j]].weights << VertexWeight( i, wts[j] );
			}
		}

		auto b = nif->getIndex( iSkinData, "Bone List" );
		for ( int i = 0; i < nTotalWeights; i++ )
			weights[i].setTransform( nif, QModelIndex_child( b, i ) );
	}
}

QModelIndex BSShape::vertexAt( int idx ) const
{
	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif )
		return QModelIndex();

	// Vertices are on NiSkinPartition in version 100
	auto blk = iBlock;
	if ( iSkinPart.isValid() ) {
		if ( isDynamic )
			return QModelIndex_child( nif->getIndex( blk, "Vertices" ), idx );

		blk = iSkinPart;
	}

	return nif->getIndex( QModelIndex_child( nif->getIndex( blk, "Vertex Data" ), idx ), "Vertex" );
}

void BSShape::transformShapes()
{
	if ( isHidden() )
		return;

	auto nif = NifModel::fromValidIndex( iBlock );
	if ( !nif ) {
		clear();
		return;
	}

	Node::transformShapes();

	transformRigid = true;

	if ( isSkinned && weights.count() && scene->hasOption(Scene::DoSkinning) ) {
		transformRigid = false;

		transVerts.resize( numVerts );
		transVerts.fill( Vector3() );
		transNorms.resize( numVerts );
		transNorms.fill( Vector3() );
		transTangents.resize( numVerts );
		transTangents.fill( Vector3() );
		transBitangents.resize( numVerts );
		transBitangents.fill( Vector3() );

		Node * root = findParent( 0 );
		for ( const BoneWeights & bw : weights ) {
			Node * bone = root ? root->findChild( bw.bone ) : nullptr;
			if ( bone ) {
				Transform t = scene->view * bone->localTrans( 0 ) * bw.trans;
				for ( const VertexWeight & w : bw.weights ) {
					if ( w.vertex >= numVerts )
						continue;

					transVerts[w.vertex] += t * verts[w.vertex] * w.weight;
					transNorms[w.vertex] += t.rotation * norms[w.vertex] * w.weight;
					transTangents[w.vertex] += t.rotation * tangents[w.vertex] * w.weight;
					transBitangents[w.vertex] += t.rotation * bitangents[w.vertex] * w.weight;
				}
			}
		}

		for ( int n = 0; n < numVerts; n++ ) {
			transNorms[n].normalize();
			transTangents[n].normalize();
			transBitangents[n].normalize();
		}

		boundSphere = BoundSphere( transVerts );
		boundSphere.applyInv( viewTrans() );
		needUpdateBounds = false;
	} else {
		transVerts = verts;
		transNorms = norms;
		transTangents = tangents;
		transBitangents = bitangents;
	}

	transColors = colors;
	// TODO (Gavrant): suspicious code. Should the check be replaced with !bssp.hasVertexAlpha ?
	if ( nif->getBSVersion() < 130 && bslsp && !bslsp->hasSF1(ShaderFlags::SLSF1_Vertex_Alpha) ) {
		for ( int c = 0; c < colors.count(); c++ )
			transColors[c] = Color4( colors[c].red(), colors[c].green(), colors[c].blue(), 1.0f );
	}
}

void BSShape::drawShapes( NodeList * secondPass )
{
	if ( isHidden() )
		return;

	// TODO: Only run this if BSXFlags has "EditorMarkers present" flag
	if ( !scene->hasOption(Scene::ShowMarkers) && name.contains( "EditorMarker" ) )
		return;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add( this );
		return;
	}

	auto nif = NifModel::fromIndex( iBlock );

	if ( Node::SELECTING ) {
		if ( scene->isSelModeObject() ) {
			setColorKeyFromID( nodeId );
		} else {
			glColor4f( 0, 0, 0, 1 );
		}
	}

	if ( transformRigid ) {
		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	// Render polygon fill slightly behind alpha transparency and wireframe
	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, 0, transVerts.constData() );

	if ( !Node::SELECTING ) [[likely]] {
		glEnableClientState( GL_NORMAL_ARRAY );
		glNormalPointer( GL_FLOAT, 0, transNorms.constData() );

		bool doVCs = ( bssp && bssp->hasSF2(ShaderFlags::SLSF2_Vertex_Colors) );
		// Always do vertex colors for FO4 if colors present
		if ( nif->getBSVersion() >= 130 && hasVertexColors && colors.count() )
			doVCs = true;

		if ( transColors.count() && scene->hasOption(Scene::DoVertexColors) && doVCs ) {
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_FLOAT, 0, transColors.constData() );
		} else if ( nif->getBSVersion() < 130 && !hasVertexColors && (bslsp && bslsp->hasVertexColors) ) {
			// Correctly blacken the mesh if SLSF2_Vertex_Colors is still on
			//	yet "Has Vertex Colors" is not.
			glColor( Color3( 0.0f, 0.0f, 0.0f ) );
		} else {
			glColor( Color3( 1.0f, 1.0f, 1.0f ) );
		}

		if ( nif->getBSVersion() >= 151 )
			glEnable( GL_FRAMEBUFFER_SRGB );
		else
			glDisable( GL_FRAMEBUFFER_SRGB );
		shader = scene->renderer->setupProgram( this, shader );

	} else {
		if ( nif->getBSVersion() >= 151 )
			glDisable( GL_FRAMEBUFFER_SRGB );

		if ( drawInSecondPass ) {
			glDisableClientState( GL_VERTEX_ARRAY );

			glDisable( GL_POLYGON_OFFSET_FILL );

			if ( scene->isSelModeVertex() )
				drawVerts();

			if ( transformRigid )
				glPopMatrix();

			return;
		}
	}

	if ( isDoubleSided ) {
		glCullFace( GL_FRONT );
		glDrawElements( GL_TRIANGLES, triangles.count() * 3, GL_UNSIGNED_SHORT, triangles.constData() );
		glCullFace( GL_BACK );
	}

	if ( !isLOD ) {
		glDrawElements( GL_TRIANGLES, triangles.count() * 3, GL_UNSIGNED_SHORT, triangles.constData() );
	} else if ( triangles.count() ) {
		auto lod0 = nif->get<uint>( iBlock, "LOD0 Size" );
		auto lod1 = nif->get<uint>( iBlock, "LOD1 Size" );
		auto lod2 = nif->get<uint>( iBlock, "LOD2 Size" );

		auto lod0tris = triangles.mid( 0, lod0 );
		auto lod1tris = triangles.mid( lod0, lod1 );
		auto lod2tris = triangles.mid( lod0 + lod1, lod2 );

		// If Level2, render all
		// If Level1, also render Level0
		switch ( scene->lodLevel ) {
		case Scene::Level0:
			if ( lod2tris.count() )
				glDrawElements( GL_TRIANGLES, lod2tris.count() * 3, GL_UNSIGNED_SHORT, lod2tris.constData() );
			[[fallthrough]];
		case Scene::Level1:
			if ( lod1tris.count() )
				glDrawElements( GL_TRIANGLES, lod1tris.count() * 3, GL_UNSIGNED_SHORT, lod1tris.constData() );
			[[fallthrough]];
		case Scene::Level2:
		default:
			if ( lod0tris.count() )
				glDrawElements( GL_TRIANGLES, lod0tris.count() * 3, GL_UNSIGNED_SHORT, lod0tris.constData() );
			break;
		}
	}

	if ( !Node::SELECTING )
		scene->renderer->stopProgram();

	glDisableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( scene->isSelModeVertex() )
		drawVerts();

	if ( transformRigid )
		glPopMatrix();
}

void BSShape::drawVerts() const
{
	glDisable( GL_LIGHTING );
	glPointSize( GLView::Settings::vertexSelectPointSize );
	glNormalColor();

	glBegin( GL_POINTS );

	for ( int i = 0; i < numVerts; i++ ) {
		if ( Node::SELECTING ) {
			setColorKeyFromID( ( shapeNumber << 16 ) + i );
		}
		glVertex( transVerts.value( i ) );
	}

	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif ) {
		glEnd();
		return;
	}

	// Vertices are on NiSkinPartition in version 100
	bool selected = iBlock == scene->currentBlock;
	if ( iSkinPart.isValid() ) {
		selected |= iSkinPart == scene->currentBlock;
		selected |= isDynamic;
	}


	// Highlight selected vertex
	if ( !Node::SELECTING && selected ) {
		auto idx = scene->currentIndex;
		auto n = idx.data( NifSkopeDisplayRole ).toString();
		if ( n == "Vertex" || n == "Vertices" ) {
			glHighlightColor();
			glVertex( transVerts.value( idx.parent().row() ) );
		}
	}

	glEnd();
}

void BSShape::drawSelection() const
{
	glDisable(GL_FRAMEBUFFER_SRGB);
	if ( scene->hasOption(Scene::ShowNodes) )
		Node::drawSelection();

	if ( isHidden() || !scene->isSelModeObject() )
		return;

	auto idx = scene->currentIndex;
	auto blk = scene->currentBlock;

	// Is the current block extra data
	bool extraData = false;

	auto nif = NifModel::fromValidIndex(blk);
	if ( !nif )
		return;

	// Set current block name and detect if extra data
	auto blockName = nif->itemName( blk );
	if ( blockName.startsWith( "BSPackedCombined" ) )
		extraData = true;

	// Don't do anything if this block is not the current block
	//	or if there is not extra data
	if ( blk != iBlock && blk != iSkin && blk != iSkinData && blk != iSkinPart && !extraData )
		return;

	// Name of this index
	auto n = idx.data( NifSkopeDisplayRole ).toString();
	// Name of this index's parent
	auto p = idx.parent().data( NifSkopeDisplayRole ).toString();
	// Parent index
	auto pBlock = nif->getBlockIndex( nif->getParent( blk ) );

	auto push = [this] ( const Transform & t ) {
		if ( transformRigid ) {
			glPushMatrix();
			glMultMatrix( t );
		}
	};

	auto pop = [this] () {
		if ( transformRigid )
			glPopMatrix();
	};

	push( viewTrans() );

	glDepthFunc( GL_LEQUAL );

	glDisable( GL_LIGHTING );
	glDisable( GL_COLOR_MATERIAL );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_NORMALIZE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glDisable( GL_ALPHA_TEST );

	glDisable( GL_CULL_FACE );

	GLfloat lineWidth = GLView::Settings::lineWidthWireframe;
	GLfloat pointSize = GLView::Settings::vertexPointSize;

	glLineWidth( lineWidth );
	glPointSize( pointSize );

	glNormalColor();

	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( -1.0f, -2.0f );

	float normalScale = bounds().radius / 20;
	normalScale /= 2.0f;

	if ( normalScale < 0.1f )
		normalScale = 0.1f;


	// Draw All Verts lambda
	auto allv = [this]( float size ) {
		glPointSize( size );
		glBegin( GL_POINTS );

		for ( int j = 0; j < transVerts.count(); j++ )
			glVertex( transVerts.value( j ) );

		glEnd();
	};

	if ( !extraData ) {
		if ( n == "Bounding Sphere" ) {
			auto sph = BoundSphere( nif, idx );
			if ( sph.radius > 0.0 ) {
				glColor4f( 1, 1, 1, 0.33f );
				drawSphereSimple( sph.center, sph.radius, 72 );
			}
		} else if ( nif->getBSVersion() >= 151 && n == "Bounding Box" ) {
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
		}
	}

	if ( blockName.startsWith( "BSPackedCombined" ) && pBlock == iBlock ) {
		QVector<QModelIndex> idxs;
		if ( n == "Bounding Sphere" ) {
			idxs += idx;
		} else if ( n.startsWith( "BSPackedCombined" ) ) {
			auto data = nif->getIndex( idx, "Object Data" );
			int dataCt = nif->rowCount( data );

			for ( int i = 0; i < dataCt; i++ ) {
				auto d = QModelIndex_child( data, i );

				auto c = nif->getIndex( d, "Combined" );
				int cCt = nif->rowCount( c );

				for ( int j = 0; j < cCt; j++ ) {
					idxs += nif->getIndex( QModelIndex_child( c, j ), "Bounding Sphere" );
				}
			}
		}

		if ( !idxs.count() ) {
			glPopMatrix();
			return;
		}

#if 0
		Vector3 pTrans = nif->get<Vector3>( QModelIndex_child( pBlock, 1 ), "Translation" );
#endif
		auto iBSphere = nif->getIndex( pBlock, "Bounding Sphere" );
		Vector3 pbvC = nif->get<Vector3>( QModelIndex_child( iBSphere, 0, 2 ) );
		float pbvR = nif->get<float>( QModelIndex_child( iBSphere, 1, 2 ) );

		if ( pbvR > 0.0 ) {
			glColor4f( 0, 1, 0, 0.33f );
			drawSphereSimple( pbvC, pbvR, 72 );
		}

		glPopMatrix();

		for ( auto i : idxs ) {
			// Transform compound
			auto iTrans = QModelIndex_child( i.parent(), 1 );
			Matrix mat = nif->get<Matrix>( iTrans, "Rotation" );
			//auto trans = nif->get<Vector3>( iTrans, "Translation" );
			float scale = nif->get<float>( iTrans, "Scale" );

			Vector3 bvC = nif->get<Vector3>( i, "Center" );
			float bvR = nif->get<float>( i, "Radius" );

			Transform t;
			t.rotation = mat.inverted();
			t.translation = bvC;
			t.scale = scale;

			glPushMatrix();
			glMultMatrix( scene->view * t );

			if ( bvR > 0.0 ) {
				glColor4f( 1, 1, 1, 0.33f );
				drawSphereSimple( Vector3( 0, 0, 0 ), bvR, 72 );
			}

			glPopMatrix();
		}

		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	if ( n == "Vertex Data" || n == "Vertex" || n == "Vertices" ) {
		allv( GLView::Settings::vertexPointSize );

		int s = -1;
		if ( (n == "Vertex Data" && p == "Vertex Data")
			 || (n == "Vertices" && p == "Vertices") ) {
			s = idx.row();
		} else if ( n == "Vertex" ) {
			s = idx.parent().row();
		}

		if ( s >= 0 ) {
			glPointSize( GLView::Settings::vertexPointSizeSelected );
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_POINTS );
			glVertex( transVerts.value( s ) );
			glEnd();
		}
	}

	glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	// Draw Lines lambda
	auto lines = [this, &normalScale, &allv, &lineWidth]( const QVector<Vector3> & v ) {
		allv( GLView::Settings::tbnPointSize );

		int s = scene->currentIndex.parent().row();
		glBegin( GL_LINES );

		for ( int j = 0; j < transVerts.count() && j < v.count(); j++ ) {
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) + v.value( j ) * normalScale * 2 );
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) - v.value( j ) * normalScale / 2 );
		}

		glEnd();

		if ( s >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glLineWidth( GLView::Settings::lineWidthHighlight * 1.2f );
			glBegin( GL_LINES );
			glVertex( transVerts.value( s ) );
			glVertex( transVerts.value( s ) + v.value( s ) * normalScale * 2 );
			glVertex( transVerts.value( s ) );
			glVertex( transVerts.value( s ) - v.value( s ) * normalScale / 2 );
			glEnd();
			glLineWidth( lineWidth );
		}
	};

	// Draw Normals
	if ( n.contains( "Normal" ) ) {
		lines( transNorms );
	}

	// Draw Tangents
	if ( n.contains( "Tangent" ) ) {
		lines( transTangents );
	}

	// Draw Triangles
	if ( n == "Triangles" ) {
		int s = scene->currentIndex.row();
		if ( s >= 0 ) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			glHighlightColor();

			Triangle tri = triangles.value( s );
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value( tri.v1() ) );
			glVertex( transVerts.value( tri.v2() ) );
			glVertex( transVerts.value( tri.v3() ) );
			glEnd();
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
	}

	// Draw Segments/Subsegments
	if ( n == "Segment" || n == "Sub Segment" || n == "Num Primitives" ) {
		auto sidx = idx;
		int s;

		QVector<QColor> cols = { { 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 }, { 255, 255, 0, 128 },
								{ 0, 255, 255, 128 }, { 255, 0, 255, 128 }, { 255, 255, 255, 128 }
		};

		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

		auto type = idx.sibling( idx.row(), 1 ).data( NifSkopeDisplayRole ).toString();

		bool isSegmentArray = (n == "Segment" && type == "BSGeometrySegmentData" && nif->isArray( idx ));
		bool isSegmentItem = (n == "Segment" && type == "BSGeometrySegmentData" && !nif->isArray( idx ));
		bool isSubSegArray = (n == "Sub Segment" && nif->isArray( idx ));

		int off = 0;
		int cnt = 0;
		int numRec = 0;

		int o = 0;
		if ( isSegmentItem || isSegmentArray ) {
			o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData
		} else if ( isSubSegArray ) {
			o = -3; // Look 3 rows above for Sub Seg Array info
		}

		int maxTris = triangles.count();

		int loopNum = 1;
		if ( isSegmentArray )
			loopNum = nif->rowCount( idx );

		for ( int l = 0; l < loopNum; l++ ) {

			if ( n != "Num Primitives" && !isSubSegArray && !isSegmentArray ) {
				sidx = QModelIndex_child( idx, 1 );
			} else if ( isSegmentArray ) {
				sidx = QModelIndex_child( QModelIndex_child( idx, l ), 1 );
			}
			s = sidx.row() + o;

			off = sidx.sibling( s - 1, 2 ).data().toInt() / 3;
			cnt = sidx.sibling( s, 2 ).data().toInt();
			numRec = sidx.sibling( s + 2, 2 ).data().toInt();

			auto recs = sidx.sibling( s + 3, 0 );
			for ( int i = 0; i < numRec; i++ ) {
				auto subrec = QModelIndex_child( recs, i );
				int o = 0;
				if ( subrec.data( NifSkopeDisplayRole ).toString() != "Sub Segment" )
					o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData

				auto suboff = QModelIndex_child( subrec, o, 2 ).data().toInt() / 3;
				auto subcnt = QModelIndex_child( subrec, o + 1, 2 ).data().toInt();

				for ( int j = suboff; j < subcnt + suboff; j++ ) {
					if ( j >= maxTris )
						continue;

					glColor( Color4( cols.value( i % 7 ) ) );
					Triangle tri = triangles[j];
					glBegin( GL_TRIANGLES );
					glVertex( transVerts.value( tri.v1() ) );
					glVertex( transVerts.value( tri.v2() ) );
					glVertex( transVerts.value( tri.v3() ) );
					glEnd();
				}
			}

			// Sub-segmentless Segments
			if ( numRec == 0 && cnt > 0 ) {
				glColor( Color4( cols.value( (idx.row() + l) % 7 ) ) );

				for ( int i = off; i < cnt + off; i++ ) {
					if ( i >= maxTris )
						continue;

					Triangle tri = triangles[i];
					glBegin( GL_TRIANGLES );
					glVertex( transVerts.value( tri.v1() ) );
					glVertex( transVerts.value( tri.v2() ) );
					glVertex( transVerts.value( tri.v3() ) );
					glEnd();
				}
			}
		}

		pop();
		return;
	}

	// Draw all bones' bounding spheres
	if ( n == "NiSkinData" || n == "BSSkin::BoneData" ) {
		// Get shape block
		if ( nif->getBlockIndex( nif->getParent( nif->getParent( blk ) ) ) == iBlock ) {
			auto iBones = nif->getIndex( blk, "Bone List" );
			int ct = nif->rowCount( iBones );

			for ( int i = 0; i < ct; i++ ) {
				auto b = QModelIndex_child( iBones, i );
				boneSphere( nif, b );
			}
		}
		pop();
		return;
	}

	// Draw bone bounding sphere
	if ( n == "Bone List" ) {
		if ( nif->isArray( idx ) ) {
			for ( int i = 0; i < nif->rowCount( idx ); i++ )
				boneSphere( nif, QModelIndex_child( idx, i ) );
		} else {
			boneSphere( nif, idx );
		}
	}

	// General wireframe
	if ( blk == iBlock && idx != iData && p != "Vertex Data" && p != "Vertices" ) {
		glLineWidth( lineWidth );
		glNormalColor();
		for ( const Triangle& tri : triangles ) {
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value( tri.v1() ) );
			glVertex( transVerts.value( tri.v2() ) );
			glVertex( transVerts.value( tri.v3() ) );
			glEnd();
		}
	}

	glDisable( GL_POLYGON_OFFSET_FILL );

	pop();
}

BoundSphere BSShape::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		if ( verts.count() ) {
			boundSphere = BoundSphere( verts );
		} else {
			boundSphere = dataBound;
		}
	}

	return worldTrans() * boundSphere;
}
