/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "glnode.h"

#include "nifskope.h"
#include "gl/controllers.h"
#include "gl/glscene.h"
#include "gl/glmarker.h"
#include "model/nifmodel.h"
#include "ui/settingsdialog.h"
#include "glview.h"
#include "renderer.h"

#include "lib/nvtristripwrapper.h"

#include <QRegularExpression>
#include <QSettings>

#include <algorithm> // std::stable_sort

//! @file glnode.cpp Scene management for visible NiNodes and their children.

/*
 *  Node list
 */

NodeList::NodeList()
{
}

NodeList::NodeList( const NodeList & other )
{
	operator=( other );
}

NodeList::~NodeList()
{
	clear();
}

void NodeList::clear()
{
	foreach ( Node * n, nodes ) {
		del( n );
	}
}

NodeList & NodeList::operator=( const NodeList & other )
{
	clear();
	for ( Node * n : other.list() ) {
		add( n );
	}
	return *this;
}

void NodeList::add( Node * n )
{
	if ( n && !nodes.contains( n ) ) {
		++n->ref;
		nodes.append( n );
	}
}

void NodeList::del( Node * n )
{
	if ( nodes.contains( n ) ) {
		int cnt = nodes.removeAll( n );

		if ( n->ref <= cnt ) {
			delete n;
		} else {
			n->ref -= cnt;
		}
	}
}

Node * NodeList::get( const QModelIndex & index ) const
{
	for ( Node * n : nodes ) {
		if ( n->index().isValid() && n->index() == index )
			return n;
	}
	return nullptr;
}

void NodeList::validate()
{
	QList<Node *> rem;
	for ( Node * n : nodes ) {
		if ( !n->isValid() )
			rem.append( n );
	}
	foreach ( Node * n, rem ) {
		del( n );
	}
}

static bool compareNodes( const Node * node1, const Node * node2 )
{
	return node1->id() < node2->id();
}

static bool compareNodesAlpha( const Node * node1, const Node * node2 )
{
	// Presorted meshes override other sorting
	// Alpha enabled meshes on top (sorted from rear to front)

	bool p1 = node1->isPresorted();
	bool p2 = node2->isPresorted();

	// Presort meshes
	if ( p1 && p2 ) {
		return node1->id() < node2->id();
	}

	bool a1 = node1->findProperty<AlphaProperty>();
	bool a2 = node2->findProperty<AlphaProperty>();

	float d1 = node1->viewDepth();
	float d2 = node2->viewDepth();

	// Alpha sort meshes
	if ( a1 == a2 ) {
		return (d1 < d2);
	}

	return a2;
}

void NodeList::orderedNodeSort()
{
	for ( Node * node : nodes )
		node->presorted = true;
	std::stable_sort( nodes.begin(), nodes.end(), compareNodes );
}

void NodeList::alphaSort()
{
	std::stable_sort( nodes.begin(), nodes.end(), compareNodesAlpha );
}

/*
 *	Node
 */


Node::Node( Scene * s, const QModelIndex & iBlock) : IControllable( s, iBlock ), parent( 0 ), ref( 0 )
{
	nodeId = 0;
	flags.bits = 0;
}

void Node::setGLColor( FloatVector4 c ) const
{
	NifSkopeOpenGLContext::Program *	prog;
	if ( scene->renderer && ( prog = scene->renderer->getCurrentProgram() ) != nullptr )
		prog->uni4f( "vertexColorOverride", FloatVector4( 1.0e-15f ).maxValues( c ) );
}


void Node::clear()
{
	IControllable::clear();

	nodeId = 0;
	flags.bits = 0;
	local = Transform();

	children.clear();
	properties.clear();
}

Controller * Node::findController( const QString & proptype, const QString & ctrltype, const QString & var1, const QString & var2 )
{
	if ( proptype != "<empty>" && !proptype.isEmpty() ) {
		for ( Property * prp : properties ) {
			if ( prp->typeId() == proptype ) {
				return prp->findController( ctrltype, var1, var2 );
			}
		}
		return nullptr;
	}

	return IControllable::findController( ctrltype, var1, var2 );
}

Controller * Node::findController( const QString & proptype, const QModelIndex & index )
{
	Controller * c = nullptr;

	for ( Property * prp : properties ) {
		if ( prp->typeId() == proptype ) {
			if ( c )
				break;

			c = prp->findController( index );
		}
	}

	return c;
}

void Node::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	IControllable::updateImpl( nif, index );

	nodeId = nif->getBlockNumber( iBlock );

	if ( iBlock == index ) {
		flags.bits = nif->get<int>( iBlock, "Flags" );
		local = Transform( nif, iBlock );
		// BSOrderedNode support
		//	Only set if true (|=) so that it propagates to all children
		if ( nif->getBlockIndex( iBlock, "BSOrderedNode" ).isValid() )
			presorted = true;

		// Properties
		properties.clear();
		for ( auto l : nif->getLinkArray(iBlock, "Properties") )
			properties.add( scene->getProperty(nif, nif->getBlockIndex(l)) );

		properties.add( scene->getProperty(nif, iBlock, "Shader Property", "BSShaderProperty") );
		properties.add( scene->getProperty(nif, iBlock, "Alpha Property", "NiAlphaProperty") );

		// Children
		children.clear();
		QModelIndex iChildren = nif->getIndex( iBlock, "Children" );
		if ( iChildren.isValid() ) {
			int nChildren = nif->rowCount(iChildren);
			if ( nChildren > 0 ) {
				QList<qint32> lChildren = nif->getChildLinks( nodeId );
				for ( int c = 0; c < nChildren; c++ ) {
					qint32 link = nif->getLink( nif->getIndex( iChildren, c ) );

					if ( lChildren.contains( link ) ) {
						QModelIndex iChild = nif->getBlockIndex( link );
						Node * node = scene->getNode( nif, iChild );
						if ( node )
							node->makeParent( this );
					}
				}
			}
		}
	}
}

void Node::makeParent( Node * newParent )
{
	if ( parent )
		parent->children.del( this );

	parent = newParent;

	if ( parent )
		parent->children.add( this );
}

void Node::setController( const NifModel * nif, const QModelIndex & iController )
{
	QString cname = nif->itemName( iController );

	if ( cname == "NiTransformController" ) {
		Controller * ctrl = new TransformController( this, iController );
		registerController(nif, ctrl);
	} else if ( cname == "NiMultiTargetTransformController" ) {
		Controller * ctrl = new MultiTargetTransformController( this, iController );
		registerController(nif, ctrl);
	} else if ( cname == "NiControllerManager" ) {
		Controller * ctrl = new ControllerManager( this, iController );
		registerController(nif, ctrl);
	} else if ( cname == "NiKeyframeController" ) {
		Controller * ctrl = new KeyframeController( this, iController );
		registerController(nif, ctrl);
	} else if ( cname == "NiVisController" ) {
		Controller * ctrl = new VisibilityController( this, iController );
		registerController(nif, ctrl);
	}
}

void Node::activeProperties( PropertyList & list ) const
{
	list.merge( properties );

	if ( parent )
		parent->activeProperties( list );
}

const Transform & Node::viewTrans() const
{
	std::uint64_t	key = viewTransKey( nodeId );
	Transform *	p;
	if ( scene->transformCache.find( p, key ) )
		return *p;

	*p = ( !parent ? scene->view : parent->viewTrans() ) * local;
	return *p;
}

const Transform & Node::worldTrans() const
{
	std::uint64_t	key = worldTransKey( nodeId );
	Transform *	p;
	if ( scene->transformCache.find( p, key ) )
		return *p;

	*p = local;
	if ( parent )
		*p = parent->worldTrans() * *p;

	return *p;
}

const Transform & Node::localTrans( int root ) const
{
	std::uint64_t	key = localTransKey( root, nodeId );
	Transform *	p;
	if ( scene->transformCache.find( p, key ) )
		return *p;

	Transform trans;
	if ( nodeId != root ) {
		trans = local;
		if ( parent && parent->nodeId != root )
			trans = parent->localTrans( root ) * trans;
	}
	*p = trans;

	return *p;
}

const Vector3 Node::center() const
{
	return worldTrans().translation;
}

float Node::viewDepth() const
{
	return viewTrans().translation[2];
}

Node * Node::findParent( int id ) const
{
	id = std::max< int >( id, 0 );
	Node * node = const_cast< Node * >( this );
	do {
		node = node->parent;
	} while ( node && node->nodeId != id );

	return node;
}

Node * Node::findChild( int id ) const
{
	for ( Node * child : children.list() ) {
		if ( child ) {
			if ( child->nodeId == id )
				return child;

			child = child->findChild( id );
			if ( child )
				return child;
		}
	}
	return nullptr;
}

Node * Node::findChild( const QString & str ) const
{
	if ( this->name == str )
		return const_cast<Node *>( this );

	for ( Node * child : children.list() ) {
		Node * n = child->findChild( str );

		if ( n )
			return n;
	}
	return nullptr;
}

bool Node::isHidden() const
{
	if ( scene->hasOption(Scene::ShowHidden) )
		return false;
	if ( flags.node.hidden )
		return true;
	if ( parent && parent->isHidden() )
		return true;
	return false; /*!Options::cullExpression().pattern().isEmpty() && name.contains( Options::cullExpression() );*/
}

void Node::transform()
{
	IControllable::transform();

	// if there's a rigid body attached, then calculate and cache the body's transform
	// (need this later in the drawing stage for the constraints)
	auto nif = NifModel::fromValidIndex( iBlock );
	if ( nif && nif->getBSVersion() > 0 ) {
		QModelIndex iObject = nif->getBlockIndex( nif->getLink( iBlock, "Collision Object" ) );
		if ( iObject.isValid() ) {
			QModelIndex iBody = nif->getBlockIndex( nif->getLink( iObject, "Body" ) );

			if ( iBody.isValid() ) {
				Transform t;
				t.scale = bhkScale( nif );

				if ( nif->isNiBlock( iBody, "bhkRigidBodyT" ) ) {
					auto cinfo = nif->getIndex( iBody, "Rigid Body Info" );
					t.rotation.fromQuat( nif->get<Quat>( cinfo, "Rotation" ) );
					t.translation = Vector3( nif->get<Vector4>( cinfo, "Translation" ) * bhkScale( nif ) );
				}

				scene->transformCache.insert( bhkBodyTransKey( nif->getBlockNumber( iBody ) ), worldTrans() * t );
			}
		}
	}

	for ( Node * node : children.list() ) {
		node->transform();
	}
}

void Node::transformShapes()
{
	for ( Node * node : children.list() ) {
		node->transformShapes();
	}
}

void Node::draw()
{
	if ( isHidden() || iBlock == scene->currentBlock )
		return;

	if ( !scene->isSelModeObject() )
		return;

	FloatVector4	color = scene->wireframeColor;
	float	lineWidth = GLView::Settings::lineWidthHighlight;
	float	pointSize = GLView::Settings::vertexSelectPointSize;
	if ( scene->selecting ) {
		color = getColorKeyFromID( nodeId );
		lineWidth = GLView::Settings::lineWidthSelect;	// make hitting a line a little bit more easy
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glDepthMask( GL_TRUE );
	}
	scene->setGLColor( color );
	scene->setGLLineWidth( lineWidth );
	scene->setGLPointSize( pointSize );

	scene->loadModelViewMatrix( scene->view );

	Vector3 a = worldTrans().translation;
	if ( !parent ) {
		scene->drawPoints( &a );
	} else {
		Vector3 b = parent->worldTrans().translation;
		if ( scene->selecting ) {
			scene->drawLine( a, b );
		} else {
			scene->setGLColor( color * FloatVector4( 1.0f, 1.0f, 1.0f, 1.0f / 3.0f ) );
			scene->drawDashLine( a, b, 144 );
			scene->setGLColor( color );
		}
		scene->pushAndMultModelViewMatrix( Transform( a, 1.0f ) );
		scene->drawPoints();
		scene->popModelViewMatrix();
	}

	for ( Node * node : children.list() ) {
		node->draw();
	}
}

void Node::drawSelection() const
{
	auto nif = NifModel::fromIndex( scene->currentIndex );
	if ( !nif )
		return;

	if ( !scene->isSelModeObject() )
		return;

	bool extraData = false;
	auto currentBlock = nif->itemName( scene->currentBlock );
	if ( currentBlock == "BSConnectPoint::Parents" )
		extraData = nif->getBlockNumber( iBlock ) == 0; // Root Node only

	if ( scene->currentBlock != iBlock && !extraData )
		return;

	auto n = scene->currentIndex.data( NifSkopeDisplayRole ).toString();

	FloatVector4 color = scene->highlightColor;
	float lineWidth = GLView::Settings::lineWidthHighlight;
	if ( scene->selecting ) {
		color = getColorKeyFromID( nodeId );
		lineWidth = GLView::Settings::lineWidthSelect;
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_ALWAYS );
		glDepthMask( GL_TRUE );
	}
	scene->setGLColor( color );
	scene->setGLLineWidth( lineWidth );
	scene->setGLPointSize( GLView::Settings::vertexSelectPointSize );

	scene->loadModelViewMatrix( viewTrans() );

	float sceneRadius = scene->bounds().radius;
	float normalScale = std::min( sceneRadius / 9.375f, ( nif->getBSVersion() < 170 ? 16.0f : 0.25f ) );

	if ( currentBlock == "BSConnectPoint::Parents" ) {
		auto cp = nif->getIndex( scene->currentBlock, "Connect Points" );
		bool isChild = scene->currentIndex.parent().data( NifSkopeDisplayRole ).toString() == "Connect Points";

		int sel = -1;
		if ( n == "Connect Points" && !nif->isArray( scene->currentIndex ) ) {
			sel = scene->currentIndex.row();
		} else if ( isChild ) {
			sel = scene->currentIndex.parent().row();
		}

		int ct = nif->rowCount( cp );
		for ( int i = 0; i < ct; i++ ) {
			auto p = nif->getIndex( cp, i );

			auto trans = nif->get<Vector3>( p, "Translation" );
			auto rot = nif->get<Quat>( p, "Rotation" );
			//auto scale = nif->get<float>( p, "Scale" );

			Transform t;
			Matrix m;
			m.fromQuat( rot );
			t.rotation = m;
			t.translation = trans;
			t.scale = normalScale;

			scene->pushAndMultModelViewMatrix( t );

			if ( i == sel ) {
				scene->setGLColor( scene->highlightColor );
			} else {
				scene->setGLColor( scene->wireframeColor );
			}

			auto pos = Vector3( 0, 0, 0 );

			scene->drawDashLine( pos, Vector3( 0, 1, 0 ), 15 );
			scene->drawDashLine( pos, Vector3( 1, 0, 0 ), 15 );
			scene->drawDashLine( pos, Vector3( 0, 0, 1 ), 15 );
			scene->drawCircle( pos, Vector3( 0, 1, 0 ), 1.0f, 64 );

			scene->popModelViewMatrix();
		}
	}

	if ( currentBlock.endsWith( "Node" ) && scene->hasOption(Scene::ShowNodes) && scene->hasOption(Scene::ShowAxes) ) {
		Transform t;
		t.rotation = nif->get<Matrix>( scene->currentIndex, "Rotation" );
		t.scale = normalScale;

		scene->pushAndMultModelViewMatrix( t );

		auto pos = Vector3( 0, 0, 0 );

		scene->setGLColor( 0.0f, 1.0f, 0.0f, 1.0f );
		scene->drawDashLine( pos, Vector3( 0, 1, 0 ), 15 );
		scene->setGLColor( 1.0f, 0.0f, 0.0f, 1.0f );
		scene->drawDashLine( pos, Vector3( 1, 0, 0 ), 15 );
		scene->setGLColor( 0.0f, 0.0f, 1.0f, 1.0f );
		scene->drawDashLine( pos, Vector3( 0, 0, 1 ), 15 );

		scene->popModelViewMatrix();
	}

	if ( extraData )
		return;

	scene->loadModelViewMatrix( scene->view );

	Vector3 a = worldTrans().translation;
	if ( !parent ) {
		scene->drawPoints( &a );
	} else {
		Vector3 b = parent->worldTrans().translation;
		if ( scene->selecting ) {
			scene->setGLColor( color );
			scene->drawLine( a, b );
		} else {
			scene->setGLColor( color * FloatVector4( 1.0f, 1.0f, 1.0f, 0.8f ) );
			scene->drawLine( a, b );
			scene->setGLColor( color );
		}
		scene->pushAndMultModelViewMatrix( Transform( a, 1.0f ) );
		scene->drawPoints();
		scene->popModelViewMatrix();
	}

	for ( Node * node : children.list() ) {
		node->draw();
	}
}

void Node::drawVertexSelection( qsizetype numVerts, int i )
{
	glDepthFunc( GL_LEQUAL );

	scene->setGLColor( scene->wireframeColor );
	scene->setGLPointSize( GLView::Settings::vertexPointSize );
	scene->drawPoints( nullptr, size_t( numVerts ) );

	if ( i >= 0 && i < numVerts ) {
		glDepthFunc( GL_ALWAYS );
		scene->setGLColor( scene->highlightColor );
		scene->setGLPointSize( GLView::Settings::vertexPointSizeSelected );

		if ( scene->setupProgram( "selection.prog", GL_POINTS ) )
			scene->renderer->fn->glDrawArrays( GL_POINTS, GLint( i ), 1 );
	}
}

void Node::drawTriangleSelection( const QVector<Triangle> & triangles, int i, int n, int startVertex, int endVertex )
{
	if ( i < 0 || i >= triangles.size() || n < 1 )
		return;

	glDepthFunc( GL_ALWAYS );

	scene->setGLColor( scene->highlightColor );
	scene->setGLLineWidth( GLView::Settings::lineWidthWireframe );
	if ( !scene->setupProgram( "wireframe.prog", GL_TRIANGLES ) )
		return;

	n = std::min( n, int( triangles.size() - i ) );
	if ( endVertex < 0 ) {
		scene->renderer->fn->glDrawElements( GL_TRIANGLES, GLsizei( n ) * 3,
												GL_UNSIGNED_SHORT, (void *) ( qsizetype( i ) * 6 ) );
		return;
	}

	int	startPos = 0;
	int	endPos = 0;
	for ( ; n > 0; i++, n-- ) {
		const Triangle &	tri = triangles.at( i );
		if ( int( tri[0] ) >= startVertex && int( tri[0] ) < endVertex ) {
			if ( std::min< int >( tri[1], tri[2] ) >= startVertex && std::max< int >( tri[1], tri[2] ) < endVertex ) {
				endPos++;
				continue;
			}
			qDebug() << "triangle with multiple materials?" << i;
		}
		if ( endPos > startPos ) {
			scene->renderer->fn->glDrawElements( GL_TRIANGLES, GLsizei( endPos - startPos ) * 3,
													GL_UNSIGNED_SHORT, (void *) ( qsizetype( startPos ) * 6 ) );
		}
		startPos = i + 1;
	}
	if ( endPos > startPos ) {
		scene->renderer->fn->glDrawElements( GL_TRIANGLES, GLsizei( endPos - startPos ) * 3,
												GL_UNSIGNED_SHORT, (void *) ( qsizetype( startPos ) * 6 ) );
	}
}

void Node::drawTriangleIndex( const QVector<Vector3> & verts, const Triangle & t, int i )
{
	Vector3	position;
	int	n = 0;
	for ( int i = 0; i < 3; i++ ) {
		if ( qsizetype( t[i] ) < verts.size() ) {
			position += verts[t[i]];
			n++;
		}
	}
	if ( !n )
		return;
	position = position / float( n );
	scene->renderText( position, QString( "%1" ).arg( i ) );
}

void Node::drawHvkShape( const NifModel * nif, const QModelIndex & iShape, HvkShapeStackItem * stack,
							Scene * scene, FloatVector4 origin_color4fv, const Matrix4 & parentTransform )
{
	if ( !nif || !iShape.isValid() || !scene->isSelModeObject() || !scene->renderer )
		return;

	for ( auto i = stack; i; i = i->parent ) {
		if ( i->iShape == iShape )
			return;
	}

	QString name = nif->itemName( iShape );

	//qDebug() << "draw shape" << nif->getBlockNumber( iShape ) << name;

	if ( name.endsWith( QLatin1String("ListShape") ) ) {
		QModelIndex iShapes = nif->getIndex( iShape, "Sub Shapes" );

		if ( iShapes.isValid() ) {
			HvkShapeStackItem	shapeStack( iShape, stack );

			for ( int r = 0; r < nif->rowCount( iShapes ); r++ ) {
				if ( !scene->selecting ) {
					if ( scene->currentBlock == nif->getBlockIndex( nif->getLink( nif->getIndex( iShapes, r ) ) ) ) {
						// fix: add selected visual to havok meshes
						scene->setGLColor( scene->highlightColor );
						scene->setGLLineWidth( GLView::Settings::lineWidthHighlight );
					} else {
						if ( scene->currentBlock != iShape ) {
							// allow group highlighting
							scene->setGLColor( origin_color4fv );
							scene->setGLLineWidth( GLView::Settings::lineWidthWireframe * 0.625f );
						}
					}
				}

				drawHvkShape( nif, nif->getBlockIndex( nif->getLink( nif->getIndex( iShapes, r ) ) ), &shapeStack,
								scene, origin_color4fv, parentTransform );
			}
		}
		return;

	} else if ( name == "bhkTransformShape" || name == "bhkConvexTransformShape" || name == "bhkMoppBvTreeShape" ) {
		QModelIndex	iChild = nif->getBlockIndex( nif->getLink( iShape, "Shape" ) );
		if ( !scene->selecting && scene->currentBlock == iChild ) {
			// fix: add selected visual to havok meshes
			scene->setGLColor( scene->highlightColor );
			scene->setGLLineWidth( GLView::Settings::lineWidthWireframe );	// taken from "DrawTriangleSelection"
		}
		Matrix4	tm( parentTransform );
		if ( name.endsWith( QLatin1String("TransformShape") ) )
			tm.multiply4x3( nif->get<Matrix4>( iShape, "Transform" ) );
		HvkShapeStackItem	shapeStack( iShape, stack );
		drawHvkShape( nif, iChild, &shapeStack, scene, origin_color4fv, tm );
		return;
	}

	scene->loadModelViewMatrix( parentTransform );
	if ( scene->selecting )
		scene->setGLColor( getColorKeyFromID( nif->getBlockNumber( iShape ) ) );

	if ( name == "bhkSphereShape" ) {
		scene->drawSphere( Vector3(), nif->get<float>( iShape, "Radius" ) );

	} else if ( name == "bhkMultiSphereShape" ) {
		QModelIndex iSpheres = nif->getIndex( iShape, "Spheres" );

		for ( int r = 0; r < nif->rowCount( iSpheres ); r++ ) {
			scene->drawSphere( nif->get<Vector3>( nif->getIndex( iSpheres, r ), "Center" ),
								nif->get<float>( nif->getIndex( iSpheres, r ), "Radius" ) );
		}

	} else if ( name == "bhkBoxShape" ) {
		Vector3 v = nif->get<Vector3>( iShape, "Dimensions" );
		scene->drawBox( v, -v );

	} else if ( name == "bhkCapsuleShape" ) {
		scene->drawCapsule( nif->get<Vector3>( iShape, "First Point" ),
							nif->get<Vector3>( iShape, "Second Point" ), nif->get<float>( iShape, "Radius" ) );

	} else if ( name == "bhkCylinderShape" ) {
		scene->drawCylinder( Vector3( nif->get<Vector4>( iShape, "Vertex A" ) ),
								Vector3( nif->get<Vector4>( iShape, "Vertex B" ) ),
								nif->get<float>( iShape, "Cylinder Radius" ) );

	} else if ( name == "bhkNiTriStripsShape" ) {
		scene->pushAndMultModelViewMatrix( Transform( Vector3(), bhkInvScale( nif ) ) );

		scene->drawNiTSS( nif, iShape );
#if 0
		if ( Options::getHavokState() == HAVOK_SOLID ) {
			QColor c = Options::hlColor();
			c.setAlphaF( 0.3 );
			scene->setGLColor( c );

			scene->drawNiTSS( nif, iShape, true );
		}
#endif
		scene->popModelViewMatrix();

	} else if ( name == "bhkConvexVerticesShape" ) {
		scene->drawConvexHull( nif, iShape, 1.0 );
#if 0
		if ( Options::getHavokState() == HAVOK_SOLID ) {
			QColor c = Options::hlColor();
			c.setAlphaF( 0.3 );
			scene->setGLColor( c );

			scene->drawConvexHull( nif, iShape, havokScale, true );
		}
#endif

	} else if ( name == "bhkPackedNiTriStripsShape" || name == "hkPackedNiTriStripsData" ) {
		QModelIndex iData = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );
		QModelIndex iVerts, iTriangles;

		if ( iData.isValid()
			&& ( iVerts = nif->getIndex( iData, "Vertices" ) ).isValid() && nif->rowCount( iVerts ) >= 2
			&& ( iTriangles = nif->getIndex( iData, "Triangles" ) ).isValid() && nif->rowCount( iTriangles ) >= 1 ) {

			QVector<Vector3>	verts = nif->getArray<Vector3>( iVerts );
			QVector<Triangle>	triangles( nif->rowCount( iTriangles ) );
			for ( int i = 0; i < triangles.size(); i++ ) {
				// assume that "Triangle" item is in row 0 of "TriangleData"
				if ( auto iTriangle = nif->getIndex( nif->getIndex( iTriangles, i ), 0 ); iTriangle.isValid() )
					triangles[i] = nif->get<Triangle>( iTriangle );
			}

			scene->drawTriangles( verts.constData(), size_t( verts.size() ), nullptr, false, GL_TRIANGLES,
									size_t( triangles.size() ) * 3, GL_UNSIGNED_SHORT, triangles.constData() );

			// Handle Selection of hkPackedNiTriStripsData
			if ( scene->currentBlock == iData && !scene->selecting ) {
				int i = -1;
				QString n = scene->currentIndex.data( NifSkopeDisplayRole ).toString();
				QModelIndex iParent = scene->currentIndex.parent();

				if ( iParent.isValid() && iParent != iData ) {
					n = iParent.data( NifSkopeDisplayRole ).toString();
					i = scene->currentIndex.row();
				}

				if ( n == "Vertices" || n == "Normals" || n == "Vertex Colors" || n == "UV Sets" ) {
					drawVertexSelection( verts.size(), i );
				} else if ( n == "Faces" || n == "Triangles" ) {
					if ( i == -1 ) {
						glDepthFunc( GL_ALWAYS );
						scene->setGLColor( scene->highlightColor );
#if 0
						for ( int t = 0; t < triangles.size(); t++ )
							drawTriangleIndex( verts, triangles[t], t );
#endif
					} else if ( nif->isCompound( nif->itemStrType( scene->currentIndex ) ) ) {
						drawTriangleSelection( triangles, i );
#if 0
						drawTriangleIndex( verts, triangles[i], i );
#endif
					} else if ( nif->itemName( scene->currentIndex ) == "Normal" ) {
						Triangle tri = nif->get<Triangle>( scene->currentIndex.parent(), "Triangle" );
						Vector3 triCentre = ( verts.value( tri.v1() ) + verts.value( tri.v2() ) + verts.value( tri.v3() ) ) /  3.0;
						scene->setGLColor( scene->highlightColor );
						scene->setGLLineWidth( GLView::Settings::lineWidthWireframe );
						glDepthFunc( GL_ALWAYS );
						scene->drawLine( triCentre, triCentre + nif->get<Vector3>( scene->currentIndex ) );
					}
				} else if ( n == "Sub Shapes" ) {
					int start_vertex = 0;
					int end_vertex = 0;
					int num_vertices = nif->get<int>( scene->currentIndex, "Num Vertices" );

					int totalVerts = 0;
					if ( num_vertices > 0 ) {
						QModelIndex iParent = scene->currentIndex.parent();
						for ( int j = 0; j < i; j++ ) {
							totalVerts += nif->get<int>( nif->getIndex( iParent, j ), "Num Vertices" );
						}

						end_vertex += totalVerts + num_vertices;
						start_vertex += totalVerts;
					}

					drawTriangleSelection( triangles, 0, int( triangles.size() ), start_vertex, end_vertex );
#if 0
					for ( int t = 0; t < triangles.size(); t++ ) {
						Triangle tri = triangles.at( t );

						if ( start_vertex <= tri[0] && tri[0] < end_vertex ) {
							if ( start_vertex <= std::min( tri[1], tri[2] ) && std::max( tri[1], tri[2] ) < end_vertex )
								drawTriangleIndex( verts, tri, t );
						}
					}
#endif
				}

			} else if ( scene->currentBlock == iShape && !scene->selecting ) {
				// Handle Selection of bhkPackedNiTriStripsShape
				QString n = scene->currentIndex.data( NifSkopeDisplayRole ).toString();
				QModelIndex iParent = scene->currentIndex.parent();

				if ( iParent.isValid() && iParent != iShape ) {
					n = iParent.data( NifSkopeDisplayRole ).toString();
				}

				//qDebug() << n;
				// n == "Sub Shapes" if the array is selected and if an element of the array is selected
				// iParent != iShape only for the elements of the array
				if ( ( n == "Sub Shapes" ) && ( iParent != iShape ) ) {
					// get subshape vertex indices
					QModelIndex iSubShapes = iParent;
					QModelIndex iSubShape  = scene->currentIndex;
					int start_vertex = 0;
					int end_vertex = 0;

					for ( int subshape = 0; subshape < nif->rowCount( iSubShapes ); subshape++ ) {
						QModelIndex iCurrentSubShape = nif->getIndex( iSubShapes, subshape );
						int num_vertices = nif->get<int>( iCurrentSubShape, "Num Vertices" );
						//qDebug() << num_vertices;
						end_vertex += num_vertices;

						if ( iCurrentSubShape == iSubShape ) {
							break;
						} else {
							start_vertex += num_vertices;
						}
					}

					// highlight the triangles of the subshape
					drawTriangleSelection( triangles, 0, int( triangles.size() ), start_vertex, end_vertex );
#if 0
					for ( int t = 0; t < triangles.size(); t++ ) {
						Triangle tri = triangles.at( t );

						if ( start_vertex <= tri[0] && tri[0] < end_vertex ) {
							if ( start_vertex <= std::min( tri[1], tri[2] ) && std::max( tri[1], tri[2] ) < end_vertex )
								drawTriangleIndex( verts, tri, t );
						}
					}
#endif
				}
			}
		}

	} else if ( name == "bhkCompressedMeshShape" ) {
		scene->drawCMS( nif, iShape );
#if 0
		if ( Options::getHavokState() == HAVOK_SOLID ) {
			QColor c = Options::hlColor();
			c.setAlphaF( 0.3 );
			scene->setGLColor( c );

			scene->drawCMS( nif, iShape, true );
		}
#endif
	}
}

void Node::drawHvkConstraint( const NifModel * nif, const QModelIndex & iConstraint, Scene * scene )
{
	if ( !( nif && iConstraint.isValid() && scene && scene->hasOption(Scene::ShowConstraints)
			&& scene->isSelModeObject() ) ) {
		return;
	}

	Transform tBodyA;
	Transform tBodyB;

	auto iEntityA = bhkGetEntity( nif, iConstraint, "Entity A" );
	auto iEntityB = bhkGetEntity( nif, iConstraint, "Entity B" );
	if ( !iEntityA.isValid() || !iEntityB.isValid() )
		return;

	auto linkA = nif->getLink( iEntityA );
	auto linkB = nif->getLink( iEntityB );
	{
		const Transform *	p0 = scene->transformCache.find( bhkBodyTransKey( linkA ) );
		const Transform *	p1 = scene->transformCache.find( bhkBodyTransKey( linkB ) );
		if ( !p0 || !p1 )
			return;
		tBodyA = *p0;
		tBodyB = *p1;
	}

	auto hkFactor = bhkScaleMult( nif );
	auto hkFactorInv = 1.0 / hkFactor;

	tBodyA.scale = tBodyA.scale * hkFactorInv;
	tBodyB.scale = tBodyB.scale * hkFactorInv;

	FloatVector4 color_a( 0.8f, 0.6f, 0.0f, 1.0f );
	FloatVector4 color_b( 0.6f, 0.8f, 0.0f, 1.0f );

	float	lineWidth = GLView::Settings::lineWidthHighlight;
	if ( scene->selecting ) {
		color_a = getColorKeyFromID( nif->getBlockNumber( iConstraint ) );
		color_b = color_a;
		lineWidth = GLView::Settings::lineWidthSelect;	// make hitting a line a litlle bit more easy
	} else {
		if ( scene->currentBlock == nif->getBlockIndex( iConstraint ) ) {
			// fix: add selected visual to havok meshes
			color_a = scene->highlightColor;
			color_b.blendValues( color_a, 0x07 );
		}
	}
	scene->setGLLineWidth( lineWidth );

	Matrix4 vt = scene->view.toMatrix4();
	Matrix4 mBodyA = vt * tBodyA;
	Matrix4 mBodyB = vt * tBodyB;

	glEnable( GL_DEPTH_TEST );

	QString name = nif->itemName( iConstraint );

	QModelIndex iConstraintInfo;

	if ( name == "bhkMalleableConstraint" || name == "bhkBreakableConstraint" ) {
		if ( nif->getIndex( iConstraint, "Ragdoll" ).isValid() ) {
			name = "bhkRagdollConstraint";
			iConstraintInfo = nif->getIndex( iConstraint, "Ragdoll" );
		} else if ( nif->getIndex( iConstraint, "Limited Hinge" ).isValid() ) {
			name = "bhkLimitedHingeConstraint";
			iConstraintInfo = nif->getIndex( iConstraint, "Limited Hinge" );
		} else if ( nif->getIndex( iConstraint, "Hinge" ).isValid() ) {
			name = "bhkHingeConstraint";
			iConstraintInfo = nif->getIndex( iConstraint, "Hinge" );
		} else if ( nif->getIndex( iConstraint, "Stiff Spring" ).isValid() ) {
			name = "bhkStiffSpringConstraint";
			iConstraintInfo = nif->getIndex( iConstraint, "Stiff Spring" );
		}
	} else {
		iConstraintInfo = nif->getIndex( iConstraint, "Constraint" );
		if ( !iConstraintInfo.isValid() )
			iConstraintInfo = iConstraint;
	}

	Vector3 pivotA( nif->get<Vector4>( iConstraintInfo, "Pivot A" ) * hkFactor );
	Vector3 pivotB( nif->get<Vector4>( iConstraintInfo, "Pivot B" ) * hkFactor );

	if ( name == "bhkLimitedHingeConstraint" ) {
		const Vector3 axisA( nif->get<Vector4>( iConstraintInfo, "Axis A" ) );
		const Vector3 axisA1( nif->get<Vector4>( iConstraintInfo, "Perp Axis In A1" ) );
		const Vector3 axisA2( nif->get<Vector4>( iConstraintInfo, "Perp Axis In A2" ) );

		const Vector3 axisB( nif->get<Vector4>( iConstraintInfo, "Axis B" ) );
		const Vector3 axisB2( nif->get<Vector4>( iConstraintInfo, "Perp Axis In B2" ) );

		const float minAngle = nif->get<float>( iConstraintInfo, "Min Angle" );
		const float maxAngle = nif->get<float>( iConstraintInfo, "Max Angle" );

		scene->loadModelViewMatrix( mBodyA );

		scene->setGLColor( color_a );

		scene->drawPoints( &pivotA );
		scene->drawLine( pivotA, pivotA + axisA );
		scene->drawDashLine( pivotA, pivotA + axisA1, 14 );
		scene->drawDashLine( pivotA, pivotA + axisA2, 14 );
		scene->drawCircle( pivotA, axisA, 1.0f );
		scene->drawSolidArc( pivotA, axisA / 5, axisA2, axisA1, minAngle, maxAngle, 1.0f );

		scene->loadModelViewMatrix( mBodyB );

		scene->setGLColor( color_b );

		scene->drawPoints( &pivotB );
		scene->drawLine( pivotB, pivotB + axisB );
		scene->drawDashLine( pivotB + axisB2, pivotB, 14 );
		scene->drawDashLine( pivotB + Vector3::crossproduct( axisB2, axisB ), pivotB, 14 );
		scene->drawCircle( pivotB, axisB, 1.01f );
		scene->drawSolidArc( pivotB, axisB / 7, axisB2, Vector3::crossproduct( axisB2, axisB ),
								minAngle, maxAngle, 1.01f );

		scene->loadModelViewMatrix( mBodyA );

		float angle = Vector3::angle( tBodyA.rotation * axisA2, tBodyB.rotation * axisB2 );

		scene->setGLColor( color_a );

		scene->drawLine( pivotA, pivotA + axisA1 * cosf( angle ) + axisA2 * sinf( angle ) );
	} else if ( name == "bhkHingeConstraint" ) {
		const Vector3 axisA1( nif->get<Vector4>( iConstraintInfo, "Perp Axis In A1" ) );
		const Vector3 axisA2( nif->get<Vector4>( iConstraintInfo, "Perp Axis In A2" ) );
		const Vector3 axisA( Vector3::crossproduct( axisA1, axisA2 ) );

		const Vector3 axisB( nif->get<Vector4>( iConstraintInfo, "Axis B" ) );

		const Vector3 axisB1( axisB[1], axisB[2], axisB[0] );
		const Vector3 axisB2( Vector3::crossproduct( axisB, axisB1 ) );

		/*
		 * This should be correct but is visually strange...
		 *
		Vector3 axisB1temp;
		Vector3 axisB2temp;

		if ( nif->checkVersion( 0, 0x14000002 ) )
		{
		    Vector3 axisB1temp( axisB[1], axisB[2], axisB[0] );
		    Vector3 axisB2temp( Vector3::crossproduct( axisB, axisB1temp ) );
		}
		else if ( nif->checkVersion( 0x14020007, 0 ) )
		{
		    Vector3 axisB1temp( nif->get<Vector4>( iConstraintInfo, "Perp Axis In B1" ) );
		    Vector3 axisB2temp( nif->get<Vector4>( iConstraintInfo, "Perp Axis In B2" ) );
		}

		const Vector3 axisB1( axisB1temp );
		const Vector3 axisB2( axisB2temp );
		*/

		const float minAngle = (float)-PI;
		const float maxAngle = (float)+PI;

		scene->loadModelViewMatrix( mBodyA );

		scene->setGLColor( color_a );

		scene->drawPoints( &pivotA );
		scene->drawDashLine( pivotA, pivotA + axisA1 );
		scene->drawDashLine( pivotA, pivotA + axisA2 );
		scene->drawSolidArc( pivotA, axisA / 5, axisA2, axisA1, minAngle, maxAngle, 1.0f, 16 );

		scene->loadModelViewMatrix( mBodyB );

		scene->setGLColor( color_b );

		scene->drawPoints( &pivotB );
		scene->drawLine( pivotB, pivotB + axisB );
		scene->drawSolidArc( pivotB, axisB / 7, axisB2, axisB1, minAngle, maxAngle, 1.01f, 16 );
	} else if ( name == "bhkStiffSpringConstraint" ) {
		const float length = nif->get<float>( iConstraintInfo, "Length" );

		scene->loadModelViewMatrix( vt );

		scene->setGLColor( color_b );

		scene->drawSpring( pivotA, pivotB, length );
	} else if ( name == "bhkRagdollConstraint" ) {
		const Vector3 planeA( nif->get<Vector4>( iConstraintInfo, "Plane A" ) );
		const Vector3 planeB( nif->get<Vector4>( iConstraintInfo, "Plane B" ) );

		const Vector3 twistA( nif->get<Vector4>( iConstraintInfo, "Twist A" ) );
		const Vector3 twistB( nif->get<Vector4>( iConstraintInfo, "Twist B" ) );

		const float coneAngle( nif->get<float>( iConstraintInfo, "Cone Max Angle" ) );

		const float minPlaneAngle( nif->get<float>( iConstraintInfo, "Plane Min Angle" ) );
		const float maxPlaneAngle( nif->get<float>( iConstraintInfo, "Plane Max Angle" ) );

		// Unused? GCC complains
		/*
		const float minTwistAngle( nif->get<float>( iConstraintInfo, "Twist Min Angle" ) );
		const float maxTwistAngle( nif->get<float>( iConstraintInfo, "Twist Max Angle" ) );
		*/

		scene->loadModelViewMatrix( mBodyA );

		scene->setGLColor( color_a );

		scene->drawPoints( &pivotA );
		scene->drawLine( pivotA, pivotA + twistA );
		scene->drawDashLine( pivotA, pivotA + planeA, 14 );
		scene->drawRagdollCone( pivotA, twistA, planeA, coneAngle, minPlaneAngle, maxPlaneAngle );

		scene->loadModelViewMatrix( mBodyB );

		scene->setGLColor( color_b );

		scene->drawPoints( &pivotB );
		scene->drawLine( pivotB, pivotB + twistB );
		scene->drawDashLine( pivotB + planeB, pivotB, 14 );
		scene->drawRagdollCone( pivotB, twistB, planeB, coneAngle, minPlaneAngle, maxPlaneAngle );
	} else if ( name == "bhkPrismaticConstraint" ) {
		const Vector3 planeNormal( nif->get<Vector4>( iConstraintInfo, "Plane A" ) );
		const Vector3 slidingAxis( nif->get<Vector4>( iConstraintInfo, "Sliding A" ) );

		const float minDistance = nif->get<float>( iConstraintInfo, "Min Distance" );
		const float maxDistance = nif->get<float>( iConstraintInfo, "Max Distance" );

		const Vector3 d1 = pivotA + slidingAxis * minDistance;
		const Vector3 d2 = pivotA + slidingAxis * maxDistance;

		/* draw Pivot A and Plane */
		scene->loadModelViewMatrix( mBodyA );

		scene->setGLColor( color_a );

		scene->drawPoints( &pivotA );
		scene->drawLine( pivotA, pivotA + planeNormal );
		scene->drawDashLine( pivotA, d1, 14 );

		/* draw rail */
		if ( minDistance < maxDistance ) {
			scene->drawRail( d1, d2 );
		}

		/*draw first marker*/
		Transform t;
		float angle = atan2f( slidingAxis[1], slidingAxis[0] );

		if ( slidingAxis[0] < 0.0001f && slidingAxis[1] < 0.0001f ) {
			angle = float(HALF_PI);
		}

		t.translation = d1;
		t.rotation.fromEuler( 0.0f, 0.0f, angle );
		scene->multModelViewMatrix( t );

		angle = -asinf( slidingAxis[2] / slidingAxis.length() );
		t.translation = Vector3( 0.0f, 0.0f, 0.0f );
		t.rotation.fromEuler( 0.0f, angle, 0.0f );
		scene->multModelViewMatrix( t );

		GLMarker::BumperMarker01.drawMarker( scene, true );

		/*draw second marker*/
		t.translation = Vector3( minDistance < maxDistance ? ( d2 - d1 ).length() : 0.0f, 0.0f, 0.0f );
		t.rotation.fromEuler( 0.0f, 0.0f, (float)PI );
		scene->multModelViewMatrix( t );

		GLMarker::BumperMarker01.drawMarker( scene, true );

		/* draw Pivot B */
		scene->loadModelViewMatrix( mBodyB );

		scene->setGLColor( color_b );

		scene->drawPoints( &pivotB );
	}
}

void Node::drawHavok()
{
	if ( !scene->isSelModeObject() )
		return;

	// TODO: Why are all these here - "drawNodes", "drawFurn", "drawHavok"?
	// Idea: Make them go to their own classes in different cpp files
	for ( Node * node : children.list() ) {
		node->drawHavok();
	}

	auto nif = NifModel::fromValidIndex(iBlock);
	if ( !nif )
		return;

	scene->loadModelViewMatrix( scene->view );

	//Check if there's any old style collision bounding box set
	if ( nif->get<bool>( iBlock, "Has Bounding Box" ) == true ) {
		QModelIndex iBox = nif->getIndex( iBlock, "Bounding Box" );

		Transform bt;

		bt.translation = nif->get<Vector3>( iBox, "Translation" );
		bt.rotation = nif->get<Matrix>( iBox, "Rotation" );
		bt.scale = 1.0f;

		Vector3 rad = nif->get<Vector3>( iBox, "Radius" );

		// The Morrowind construction set seems to completely ignore the node transform
		//glMultMatrix( worldTrans() );
		scene->pushAndMultModelViewMatrix( bt );

		FloatVector4	color( 1.0f, 0.0f, 0.0f, 1.0f );
		if ( scene->selecting )
			color = getColorKeyFromID( nodeId );

		scene->setGLColor( color );
		scene->setGLLineWidth( GLView::Settings::lineWidthWireframe * 0.625f );
		scene->drawBox( rad, -rad );

		scene->popModelViewMatrix();
	}

	// Only Bethesda support after this
	if ( nif->getBSVersion() == 0 )
		return;

	// Draw BSMultiBound
	auto iBSMultiBound = nif->getBlockIndex( nif->getLink( iBlock, "Multi Bound" ), "BSMultiBound" );
	if ( iBSMultiBound.isValid() ) {

		auto iBSMultiBoundData = nif->getBlockIndex( nif->getLink( iBSMultiBound, "Data" ), "BSMultiBoundData" );
		if ( iBSMultiBoundData.isValid() ) {

			Vector3 a, b;

			scene->pushAndMultModelViewMatrix( worldTrans() );

			// BSMultiBoundAABB
			if ( nif->isNiBlock( iBSMultiBoundData, "BSMultiBoundAABB" ) ) {
				auto pos = nif->get<Vector3>( iBSMultiBoundData, "Position" );
				auto extent = nif->get<Vector3>( iBSMultiBoundData, "Extent" );

				a = pos + extent;
				b = pos - extent;
			}

			// BSMultiBoundOBB
			if ( nif->isNiBlock( iBSMultiBoundData, "BSMultiBoundOBB" ) ) {
				auto center = nif->get<Vector3>( iBSMultiBoundData, "Center" );
				auto size = nif->get<Vector3>( iBSMultiBoundData, "Size" );
				auto matrix = nif->get<Matrix>( iBSMultiBoundData, "Rotation" );

				a = size;
				b = -size;

				Transform t( center, 1.0f );
				t.rotation = matrix;
				scene->multModelViewMatrix( t );
			}

			FloatVector4	color( 1.0f, 1.0f, 1.0f, 0.6f );
			float	lineWidth = GLView::Settings::lineWidthWireframe * 0.625f;
			if ( scene->selecting ) {
				color = getColorKeyFromID( nif->getBlockNumber( iBSMultiBoundData ) );
				lineWidth = GLView::Settings::lineWidthSelect;
			}

			scene->setGLColor( color );
			scene->setGLLineWidth( lineWidth );
			scene->drawBox( a, b );

			scene->popModelViewMatrix();
		}
	}

	// Draw BSBound dimensions
	QModelIndex iExtraDataList = nif->getIndex( iBlock, "Extra Data List" );

	if ( iExtraDataList.isValid() ) {
		for ( int d = 0; d < nif->rowCount( iExtraDataList ); d++ ) {
			QModelIndex iBound = nif->getBlockIndex( nif->getLink( nif->getIndex( iExtraDataList, d ) ), "BSBound" );

			if ( !iBound.isValid() )
				continue;

			Vector3 center = nif->get<Vector3>( iBound, "Center" );
			Vector3 dim = nif->get<Vector3>( iBound, "Dimensions" );

			// Not sure if world transform is taken into account
			scene->pushAndMultModelViewMatrix( worldTrans() );

			FloatVector4	color( 1.0f, 0.0f, 0.0f, 1.0f );
			if ( scene->selecting )
				color = getColorKeyFromID( nif->getBlockNumber( iBound ) );

			scene->setGLColor( color );
			scene->setGLLineWidth( GLView::Settings::lineWidthWireframe * 0.625f );
			scene->drawBox( dim + center, -dim + center );

			scene->popModelViewMatrix();
		}
	}

	QModelIndex iObject = nif->getBlockIndex( nif->getLink( iBlock, "Collision Object" ) );
	if ( !iObject.isValid() )
		return;

	QModelIndex iBody = nif->getBlockIndex( nif->getLink( iObject, "Body" ) );

	if ( const Transform * t = scene->transformCache.find( bhkBodyTransKey( nif->getBlockNumber( iBody ) ) ); t )
		scene->multModelViewMatrix( *t );

	//qDebug() << "draw obj" << nif->getBlockNumber( iObject ) << nif->itemName( iObject );

	if ( !scene->selecting ) {
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LEQUAL );
	}

	scene->setGLPointSize( GLView::Settings::vertexPointSize );
	scene->setGLLineWidth( GLView::Settings::lineWidthWireframe );

	static const FloatVector4 colors[8] = {
		{ 0.0f, 1.0f, 0.0f, 1.0f },
		{ 1.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 0.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 0.5f, 0.5f, 1.0f, 1.0f },
		{ 1.0f, 0.8f, 0.0f, 1.0f },
		{ 1.0f, 0.8f, 0.4f, 1.0f },
		{ 0.0f, 1.0f, 1.0f, 1.0f }
	};

	int color_index = nif->get<int>( iBody, "Layer" ) & 7;
	scene->setGLColor( colors[ color_index ] );

	if ( !scene->selecting ) {
		if ( scene->currentBlock == nif->getBlockIndex( nif->getLink( iBody, "Shape" ) ) ) {
			// fix: add selected visual to havok meshes
			scene->setGLColor( scene->highlightColor );
			scene->setGLLineWidth( GLView::Settings::lineWidthHighlight );
			//scene->setGLPointSize( GLView::Settings::vertexSelectPointSize );
		}
	} else {
		scene->setGLLineWidth( GLView::Settings::lineWidthSelect );	// make selection click a little more easy
	}

	Matrix4	m = *( scene->currentModelViewMatrix );
	drawHvkShape( nif, nif->getBlockIndex( nif->getLink( iBody, "Shape" ) ), nullptr, scene, colors[ color_index ], m );

	if ( scene->selecting && scene->hasOption(Scene::ShowAxes) ) {
		scene->setGLColor( getColorKeyFromID( nif->getBlockNumber( iBody ) ) );
		glDepthFunc( GL_ALWAYS );
		scene->drawAxes( Vector3( nif->get<Vector4>( iBody, "Center" ) ), 1.0f / bhkScaleMult( nif ), false );
		glDepthFunc( GL_LEQUAL );
	} else if ( scene->hasOption(Scene::ShowAxes) ) {
		scene->drawAxes( Vector3( nif->get<Vector4>( iBody, "Center" ) ), 1.0f / bhkScaleMult( nif ) );
	}

	for ( const auto l : nif->getLinkArray( iBody, "Constraints" ) ) {
		QModelIndex iConstraint = nif->getBlockIndex( l );

		if ( nif->blockInherits( iConstraint, "bhkConstraint" ) )
			drawHvkConstraint( nif, iConstraint, scene );
	}
}

void Node::drawFurnitureMarker( const NifModel * nif, const QModelIndex & iPosition )
{
	Vector3 offs = nif->get<Vector3>( iPosition, "Offset" );
	quint16 orient = nif->get<quint16>( iPosition, "Orientation" );
	quint8 ref1 = nif->get<quint8>( iPosition, "Position Ref 1" );
	quint8 ref2 = nif->get<quint8>( iPosition, "Position Ref 2" );

	const GLMarker * mark[5];
	Vector3 flip[5];
	Vector3 pos( 1, 1, 1 );
	Vector3 neg( -1, 1, 1 );

	float xOffset = 0.0f;
	float zOffset = 0.0f;
	float yOffset = 0.0f;
	float roll;

	int i = 0;

	if ( ref1 == 0 ) {
		float heading = nif->get<float>( iPosition, "Heading" );
		quint16 type = nif->get<quint16>( iPosition, "Animation Type" );
		int entry = nif->get<int>( iPosition, "Entry Properties" );

		if ( type == 0 ) return;

		// Sit=1, Sleep=2, Lean=3
		// Front=1, Behind=2, Right=4, Left=8, Up=16(0x10)

		switch ( type ) {
		case 1:
			// Sit Type

			zOffset = -34.00f;

			if ( entry & 0x1 ) {
				// Chair Front
				flip[i] = pos;
				mark[i] = &GLMarker::ChairFront;
				i++;
			}
			if ( entry & 0x2 ) {
				// Chair Behind
				flip[i] = pos;
				mark[i] = &GLMarker::ChairBehind;
				i++;
			}
			if ( entry & 0x4 ) {
				// Chair Right
				flip[i] = neg;
				mark[i] = &GLMarker::ChairLeft;
				i++;
			}
			if ( entry & 0x8 ) {
				// Chair Left
				flip[i] = pos;
				mark[i] = &GLMarker::ChairLeft;
				i++;
			}
			break;
		case 2:
			// Sleep Type

			zOffset = -34.00f;

			if ( entry & 0x1 ) {
				// Bed Front
				//flip[i] = pos;
				//mark[i] = &GLMarker::FurnitureMarker03;
				//i++;
			}
			if ( entry & 0x2 ) {
				// Bed Behind
				//flip[i] = pos;
				//mark[i] = &GLMarker::FurnitureMarker04;
				//i++;
			}
			if ( entry & 0x4 ) {
				// Bed Right
				flip[i] = neg;
				mark[i] = &GLMarker::BedLeft;
				i++;
			}
			if ( entry & 0x8 ) {
				// Bed Left
				flip[i] = pos;
				mark[i] = &GLMarker::BedLeft;
				i++;
			}
			if ( entry & 0x10 ) {
				// Bed Up????
				// This is sometimes used as a real bed position
				// Other times it is a dummy
				flip[i] = neg;
				mark[i] = &GLMarker::BedLeft;
				i++;
			}
			break;
		case 3:
			break;
		default:
			break;
		}

		roll = -heading;
	} else {
		if ( ref1 != ref2 ) {
			qDebug() << "Position Ref 1 and 2 are not equal";
			return;
		}

		switch ( ref1 ) {
		case 1:
			mark[0] = &GLMarker::FurnitureMarker01; // Single Bed
			break;

		case 2:
			flip[0] = neg;
			mark[0] = &GLMarker::FurnitureMarker01;
			break;

		case 3:
			mark[0] = &GLMarker::FurnitureMarker03; // Ground Bed?
			break;

		case 4:
			mark[0] = &GLMarker::FurnitureMarker04; // Ground Bed? Behind
			break;

		case 11:
			mark[0] = &GLMarker::FurnitureMarker11; // Chair Left
			break;

		case 12:
			flip[0] = neg;
			mark[0] = &GLMarker::FurnitureMarker11;
			break;

		case 13:
			mark[0] = &GLMarker::FurnitureMarker13; // Chair Behind
			break;

		case 14:
			mark[0] = &GLMarker::FurnitureMarker14; // Chair Front
			break;

		default:
			qDebug() << "Unknown furniture marker " << ref1;
			return;
		}

		i = 1;

		// TODO: FIX: This makes no sense
		roll = float( orient ) / 6284.0 * 2.0 * (-M_PI);
	}

	if ( scene->selecting ) {
		GLint id = ( nif->getBlockNumber( iPosition ) & 0xffff ) | ( ( iPosition.row() & 0xffff ) << 16 );
		scene->setGLColor( getColorKeyFromID( id ) );
	}

	for ( int n = 0; n < i; n++ ) {
		Transform t( offs + Vector3( xOffset, yOffset, zOffset ), 1.0f );
		t.rotation.fromEuler( 0, 0, roll );

		scene->pushAndMultModelViewMatrix( t );
		scene->multModelViewMatrix( Transform( Vector3(), flip[n] ) );

		mark[n]->drawMarker( scene );

		scene->popModelViewMatrix();
	}
}

void Node::drawFurn()
{
	for ( Node * node : children.list() ) {
		node->drawFurn();
	}

	auto nif = NifModel::fromValidIndex(iBlock);
	if ( !nif )
		return;

	if ( !scene->isSelModeObject() )
		return;

	QModelIndex iExtraDataList = nif->getIndex( iBlock, "Extra Data List" );

	if ( !iExtraDataList.isValid() )
		return;

	if ( !scene->selecting ) {
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_FALSE );
		glDepthFunc( GL_LEQUAL );
		glDisable( GL_CULL_FACE );
		scene->setGLColor( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	scene->setGLLineWidth( GLView::Settings::lineWidthWireframe * 0.625f );

	scene->loadModelViewMatrix( viewTrans() );

	for ( int p = 0; p < nif->rowCount( iExtraDataList ); p++ ) {
		// DONE: never seen Furn in nifs, so there may be a need of a fix here later - saw one, fixed a bug
		QModelIndex iFurnMark = nif->getBlockIndex( nif->getLink( nif->getIndex( iExtraDataList, p ) ), "BSFurnitureMarker" );

		if ( !iFurnMark.isValid() )
			continue;

		QModelIndex iPositions = nif->getIndex( iFurnMark, "Positions" );

		if ( !iPositions.isValid() )
			break;

		for ( int j = 0; j < nif->rowCount( iPositions ); j++ ) {
			QModelIndex iPosition = nif->getIndex( iPositions, j );

			if ( scene->currentIndex == iPosition )
				scene->setGLColor( scene->highlightColor );
			else
				scene->setGLColor( scene->wireframeColor );

			drawFurnitureMarker( nif, iPosition );
		}
	}
}

void Node::drawShapes( NodeList * secondPass )
{
	if ( isHidden() )
		return;

	if ( presorted )
		children.orderedNodeSort();

	for ( Node * node : children.list() )
		node->drawShapes( secondPass );
}

#define Farg( X ) arg( X, 0, 'f', 5 )

QString trans2string( Transform t )
{
	float xr, yr, zr;
	t.rotation.toEuler( xr, yr, zr );
	return QString( "translation  X %1, Y %2, Z %3\n" ).Farg( t.translation[0] ).Farg( t.translation[1] ).Farg( t.translation[2] )
	       +   QString( "rotation     Y %1, P %2, R %3  " ).Farg( rad2deg(xr) ).Farg( rad2deg(yr) ).Farg( rad2deg(zr) )
	       +   QString( "( (%1, %2, %3), " ).Farg( t.rotation( 0, 0 ) ).Farg( t.rotation( 0, 1 ) ).Farg( t.rotation( 0, 2 ) )
	       +   QString( "(%1, %2, %3), " ).Farg( t.rotation( 1, 0 ) ).Farg( t.rotation( 1, 1 ) ).Farg( t.rotation( 1, 2 ) )
	       +   QString( "(%1, %2, %3) )\n" ).Farg( t.rotation( 2, 0 ) ).Farg( t.rotation( 2, 1 ) ).Farg( t.rotation( 2, 2 ) )
	       +   QString( "scale        %1\n" ).Farg( t.scale );
}

QString Node::textStats() const
{
	return QString( "%1\n\nglobal\n%2\nlocal\n%3\n" ).arg( name, trans2string( worldTrans() ), trans2string( localTrans() ) );
}

BoundSphere Node::bounds() const
{
	BoundSphere boundsphere;

	// the node itself
	if ( scene->hasOption(Scene::ShowNodes) || scene->hasOption(Scene::ShowCollision) ) {
		boundsphere |= BoundSphere( worldTrans().translation, 0 );
	}

	auto nif = NifModel::fromValidIndex(iBlock);
	if ( !nif )
		return boundsphere;

	// old style collision bounding box
	if ( nif->get<bool>( iBlock, "Has Bounding Box" ) == true ) {
		QModelIndex iBox = nif->getIndex( iBlock, "Bounding Box" );
		Vector3 trans = nif->get<Vector3>( iBox, "Translation" );
		Vector3 rad = nif->get<Vector3>( iBox, "Radius" );
		boundsphere |= BoundSphere( trans, rad.length() );
	}

	if ( nif->itemStrType( iBlock ) == "NiMesh" )
		boundsphere |= BoundSphere( nif, iBlock );

	// BSBound collision bounding box
	QModelIndex iExtraDataList = nif->getIndex( iBlock, "Extra Data List" );

	if ( iExtraDataList.isValid() ) {
		for ( int d = 0; d < nif->rowCount( iExtraDataList ); d++ ) {
			QModelIndex iBound = nif->getBlockIndex( nif->getLink( nif->getIndex( iExtraDataList, d ) ), "BSBound" );

			if ( !iBound.isValid() )
				continue;

			Vector3 center = nif->get<Vector3>( iBound, "Center" );
			Vector3 dim = nif->get<Vector3>( iBound, "Dimensions" );
			boundsphere |= BoundSphere( center, dim.length() );
		}
	}

	return boundsphere;
}


LODNode::LODNode( Scene * scene, const QModelIndex & iBlock )
	: Node( scene, iBlock )
{
}

void LODNode::clear()
{
	Node::clear();
	ranges.clear();
}

void LODNode::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Node::updateImpl( nif, index );

	if ( ( index == iBlock ) || ( iData.isValid() && index == iData ) ) {
		ranges.clear();
		iData = nif->getBlockIndex( nif->getLink( iBlock, "LOD Level Data" ), "NiRangeLODData" );
		QModelIndex iLevels;

		if ( iData.isValid() ) {
			center  = nif->get<Vector3>( iData, "LOD Center" );
			iLevels = nif->getIndex( iData, "LOD Levels" );
		} else {
			center  = nif->get<Vector3>( iBlock, "LOD Center" );
			iLevels = nif->getIndex( iBlock, "LOD Levels" );
		}

		if ( iLevels.isValid() ) {
			for ( int r = 0; r < nif->rowCount( iLevels ); r++ ) {
				ranges.append( { nif->get<float>( nif->getIndex( iLevels, r ), "Near Extent" ),
				                 nif->get<float>( nif->getIndex( iLevels, r ), "Far Extent" ) }
				);
			}
		}
	}
}

void LODNode::transform()
{
	Node::transform();

	if ( children.list().isEmpty() )
		return;

	if ( ranges.isEmpty() ) {
		for ( Node * child : children.list() ) {
			child->flags.node.hidden = true;
		}
		children.list().first()->flags.node.hidden = false;
		return;
	}

	float distance = ( viewTrans() * center ).length();

	int c = 0;
	for ( Node * child : children.list() ) {
		if ( c < ranges.count() )
			child->flags.node.hidden = !( ranges[c].first <= distance && distance < ranges[c].second );
		else
			child->flags.node.hidden = true;

		c++;
	}
}


BillboardNode::BillboardNode( Scene * scene, const QModelIndex & iBlock )
	: Node( scene, iBlock )
{
}

const Transform & BillboardNode::viewTrans() const
{
	std::uint64_t	key = viewTransKey( nodeId );
	Transform *	p;
	if ( scene->transformCache.find( p, key ) )
		return *p;

	Transform t;

	if ( parent )
		t = parent->viewTrans() * local;
	else
		t = scene->view * local;

	t.rotation = Matrix();

	*p = t;
	return *p;
}
