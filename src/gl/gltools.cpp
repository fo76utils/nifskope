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

#include "gltools.h"

#include "model/nifmodel.h"
#include "glview.h"
#include "gl/renderer.h"

#include <QMap>
#include <QStack>
#include <QVector>

#include <stack>
#include <map>
#include <algorithm>
#include <functional>

#include "fp32vec4.hpp"
#include "miniball/Seb.h"

//! \file gltools.cpp GL helper functions

BoneData::BoneData( const NifModel * nif, const QModelIndex & index, int b )
{
	trans  = Transform( nif, index );
	auto sph = BoundSphere( nif, index );
	center = sph.center;
	radius = sph.radius;
	bone = b;
}

void BoneData::setTransform( const NifModel * nif, const QModelIndex & index )
{
	trans = Transform( nif, index );
	auto sph = BoundSphere( nif, index );
	center = sph.center;
	radius = sph.radius;
}


SkinPartition::SkinPartition( const NifModel * nif, const QModelIndex & index )
{
	numWeightsPerVertex = nif->get<int>( index, "Num Weights Per Vertex" );

	vertexMap = nif->getArray<int>( index, "Vertex Map" );

	if ( vertexMap.isEmpty() ) {
		vertexMap.resize( nif->get<int>( index, "Num Vertices" ) );

		for ( int x = 0; x < vertexMap.count(); x++ )
			vertexMap[x] = x;
	}

	boneMap = nif->getArray<int>( index, "Bones" );

	QModelIndex iWeights = nif->getIndex( index, "Vertex Weights" );
	QModelIndex iBoneIndices = nif->getIndex( index, "Bone Indices" );

	weights.resize( vertexMap.count() * numWeightsPerVertex );

	for ( int v = 0; v < vertexMap.count(); v++ ) {
		for ( int w = 0; w < numWeightsPerVertex; w++ ) {
			QModelIndex iw = nif->getIndex( nif->getIndex( iWeights, v ), w );
			QModelIndex ib = nif->getIndex( nif->getIndex( iBoneIndices, v ), w );

			weights[ v * numWeightsPerVertex + w ].first  = ( ib.isValid() ? nif->get<int>( ib ) : 0 );
			weights[ v * numWeightsPerVertex + w ].second = ( iw.isValid() ? nif->get<float>( iw ) : 0 );
		}
	}

	QModelIndex iStrips = nif->getIndex( index, "Strips" );

	for ( int s = 0; s < nif->rowCount( iStrips ); s++ ) {
		tristrips << nif->getArray<quint16>( nif->getIndex( iStrips, s ) );
	}

	triangles = nif->getArray<Triangle>( index, "Triangles" );
}

QVector<Triangle> SkinPartition::getRemappedTriangles() const
{
	QVector<Triangle> tris;

	for ( const auto& t : triangles )
		tris << Triangle( vertexMap[t.v1()], vertexMap[t.v2()], vertexMap[t.v3()] );

	return tris;
}

QVector<QVector<quint16>> SkinPartition::getRemappedTristrips() const
{
	QVector<QVector<quint16>> tris;

	for ( const auto& t : tristrips ) {
		QVector<quint16> points;
		for ( const auto& p : t )
			points << vertexMap[p];
		tris << points;
	}

	return tris;
}

/*
 *  Bound Sphere
 */


BoundSphere::BoundSphere()
{
	radius = -1;
}

BoundSphere::BoundSphere( const Vector3 & c, float r )
{
	center = c;
	radius = r;
}

BoundSphere::BoundSphere( const BoundSphere & other )
{
	operator=( other );
}

BoundSphere::BoundSphere( const NifModel * nif, const QModelIndex & index )
{
	auto idx = index;
	auto sph = nif->getIndex( idx, "Bounding Sphere" );
	if ( sph.isValid() )
		idx = sph;

	center = nif->get<Vector3>( idx, "Center" );
	radius = nif->get<float>( idx, "Radius" );
}

struct BoundSphereVertexData
{
	const Vector3 *	startp;
	const Vector3 *	endp;
	inline BoundSphereVertexData( const Vector3 * verts, qsizetype vertexCnt )
		: startp( verts ), endp( verts + vertexCnt )
	{
	}
	inline size_t size() const
	{
		return size_t( endp - startp );
	}
	inline const Vector3 * data() const
	{
		return startp;
	}
	inline const Vector3 * begin() const
	{
		return startp;
	}
	inline const Vector3 * end() const
	{
		return endp;
	}
	inline const Vector3 & operator[]( size_t i ) const
	{
		return startp[i];
	}
};

BoundSphere::BoundSphere( const Vector3 * vertexData, qsizetype vertexCnt, bool useMiniball )
{
	if ( vertexCnt < 1 ) {
		center = Vector3();
		radius = -1;
		return;
	}
	BoundSphereVertexData	verts( vertexData, vertexCnt );

	// old algorithm: center of bounding sphere = bounds1 = centroid of verts
	FloatVector4	bounds1( 0.0f );
	// p1 and p2 are searched for Ritter's algorithm
	FloatVector4	p0( verts[0] );
	FloatVector4	p1( p0 );
	float	maxDistSqr = 0.0f;
	for ( const auto & v : verts ) {
		FloatVector4	tmp( v );
		bounds1 += tmp;
		float	d = ( tmp - p0 ).dotProduct3( tmp - p0 );
		if ( d > maxDistSqr ) {
			p1 = tmp;
			maxDistSqr = d;
		}
	}
	bounds1 /= float( int(vertexCnt) );

	FloatVector4	bounds2;
	if ( vertexCnt < 3 ) {
		// bounds2 = center of bounding sphere,
		bounds2 = bounds1;
	} else if ( !useMiniball ) {
		// calculated with an improved version of Ritter's algorithm,
		maxDistSqr = 0.0f;
		FloatVector4	p2( p1 );
		for ( const auto & v : verts ) {
			FloatVector4	tmp( v );
			float	d = ( tmp - p1 ).dotProduct3( tmp - p1 );
			if ( d > maxDistSqr ) {
				p2 = tmp;
				maxDistSqr = d;
			}
		}

		bounds2 = ( p1 + p2 ) * 0.5f;
		float	radiusSqr = maxDistSqr * 0.25f;

		if ( radiusSqr > 1.0e-10f ) {
			FloatVector4	p3( p1 );
			maxDistSqr = radiusSqr;
			for ( const auto & v : verts ) {
				// find the point (p3) most distant from (p1 + p2) / 2
				FloatVector4	tmp( v );
				float	d = ( tmp - p1 ).dotProduct3( tmp - p1 );
				if ( d > maxDistSqr ) [[unlikely]] {
					p3 = tmp;
					maxDistSqr = d;
				}
			}
			if ( maxDistSqr > ( radiusSqr * 1.000001f ) ) {
				// calculate the circumsphere of p1, p2 and p3
				FloatVector4	a( p1 - p3 );
				FloatVector4	b( p2 - p3 );
				float	a2 = a.dotProduct3( a );
				float	b2 = b.dotProduct3( b );
				FloatVector4	axb( a.crossProduct3( b ) );
				float	d = axb.dotProduct3( axb );
				if ( d > 0.0f ) {
					FloatVector4	c( ( b * a2 ) - ( a * b2 ) );
					c = c.crossProduct3( axb ) / ( d * 2.0f );
					bounds2 = p3 + c;
					radiusSqr = c.dotProduct3( c );
				}
			}
		}

		for ( const auto & v : verts ) {
			FloatVector4	tmp( v );
			float	d = ( tmp - bounds2 ).dotProduct3( tmp - bounds2 );
			if ( d > radiusSqr ) [[unlikely]] {
				if ( radiusSqr > 0.0f ) {
					float	radius1 = float( std::sqrt( radiusSqr ) );
					float	radius2 = float( std::sqrt( d ) );
					bounds2 += ( tmp - bounds2 ) * ( ( radius2 - radius1 ) * 0.5f / radius2 );
					radiusSqr = ( radiusSqr + d ) * 0.25f + ( radius1 * radius2 * 0.5f );
				} else {
					radiusSqr = d * 0.25f;
					bounds2 = ( bounds2 + tmp ) * 0.5f;
				}
			}
		}
	} else {
		// or Miniball
		SEB_NAMESPACE::Smallest_enclosing_ball<float, Vector3, BoundSphereVertexData>	mb( 3, verts );
		auto	i = mb.center_begin();
		bounds2 = FloatVector4( float(i[0]), float(i[1]), float(i[2]), 0.0f );
	}

	float	rSqr1 = 0.0f;
	float	rSqr2 = 0.0f;
	for ( const auto & v : verts ) {
		FloatVector4	tmp( v );
		rSqr1 = std::max( rSqr1, ( tmp - bounds1 ).dotProduct3( tmp - bounds1 ) );
		rSqr2 = std::max( rSqr2, ( tmp - bounds2 ).dotProduct3( tmp - bounds2 ) );
	}
	bounds1[3] = float( std::sqrt( rSqr1 ) );
	bounds2[3] = float( std::sqrt( rSqr2 ) );

	// use the result of whichever method gives a smaller radius
	if ( bounds2[3] < bounds1[3] ) [[likely]]
		bounds1 = bounds2;
	center = Vector3( bounds1 );
	radius = bounds1[3];
}

void BoundSphere::update( NifModel * nif, const QModelIndex & index )
{
	auto idx = index;
	auto sph = nif->getIndex( idx, "Bounding Sphere" );
	if ( sph.isValid() )
		idx = sph;

	nif->set<Vector3>( idx, "Center", center );
	nif->set<float>( idx, "Radius", radius );
}

void BoundSphere::setBounds( NifModel * nif, const QModelIndex & index, const Vector3 & center, float radius )
{
	BoundSphere( center, radius ).update( nif, index );
}

BoundSphere & BoundSphere::operator=( const BoundSphere & o )
{
	center = o.center;
	radius = o.radius;
	return *this;
}

BoundSphere & BoundSphere::operator|=( const BoundSphere & o )
{
	FloatVector4	bounds1( center[0], center[1], center[2], radius );
	FloatVector4	bounds2( o.center[0], o.center[1], o.center[2], o.radius );
	if ( !( bounds1[3] >= bounds2[3] ) )
		std::swap( bounds1, bounds2 );

	float	r2 = bounds2[3];
	if ( r2 >= 0.0f ) {
		float	r1 = bounds1[3];

		FloatVector4	a( bounds2 - bounds1 );
		float	d = a.dotProduct3( a );

		if ( d > 0.0f ) {
			d = float( std::sqrt( d ) );
			if ( r1 < ( d + r2 ) ) {
				float	newRadius = ( r1 + r2 + d ) * 0.5f;

				bounds1 += a * ( ( newRadius - r1 ) / d );
				bounds1[3] = newRadius;
			}
		}
	}

	center[0] = bounds1[0];
	center[1] = bounds1[1];
	center[2] = bounds1[2];
	radius = bounds1[3];
	return *this;
}

BoundSphere BoundSphere::operator|( const BoundSphere & other )
{
	BoundSphere b( *this );
	b |= other;
	return b;
}

BoundSphere & BoundSphere::apply( const Transform & t )
{
	center  = t * center;
	radius *= fabs( t.scale );
	return *this;
}

BoundSphere & BoundSphere::applyInv( const Transform & t )
{
	center  = t.rotation.inverted() * ( center - t.translation ) / t.scale;
	radius /= fabs( t.scale );
	return *this;
}

BoundSphere operator*( const Transform & t, const BoundSphere & sphere )
{
	BoundSphere bs( sphere );
	return bs.apply( t );
}


/*
 * draw primitives
 */

static const float	defaultAttrData[12] = {
	1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f
};

const float * const	Scene::defaultVertexAttrs[16] = {
	// position, color, normal, tangent
	&( defaultAttrData[4] ), &( defaultAttrData[0] ), &( defaultAttrData[6] ), &( defaultAttrData[9] ),
	// bitangent, weights0, weights1, texcoord0
	&( defaultAttrData[3] ), &( defaultAttrData[4] ), &( defaultAttrData[4] ), &( defaultAttrData[4] ),
	// texcoord1..texcoord8
	&( defaultAttrData[4] ), &( defaultAttrData[4] ), &( defaultAttrData[4] ), &( defaultAttrData[4] ),
	&( defaultAttrData[4] ), &( defaultAttrData[4] ), &( defaultAttrData[4] ), &( defaultAttrData[4] )
};

Vector3 * Scene::allocateVertexAttr( size_t numVerts, FloatVector4 ** colors )
{
	if ( ( numVerts - 1 ) & size_t( -65536 ) ) [[unlikely]] {
		if ( colors )
			*colors = nullptr;
		return nullptr;
	}
	size_t	n = ( numVerts * 3 + 3 ) >> 2;
	size_t	n2 = ( !colors ? n : n + numVerts );
	if ( n2 > vertexAttrBuf.size() )
		vertexAttrBuf.resize( n2 );
	FloatVector4 *	p = vertexAttrBuf.data();
	if ( colors )
		*colors = p + n;
	return reinterpret_cast< Vector3 * >( p );
}

NifSkopeOpenGLContext::Program * Scene::useProgram( std::string_view name )
{
	NifSkopeOpenGLContext *	context = renderer;
	if ( !context ) [[unlikely]]
		return nullptr;
	auto	prog = context->getCurrentProgram();
	if ( prog && prog->name == name )
		return prog;
	return context->useProgram( name );
}

void Scene::setGLColor( const QColor & c )
{
	currentGLColor = FloatVector4( Color4( c ) );
}

void Scene::drawPoints( const Vector3 * positions, size_t numVerts )
{
	auto	prog = useProgram( "selection.prog" );
	if ( !prog || numVerts < 1 )
		return;
	NifSkopeOpenGLContext *	context = renderer;
	prog->uni4f( "vertexColorOverride", FloatVector4( 1.0e-15f ).maxValues( currentGLColor ) );
	prog->uni1i( "selectionParam", -1 );

	float	pointSize = currentGLPointSize;
	if ( selecting ) {
		prog->uni1i( "selectionFlags", 0x0003 );
		glDisable( GL_BLEND );
	} else {
		pointSize += 0.5f;
		prog->uni1i( "selectionFlags", ( roundFloat( std::min( pointSize * 8.0f, 255.0f ) ) << 8 ) | 0x0002 );
		glEnable( GL_BLEND );
		context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}
	glPointSize( pointSize );

	if ( !positions ) {
		prog->uni4m( "modelViewMatrix", *currentModelViewMatrix );
	} else if ( numVerts < 2 ) {
		prog->uni4m( "modelViewMatrix", *currentModelViewMatrix * Transform( *positions, 1.0f ) );
		context->bindShape( 1, 0x03, 0, defaultVertexAttrs, nullptr );
	} else {
		prog->uni4m( "modelViewMatrix", *currentModelViewMatrix );
		const float *	attrData = &( positions[0][0] );
		context->bindShape( (unsigned int) numVerts, 0x03, 0, &attrData, nullptr );
	}
	context->fn->glDrawArrays( GL_POINTS, 0, GLsizei( numVerts ) );
}

void Scene::drawLine( const Vector3 & a, const Vector3 & b )
{
	auto	prog = useProgram( "lines.prog" );
	if ( !prog )
		return;
	NifSkopeOpenGLContext *	context = renderer;
	prog->uni4m( "modelViewMatrix", *currentModelViewMatrix * Transform( a, b - a ) );
	prog->uni4f( "vertexColorOverride", FloatVector4( 1.0e-15f ).maxValues( currentGLColor ) );
	prog->uni1f( "lineWidth", currentGLLineWidth );

	if ( selecting ) {
		glDisable( GL_BLEND );
	} else {
		glEnable( GL_BLEND );
		context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}

	static const float	positions[6] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
	const float *	attrData = positions;
	context->bindShape( 2, 0x03, 0, &attrData, nullptr );
	context->fn->glDrawArrays( GL_LINES, 0, 2 );
}

void Scene::drawLines( const Vector3 * positions, size_t numVerts, const FloatVector4 * colors,
						unsigned int elementMode )
{
	auto	prog = useProgram( "lines.prog" );
	if ( !prog )
		return;
	NifSkopeOpenGLContext *	context = renderer;
	prog->uni4m( "modelViewMatrix", *currentModelViewMatrix );
	prog->uni1f( "lineWidth", currentGLLineWidth );

	if ( selecting ) {
		glDisable( GL_BLEND );
	} else {
		glEnable( GL_BLEND );
		context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}

	FloatVector4	color( currentGLColor );
	const float *	attrData[2];
	attrData[0] = &( positions[0][0] );
	if ( colors ) {
		attrData[1] = &( colors[0][0] );
		context->bindShape( (unsigned int) numVerts, 0x43, 0, attrData, nullptr );
		color = FloatVector4( 0.0f );
	} else {
		context->bindShape( (unsigned int) numVerts, 0x03, 0, attrData, nullptr );
		color.maxValues( FloatVector4( 1.0e-15f ) );
	}
	prog->uni4f( "vertexColorOverride", color );
	context->fn->glDrawArrays( GLenum( elementMode ), 0, GLsizei( numVerts ) );
}

void Scene::drawLineStrip( const Vector3 * positions, size_t numVerts, const FloatVector4 * colors )
{
	drawLines( positions, numVerts, colors, GL_LINE_STRIP );
}

void Scene::drawTriangles( const Vector3 * positions, size_t numVerts, const FloatVector4 * colors, bool solid,
							unsigned int elementMode, size_t numElements, unsigned int elementType,
							const void * elementData )
{
	auto	prog = useProgram( !solid ? "wireframe.prog" : "selection.prog" );
	if ( !prog )
		return;
	NifSkopeOpenGLContext *	context = renderer;
	if ( !solid ) {
		prog->uni3m( "normalMatrix", Matrix() );
		prog->uni1f( "lineWidth", currentGLLineWidth );
	} else {
		prog->uni1i( "selectionFlags", 0 );
	}
	prog->uni4m( "modelViewMatrix", *currentModelViewMatrix );
	prog->uni1i( "selectionParam", -1 );

	if ( selecting ) {
		glDisable( GL_BLEND );
	} else {
		glEnable( GL_BLEND );
		context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}

	size_t	elementDataSize = 0;
	if ( numElements > 0 ) {
		elementDataSize = ( elementType == GL_UNSIGNED_SHORT ? 2 : ( elementType == GL_UNSIGNED_INT ? 4 : 1 ) );
		elementDataSize = elementDataSize * numElements;
	}

	FloatVector4	color = FloatVector4( 1.0e-15f ).maxValues( currentGLColor );
	const float *	attrData[2];
	attrData[0] = &( positions[0][0] );
	unsigned int	attrMask = 0x03;
	if ( colors ) {
		attrData[1] = &( colors[0][0] );
		attrMask = 0x43;
		color = FloatVector4( 0.0f );
	}
	context->bindShape( (unsigned int) numVerts, attrMask, elementDataSize, attrData, elementData );
	prog->uni4f( "vertexColorOverride", color );
	if ( !elementMode ) [[unlikely]]
		return;
	if ( numElements > 0 )
		context->fn->glDrawElements( GLenum( elementMode ), GLsizei( numElements ), GLenum( elementType ), (void *) 0 );
	else
		context->fn->glDrawArrays( GLenum( elementMode ), 0, GLsizei( numVerts ) );
}

void Scene::drawAxes( const Vector3 & c, float axis, bool color )
{
	pushAndMultModelViewMatrix( Transform( c, axis ) );

	FloatVector4 *	colors = nullptr;
	Vector3 *	positions = allocateVertexAttr( 30, ( color ? &colors : nullptr ) );

	float	arrow = 1.0f / 36.0f;

	positions[0] = Vector3( -1.0f, 0.0f, 0.0f );
	positions[1] = Vector3( +1.0f, 0.0f, 0.0f );
	positions[2] = Vector3( +1.0f, 0.0f, 0.0f );
	positions[3] = Vector3( +1.0f - 3.0f * arrow, +arrow, +arrow );
	positions[4] = Vector3( +1.0f, 0.0f, 0.0f );
	positions[5] = Vector3( +1.0f - 3.0f * arrow, -arrow, +arrow );
	positions[6] = Vector3( +1.0f, 0.0f, 0.0f );
	positions[7] = Vector3( +1.0f - 3.0f * arrow, +arrow, -arrow );
	positions[8] = Vector3( +1.0f, 0.0f, 0.0f );
	positions[9] = Vector3( +1.0f - 3.0f * arrow, -arrow, -arrow );

	positions[10] = Vector3( 0.0f, -1.0f, 0.0f );
	positions[11] = Vector3( 0.0f, +1.0f, 0.0f );
	positions[12] = Vector3( 0.0f, +1.0f, 0.0f );
	positions[13] = Vector3( +arrow, +1.0f - 3.0f * arrow, +arrow );
	positions[14] = Vector3( 0.0f, +1.0f, 0.0f );
	positions[15] = Vector3( -arrow, +1.0f - 3.0f * arrow, +arrow );
	positions[16] = Vector3( 0.0f, +1.0f, 0.0f );
	positions[17] = Vector3( +arrow, +1.0f - 3.0f * arrow, -arrow );
	positions[18] = Vector3( 0.0f, +1.0f, 0.0f );
	positions[19] = Vector3( -arrow, +1.0f - 3.0f * arrow, -arrow );

	positions[20] = Vector3( 0.0f, 0.0f, -1.0f );
	positions[21] = Vector3( 0.0f, 0.0f, +1.0f );
	positions[22] = Vector3( 0.0f, 0.0f, +1.0f );
	positions[23] = Vector3( +arrow, +arrow, +1.0f - 3.0f * arrow );
	positions[24] = Vector3( 0.0f, 0.0f, +1.0f );
	positions[25] = Vector3( -arrow, +arrow, +1.0f - 3.0f * arrow );
	positions[26] = Vector3( 0.0f, 0.0f, +1.0f );
	positions[27] = Vector3( +arrow, -arrow, +1.0f - 3.0f * arrow );
	positions[28] = Vector3( 0.0f, 0.0f, +1.0f );
	positions[29] = Vector3( -arrow, -arrow, +1.0f - 3.0f * arrow );

	if ( color ) {
		for ( size_t i = 0; i < 10; i++ ) {
			colors[i] = FloatVector4( 1.0f, 0.0f, 0.0f, 1.0f );
			colors[i + 10] = FloatVector4( 0.0f, 1.0f, 0.0f, 1.0f );
			colors[i + 20] = FloatVector4( 0.0f, 0.0f, 1.0f, 1.0f );
		}
	}

	drawLines( positions, 30, colors );

	popModelViewMatrix();
}

static const float hkScale660 = 1.0 / 1.42875 * 10.0;
static const float hkScale2010 = 1.0 / 1.42875 * 100.0;

float bhkScale( const NifModel * nif )
{
	return (nif->getBSVersion() < 47) ? hkScale660 : hkScale2010;
}

float bhkInvScale( const NifModel * nif )
{
	return (nif->getBSVersion() < 47) ? 1.0 / hkScale660 : 1.0 / hkScale2010;
}

float bhkScaleMult( const NifModel * nif )
{
	return (nif->getBSVersion() < 47) ? 1.0 : 10.0;
}

Transform bhkBodyTrans( const NifModel * nif, const QModelIndex & index )
{
	Transform t;

	if ( nif->isNiBlock( index, "bhkRigidBodyT" ) ) {
		t.translation = Vector3( nif->get<Vector4>( index, "Translation" ) * bhkScale( nif ) );
		t.rotation.fromQuat( nif->get<Quat>( index, "Rotation" ) );
	}

	t.scale = bhkScale( nif );

	qint32 l = nif->getBlockNumber( index );

	while ( (l = nif->getParent( l )) >= 0 ) {
		QModelIndex iAV = nif->getBlockIndex( l, "NiAVObject" );

		if ( iAV.isValid() )
			t = Transform( nif, iAV ) * t;
	}

	return t;
}

QModelIndex bhkGetEntity( const NifModel * nif, const QModelIndex & index, const QString & name )
{
	auto iEntity = nif->getIndex( index, name );
	if ( !iEntity.isValid() ) {
		iEntity = nif->getIndex( nif->getIndex( index, "Constraint Info" ), name );
		if ( !iEntity.isValid() )
			return {};
	}

	return iEntity;
}

QModelIndex bhkGetRBInfo( const NifModel * nif, const QModelIndex & index, const QString & name )
{
	auto iInfo = nif->getIndex( index, name );
	if ( !iInfo.isValid() ) {
		iInfo = nif->getIndex( nif->getIndex( index, "Rigid Body Info" ), name );
		if ( !iInfo.isValid() )
			return {};
	}

	return iInfo;
}

static void sortAxes( int * axesOrder, FloatVector4 axesDots )
{
	// Retrieve position of X, Y, Z axes in sorted list
	axesOrder[0] = 0;
	axesOrder[1] = 1;
	axesOrder[2] = 2;
	if ( axesDots[1] > axesDots[2] ) {
		axesDots.shuffleValues( 0xD8 );		// 0, 2, 1, 3
		std::swap( axesOrder[1], axesOrder[2] );
	}
	if ( axesDots[0] > axesDots[1] ) {
		axesDots.shuffleValues( 0xE1 );		// 1, 0, 2, 3
		std::swap( axesOrder[0], axesOrder[1] );
	}
	if ( axesDots[1] > axesDots[2] ) {
		axesDots.shuffleValues( 0xD8 );		// 0, 2, 1, 3
		std::swap( axesOrder[1], axesOrder[2] );
	}
}

void Scene::drawAxesOverlay( const Vector3 & c, float axis, const Vector3 & axesDots )
{
	if ( selecting )
		return;

	int	axesOrder[3];
	sortAxes( axesOrder, FloatVector4( axesDots ) );

	pushAndMultModelViewMatrix( Transform( c, axis ) );

	FloatVector4 *	colors = nullptr;
	Vector3 *	positions = allocateVertexAttr( 30, &colors );

	float	arrow = 1.0f / 36.0f;

	for ( int i = 0; i < 3; i++ ) {
		Vector3 *	v = positions + ( i * 10 );
		FloatVector4	color( 0.0f, 0.0f, 1.0f, 1.0f );
		switch ( axesOrder[i] ) {
		case 0:
			// Render the X axis
			color = FloatVector4( 1.0f, 0.0f, 0.0f, 1.0f );
			v[0] = Vector3( 0.0f, 0.0f, 0.0f );
			v[1] = Vector3( 1.0f, 0.0f, 0.0f );
			v[2] = Vector3( 1.0f, 0.0f, 0.0f );
			v[3] = Vector3( 1.0f - 3.0f * arrow, +arrow, +arrow );
			v[4] = Vector3( 1.0f, 0.0f, 0.0f );
			v[5] = Vector3( 1.0f - 3.0f * arrow, -arrow, +arrow );
			v[6] = Vector3( 1.0f, 0.0f, 0.0f );
			v[7] = Vector3( 1.0f - 3.0f * arrow, +arrow, -arrow );
			v[8] = Vector3( 1.0f, 0.0f, 0.0f );
			v[9] = Vector3( 1.0f - 3.0f * arrow, -arrow, -arrow );
			break;
		case 1:
			// Render the Y axis
			color = FloatVector4( 0.0f, 1.0f, 0.0f, 1.0f );
			v[0] = Vector3( 0.0f, 0.0f, 0.0f );
			v[1] = Vector3( 0.0f, 1.0f, 0.0f );
			v[2] = Vector3( 0.0f, 1.0f, 0.0f );
			v[3] = Vector3( +arrow, 1.0f - 3.0f * arrow, +arrow );
			v[4] = Vector3( 0.0f, 1.0f, 0.0f );
			v[5] = Vector3( -arrow, 1.0f - 3.0f * arrow, +arrow );
			v[6] = Vector3( 0.0f, 1.0f, 0.0f );
			v[7] = Vector3( +arrow, 1.0f - 3.0f * arrow, -arrow );
			v[8] = Vector3( 0.0f, 1.0f, 0.0f );
			v[9] = Vector3( -arrow, 1.0f - 3.0f * arrow, -arrow );
			break;
		default:
			// Render the Z axis
			v[0] = Vector3( 0.0f, 0.0f, 0.0f );
			v[1] = Vector3( 0.0f, 0.0f, 1.0f );
			v[2] = Vector3( 0.0f, 0.0f, 1.0f );
			v[3] = Vector3( +arrow, +arrow, 1.0f - 3.0f * arrow );
			v[4] = Vector3( 0.0f, 0.0f, 1.0f );
			v[5] = Vector3( -arrow, +arrow, 1.0f - 3.0f * arrow );
			v[6] = Vector3( 0.0f, 0.0f, 1.0f );
			v[7] = Vector3( +arrow, -arrow, 1.0f - 3.0f * arrow );
			v[8] = Vector3( 0.0f, 0.0f, 1.0f );
			v[9] = Vector3( -arrow, -arrow, 1.0f - 3.0f * arrow );
			break;
		}
		for ( int j = 0; j < 10; j++ )
			colors[i * 10 + j] = color;
	}

	setGLLineWidth( GLView::Settings::lineWidthAxes );

	glDisable( GL_DEPTH_TEST );
	drawLines( positions, 30, colors );
	glDisable( GL_BLEND );

	popModelViewMatrix();
}

void Scene::drawBox( const Vector3 & a, const Vector3 & b )
{
	static const Vector3	positions[16] = {
		// line strip
		Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 0.0f ),
		Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 1.0f ),
		Vector3( 1.0f, 0.0f, 1.0f ), Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 1.0f, 1.0f ),
		Vector3( 0.0f, 0.0f, 1.0f ),
		// lines
		Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 1.0f ),
		Vector3( 1.0f, 1.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ),
		Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, 1.0f, 1.0f )
	};

	pushAndMultModelViewMatrix( Transform( a, b - a ) );
	drawLineStrip( positions, 10 );
	drawLines( &( positions[10] ), 6 );
	popModelViewMatrix();
}

void Scene::drawGrid( float s /* grid size / 2 */, int lines /* number of lines - 1 */, int sub /* # subdivisions */,
						FloatVector4 color, FloatVector4 axis1Color, FloatVector4 axis2Color )
{
	if ( selecting )
		return;

	lines = std::max< int >( lines, 2 ) & ~1;
	sub = std::max< int >( sub, 1 );

	size_t	numVerts = ( size_t( lines ) + 1 ) * 4;
	FloatVector4 *	colors = nullptr;
	Vector3 *	positions = allocateVertexAttr( numVerts, &colors );
	if ( !positions )
		return;

	float	scale1 = s * 2.0f / float( lines );
	Vector3 *	p = positions;
	FloatVector4 *	c = colors;
	for ( int i = 0; i <= lines; i++, p = p + 4, c = c + 4 ) {
		float	t = float( i ) * scale1 - s;
		p[0] = Vector3( t, -s, 0.0f );
		p[1] = Vector3( t, s, 0.0f );
		p[2] = Vector3( -s, t, 0.0f );
		p[3] = Vector3( s, t, 0.0f );
		if ( i == ( lines >> 1 ) ) {
			c[0] = axis2Color;
			c[1] = axis2Color;
			c[2] = axis1Color;
			c[3] = axis1Color;
		} else {
			c[0] = color;
			c[1] = color;
			c[2] = color;
			c[3] = color;
		}
	}
	setGLLineWidth( GLView::Settings::lineWidthGrid );
	drawLines( positions, numVerts, colors );

	numVerts = size_t( lines ) * size_t( sub - 1 ) * 4;
	positions = allocateVertexAttr( numVerts );
	if ( positions ) {
		float	scale2 = s * 2.0f / float( lines * sub );
		p = positions;
		for ( int i = 0; i < lines; i++ ) {
			for ( int j = 1; j < sub; j++, p = p + 4 ) {
				float	t = float( i * sub + j ) * scale2 - s;
				p[0] = Vector3( t, -s, 0.0f );
				p[1] = Vector3( t, s, 0.0f );
				p[2] = Vector3( -s, t, 0.0f );
				p[3] = Vector3( s, t, 0.0f );
			}
		}
		setGLColor( color );
		setGLLineWidth( GLView::Settings::lineWidthGrid * 0.25f );
		drawLines( positions, numVerts );
	}

	glDisable( GL_BLEND );
}

void Scene::drawCircle( const Vector3 & c, const Vector3 & n, float r, int sd )
{
	Vector3 x = Vector3::crossproduct( n, Vector3( n[1], n[2], n[0] ) );
	Vector3 y = Vector3::crossproduct( n, x );
	drawArc( c, x * r, y * r, -PI, +PI, sd );
}

void Scene::drawArc( const Vector3 & c, const Vector3 & x, const Vector3 & y, float an, float ax, int sd )
{
	if ( sd < 1 )
		return;
	Vector3 *	positions = allocateVertexAttr( size_t( sd ) + 1 );
	if ( !positions )
		return;

	Transform	t( c, 1.0f );
	float	m[9] = { x[0], y[0], 0.0f, x[1], y[1], 0.0f, x[2], y[2], 0.0f };
	t.rotation = Matrix( m );
	pushAndMultModelViewMatrix( t );

	for ( int j = 0; j <= sd; j++ ) {
		float f = ( ax - an ) * float(j) / float(sd) + an;
		positions[j] = Vector3( std::sin( f ), std::cos( f ), 0.0f );
	}

	drawLineStrip( positions, size_t( sd ) + 1 );
	popModelViewMatrix();
}

Matrix4 Scene::calculateTransform( const Vector3 & c, const Vector3 & n, float xyScale )
{
	FloatVector4	m[4];

	FloatVector4	normal = FloatVector4::convertVector3( &( n[0] ) );
	FloatVector4	n2 = normal * normal;
	float	tmp = ( n2[2] < ( n2[0] + n2[1] ) ? 0.0f : 1.0f );
	m[1] = normal.crossProduct3( FloatVector4( tmp, 0.0f, 1.0f - tmp, 0.0f ) ).normalize();
	m[0] = m[1].crossProduct3( normal ).normalize() * xyScale;
	m[1] *= xyScale;
	m[2] = normal;
	m[3] = FloatVector4::convertVector3( &( c[0] ) );
	m[3][3] = 1.0f;

	return Matrix4( &( m[0][0] ) );
}

void Scene::drawCone( const Vector3 & c, Vector3 n, float a, int sd )
{
	Vector3 *	positions = allocateVertexAttr( size_t( sd ) * 4 );
	if ( !positions )
		return;

	pushAndMultModelViewMatrix( calculateTransform( c, n * std::cos( a ), n.length() * std::sin( a ) ) );

	Vector3	p0( 1.0f, 0.0f, 1.0f );
	for ( int i = 0; i < sd; i++ ) {
		float	f = ( 2.0f * PI * float(i + 1) / float(sd) );
		Vector3	p1( std::cos( f ), std::sin( f ), 1.0f );
		positions[i * 4] = Vector3();
		positions[i * 4 + 1] = p0;
		positions[i * 4 + 2] = p0;
		positions[i * 4 + 3] = p1;
		p0 = p1;
	}

	drawLines( positions, size_t( sd ) * 4 );
	popModelViewMatrix();
}

void Scene::drawRagdollCone( const Vector3 & pivot, const Vector3 & twist, const Vector3 & plane,
								float coneAngle, float minPlaneAngle, float maxPlaneAngle, int sd )
{
	Vector3 *	positions = allocateVertexAttr( size_t( sd ) + 2 );
	if ( !positions )
		return;

	FloatVector4	m[4];
	m[2] = FloatVector4::convertVector3( &( twist[0] ) );	// z
	m[1] = FloatVector4::convertVector3( &( plane[0] ) );	// y
	m[0] = m[2].crossProduct3( m[1] ) * float( std::sin( coneAngle ) );	// x
	m[3] = FloatVector4::convertVector3( &( pivot[0] ) );	// translation
	m[3][3] = 1.0f;
	pushAndMultModelViewMatrix( Matrix4( &( m[0][0] ) ) );

	positions[0] = Vector3();
	for ( int i = 0; i <= sd; i++ ) {
		float	f = ( 2.0f * PI * float(i) / float(sd) );
		float	x = std::sin( f );
		float	y = std::sin( f <= PI / 2 || f >= 3 * PI / 2 ? maxPlaneAngle : -minPlaneAngle ) * std::cos( f );
		positions[i + 1] = Vector3( x, y, 1.0f );
	}

	drawTriangles( positions, size_t( sd ) + 2, nullptr, true, GL_TRIANGLE_FAN );
	popModelViewMatrix();
}

void Scene::drawSpring( const Vector3 & a, const Vector3 & b, float stiffness, int sd, [[maybe_unused]] bool solid )
{
	// draw a spring with stiffness turns (TODO: implement solid mode)
	int	m = int( stiffness * sd );
	if ( sd < 1 || m < 1 )
		return;
	size_t	numVerts = size_t( m ) + 3;
	Vector3 *	positions = allocateVertexAttr( numVerts );
	if ( !positions )
		return;

	pushAndMultModelViewMatrix( calculateTransform( a, b - a, ( b - a ).length() / 5.0f ) );

	positions[0] = Vector3();
	for ( int i = 0; i <= m; i++ ) {
		float f = 2.0f * PI * float( i ) / float( sd );
		positions[i + 1] = Vector3( std::cos( f ), std::sin( f ), float( i ) / float( m ) );
	}
	positions[m + 2] = Vector3( 0.0f, 0.0f, 1.0f );

	drawLineStrip( positions, numVerts );
	popModelViewMatrix();
}

void Scene::drawRail( const Vector3 & a, const Vector3 & b )
{
	/* offset between beginning and end points */
	Vector3 off = b - a;

	/* direction vector of "rail track width", in xy-plane */
	Vector3 x = Vector3( -off[1], off[0], 0 );

	if ( x.length() < 0.0001f ) {
		x[0] = 1.0f;
	}

	x.normalize();

	int	len = std::min< int >( std::max< int >( int( off.length() ), 1 ), 32000 );
	size_t	numVerts = size_t( len + 1 ) * 2 + 4;
	Vector3 *	positions = allocateVertexAttr( numVerts );
	if ( !positions )
		return;

	Transform	t( a, 1.0f );
	t.rotation( 0, 0 ) = x[0];
	t.rotation( 0, 2 ) = off[0];
	t.rotation( 1, 0 ) = x[1];
	t.rotation( 1, 2 ) = off[1];
	t.rotation( 2, 0 ) = x[2];
	t.rotation( 2, 2 ) = off[2];
	pushAndMultModelViewMatrix( t );

	positions[0] = Vector3( 0.0f, 0.0f, 0.0f );
	positions[1] = Vector3( 0.0f, 0.0f, 1.0f );
	drawPoints( positions, 2 );

	/* draw the rail */
	positions[0] = Vector3( 1.0f, 0.0f, 0.0f );
	positions[1] = Vector3( 1.0f, 0.0f, 1.0f );
	positions[2] = Vector3( -1.0f, 0.0f, 0.0f );
	positions[3] = Vector3( -1.0f, 0.0f, 1.0f );

	/* draw the logs */
	for ( int i = 0; i <= len; i++ ) {
		float rel_off = ( 1.0f * i ) / len;
		positions[i * 2 + 4] = Vector3( 1.3f, 0.0f, rel_off );
		positions[i * 2 + 5] = Vector3( -1.3f, 0.0f, rel_off );
	}

	drawLines( positions, numVerts );
	popModelViewMatrix();
}

void Scene::drawSolidArc( const Vector3 & c, const Vector3 & n, const Vector3 & x, const Vector3 & y,
							float an, float ax, float r, int sd )
{
	if ( sd < 1 )
		return;
	size_t	numVerts = ( size_t( sd ) + 1 ) * 2;
	Vector3 *	positions = allocateVertexAttr( numVerts );
	if ( !positions )
		return;

	FloatVector4	m[4];
	m[0] = FloatVector4( x ) * r;
	m[1] = FloatVector4( y ) * r;
	m[2] = FloatVector4( n );
	m[3] = FloatVector4( c ).blendValues( FloatVector4( 1.0f ), 0x08 );
	pushAndMultModelViewMatrix( Matrix4( &(m[0][0]) ) );

	bool cull = glIsEnabled( GL_CULL_FACE );
	glDisable( GL_CULL_FACE );

	for ( int j = 0; j <= sd; j++ ) {
		float f = ( ax - an ) * float(j) / float(sd) + an;

		positions[j * 2] = Vector3( std::sin( f ), std::cos( f ), 1.0f );
		positions[j * 2 + 1] = Vector3( std::sin( f ), std::cos( f ), -1.0f );
	}

	drawTriangles( positions, numVerts, nullptr, true, GL_TRIANGLE_STRIP );
	popModelViewMatrix();

	if ( cull )
		glEnable( GL_CULL_FACE );
}

void Scene::drawSphereSimple( const Vector3 & c, float r, int sd, int s2 )
{
	s2 = std::max< int >( s2, 2 );
	int	n = s2 * 2;
	sd = std::max< int >( sd, n );
	n = ( sd + n - 1 ) / n;
	sd = n * ( s2 * 2 );

	Vector3 *	positions = allocateVertexAttr( size_t(sd) * ( size_t(s2) * 2 - 1 ) + 1 );
	if ( !positions )
		return;

	positions[0] = Vector3( 0.0f, 0.0f, -1.0f );
	Vector3 *	p = positions + 1;
	double	r1 = 2.0 * PI / double( sd );
	double	r2 = PI / double( s2 );
	double	r1x = std::cos( r1 );
	double	r1y = std::sin( r1 );
	double	r2x = std::cos( r2 );
	double	r2y = std::sin( r2 );
	double	a1x = 0.0;
	double	a1z = -1.0;
	double	a2x = 1.0;
	double	a2y = 0.0;
	for ( int i = 0; i < s2; i++ ) {
		for ( int j = 0; j++ < sd; ) {
			double	tmp = a1x * r1x - a1z * r1y;
			a1z = a1x * r1y + a1z * r1x;
			a1x = tmp;
			double	a3x = a1x * a2x;
			double	a3y = a1x * a2y;
			*p = Vector3( float( a3x ), float( a3y ), float( a1z ) );
			p++;

			if ( i == 0 && j < ( sd >> 1 ) && ( j % n ) == 0 ) {
				for ( int l = 0; l < sd; l++ ) {
					tmp = a3x * r1x - a3y * r1y;
					a3y = a3x * r1y + a3y * r1x;
					a3x = tmp;
					*p = Vector3( float( a3x ), float( a3y ), float( a1z ) );
					p++;
				}
			}
		}

		double	tmp = a2x * r2x - a2y * r2y;
		a2y = a2x * r2y + a2y * r2x;
		a2x = tmp;
	}

	pushAndMultModelViewMatrix( Transform( c, r ) );
	drawLineStrip( positions, size_t( p - positions ) );
	popModelViewMatrix();
}

void Scene::drawSphere( const Vector3 & c, float r, int sd )
{
	if ( sd < 1 )
		return;
	size_t	numVerts = size_t( sd ) * ( size_t( sd ) * 2 + 1 ) * 12;
	Vector3 *	positions = allocateVertexAttr( numVerts );
	if ( !positions )
		return;

	pushAndMultModelViewMatrix( Transform( c, r ) );

	Vector3 *	p = positions;
	for ( int j = -sd; j <= sd; j++ ) {
		float f = PI * float(j) / float(sd);
		Vector3 cj( 0.0f, 0.0f, std::cos( f ) );
		float rj = std::sin( f );

		Vector3 p0 = Vector3( 0.0f, rj, 0.0f ) + cj;
		for ( int i = 0; i < sd * 2; p = p + 2 ) {
			i++;
			Vector3 p1 = Vector3( std::sin( PI / sd * i ), std::cos( PI / sd * i ), 0.0f ) * rj + cj;
			p[0] = p0;
			p[1] = p1;
			p0 = p1;
		}
	}

	for ( int j = -sd; j <= sd; j++ ) {
		float f = PI * float(j) / float(sd);
		Vector3 cj( 0.0f, std::cos( f ), 0.0f );
		float rj = std::sin( f );

		Vector3 p0 = Vector3( 0.0f, 0.0f, rj ) + cj;
		for ( int i = 0; i < sd * 2; p = p + 2 ) {
			i++;
			Vector3 p1 = Vector3( std::sin( PI / sd * i ), 0.0f, std::cos( PI / sd * i ) ) * rj + cj;
			p[0] = p0;
			p[1] = p1;
			p0 = p1;
		}
	}

	for ( int j = -sd; j <= sd; j++ ) {
		float f = PI * float(j) / float(sd);
		Vector3 cj( std::cos( f ), 0.0f, 0.0f );
		float rj = std::sin( f );

		Vector3 p0 = Vector3( 0.0f, 0.0f, rj ) + cj;
		for ( int i = 0; i < sd * 2; p = p + 2 ) {
			i++;
			Vector3 p1 = Vector3( 0.0f, std::sin( PI / sd * i ), std::cos( PI / sd * i ) ) * rj + cj;
			p[0] = p0;
			p[1] = p1;
			p0 = p1;
		}
	}

	drawLines( positions, numVerts );
	popModelViewMatrix();
}

void Scene::drawCapsule( const Vector3 & a, const Vector3 & b, float r, int sd )
{
	if ( !drawCylinder( a, b, r, sd ) )
		return;

	int	s2 = std::max< int >( ( sd + 1 ) & ~1, 2 );
	sd = std::max< int >( sd * 2, 4 );
	size_t	numVerts = size_t( sd ) * size_t( s2 - 1 ) * 2;
	Vector3 *	positions = allocateVertexAttr( numVerts );
	if ( !positions )
		return;

	Vector3 *	p = positions;
	float	prvX = 1.0f;
	float	prvY = 0.0f;
	for ( int i = 0; i < sd; i++ ) {
		float	x = std::cos( PI * 2.0f * ( float( i + 1 ) / float( sd ) ) );
		float	y = std::sin( PI * 2.0f * ( float( i + 1 ) / float( sd ) ) );
		float	prvZ = 1.0f;
		float	prvW = 0.0f;
		for ( int j = 0; true; j++, p = p + 2 ) {
			float	z = std::cos( PI * ( float( j + 1 ) / float( s2 ) ) );
			float	w = std::sqrt( std::max( 1.0f - z * z, 0.0f ) );
			p[0] = Vector3( prvX * prvW, prvY * prvW, prvZ );
			p[1] = Vector3( prvX * w, prvY * w, z );
			p = p + 2;
			if ( ( j + 1 ) >= ( s2 >> 1 ) )
				break;
			p[0] = Vector3( prvX * w, prvY * w, z );
			p[1] = Vector3( x * w, y * w, z );
			prvZ = z;
			prvW = w;
		}
		prvX = x;
		prvY = y;
	}

	Vector3	d = b - a;
	d = ( d / d.length() ) * r;

	pushAndMultModelViewMatrix( calculateTransform( a, d * -1.0f, r ) );
	drawLines( positions, numVerts );
	popModelViewMatrix();

	pushAndMultModelViewMatrix( calculateTransform( b, d, r ) );
	drawLines( positions, numVerts );
	popModelViewMatrix();
}

bool Scene::drawCylinder( const Vector3 & a, const Vector3 & b, float r, int sd )
{
	Vector3	d = b - a;

	float	l = d.length();
	if ( l < 0.001f ) {
		drawSphere( a, r );
		return false;
	}

	sd = std::max< int >( sd * 2, 4 );
	size_t	numVerts = size_t( sd ) * 8;
	Vector3 *	positions = allocateVertexAttr( numVerts );
	if ( !positions )
		return true;

	pushAndMultModelViewMatrix( calculateTransform( a, d, r ) );

	Vector3 *	p = positions;
	float	prvX = 1.0f;
	float	prvY = 0.0f;
	for ( int i = 0; i < sd; i++, p = p + 8 ) {
		float	x = std::cos( PI * 2.0f * ( float( i + 1 ) / float( sd ) ) );
		float	y = std::sin( PI * 2.0f * ( float( i + 1 ) / float( sd ) ) );
		p[0] = Vector3( prvX, prvY, 0.0f );
		p[1] = Vector3( x, y, 0.0f );
		p[2] = Vector3( prvX, prvY, 0.5f );
		p[3] = Vector3( x, y, 0.5f );
		p[4] = Vector3( prvX, prvY, 1.0f );
		p[5] = Vector3( x, y, 1.0f );
		p[6] = Vector3( prvX, prvY, 0.0f );
		p[7] = Vector3( prvX, prvY, 1.0f );
		prvX = x;
		prvY = y;
	}

	drawLines( positions, numVerts );
	popModelViewMatrix();

	return true;
}

void Scene::drawDashLine( const Vector3 & a, const Vector3 & b, int sd )
{
	sd = ( sd + 1 ) & ~1;
	if ( sd < 2 )
		return;
	Vector3 *	positions = allocateVertexAttr( size_t( sd ) );
	if ( !positions )
		return;

	float	d = 1.0f / float( sd - 1 );
	for ( int c = 0; c < sd; c++ )
		positions[c] = Vector3( FloatVector4( d * float( c ) ) );

	pushAndMultModelViewMatrix( Transform( a, b - a ) );
	drawLines( positions, size_t( sd ) );
	popModelViewMatrix();
}

//! Find the dot product of two vectors
static float dotproduct( const Vector3 & v1, const Vector3 & v2 )
{
	return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}
//! Find the cross product of two vectors
static Vector3 crossproduct( const Vector3 & a, const Vector3 & b )
{
	return Vector3( a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] );
}

//! Generate triangles for convex hull
static QVector<Vector3> generateTris( const NifModel * nif, const QModelIndex & iShape, float scale )
{
	QVector<Vector4> vertices = nif->getArray<Vector4>( iShape, "Vertices" );
	//QVector<Vector4> normals = nif->getArray<Vector4>( iShape, "Normals" );

	if ( vertices.isEmpty() )
		return QVector<Vector3>();

	Vector3 A, B, C, N, V;
	float D;
	int L, prev, eps;
	bool good;

	L = vertices.count();
	QVector<Vector3> P( L );
	QVector<Vector3> tris;

	// Convert Vector4 to Vector3
	for ( int v = 0; v < L; v++ ) {
		P[v] = Vector3( vertices[v] );
	}

	for ( int i = 0; i < L - 2; i++ ) {
		A = P[i];

		for ( int j = i + 1; j < L - 1; j++ ) {
			B = P[j];

			for ( int k = j + 1; k < L; k++ ) {
				C = P[k];

				prev = 0;
				good = true;

				N = crossproduct( (B - A), (C - A) );

				for ( int p = 0; p < L; p++ ) {
					V = P[p];

					if ( (V == A) || (V == B) || (V == C) ) continue;

					D = dotproduct( (V - A), N );

					if ( D == 0 ) continue;

					eps = (D > 0) ? 1 : -1;

					if ( eps + prev == 0 ) {
						good = false;
						continue;
					}

					prev = eps;
				}

				if ( good ) {
					// Append ABC
					tris << (A*scale) << (B*scale) << (C*scale);
				}
			}
		}
	}

	return tris;
}

void Scene::drawConvexHull( const NifModel * nif, const QModelIndex & iShape, float scale, bool solid )
{
	static QMap<QModelIndex, QVector<Vector3>> shapes;
	QVector<Vector3> shape;

	shape = shapes[iShape];

	if ( shape.empty() ) {
		shape = generateTris( nif, iShape, scale );
		shapes[iShape] = shape;
	}

	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glDisable( GL_CULL_FACE );

	drawTriangles( shape.constData(), size_t( shape.size() ), nullptr, solid );

	glEnable( GL_CULL_FACE );
}

void Scene::drawNiTSS( const NifModel * nif, const QModelIndex & iShape, bool solid )
{
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glDisable( GL_CULL_FACE );
	glEnable( GL_PRIMITIVE_RESTART );
	renderer->fn->glPrimitiveRestartIndex( 0xFFFF );

	QModelIndex iStrips = nif->getIndex( iShape, "Strips Data" );
	for ( int r = 0; r < nif->rowCount( iStrips ); r++ ) {
		QModelIndex iStripData = nif->getBlockIndex( nif->getLink( nif->getIndex( iStrips, r ) ), "NiTriStripsData" );
		if ( !iStripData.isValid() )
			continue;
		QModelIndex iPoints = nif->getIndex( iStripData, "Points" );
		int numStrips;
		if ( !iPoints.isValid() || ( numStrips = nif->rowCount( iPoints ) ) < 1 )
			continue;

		QVector<Vector3> verts = nif->getArray<Vector3>( iStripData, "Vertices" );
		qsizetype numVerts = std::min< qsizetype >( verts.size(), 65535 );
		if ( numVerts < 1 )
			continue;

		qsizetype numPoints = 0;
		for ( int s = 0; s < numStrips; s++ ) {
			if ( QModelIndex i = nif->getIndex( iPoints, s ); i.isValid() )
				numPoints += qsizetype( nif->rowCount( i ) ) + 1;
		}
		if ( numPoints < 2 )
			continue;
		numPoints--;

		QVector<quint16> points( numPoints );
		quint16 * p = points.data();
		qsizetype k = 0;
		for ( int s = 0; s < numStrips; s++ ) {
			// draw the strips like they appear in the tescs
			// (use the unstich strips spell to avoid the spider web effect)
			QModelIndex iStrip = nif->getIndex( iPoints, s );
			const NifItem * i;
			int stripLen;
			if ( !iStrip.isValid() || ( i = nif->getItem( iStrip ) ) == nullptr
				|| ( stripLen = nif->rowCount( iStrip ) ) < 1 ) {
				continue;
			}
			if ( k > 0 ) {
				p[k] = 0xFFFF;
				k++;
			}
			for ( int j = 0; j < stripLen && k < numPoints; j++, k++ ) {
				quint16 v = nif->get<quint16>( i, j );
				if ( qsizetype( v ) >= numVerts )
					v = 0;
				p[k] = v;
			}
		}

		if ( k > 0 ) {
			drawTriangles( verts.constData(), size_t( numVerts ), nullptr, solid,
							GL_TRIANGLE_STRIP, size_t( k ), GL_UNSIGNED_SHORT, p );
		}
	}

	glDisable( GL_PRIMITIVE_RESTART );
	glEnable( GL_CULL_FACE );
}

void Scene::drawCMS( const NifModel * nif, const QModelIndex & iShape, bool solid )
{
	QModelIndex iData = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );
	if ( !iData.isValid() )
		return;

	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glDisable( GL_CULL_FACE );

	QModelIndex iBigVerts = nif->getIndex( iData, "Big Verts" );
	QModelIndex iBigTris = nif->getIndex( iData, "Big Tris" );
	QModelIndex iChunkTrans = nif->getIndex( iData, "Chunk Transforms" );

	qsizetype	numVerts = 0;
	Vector3 *	positions = nullptr;
	if ( nif->rowCount( iBigVerts ) > 0 ) {
		QVector<Vector4> verts = nif->getArray<Vector4>( iBigVerts );
		numVerts = std::min< qsizetype >( verts.size(), 65536 );
		positions = allocateVertexAttr( size_t( numVerts ) );
		if ( positions ) {
			for ( qsizetype i = 0; i < numVerts; i++ )
				FloatVector4( verts.at( i ) ).convertToVector3( &( positions[i][0] ) );
		}
	}
	int numTriangles = nif->rowCount( iBigTris );
	if ( numVerts >= 2 && numTriangles > 0 && positions ) {
		QVector<Triangle> triangles( numTriangles );
		for ( int i = 0; i < numTriangles; i++ ) {
#if 0
			triangles[i] = nif->get<Triangle>( nif->getIndex( iBigTris, i ), "Triangle" );
#else
			// assume that "Triangle" is in the first row of bhkCMSBigTri
			triangles[i] = nif->get<Triangle>( nif->getIndex( nif->getIndex( iBigTris, i ), 0 ) );
#endif
		}

		drawTriangles( positions, size_t( numVerts ), nullptr, solid,
						GL_TRIANGLES, size_t( numTriangles ) * 3, GL_UNSIGNED_SHORT, triangles.constData() );
	}

	QModelIndex iChunkArr = nif->getIndex( iData, "Chunks" );
	for ( int r = 0; r < nif->rowCount( iChunkArr ); r++ ) {
		auto iChunk = nif->index(r, 0, iChunkArr);
		Vector4 chunkOrigin = nif->get<Vector4>( iChunk, "Translation" );

		quint32 transformIndex = nif->get<quint32>( iChunk, "Transform Index" );
		QModelIndex chunkTransform = nif->getIndex( iChunkTrans, transformIndex );
		Vector4 chunkTranslation = nif->get<Vector4>( nif->getIndex( chunkTransform, 0 ) ) + chunkOrigin;
		Quat chunkRotation = nif->get<Quat>( nif->getIndex( chunkTransform, 1 ) );

#if 0
		quint32 numOffsets = nif->get<quint32>( iChunk, "Num Vertices" ) / 3;
		quint32 numIndices = nif->get<quint32>( iChunk, "Num Indices" );
		quint32 numStrips = nif->get<quint32>( iChunk, "Num Strips" );
#endif
		QVector<Vector3> vertices = nif->getArray<Vector3>( iChunk, "Vertices" );
		QVector<quint16> indices = nif->getArray<quint16>( iChunk, "Indices" );
		QVector<quint16> strips = nif->getArray<quint16>( iChunk, "Strips" );

		if ( vertices.size() < 2 || indices.size() < 3 )
			continue;

		Transform trans;
		trans.rotation.fromQuat( chunkRotation );

		pushAndMultModelViewMatrix( trans.toMatrix4() * Transform( Vector3( chunkTranslation ), 0.001f ) );
		// load data without rendering the mesh
		drawTriangles( vertices.constData(), size_t( vertices.size() ), nullptr, solid,
						0, size_t( indices.size() ), GL_UNSIGNED_SHORT, indices.constData() );

		// Stripped tris
		qsizetype	offset = 0;
		for ( qsizetype s = 0; s < strips.size(); s++ ) {

			qsizetype	numIndices = strips[s];
			if ( ( offset + numIndices ) > indices.size() )
				numIndices = indices.size() - offset;
			if ( numIndices < 3 )
				continue;

			renderer->fn->glDrawElements( GL_TRIANGLE_STRIP, GLsizei( numIndices ),
											GL_UNSIGNED_SHORT, (void *) ( offset * 2 ) );

			offset += numIndices;
		}

		// Non-stripped tris
		if ( ( offset + 3 ) <= indices.size() ) {
			qsizetype	numTriangles = ( indices.size() - offset ) / 3;

			renderer->fn->glDrawElements( GL_TRIANGLES, GLsizei( numTriangles * 3 ),
											GL_UNSIGNED_SHORT, (void *) ( offset * 2 ) );
		}

		popModelViewMatrix();
	}

	glEnable( GL_CULL_FACE );
}

// Renders text using the font initialized in the primary view class
void Scene::renderText( [[maybe_unused]] const Vector3 & c, [[maybe_unused]] const QString & str )
{
	// TODO: implement this function
}

NifSkopeOpenGLContext::Program * Scene::setupProgram( std::string_view name, unsigned int elementMode )
{
	NifSkopeOpenGLContext *	context = renderer;
	if ( !context ) [[unlikely]]
		return nullptr;
	auto	prog = context->getCurrentProgram();
	if ( !( ( prog && prog->name == name ) || ( prog = context->useProgram( name ) ) != nullptr ) )
		return nullptr;

	prog->uni4f( "vertexColorOverride", FloatVector4( 1.0e-15f ).maxValues( currentGLColor ) );
	prog->uni4m( "modelViewMatrix", *currentModelViewMatrix );

	if ( elementMode == GL_LINES || elementMode == GL_LINE_STRIP || elementMode == GL_LINE_LOOP ) {
		prog->uni1f( "lineWidth", currentGLLineWidth );
	} else {
		prog->uni1i( "selectionParam", -1 );
		if ( elementMode == GL_POINTS ) {
			float	pointSize = currentGLPointSize;
			if ( selecting ) {
				prog->uni1i( "selectionFlags", ( isSelModeVertex() ? 0x0003 : 0x0002 ) );
			} else {
				pointSize += 0.5f;
				prog->uni1i( "selectionFlags", ( roundFloat( std::min( pointSize * 8.0f, 255.0f ) ) << 8 ) | 0x0002 );
			}
			glPointSize( pointSize );
		} else if ( name == "wireframe.prog" ) {
			prog->uni3m( "normalMatrix", Matrix() );
			prog->uni1f( "lineWidth", currentGLLineWidth );
		} else {
			prog->uni1i( "selectionFlags", 0 );
		}
	}

	if ( selecting ) {
		glDisable( GL_BLEND );
	} else {
		glEnable( GL_BLEND );
		context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}

	return prog;
}
