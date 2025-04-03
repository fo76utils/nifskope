#include "bsshape.h"

#include "gl/glnode.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/material.h"
#include "model/nifmodel.h"
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
	int numVerts = 0;
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
		int nDynVerts = dynVerts.size();
		if ( nDynVerts < numVerts )
			numVerts = nDynVerts;
	}

	static const char *	attrNames[8] = {
		"Vertex", "Bitangent X", "Bitangent Y", "Bitangent Z", "UV", "Normal", "Tangent", "Vertex Colors"
	};
	int	attrRows[8];
	for ( int i = 0; i < 8; i++ ) {
		QModelIndex	j = nif->getIndex( nif->getIndex( iData, 0 ), attrNames[i] );
		attrRows[i] = ( j.isValid() ? j.row() : -1 );
	}
	auto	vertsData = verts.fill( Vector3(), numVerts ).data();
	auto	normsData = norms.fill( Vector3(), numVerts ).data();
	auto	colorsData = colors.fill( Color4( 0.0f, 0.0f, 0.0f, 1.0f ), numVerts ).data();
	auto	tangentsData = tangents.fill( Vector3(), numVerts ).data();
	auto	bitangentsData = bitangents.fill( Vector3(), numVerts ).data();
	auto	coordsetData = coordset.fill( Vector2(), numVerts ).data();
	for ( int i = 0; i < numVerts; i++ ) {
		auto idx = nif->getIndex( iData, i );
		if ( !idx.isValid() )
			continue;
		auto item = nif->getItem( idx );
		if ( !item )
			continue;

		float bitX;

		if ( isDynamic ) {
			auto& dynv = dynVerts.at(i);
			vertsData[i] = Vector3( dynv );
			bitX = dynv[3];
		} else {
			vertsData[i] = nif->get<Vector3>( item->child( attrRows[0] ) );	// "Vertex"
			bitX = nif->get<float>( item->child( attrRows[1] ) );	// "Bitangent X"
		}

		// Bitangent Y/Z
		auto bitY = nif->get<float>( item->child( attrRows[2] ) );	// "Bitangent Y"
		auto bitZ = nif->get<float>( item->child( attrRows[3] ) );	// "Bitangent Z"

		coordsetData[i] = nif->get<HalfVector2>( item->child( attrRows[4] ) );	// "UV"
		normsData[i] = nif->get<ByteVector3>( item->child( attrRows[5] ) );	// "Normal"
		tangentsData[i] = nif->get<ByteVector3>( item->child( attrRows[6] ) );	// "Tangent"
		bitangentsData[i] = Vector3( bitX, bitY, bitZ );

		auto vcItem = item->child( attrRows[7] );	// "Vertex Colors"
		if ( vcItem )
			colorsData[i] = nif->get<ByteColor4>( vcItem );
	}

	// Add coords as the first set of QList
	coords.append( coordset );

	numVerts = int( verts.size() );

	// Fill triangle data
	if ( isSkinned && iSkinPart.isValid() ) {
		auto iPartitions = nif->getIndex( iSkinPart, "Partitions" );
		if ( iPartitions.isValid() ) {
			int n = nif->rowCount( iPartitions );
			for ( int i = 0; i < n; i++ )
				triangles << nif->getArray<Triangle>( nif->getIndex( iPartitions, i ), "Triangles" );
		}
	} else {
		auto iTriData = nif->getIndex( iBlock, "Triangles" );
		if ( iTriData.isValid() )
			triangles = nif->getArray<Triangle>( iTriData );
	}
	removeInvalidIndices();
	updateLodLevel();

	// Fill skeleton data
	resetSkeletonData();
	if ( isSkinned && iSkin.isValid() ) {
		bones = nif->getLinkArray( iSkin, "Bones" );
		auto nTotalBones = bones.size();

		boneData.fill( BoneData(), nTotalBones );
		for ( int i = 0; i < nTotalBones; i++ )
			boneData[i].bone = bones[i];

		boneWeights0.assign( size_t( numVerts ), FloatVector4( 0.0f ) );
		boneWeights1.clear();

		for ( int i = 0; i < numVerts; i++ ) {
			auto idx = nif->getIndex( iData, i );
			auto wts = nif->getArray<float>( idx, "Bone Weights" );
			auto bns = nif->getArray<quint8>( idx, "Bone Indices" );
			if ( wts.size() < 4 || bns.size() < 4 )
				continue;

			size_t	k = 0;
			for ( int j = 0; j < 4; j++ ) {
				if ( bns[j] < nTotalBones && wts[j] > 0.00001f ) {
					boneWeights0[i][k] = float( bns[j] ) + ( std::min( wts[j], 1.0f ) * float( 65535.0 / 65536.0 ) );
					k++;
				}
			}
		}

		auto b = nif->getIndex( iSkinData, "Bone List" );
		for ( int i = 0; i < nTotalBones; i++ )
			boneData[i].setTransform( nif, nif->getIndex( b, i ) );
	}
}

QModelIndex BSShape::vertexAt( int idx ) const
{
	auto nif = scene->nifModel;
	if ( !nif )
		return QModelIndex();

	// Vertices are on NiSkinPartition in version 100
	auto blk = iBlock;
	if ( iSkinPart.isValid() ) {
		if ( isDynamic )
			return nif->getIndex( nif->getIndex( blk, "Vertices" ), idx );

		blk = iSkinPart;
	}

	return nif->getIndex( nif->getIndex( nif->getIndex( blk, "Vertex Data" ), idx ), "Vertex" );
}

QModelIndex BSShape::triangleAt( int idx ) const
{
	auto nif = scene->nifModel;
	if ( !nif )
		return QModelIndex();

	auto blk = iBlock;
	if ( iSkinPart.isValid() ) {
		// Triangles are on NiSkinPartition in version 100
		auto iPartitions = nif->getIndex( iSkinPart, "Partitions" );
		if ( iPartitions.isValid() && nif->isArray( iPartitions ) ) {
			for ( int i = 0; i < nif->rowCount( iPartitions ); i++ ) {
				auto iPart = nif->getIndex( iPartitions, i );
				if ( !iPart.isValid() )
					continue;
				auto iTriangles = nif->getIndex( iPart, "Triangles" );
				if ( !( iTriangles.isValid() && nif->isArray( iTriangles ) ) )
					continue;
				int n = nif->rowCount( iTriangles );
				if ( idx < n )
					return nif->getIndex( iTriangles, idx );
				idx -= n;
			}
		}
		return QModelIndex();
	}

	return nif->getIndex( nif->getIndex( blk, "Triangles" ), idx );
}

void BSShape::transformShapes()
{
	if ( isHidden() )
		return;

	auto nif = scene->nifModel;
	if ( !nif ) {
		clear();
		return;
	}

	Node::transformShapes();

	if ( !( isSkinned && scene->hasOption(Scene::DoSkinning) ) ) [[likely]] {
		transformRigid = true;
		return;
	}

	updateBoneTransforms();
}

void BSShape::drawShapes( NodeList * secondPass )
{
	if ( isHidden() )
		return;

	// TODO: Only run this if BSXFlags has "EditorMarkers present" flag
	if ( !scene->hasOption(Scene::ShowMarkers) && name.contains( QLatin1String("EditorMarker") ) )
		return;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add( this );
		return;
	}

	auto nif = scene->nifModel;

	if ( !nif || !bindShape() )
		return;

	int	selectionFlags = scene->selecting;
	if ( ( selectionFlags & int(Scene::SelVertex) ) && drawInSecondPass ) [[unlikely]] {
		glDisable( GL_FRAMEBUFFER_SRGB );
		drawVerts();
		return;
	}

	// Render polygon fill slightly behind alpha transparency and wireframe
	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	auto	context = scene->renderer;

	qsizetype	numTriangles = std::clamp< qsizetype >( lodTriangleCount, 0, triangles.size() );

	if ( !selectionFlags ) [[likely]] {
		if ( nif->getBSVersion() >= 151 )
			glEnable( GL_FRAMEBUFFER_SRGB );
		else
			glDisable( GL_FRAMEBUFFER_SRGB );
		shader = context->setupProgram( this, shader );

	} else if ( auto prog = context->useProgram( "selection.prog" ); prog ) {
		if ( nif->getBSVersion() >= 151 )
			glDisable( GL_FRAMEBUFFER_SRGB );

		setUniforms( prog );
		prog->uni1i( "selectionFlags", selectionFlags & 5 );
		prog->uni1i( "selectionParam", ( !( selectionFlags & int(Scene::SelVertex) ) ? nodeId : -1 ) );
	}

	if ( numTriangles > 0 )
		context->fn->glDrawElements( GL_TRIANGLES, GLsizei( numTriangles * 3 ), GL_UNSIGNED_SHORT, (void *) 0 );

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( selectionFlags & int( Scene::SelVertex ) ) [[unlikely]]
		drawVerts();
}

void BSShape::drawVerts() const
{
	int	vertexSelected = -1;

	if ( !scene->selecting ) {
		bool	selected = ( iBlock == scene->currentBlock );
		if ( iSkinPart.isValid() ) {
			// Vertices are on NiSkinPartition in version 100
			selected |= ( iSkinPart == scene->currentBlock );
			selected |= isDynamic;
		}
		if ( selected ) {
			// Highlight selected vertex
			auto idx = scene->currentIndex;
			auto n = idx.data( NifSkopeDisplayRole ).toString();
			if ( n == "Vertex" || n == "Vertices" )
				vertexSelected = idx.parent().row();
		}
	}

	Shape::drawVerts( GLView::Settings::vertexSelectPointSize, vertexSelected );
}

void BSShape::drawSelection() const
{
	if ( !scene->isSelModeVertex() ) {
		if ( scene->hasOption(Scene::ShowNodes) )
			Node::drawSelection();

		if ( !scene->isSelModeObject() )
			return;
	}

	auto nif = scene->nifModel;
	if ( isHidden() || !nif )
		return;
	auto idx = scene->currentIndex;
	auto blk = scene->currentBlock;

	bool extraData = false;
	if ( !scene->isSelModeVertex() ) {
		if ( !blk.isValid() )
			return;
		// Is the current block extra data
		if ( auto i = nif->getItem( blk ); i != nullptr )
			extraData = i->name().startsWith( QLatin1String("BSPackedCombined") );
		// Don't do anything if this block is not the current block
		//	or if there is not extra data
		if ( blk != iBlock && blk != iSkin && blk != iSkinData && blk != iSkinPart && !extraData )
			return;
	}

	auto	context = scene->renderer;
	if ( !( context && bindShape() ) )
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

	// Name of this index
	auto n = idx.data( NifSkopeDisplayRole ).toString();
	// Name of this index's parent
	auto p = idx.parent().data( NifSkopeDisplayRole ).toString();
	// Parent index
	auto pBlock = nif->getBlockIndex( nif->getParent( blk ) );

	glEnable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_POLYGON_OFFSET_LINE );
	glEnable( GL_POLYGON_OFFSET_POINT );
	glPolygonOffset( -1.0f, -2.0f );

	if ( !extraData ) {
		if ( n == "Bounding Sphere" ) {
			auto sph = BoundSphere( nif, idx );
			if ( sph.radius > 0.0f )
				Shape::drawBoundingSphere( sph, FloatVector4( 1.0f, 1.0f, 1.0f, 0.33f ) );
		} else if ( nif->getBSVersion() >= 151 && n == "Bounding Box" ) {
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
		}

	} else if ( pBlock == iBlock ) {
		QVector<QModelIndex> idxs;
		if ( n == "Bounding Sphere" ) {
			idxs += idx;
		} else if ( n.startsWith( QLatin1String("BSPackedCombined") ) ) {
			auto data = nif->getIndex( idx, "Object Data" );
			int dataCt = nif->rowCount( data );

			for ( int i = 0; i < dataCt; i++ ) {
				auto d = nif->getIndex( data, i );

				auto c = nif->getIndex( d, "Combined" );
				int cCt = nif->rowCount( c );

				for ( int j = 0; j < cCt; j++ ) {
					idxs += nif->getIndex( nif->getIndex( c, j ), "Bounding Sphere" );
				}
			}
		}

#if 0
		Vector3 pTrans = nif->get<Vector3>( nif->getIndex( pBlock, 1 ), "Translation" );
#endif
		BoundSphere sph( nif, pBlock );
		if ( sph.radius > 0.0f )
			Shape::drawBoundingSphere( sph, FloatVector4( 0.0f, 1.0f, 0.0f, 0.33f ) );

		for ( auto i : idxs ) {
			// Transform compound
			auto iTrans = nif->getIndex( i.parent(), 1 );
			Matrix mat = nif->get<Matrix>( iTrans, "Rotation" );
			//auto trans = nif->get<Vector3>( iTrans, "Translation" );
			float scale = nif->get<float>( iTrans, "Scale" );

			Vector3 bvC = nif->get<Vector3>( i, "Center" );
			float bvR = nif->get<float>( i, "Radius" );

			Transform t;
			t.rotation = mat.inverted();
			t.translation = bvC;
			t.scale = scale;

			scene->loadModelViewMatrix( scene->view.toMatrix4() * t );

			if ( bvR > 0.0 ) {
				scene->setGLColor( 1.0f, 1.0f, 1.0f, 0.33f );
				scene->drawSphereSimple( Vector3( 0, 0, 0 ), bvR, 72 );
			}
		}
	}

	bindShape();

	if ( n == "Vertex Data" || n == "Vertex" || n == "Vertices" ) {
		int	s = -1;
		if ( (n == "Vertex Data" && p == "Vertex Data")
			 || (n == "Vertices" && p == "Vertices") ) {
			s = idx.row();
		} else if ( n == "Vertex" ) {
			s = idx.parent().row();
		}

		Shape::drawVerts( GLView::Settings::vertexPointSize, s );
	}

	// Draw Normals, Tangents or Bitangents
	int	btnMask = 0;
	if ( n.contains( QLatin1String("Normal") ) )
		btnMask = 0x04;
	else if ( n.contains( QLatin1String("Tangent") ) )
		btnMask = 0x02;
	else if ( n.contains( QLatin1String("Bitangent") ) )
		btnMask = 0x01;
	if ( btnMask ) {
		int	s = scene->currentIndex.parent().row();
		Shape::drawVerts( GLView::Settings::tbnPointSize, s );
		float	normalScale = std::max< float >( bounds().radius / ( btnMask == 0x04 ? 8.0f : 16.0f ), 0.25f );
		Shape::drawNormals( btnMask, s, normalScale * viewTrans().scale );
	}

	// Draw Triangles
	if ( n == "Triangles" ) {
		int s = -1;
		if ( n == p ) {
			s = idx.row();
			if ( iSkinPart.isValid() ) {
				int i = idx.parent().parent().row();
				while ( i-- > 0 ) {
					if ( auto j = nif->getIndex( nif->getIndex( iSkinPart, "Partitions" ), i ); j.isValid() )
						s += int( nif->get<quint32>( j, "Num Triangles" ) );
				}
			}
		}
		Shape::drawWireframe( scene->wireframeColor );
		if ( s >= 0 && s < triangles.size() )
			Shape::drawTriangles( s, 1, scene->highlightColor );
	}

	// Draw Segments/Subsegments
	if ( n == "Segment" || n == "Sub Segment" || n == "Num Primitives" ) {
		auto sidx = idx;
		int s;

		static const FloatVector4	cols[7] = {
			{ 1.0f, 0.0f, 0.0f, 0.5f }, { 0.0f, 1.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 1.0f, 0.5f },
			{ 1.0f, 1.0f, 0.0f, 0.5f }, { 0.0f, 1.0f, 1.0f, 0.5f }, { 1.0f, 0.0f, 1.0f, 0.5f },
			{ 1.0f, 1.0f, 1.0f, 0.5f }
		};

		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		scene->loadModelViewMatrix( viewTrans() );
		scene->drawTriangles( verts.constData(), size_t( verts.size() ), nullptr, true,
								0, size_t( triangles.size() ) * 3, GL_UNSIGNED_SHORT, triangles.constData() );

		auto type = idx.sibling( idx.row(), 1 ).data( NifSkopeDisplayRole ).toString();

		bool isSegmentArray = (n == "Segment" && type == "BSGeometrySegmentData" && nif->isArray( idx ));
		bool isSegmentItem = (n == "Segment" && type == "BSGeometrySegmentData" && !nif->isArray( idx ));
		bool isSubSegArray = (n == "Sub Segment" && nif->isArray( idx ));

		qsizetype off = 0;
		qsizetype cnt = 0;
		int numRec = 0;

		int o = 0;
		if ( isSegmentItem || isSegmentArray ) {
			o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData
		} else if ( isSubSegArray ) {
			o = -3; // Look 3 rows above for Sub Seg Array info
		}

		qsizetype maxTris = triangles.size();

		int loopNum = 1;
		if ( isSegmentArray )
			loopNum = nif->rowCount( idx );

		for ( int l = 0; l < loopNum; l++ ) {

			if ( n != "Num Primitives" && !isSubSegArray && !isSegmentArray ) {
				sidx = nif->getIndex( idx, 1 );
			} else if ( isSegmentArray ) {
				sidx = nif->getIndex( nif->getIndex( idx, l ), 1 );
			}
			s = sidx.row() + o;

			off = sidx.sibling( s - 1, 2 ).data().toInt() / 3;
			cnt = sidx.sibling( s, 2 ).data().toInt();
			numRec = sidx.sibling( s + 2, 2 ).data().toInt();

			auto recs = sidx.sibling( s + 3, 0 );
			for ( int i = 0; i < numRec; i++ ) {
				auto subrec = nif->getIndex( recs, i );
				int o = 0;
				if ( subrec.data( NifSkopeDisplayRole ).toString() != "Sub Segment" )
					o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData

				qsizetype suboff = nif->getIndex( subrec, o, 2 ).data().toInt() / 3;
				qsizetype subcnt = nif->getIndex( subrec, o + 1, 2 ).data().toInt();

				if ( suboff < 0 ) {
					subcnt += suboff;
					suboff = 0;
				}
				subcnt = std::min< qsizetype >( subcnt, maxTris - suboff );
				if ( suboff >= maxTris || subcnt < 1 )
					continue;

				setGLColor( cols[(unsigned int) i % 7U] );
				context->fn->glDrawElements( GL_TRIANGLES, GLsizei( subcnt * 3 ),
												GL_UNSIGNED_SHORT, (void *) ( suboff * 6 ) );
			}

			// Sub-segmentless Segments
			if ( numRec == 0 && cnt > 0 ) {
				if ( off < 0 ) {
					cnt += off;
					off = 0;
				}
				if ( off < maxTris && cnt > 0 ) {
					cnt = std::min< qsizetype >( cnt, maxTris - off );

					setGLColor( cols[(unsigned int) ( idx.row() + l ) % 7U] );
					context->fn->glDrawElements( GL_TRIANGLES, GLsizei( cnt * 3 ),
													GL_UNSIGNED_SHORT, (void *) ( off * 6 ) );
				}
			}
		}

	} else if ( n == "Bone Weights" ) {
		// Draw bone weights
		Shape::drawWeights( scene->currentIndex.parent().row() );

	} else if ( n == "NiSkinData" || n == "BSSkin::BoneData" ) {
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

	} else {
		// Draw bone bounding sphere
		if ( n == "Bone List" ) {
			if ( nif->isArray( idx ) ) {
				for ( int i = 0; i < nif->rowCount( idx ); i++ )
					boneSphere( nif, nif->getIndex( idx, i ) );
			} else {
				boneSphere( nif, idx );
			}
			bindShape();
		}

		// General wireframe
		if ( blk == iBlock && idx != iData && p != "Vertex Data" && p != "Vertices" && n != "Triangles" )
			Shape::drawWireframe( scene->wireframeColor );
	}

	glDisable( GL_POLYGON_OFFSET_FILL );
	glDisable( GL_POLYGON_OFFSET_LINE );
	glDisable( GL_POLYGON_OFFSET_POINT );
}

BoundSphere BSShape::bounds() const
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

void BSShape::updateLodLevel()
{
	auto	nif = scene->nifModel;
	qsizetype	numTriangles = triangles.size();
	if ( isLOD && nif && numTriangles > 0 ) {
		auto lod0 = nif->get<uint>( iBlock, "LOD0 Size" );
		auto lod1 = nif->get<uint>( iBlock, "LOD1 Size" );
		auto lod2 = nif->get<uint>( iBlock, "LOD2 Size" );

		// If Level2, render all
		// If Level1, also render Level0
		numTriangles = 0;
		switch ( scene->lodLevel ) {
		case Scene::Level0:
			numTriangles = qsizetype( lod2 );
			[[fallthrough]];
		case Scene::Level1:
			numTriangles += qsizetype( lod1 );
			[[fallthrough]];
		case Scene::Level2:
		default:
			numTriangles += qsizetype( lod0 );
			break;
		}
		numTriangles = std::clamp< qsizetype >( numTriangles, 0, triangles.size() );
	}
	lodTriangleCount = numTriangles;
}
