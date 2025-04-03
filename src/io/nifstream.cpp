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

#include "nifstream.h"

#include "data/nifvalue.h"
#include "model/nifmodel.h"

#include "fp32vec4.hpp"
#include "filebuf.hpp"

#include <QDataStream>
#include <QIODevice>
#include <QFloat16>


//! @file nifstream.cpp NIF file I/O

/*
*  NifIStream
*/

void NifIStream::init()
{
	bool32bit = (model->inherits( "NifModel" ) && model->getVersionNumber() <= 0x04000002);
	linkAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() <  0x0303000D);
	bigEndian = false; // set when tFileVersion is read

	dataStream = std::unique_ptr<QDataStream>( new QDataStream( device ) );
	dataStream->setByteOrder( QDataStream::LittleEndian );
	dataStream->setFloatingPointPrecision( QDataStream::SinglePrecision );

	maxLength = 0x8000;
}

bool NifIStream::read( NifValue & val )
{
	if ( !val.isAllocated() ) [[likely]]
		val.val.clear();

	switch ( val.type() ) {
	case NifValue::tBool:
		{
			if ( bool32bit )
				*dataStream >> val.val.u32;
			else
				*dataStream >> val.val.u08;

			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tByte:
		{
			*dataStream >> val.val.u08;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tWord:
	case NifValue::tShort:
	case NifValue::tFlags:
	case NifValue::tBlockTypeIndex:
		{
			*dataStream >> val.val.u16;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tStringOffset:
	case NifValue::tInt:
	case NifValue::tUInt:
		{
			*dataStream >> val.val.u32;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tULittle32:
		{
			if ( bigEndian )
				dataStream->setByteOrder( QDataStream::LittleEndian );

			*dataStream >> val.val.u32;

			if ( bigEndian )
				dataStream->setByteOrder( QDataStream::BigEndian );

			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tInt64:
	case NifValue::tUInt64:
		{
			*dataStream >> val.val.u64;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tStringIndex:
		{
			*dataStream >> val.val.u32;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tLink:
	case NifValue::tUpLink:
		{
			*dataStream >> val.val.i32;

			if ( linkAdjust )
				val.val.i32--;

			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tFloat:
		{
			*dataStream >> val.val.f32;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tHfloat:
		{
			qfloat16 half;
			*dataStream >> half;
			val.val.f32 = float( half );
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tNormbyte:
	{
		quint8 v;
		float fv;
		*dataStream >> v;
		fv = (double(v) / 255.0) * 2.0 - 1.0;
		val.val.f32 = fv;

		return (dataStream->status() == QDataStream::Ok);
	}
	case NifValue::tByteVector3:
		{
			quint8	xyzw[4];

			*dataStream >> xyzw[0];
			*dataStream >> xyzw[1];
			*dataStream >> xyzw[2];
			xyzw[3] = 0;

			val.val.f32v4 = FloatVector4( FileBuffer::readUInt32Fast( xyzw ) ) / 127.5f - 1.0f;

			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tShortVector3:
		{
			std::uint32_t	xy;
			std::uint16_t	z;

			*dataStream >> xy;
			*dataStream >> z;
			std::uint64_t	xyzw = ( std::uint64_t( z ) << 32 ) | xy;

			val.val.f32v4 = FloatVector4::convertInt16( xyzw ) / 32767.0f;

			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tUshortVector3:
		{
			std::uint32_t	xy;
			std::uint16_t	z;

			*dataStream >> xy;
			*dataStream >> z;
			std::uint64_t	xyzw = ( ( std::uint64_t( z ) << 32 ) | xy ) ^ 0x8000800080008000ULL;

			val.val.f32v4 = FloatVector4::convertInt16( xyzw ) + 32768.0f;

			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tHalfVector3:
		{
#if ENABLE_X86_64_SIMD >= 3
			uint32_t	xy;
			uint16_t	z;

			*dataStream >> xy;
			*dataStream >> z;

			val.val.f32v4 = FloatVector4::convertFloat16( (uint64_t(z) << 32) | uint64_t(xy) );
#else
			qfloat16	x, y, z;

			*dataStream >> x;
			*dataStream >> y;
			*dataStream >> z;

			val.val.f32v4 = FloatVector4( float(x), float(y), float(z), 0.0f );
#endif
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tHalfVector2:
		{
#if ENABLE_X86_64_SIMD >= 3
			uint32_t	xy;

			*dataStream >> xy;

			val.val.f32v4 = FloatVector4::convertFloat16(xy);
#else
			qfloat16	x, y;

			*dataStream >> x;
			*dataStream >> y;

			val.val.f32v4[0] = float(x);
			val.val.f32v4[1] = float(y);
#endif
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tVector3:
	case NifValue::tColor3:
		{
			Vector3	v;
			*dataStream >> v;
			val.val.f32v4 = FloatVector4::convertVector3( &(v[0]) );
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tVector4:
	case NifValue::tColor4:
		{
			Vector4	v;
			*dataStream >> v;
			val.val.f32v4 = FloatVector4( v );
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tByteVector4:
		{
			std::uint32_t	v;
			*dataStream >> v;
			ByteVector4	tmp( v );
			val.val.f32v4 = FloatVector4( tmp.data() );
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tUDecVector4:
		{
			std::uint32_t	v;
			*dataStream >> v;
			UDecVector4	tmp( v );
			val.val.f32v4 = FloatVector4( tmp.data() );
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tTriangle:
		{
			*dataStream >> val.val.t;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tQuat:
	case NifValue::tQuatXYZW:
		{
			Quat	q;
			*dataStream >> q;
			val.val.f32v4 = FloatVector4( &(q[0]) );
			if ( val.type() == NifValue::tQuatXYZW )
				val.val.f32v4.shuffleValues( 0x93 );	// 3, 0, 1, 2
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tMatrix:
		return device->read( (char *)static_cast<Matrix *>(val.val.data)->m, 36 ) == 36;
	case NifValue::tMatrix4:
		return device->read( (char *)static_cast<Matrix4 *>(val.val.data)->m, 64 ) == 64;
	case NifValue::tVector2:
		{
			Vector2	v;
			*dataStream >> v;
			val.val.f32v4[0] = v[0];
			val.val.f32v4[1] = v[1];
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tByteColor4:
		{
			std::uint32_t	rgba;
			*dataStream >> rgba;
			val.val.f32v4 = FloatVector4( rgba ) / 255.0f;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tByteColor4BGRA:
		{
			std::uint32_t	bgra;
			*dataStream >> bgra;
			val.val.f32v4 = FloatVector4( bgra ).shuffleValues( 0xC6 ) / 255.0f;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tSizedString:
	case NifValue::tSizedString16:
		{
			std::int32_t	len;
			if ( val.type() == NifValue::tSizedString16 ) [[unlikely]] {
				std::uint16_t	len16;
				*dataStream >> len16;
				len = len16;
			} else {
				*dataStream >> len;
			}

			if ( len > maxLength || len < 0 ) {
				*static_cast<QString *>(val.val.data) = tr( "<string too long (0x%1)>" ).arg( len, 0, 16 ); return false;
			}

			QByteArray string = device->read( len );

			if ( string.size() != len )
				return false;

			//string.replace( "\r", "\\r" );
			//string.replace( "\n", "\\n" );
			*static_cast<QString *>(val.val.data) = QString( string );
		}
		return true;
	case NifValue::tShortString:
		{
			unsigned char len;
			device->read( (char *)&len, 1 );
			QByteArray string = device->read( len );

			if ( string.size() != len )
				return false;
			while ( len > 0 && string.at( len - 1 ) == '\0' )
				len--;

			//string.replace( "\r", "\\r" );
			//string.replace( "\n", "\\n" );
			*static_cast<QString *>(val.val.data) = QString::fromLatin1( string.constData(), len );
		}
		return true;
	case NifValue::tText:
		{
			std::int32_t len;
			device->read( (char *)&len, 4 );

			if ( len > maxLength || len < 0 ) {
				*static_cast<QString *>(val.val.data) = tr( "<string too long>" ); return false;
			}

			QByteArray string = device->read( len );

			if ( string.size() != len )
				return false;

			*static_cast<QString *>(val.val.data) = QString( string );
		}
		return true;
	case NifValue::tByteArray:
		{
			std::int32_t len;
			device->read( (char *)&len, 4 );

			if ( len < 0 )
				return false;

			*static_cast<QByteArray *>(val.val.data) = device->read( len );
			return static_cast<QByteArray *>(val.val.data)->size() == len;
		}
	case NifValue::tStringPalette:
		{
			std::int32_t len;
			device->read( (char *)&len, 4 );

			if ( len > 0xffff || len < 0 )
				return false;

			*static_cast<QByteArray *>(val.val.data) = device->read( len );
			device->read( (char *)&len, 4 );
			return true;
		}
	case NifValue::tByteMatrix:
		{
			std::int32_t len1, len2;
			device->read( (char *)&len1, 4 );
			device->read( (char *)&len2, 4 );

			if ( len1 < 0 || len2 < 0 )
				return false;

			int len = len1 * len2;
			ByteMatrix tmp( len1, len2 );
			qint64 rlen = device->read( tmp.data(), len );
			tmp.swap( *static_cast<ByteMatrix *>(val.val.data) );
			return (rlen == len);
		}
	case NifValue::tHeaderString:
		{
			QByteArray string;
			int c = 0;
			char chr = 0;

			while ( c++ < 80 && device->getChar( &chr ) && chr != '\n' )
				string.append( chr );

			if ( c >= 80 )
				return false;

			quint32 version = 0;
			// Support NIF versions without "Version" in header string
			// Do for all files for now
			//if ( c == GAMEBRYO_FF || c == NETIMMERSE_FF || c == NEOSTEAM_FF ) {
			device->peek((char *)&version, 4);
			// NeoSteam Hack
			if (version == 0x08F35232)
				version = 0x0A010000;
			// Version didn't exist until NetImmerse 4.0
			else if (version < 0x04000000)
				version = 0;
			//}

			*static_cast<QString *>(val.val.data) = QString( string );
			bool x = model->setHeaderString( QString( string ), version );

			init();
			return x;
		}
	case NifValue::tLineString:
		{
			QByteArray string;
			int c = 0;
			char chr = 0;

			while ( c++ < 255 && device->getChar( &chr ) && chr != '\n' )
				string.append( chr );

			if ( c >= 255 )
				return false;

			*static_cast<QString *>(val.val.data) = QString( string );
			return true;
		}
	case NifValue::tChar8String:
		{
			QByteArray string;
			int c = 0;
			char chr = 0;

			while ( c++ < 8 && device->getChar( &chr ) )
				string.append( chr );

			if ( c > 9 )
				return false;

			*static_cast<QString *>(val.val.data) = QString( string );
			return true;
		}
	case NifValue::tFileVersion:
		{
			if ( device->read( (char *)&val.val.u32, 4 ) != 4 )
				return false;

			//bool x = model->setVersion( val.val.u32 );
			//init();
			if ( model->inherits( "NifModel" ) && model->getVersionNumber() >= 0x14000004 ) {
				bool littleEndian;
				device->peek( (char *)&littleEndian, 1 );
				bigEndian = !littleEndian;

				if ( bigEndian ) {
					dataStream->setByteOrder( QDataStream::BigEndian );
				}
			}

			// hack for neosteam
			if ( val.val.u32 == 0x08F35232 ) {
				val.val.u32 = 0x0a010000;
			}

			return true;
		}
	case NifValue::tBSVertexDesc:
		{
			*dataStream >> val.val.u64;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tBlob:
		{
			if ( val.val.data ) {
				QByteArray * array = static_cast<QByteArray *>(val.val.data);
				return device->read( array->data(), array->size() ) == array->size();
			}

			return false;
		}
	case NifValue::tNone:
		return true;
	}

	return false;
}

void NifIStream::reset()
{
	dataStream->device()->reset();
}


/*
*  NifOStream
*/

void NifOStream::init()
{
	bool32bit = (model->inherits( "NifModel" ) && model->getVersionNumber() <= 0x04000002);
	linkAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() <  0x0303000D);
}

bool NifOStream::write( const NifValue & val )
{
	switch ( val.type() ) {
	case NifValue::tBool:

		if ( bool32bit )
			return device->write( (char *)&val.val.u32, 4 ) == 4;
		else
			return device->write( (char *)&val.val.u08, 1 ) == 1;

	case NifValue::tByte:
		return device->write( (char *)&val.val.u08, 1 ) == 1;
	case NifValue::tWord:
	case NifValue::tShort:
	case NifValue::tFlags:
	case NifValue::tBlockTypeIndex:
		return device->write( (char *)&val.val.u16, 2 ) == 2;
	case NifValue::tStringOffset:
	case NifValue::tInt:
	case NifValue::tUInt:
	case NifValue::tULittle32:
	case NifValue::tStringIndex:
		return device->write( (char *)&val.val.u32, 4 ) == 4;
	case NifValue::tInt64:
	case NifValue::tUInt64:
		return device->write( (char *)&val.val.u64, 8 ) == 8;
	case NifValue::tFileVersion:
		{
			if ( NifModel * mdl = static_cast<NifModel *>(const_cast<BaseModel *>(model)) ) {
				QString headerString = mdl->getItem( mdl->getHeaderItem(), "Header String" )->value().toString();
				quint32 version;

				// hack for neosteam
				if ( headerString.startsWith( "NS" ) ) {
					version = 0x08F35232;
				} else {
					version = val.val.u32;
				}

				return device->write( (char *)&version, 4 ) == 4;
			} else {
				return device->write( (char *)&val.val.u32, 4 ) == 4;
			}
		}
	case NifValue::tLink:
	case NifValue::tUpLink:

		if ( !linkAdjust ) {
			return device->write( (char *)&val.val.i32, 4 ) == 4;
		} else {
			qint32 l = val.val.i32 + 1;
			return device->write( (char *)&l, 4 ) == 4;
		}

	case NifValue::tFloat:
		return device->write( (char *)&val.val.f32, 4 ) == 4;
	case NifValue::tHfloat:
		{
			std::uint16_t	half = std::bit_cast< std::uint16_t >( qfloat16( val.val.f32 ) );
			char	v[2];
			FileBuffer::writeUInt16Fast( v, half );
			return device->write( v, 2 ) == 2;
		}
	case NifValue::tNormbyte:
		{
			uint8_t v = round( ((val.val.f32 + 1.0) / 2.0) * 255.0 );

			return device->write( (char*)&v, 1 ) == 1;
		}
	case NifValue::tByteVector3:
		{
			uint8_t v[4];
			FileBuffer::writeUInt32Fast( v, std::uint32_t( val.val.f32v4 * 127.5f + 127.5f ) );

			return device->write( (char*)v, 3 ) == 3;
		}
	case NifValue::tShortVector3:
		{
			FloatVector4	tmp = val.val.f32v4 * 32767.0f;
			tmp.maxValues( FloatVector4(-32768.0f) ).minValues( FloatVector4(32767.0f) );
			std::int32_t	xyz[4];
			tmp.convertToInt32( xyz );

			char	v[6];
			FileBuffer::writeUInt16Fast( &(v[0]), std::uint16_t( xyz[0] ) );
			FileBuffer::writeUInt16Fast( &(v[2]), std::uint16_t( xyz[1] ) );
			FileBuffer::writeUInt16Fast( &(v[4]), std::uint16_t( xyz[2] ) );

			return device->write( v, 6 ) == 6;
		}
	case NifValue::tUshortVector3:
		{
			FloatVector4	tmp = val.val.f32v4;
			tmp.maxValues( FloatVector4(0.0f) ).minValues( FloatVector4(65535.0f) );
			std::int32_t	xyz[4];
			tmp.convertToInt32( xyz );

			char	v[6];
			FileBuffer::writeUInt16Fast( &(v[0]), std::uint16_t( xyz[0] ) );
			FileBuffer::writeUInt16Fast( &(v[2]), std::uint16_t( xyz[1] ) );
			FileBuffer::writeUInt16Fast( &(v[4]), std::uint16_t( xyz[2] ) );

			return device->write( v, 6 ) == 6;
		}
	case NifValue::tHalfVector3:
		{
			FloatVector4	vec = val.val.f32v4;

			char	v[8];
#if ENABLE_X86_64_SIMD >= 3
			uint64_t	xyz = vec.convertToFloat16();
			FileBuffer::writeUInt64Fast( v, xyz );
#else
			FileBuffer::writeUInt16Fast( &(v[0]), std::bit_cast< std::uint16_t >( qfloat16( vec[0] ) ) );
			FileBuffer::writeUInt16Fast( &(v[2]), std::bit_cast< std::uint16_t >( qfloat16( vec[1] ) ) );
			FileBuffer::writeUInt16Fast( &(v[4]), std::bit_cast< std::uint16_t >( qfloat16( vec[2] ) ) );
#endif
			return device->write( v, 6 ) == 6;
		}
	case NifValue::tHalfVector2:
		{
			FloatVector4	vec = val.val.f32v4;

			char	v[8];
#if ENABLE_X86_64_SIMD >= 3
			uint64_t	xy = vec.convertToFloat16();
			FileBuffer::writeUInt64Fast( v, xy );
#else
			FileBuffer::writeUInt16Fast( &(v[0]), std::bit_cast< std::uint16_t >( qfloat16( vec[0] ) ) );
			FileBuffer::writeUInt16Fast( &(v[2]), std::bit_cast< std::uint16_t >( qfloat16( vec[1] ) ) );
#endif
			return device->write( v, 4 ) == 4;
		}
	case NifValue::tVector3:
	case NifValue::tColor3:
		return device->write( (const char *) &(val.val.f32v4[0]), 12 ) == 12;
	case NifValue::tVector4:
	case NifValue::tQuat:
	case NifValue::tColor4:
		return device->write( (const char *) &(val.val.f32v4[0]), 16 ) == 16;
	case NifValue::tByteVector4:
		{
			ByteVector4	vec( val.val.f32v4 );
			char	v[4];
			FileBuffer::writeUInt32Fast( v, std::uint32_t( vec ) );
			return device->write( v, 4 ) == 4;
		}
	case NifValue::tUDecVector4:
		{
			UDecVector4	vec( val.val.f32v4 );
			char	v[4];
			FileBuffer::writeUInt32Fast( v, std::uint32_t( vec ) );
			return device->write( v, 4 ) == 4;
		}
	case NifValue::tTriangle:
		return device->write( (const char *) &(val.val.t), 6 ) == 6;
	case NifValue::tQuatXYZW:
		{
			FloatVector4	tmp = val.val.f32v4;
			tmp.shuffleValues( 0x39 );	// 1, 2, 3, 0
			return device->write( (const char *) &(tmp[0]), 16 ) == 16;
		}
	case NifValue::tMatrix:
		return device->write( (const char *)static_cast<Matrix *>(val.val.data)->m, 36 ) == 36;
	case NifValue::tMatrix4:
		return device->write( (const char *)static_cast<Matrix4 *>(val.val.data)->m, 64 ) == 64;
	case NifValue::tVector2:
		return device->write( (const char *) &(val.val.f32v4[0]), 8 ) == 8;
	case NifValue::tByteColor4:
		{
			ByteColor4	color( val.val.f32v4 );
			char	c[4];
			FileBuffer::writeUInt32Fast( c, std::uint32_t(color) );
			return device->write( c, 4 ) == 4;
		}
	case NifValue::tByteColor4BGRA:
		{
			ByteColor4BGRA	color( val.val.f32v4 );
			char	c[4];
			FileBuffer::writeUInt32Fast( c, std::uint32_t(color) );
			return device->write( c, 4 ) == 4;
		}
	case NifValue::tSizedString:
	case NifValue::tSizedString16:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			//string.replace( "\\r", "\r" );
			//string.replace( "\\n", "\n" );
			char	len[4];
			FileBuffer::writeUInt32Fast( len, std::uint32_t(string.size()) );
			int	lenSize = ( val.type() == NifValue::tSizedString16 ? 2 : 4 );

			if ( device->write( len, lenSize ) != lenSize )
				return false;

			return device->write( string.constData(), string.size() ) == string.size();
		}
	case NifValue::tShortString:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			//string.replace( "\\r", "\r" );
			//string.replace( "\\n", "\n" );

			qsizetype	len = std::min< qsizetype >( string.size(), 254 );
			// make sure the string has exactly one terminating null character
			while ( len > 0 && string.at( len - 1 ) == '\0' )
				len--;
			if ( len >= string.size() )
				string.append( '\0' );
			else
				string.data()[len] = '\0';
			len++;

			unsigned char tmp = (unsigned char) len;

			if ( device->write( (char *)&tmp, 1 ) != 1 )
				return false;

			return device->write( string.constData(), len ) == len;
		}
	case NifValue::tText:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			std::int32_t len = std::int32_t( string.size() );

			if ( device->write( (char *)&len, 4 ) != 4 )
				return false;

			return device->write( (const char *)string.constData(), string.size() ) == string.size();
		}
	case NifValue::tHeaderString:
	case NifValue::tLineString:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();

			if ( device->write( string.constData(), string.length() ) != string.length() )
				return false;

			return (device->write( "\n", 1 ) == 1);
		}
	case NifValue::tChar8String:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			quint32 n = std::min<quint32>( 8, string.length() );

			if ( device->write( string.constData(), n ) != n )
				return false;

			for ( quint32 i = n; i < 8; ++i ) {
				if ( device->write( "\0", 1 ) != 1 )
					return false;
			}


			return true;
		}
	case NifValue::tByteArray:
		{
			QByteArray * array = static_cast<QByteArray *>(val.val.data);
			qsizetype len = array->size();
			char lenBuf[4];
			FileBuffer::writeUInt32Fast( lenBuf, std::uint32_t( len ) );

			if ( device->write( lenBuf, 4 ) != 4 )
				return false;

			return device->write( *array ) == len;
		}
	case NifValue::tStringPalette:
		{
			QByteArray * array = static_cast<QByteArray *>(val.val.data);
			qsizetype len = array->size();
			char lenBuf[4];
			FileBuffer::writeUInt32Fast( lenBuf, std::uint32_t( len ) );

			if ( device->write( lenBuf, 4 ) != 4 )
				return false;

			if ( device->write( *array ) != len )
				return false;

			return device->write( lenBuf, 4 ) == 4;
		}
	case NifValue::tByteMatrix:
		{
			ByteMatrix * array = static_cast<ByteMatrix *>(val.val.data);
			std::int32_t len = std::int32_t( array->count( 0 ) );

			if ( device->write( (char *)&len, 4 ) != 4 )
				return false;

			len = std::int32_t( array->count( 1 ) );

			if ( device->write( (char *)&len, 4 ) != 4 )
				return false;

			len = array->count();
			return device->write( array->data(), len ) == len;
		}
	case NifValue::tBSVertexDesc:
		{
			char v[8];
			FileBuffer::writeUInt64Fast( v, val.val.u64 );

			return device->write( v, 8 ) == 8;
		}
	case NifValue::tBlob:

		if ( val.val.data ) {
			QByteArray * array = static_cast<QByteArray *>(val.val.data);
			return device->write( array->data(), array->size() ) == array->size();
		}

		return true;
	case NifValue::tNone:
		return true;
	}

	return false;
}


/*
*  NifSStream
*/

void NifSStream::init()
{
	bool32bit = (model->inherits( "NifModel" ) && model->getVersionNumber() <= 0x04000002);
}

int NifSStream::size( const NifValue & val )
{
	switch ( val.type() ) {
	case NifValue::tBool:

		if ( bool32bit )
			return 4;
		else
			return 1;

	case NifValue::tByte:
	case NifValue::tNormbyte:
		return 1;
	case NifValue::tWord:
	case NifValue::tShort:
	case NifValue::tFlags:
	case NifValue::tBlockTypeIndex:
		return 2;
	case NifValue::tStringOffset:
	case NifValue::tInt:
	case NifValue::tUInt:
	case NifValue::tULittle32:
	case NifValue::tStringIndex:
	case NifValue::tFileVersion:
	case NifValue::tLink:
	case NifValue::tUpLink:
	case NifValue::tFloat:
		return 4;
	case NifValue::tInt64:
	case NifValue::tUInt64:
		return 8;
	case NifValue::tHfloat:
		return 2;
	case NifValue::tByteVector3:
		return 3;
	case NifValue::tHalfVector3:
	case NifValue::tShortVector3:
	case NifValue::tUshortVector3:
		return 6;
	case NifValue::tHalfVector2:
	case NifValue::tByteVector4:
	case NifValue::tUDecVector4:
		return 4;
	case NifValue::tVector3:
		return 12;
	case NifValue::tVector4:
		return 16;
	case NifValue::tTriangle:
		return 6;
	case NifValue::tQuat:
	case NifValue::tQuatXYZW:
		return 16;
	case NifValue::tMatrix:
		return 36;
	case NifValue::tMatrix4:
		return 64;
	case NifValue::tVector2:
	case NifValue::tBSVertexDesc:
		return 8;
	case NifValue::tColor3:
		return 12;
	case NifValue::tByteColor4:
	case NifValue::tByteColor4BGRA:
		return 4;
	case NifValue::tColor4:
		return 16;
	case NifValue::tSizedString:
	case NifValue::tSizedString16:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			//string.replace( "\\r", "\r" );
			//string.replace( "\\n", "\n" );
			return string.size() + ( val.type() == NifValue::tSizedString16 ? 2 : 4 );
		}
	case NifValue::tShortString:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();

			//string.replace( "\\r", "\r" );
			//string.replace( "\\n", "\n" );

			qsizetype	len = std::min< qsizetype >( string.size(), 254 );
			while ( len > 0 && string.at( len - 1 ) == '\0' )
				len--;
			return int( len + 2 );
		}
	case NifValue::tText:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			return 4 + string.size();
		}
	case NifValue::tHeaderString:
	case NifValue::tLineString:
		{
			QByteArray string = static_cast<QString *>(val.val.data)->toLatin1();
			return string.length() + 1;
		}
	case NifValue::tChar8String:
		{
			return 8;
		}
	case NifValue::tByteArray:
		{
			QByteArray * array = static_cast<QByteArray *>(val.val.data);
			return 4 + array->size();
		}
	case NifValue::tStringPalette:
		{
			QByteArray * array = static_cast<QByteArray *>(val.val.data);
			return 4 + array->size() + 4;
		}
	case NifValue::tByteMatrix:
		{
			ByteMatrix * array = static_cast<ByteMatrix *>(val.val.data);
			return 4 + 4 + array->count();
		}

	case NifValue::tBlob:

		if ( val.val.data ) {
			QByteArray * array = static_cast<QByteArray *>(val.val.data);
			return array->size();
		}

		return 0;

	case NifValue::tNone:
	default:
		return 0;
	}

	return 0;
}
