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

#include "nifvalue.h"

#include "model/nifmodel.h"

#include <QRegularExpression>
#include <QSettings>


//! @file nifvalue.cpp NifValue

QHash<QString, NifValue::Type>        NifValue::typeMap;
QHash<QString, QString>               NifValue::typeTxt;
QHash<QString, NifValue::EnumOptions> NifValue::enumMap;
QHash<QString, QString>               NifValue::aliasMap;


static int OPT_PER_LINE = -1;

/*
 *  NifValue
 */

NifValue::NifValue( Type t )
{
	changeType( t );
}

NifValue::NifValue( const NifValue & other )
{
	operator=(other);
}

NifValue::~NifValue()
{
	clear();
}

void NifValue::initialize()
{
	typeMap.clear();
	typeTxt.clear();

	typeMap.insert( "bool",   NifValue::tBool );
	typeMap.insert( "byte",   NifValue::tByte );
	typeMap.insert( "sbyte",   NifValue::tByte );
	typeMap.insert( "normbyte", NifValue::tNormbyte );
	typeMap.insert( "char",   NifValue::tByte );
	typeMap.insert( "word",   NifValue::tWord );
	typeMap.insert( "short",  NifValue::tShort );
	typeMap.insert( "int",    NifValue::tInt );
	typeMap.insert( "Flags",  NifValue::tFlags );
	typeMap.insert( "ushort", NifValue::tWord );
	typeMap.insert( "uint",   NifValue::tUInt );
	typeMap.insert( "ulittle32", NifValue::tULittle32 );
	typeMap.insert( "int64",  NifValue::tInt64 );
	typeMap.insert( "uint64", NifValue::tUInt64 );
	typeMap.insert( "Ref",    NifValue::tLink );
	typeMap.insert( "Ptr",    NifValue::tUpLink );
	typeMap.insert( "float",  NifValue::tFloat );
	typeMap.insert( "SizedString", NifValue::tSizedString );
	typeMap.insert( "SizedString16", NifValue::tSizedString16 );
	typeMap.insert( "Text",        NifValue::tText );
	typeMap.insert( "ExportString", NifValue::tShortString );
	typeMap.insert( "Color3",      NifValue::tColor3 );
	typeMap.insert( "Color4",      NifValue::tColor4 );
	typeMap.insert( "Vector4",     NifValue::tVector4 );
	typeMap.insert( "ByteVector4", NifValue::tByteVector4 );
	typeMap.insert( "UDecVector4", NifValue::tUDecVector4 );
	typeMap.insert( "Vector3",     NifValue::tVector3 );
	typeMap.insert( "TBC",         NifValue::tVector3 );
	typeMap.insert( "Quaternion",  NifValue::tQuat );
	typeMap.insert( "QuaternionWXYZ", NifValue::tQuat );
	typeMap.insert( "QuaternionXYZW", NifValue::tQuatXYZW );
	typeMap.insert( "hkQuaternion",   NifValue::tQuatXYZW );
	typeMap.insert( "Matrix33",       NifValue::tMatrix );
	typeMap.insert( "Matrix44",       NifValue::tMatrix4 );
	typeMap.insert( "Vector2",        NifValue::tVector2 );
	typeMap.insert( "TexCoord",       NifValue::tVector2 );
	typeMap.insert( "Triangle",       NifValue::tTriangle );
	typeMap.insert( "ByteArray",      NifValue::tByteArray );
	typeMap.insert( "ByteMatrix",     NifValue::tByteMatrix );
	typeMap.insert( "FileVersion",    NifValue::tFileVersion );
	typeMap.insert( "HeaderString",   NifValue::tHeaderString );
	typeMap.insert( "LineString",     NifValue::tLineString );
	typeMap.insert( "StringPalette",  NifValue::tStringPalette );
	typeMap.insert( "StringOffset",   NifValue::tStringOffset );
	typeMap.insert( "NiFixedString",  NifValue::tStringIndex );
	typeMap.insert( "BlockTypeIndex", NifValue::tBlockTypeIndex );
	typeMap.insert( "char8string",    NifValue::tChar8String );
	typeMap.insert( "string",   NifValue::tStringIndex );	// these can be overridden by NifModel if version < 20.1.0.3
	typeMap.insert( "FilePath", NifValue::tStringIndex );
	typeMap.insert( "blob",     NifValue::tBlob );
	typeMap.insert( "hfloat",   NifValue::tHfloat );
	typeMap.insert( "HalfVector3", NifValue::tHalfVector3 );
	typeMap.insert( "ShortVector3",  NifValue::tShortVector3 );
	typeMap.insert( "UshortVector3",  NifValue::tUshortVector3 );
	typeMap.insert( "ByteVector3", NifValue::tByteVector3 );
	typeMap.insert( "HalfVector2", NifValue::tHalfVector2 );
	typeMap.insert( "HalfTexCoord", NifValue::tHalfVector2 );
	typeMap.insert( "ByteColor4", NifValue::tByteColor4 );
	typeMap.insert( "ByteColor4BGRA", NifValue::tByteColor4BGRA );
	//typeMap.insert( "BSVertexDesc", NifValue::tBSVertexDesc );

	enumMap.clear();
}

NifValue::Type NifValue::type( const QString & id )
{
	if ( typeMap.isEmpty() )
		initialize();

	return typeMap.value( id, tNone );
}

void NifValue::setTypeDescription( const QString & typId, const QString & txt )
{
	typeTxt[typId] = QString( txt ).replace( "<", "&lt;" ).replace( "\n", "<br/>" );
}

QString NifValue::typeDescription( const QString & typId )
{
	if ( !enumMap.contains( typId ) )
		return QString( "<p><b>%1</b></p><p>%2</p>" ).arg( typId, typeTxt.value( typId ) );

	// Cache the generated HTML description
	static QHash<QString, QString> txtCache;

	if ( txtCache.contains( typId ) )
		return txtCache[typId];

	QString txt = QString( "<p><b>%1 (%2)</b><p>%3</p>" ).arg( typId, aliasMap.value( typId ), typeTxt.value( typId ) );

	txt += "<table><tr><td><table>";
	QMapIterator<quint32, QPair<QString, QString> > it( enumMap[ typId ].o );
	int cnt = 0;

	while ( it.hasNext() ) {
		if ( cnt++ > 31 ) {
			cnt  = 0;
			txt += "</table></td><td><table>";
		}

		it.next();
		txt += QString( "<tr><td><p style='white-space:pre'>%2 %1</p></td><td><p style='white-space:pre'>%3</p></td></tr>" )
				.arg( it.value().first ).arg( it.key() ).arg( it.value().second );
	}

	txt += "</table></td></tr></table>";

	txtCache.insert( typId, txt );

	return txt;
}

bool NifValue::registerAlias( const QString & alias, const QString & original )
{
	if ( typeMap.isEmpty() )
		initialize();

	if ( typeMap.contains( original ) && !typeMap.contains( alias ) ) {
		typeMap.insert( alias, typeMap[original] );
		aliasMap.insert( alias, original );
		return true;
	}

	return false;
}

bool NifValue::registerEnumOption( const QString & eid, const QString & oid, quint32 oval, const QString & otxt )
{
	QMap<quint32, QPair<QString, QString> > & e = enumMap[eid].o;

	if ( e.contains( oval ) )
		return false;

	e[oval] = QPair<QString, QString>( oid, otxt );
	return true;
}

QStringList NifValue::enumOptions( const QString & eid )
{
	QStringList opts;

	if ( enumMap.contains( eid ) ) {
		QMapIterator<quint32, QPair<QString, QString> > it( enumMap[ eid ].o );

		while ( it.hasNext() ) {
			it.next();
			opts << it.value().first;
		}
	}

	return opts;
}

bool NifValue::registerEnumType( const QString & eid, EnumType eTyp )
{
	if ( enumMap.contains( eid ) )
		return false;

	enumMap[eid].t = eTyp;
	return true;
}

NifValue::EnumType NifValue::enumType( const QString & eid )
{
	return (enumMap.contains( eid )) ? enumMap[eid].t : EnumType::eNone;
}

QString NifValue::enumOptionName( const QString & eid, quint32 val )
{
	if ( enumMap.contains( eid ) ) {
		NifValue::EnumOptions & eo = enumMap[eid];

		if ( eo.t == NifValue::eFlags ) {
			QString text;
			quint32 val2 = 0;

			if ( OPT_PER_LINE == -1 ) {
				QSettings settings;
				OPT_PER_LINE = settings.value( "Settings/UI/Options Per Line", 3 ).toInt();
			}

			int opt = 0;
			auto it = eo.o.constBegin();
			while ( it != eo.o.constEnd() ) {
				if ( val & ( 1 << it.key() ) ) {
					val2 |= ( 1 << it.key() );

					if ( !text.isEmpty() )
						text += " | ";

					if ( it != eo.o.constEnd() && opt != 0 && opt % OPT_PER_LINE == 0 )
						text += "\n";

					text += it.value().first;

					opt++;
				}

				it++;
			}

			// Append any leftover value not covered by enums
			val2 = (val & ~val2);
			if ( val2 ) {
				if ( !text.isEmpty() )
					text += " | ";

				text += QString::number( val2, 16 );
			}

			return text;
		} else if ( eo.t == NifValue::eDefault ) {
			if ( eo.o.contains( val ) )
				return eo.o.value( val ).first;
		}

		return QString::number( val );
	}

	return QString();
}

QString NifValue::enumOptionText( const QString & eid, quint32 val )
{
	return enumMap.value( eid ).o.value( val ).second;
}

quint32 NifValue::enumOptionValue( const QString & eid, const QString & oid, bool * ok )
{
	if ( enumMap.contains( eid ) ) {
		EnumOptions & eo = enumMap[ eid ];
		QMapIterator<quint32, QPair<QString, QString> > it( eo.o );

		if ( eo.t == NifValue::eFlags ) {
			if ( ok )
				*ok = true;

			quint32 value = 0;
			QStringList list = oid.split( QRegularExpression( "\\s*\\|\\s*" ), Qt::SkipEmptyParts );
			QStringListIterator lit( list );

			while ( lit.hasNext() ) {
				QString str = lit.next();
				bool found  = false;
				it.toFront();

				while ( it.hasNext() ) {
					it.next();

					if ( it.value().first == str ) {
						value |= ( 1 << it.key() );
						found  = true;
						break;
					}
				}

				if ( !found )
					value |= str.toULong( &found, 0 );

				if ( ok )
					*ok &= found;
			}

			return value;
		} else if ( eo.t == NifValue::eDefault ) {
			while ( it.hasNext() ) {
				it.next();

				if ( it.value().first == oid ) {
					if ( ok )
						*ok = true;

					return it.key();
				}
			}
		}
	}

	if ( ok )
		*ok = false;

	return 0;
}

const NifValue::EnumOptions & NifValue::enumOptionData( const QString & eid )
{
	return enumMap[eid];
}

void NifValue::clear()
{
	switch ( typ ) {
	case tSizedString:
	case tSizedString16:
	case tText:
	case tShortString:
	case tHeaderString:
	case tLineString:
	case tChar8String:
		delete static_cast<QString *>( val.data );
		break;
	case tMatrix:
		delete static_cast<Matrix *>( val.data );
		break;
	case tMatrix4:
		delete static_cast<Matrix4 *>( val.data );
		break;
	case tByteArray:
	case tStringPalette:
		delete static_cast<QByteArray *>( val.data );
		break;
	case tByteMatrix:
		delete static_cast<ByteMatrix *>( val.data );
		break;
	case tBlob:
		delete static_cast<QByteArray *>( val.data );
		break;
	default:
		break;
	}

	typ = tNone;
	val.clear();
}

void NifValue::changeType( Type t )
{
	if ( typ == t )
		return;

	if ( typ != tNone )
		clear();

	switch ( ( typ = t ) ) {
	case tLink:
	case tUpLink:
		val.i32 = -1;
		return;
	case tVector3:
	case tHalfVector3:
	case tShortVector3:
	case tUshortVector3:
	case tByteVector3:
	case tVector4:
	case tVector2:
	case tHalfVector2:
		val.f32v4 = FloatVector4( 0.0f );
		return;
	case tByteVector4:
	case tUDecVector4:
		val.f32v4 = FloatVector4( 0.0f, 0.0f, 1.0f, 1.0f );
		return;
	case tMatrix:
		val.data = new Matrix();
		return;
	case tMatrix4:
		val.data = new Matrix4();
		return;
	case tQuat:
	case tQuatXYZW:
		{
			Quat	tmp;
			std::memcpy( &( val.f32v4[0] ), &( tmp[0] ), sizeof( FloatVector4 ) );
		}
		return;
	case tSizedString:
	case tSizedString16:
	case tText:
	case tShortString:
	case tHeaderString:
	case tLineString:
	case tChar8String:
		val.data = new QString();
		return;
	case tColor3:
	case tColor4:
	case tByteColor4:
	case tByteColor4BGRA:
		val.f32v4 = FloatVector4( 1.0f );
		return;
	case tByteArray:
	case tStringPalette:
		val.data = new QByteArray();
		return;
	case tByteMatrix:
		val.data = new ByteMatrix();
		return;
	case tStringOffset:
	case tStringIndex:
		val.u32 = 0xffffffff;
		return;
	case tBSVertexDesc:
		(void) new( &(val.u08) ) BSVertexDesc();
		return;
	case tBlob:
		val.data = new QByteArray();
		return;
	default:
		val.clear();
		return;
	}
}

void NifValue::operator=( const NifValue & other )
{
	if ( typ != other.typ )
		changeType( other.typ );

	switch ( typ ) {
	case tSizedString:
	case tSizedString16:
	case tText:
	case tShortString:
	case tHeaderString:
	case tLineString:
	case tChar8String:
		*static_cast<QString *>( val.data ) = *static_cast<QString *>( other.val.data );
		return;
	case tMatrix:
		*static_cast<Matrix *>( val.data ) = *static_cast<Matrix *>( other.val.data );
		return;
	case tMatrix4:
		*static_cast<Matrix4 *>( val.data ) = *static_cast<Matrix4 *>( other.val.data );
		return;
	case tByteArray:
	case tStringPalette:
		*static_cast<QByteArray *>( val.data ) = *static_cast<QByteArray *>( other.val.data );
		return;
	case tByteMatrix:
		*static_cast<ByteMatrix *>( val.data ) = *static_cast<ByteMatrix *>( other.val.data );
		return;
	case tBlob:
		*static_cast<QByteArray *>( val.data ) = *static_cast<QByteArray *>( other.val.data );
		return;
	default:
		std::memcpy( &val, &( other.val ), sizeof( val ) );
		return;
	}
}

bool NifValue::operator==( const NifValue & other ) const
{
	switch ( typ ) {
	case tByte:
		return val.u08 == other.val.u08;

	case tWord:
	case tFlags:
	case tStringOffset:
	case tBlockTypeIndex:
	case tShort:
		return val.u16 == other.val.u16;

	case tBool:
	case tUInt:
	case tULittle32:
	case tStringIndex:
	case tFileVersion:
		return val.u32 == other.val.u32;

	case tInt:
	case tLink:
	case tUpLink:
		return val.i32 == other.val.i32;

	case tInt64:
		return val.i64 == other.val.i64;
	case tUInt64:
	case tBSVertexDesc:
		return val.u64 == other.val.u64;

	case tNormbyte:
	case tFloat:
	case tHfloat:
		return val.f32 == other.val.f32;

	case tVector2:
	case tHalfVector2:
	{
		return ( val.f32v4[0] == other.val.f32v4[0] && val.f32v4[1] == other.val.f32v4[1] );
	}

	case tColor3:
	case tVector3:
	case tHalfVector3:
	case tShortVector3:
	case tUshortVector3:
	case tByteVector3:
	{
		return ( val.f32v4[0] == other.val.f32v4[0] && val.f32v4[1] == other.val.f32v4[1]
				&& val.f32v4[2] == other.val.f32v4[2] );
	}

	case tColor4:
	case tByteColor4:
	case tByteColor4BGRA:
	case tQuat:
	case tQuatXYZW:
	case tVector4:
	case tByteVector4:
	case tUDecVector4:
	{
		return ( val.f32v4[0] == other.val.f32v4[0] && val.f32v4[1] == other.val.f32v4[1]
				&& val.f32v4[2] == other.val.f32v4[2] && val.f32v4[3] == other.val.f32v4[3] );
	}

	case tTriangle:
	{
		return val.t == other.val.t;
	}

	case tSizedString:
	case tSizedString16:
	case tText:
	case tShortString:
	case tHeaderString:
	case tLineString:
	case tChar8String:
	{
		QString * s1 = static_cast<QString *>(val.data);
		QString * s2 = static_cast<QString *>(other.val.data);

		if ( !s1 || !s2 )
			return false;

		return *s1 == *s2;
	}

	case tMatrix:
	{
		Matrix * m1 = static_cast<Matrix *>(val.data);
		Matrix * m2 = static_cast<Matrix *>(other.val.data);

		if ( !m1 || !m2 )
			return false;

		return *m1 == *m2;
	}
	case tMatrix4:
	{
		Matrix4 * m1 = static_cast<Matrix4 *>(val.data);
		Matrix4 * m2 = static_cast<Matrix4 *>(other.val.data);

		if ( !m1 || !m2 )
			return false;

		return *m1 == *m2;
	}

	case tByteArray:
	case tByteMatrix:
	case tStringPalette:
	case tBlob:
	{
		QByteArray * a1 = static_cast<QByteArray *>(val.data);
		QByteArray * a2 = static_cast<QByteArray *>(other.val.data);

		if ( a1->isNull() || a2->isNull() )
			return false;

		return *a1 == *a2;
	}

	case tNone:
	default:
		return false;
	}

	return false;
}

bool NifValue::operator<( const NifValue & other ) const
{
	Q_UNUSED( other );
	return false;
}


QVariant NifValue::toVariant() const
{
	QVariant v;
	v.setValue( *this );
	return v;
}

bool NifValue::setFromVariant( const QVariant & var )
{
	if ( var.canConvert<NifValue>() ) {
		operator=( var.value<NifValue>() );
		return true;
	} else if ( var.type() == QVariant::String ) {
		return set<QString>( var.toString(), nullptr, nullptr );
	}

	return false;
}

bool NifValue::setFromString( const QString & s, const BaseModel * model, const NifItem * item )
{
	if ( !isAllocated() ) [[likely]]
		val.clear();

	bool ok = false;

	switch ( typ ) {
	case tBool:
		if ( s == QLatin1String("yes") || s == QLatin1String("true") ) {
			val.u32 = 1;
			ok = true;
		} else if ( s == QLatin1String("no") || s == QLatin1String("false") ) {
			val.u32 = 0;
			ok = true;
		} else if ( s == QLatin1String("undefined") ) {
			val.u32 = 2;
			ok = true;
		}
		break;
	case tByte:
		val.u08 = s.toUInt( &ok, 0 );
		break;
	case tWord:
	case tFlags:
	case tStringOffset:
	case tBlockTypeIndex:
	case tShort:
		val.u16 = s.toShort( &ok, 0 );
		break;
	case tInt:
		val.i32 = s.toInt( &ok, 0 );
		break;
	case tUInt:
	case tULittle32:
		val.u32 = s.toUInt( &ok, 0 );
		break;
	case tInt64:
		val.i64 = s.toLongLong( &ok, 0 );
		break;
	case tUInt64:
		val.u64 = s.toULongLong( &ok, 0 );
		break;
	case tStringIndex:
		val.u32 = s.toUInt( &ok );
		break;
	case tLink:
	case tUpLink:
		val.i32 = s.toInt( &ok );
		break;
	case tFloat:
		val.f32 = s.toDouble( &ok );
		break;
	case tHfloat:
	case tNormbyte:
		val.f32 = s.toDouble( &ok );
		break;
	case tSizedString:
	case tSizedString16:
	case tText:
	case tShortString:
	case tHeaderString:
	case tLineString:
	case tChar8String:
		*static_cast<QString *>( val.data ) = s;
		ok = true;
		break;
	case tColor3:
	case tColor4:
	case tByteColor4:
	case tByteColor4BGRA:
		val.f32v4 = FloatVector4( Color4( QColor(s) ) );
		ok = true;
		break;
	case tFileVersion:
		val.u32 = NifModel::version2number( s );
		ok = (val.u32 != 0);
		break;
	case tVector2:
	case tHalfVector2:
		{
			Vector2	tmp;
			tmp.fromString( s );
			val.f32v4[0] = tmp[0];
			val.f32v4[1] = tmp[1];
		}
		ok = true;
		break;
	case tVector3:
	case tHalfVector3:
	case tShortVector3:
	case tUshortVector3:
	case tByteVector3:
		{
			Vector3	tmp;
			tmp.fromString( s );
			val.f32v4 = FloatVector4( tmp );
		}
		ok = true;
		break;
	case tVector4:
	case tByteVector4:
	case tUDecVector4:
		{
			Vector4	tmp;
			tmp.fromString( s );
			val.f32v4 = FloatVector4( tmp );
		}
		ok = true;
		break;
	case tQuat:
	case tQuatXYZW:
		{
			Quat	tmp;
			tmp.fromString( s );
			val.f32v4 = FloatVector4( &(tmp[0]) );
		}
		ok = true;
		break;
	default:
		break;
	}

	if ( !ok && model )
		reportConvertFromError( model, item, QString("string \"%1\"").arg(s) );
	return ok;
}

QString NifValue::toString() const
{
	switch ( typ ) {
	case tBool:
		return ( (val.u32 == 2) ? "undefined" : (val.u32 ? "yes" : "no") );
	case tByte:
	case tWord:
	case tFlags:
	case tStringOffset:
	case tBlockTypeIndex:
	case tUInt:
	case tULittle32:
		return QString::number( val.u32 );
	case tStringIndex:
		return QString::number( val.u32 );
	case tInt64:
		return QString::number( val.i64 );
	case tUInt64:
		return QString::number( val.u64 );
	case tShort:
		return QString::number( qint16( val.u16 ) );
	case tInt:
		return QString::number( qint32( val.u32 ) );
	case tLink:
	case tUpLink:
		return QString::number( val.i32 );
	case tFloat:
		if ( val.f32 == 0.0f )
			return QString("0.0");
		return NumOrMinMax( val.f32, 'G', 6 );
	case tHfloat:
	case tNormbyte:
		return QString::number( val.f32, 'f', 4 );
	case tSizedString:
	case tSizedString16:
	case tText:
	case tShortString:
	case tHeaderString:
	case tLineString:
	case tChar8String:
		return *static_cast<QString *>( val.data );
	case tColor3:
		{
			FloatVector4	c = FloatVector4( 0.0f ).blendValues( val.f32v4, 0x07 );

			// HDR Colors
			if ( std::max( std::max( c[0], c[1] ), c[2] ) > 1.0f )
				return QString( "R %1 G %2 B %3" ).arg( c[0], 0, 'f', 3 ).arg( c[1], 0, 'f', 3 ).arg( c[2], 0, 'f', 3 );

			// #RRGGBB
			return QString( "#%1" ).arg( std::uint32_t( c.shuffleValues( 0xC6 ) * 255.0f ), 6, 16, QChar( '0' ) );
		}
	case tColor4:
	case tByteColor4:
	case tByteColor4BGRA:
		{
			FloatVector4	c = val.f32v4;

			// HDR Colors
			if ( std::max( std::max( c[0], c[1] ), std::max( c[2], c[3] ) ) > 1.0f )
				return QString( "R %1 G %2 B %3 A %4" )
						.arg( c[0], 0, 'f', 3 ).arg( c[1], 0, 'f', 3 ).arg( c[2], 0, 'f', 3 ).arg( c[3], 0, 'f', 3 );

			// #RRGGBBAA
			return QString( "#%1" ).arg( std::uint32_t( c.shuffleValues( 0x1B ) * 255.0f ), 8, 16, QChar( '0' ) );
		}
	case tVector2:
	case tHalfVector2:
		{
			return QString( "X %1 Y %2" )
					.arg( NumOrMinMax( val.f32v4[0], 'f', VECTOR_DECIMALS ) )
					.arg( NumOrMinMax( val.f32v4[1], 'f', VECTOR_DECIMALS ) );
		}
	case tVector3:
	case tHalfVector3:
	case tShortVector3:
	case tUshortVector3:
	case tByteVector3:
		{
			return QString( "X %1 Y %2 Z %3" )
					.arg( NumOrMinMax( val.f32v4[0], 'f', VECTOR_DECIMALS ) )
					.arg( NumOrMinMax( val.f32v4[1], 'f', VECTOR_DECIMALS ) )
					.arg( NumOrMinMax( val.f32v4[2], 'f', VECTOR_DECIMALS ) );
		}
	case tVector4:
	case tByteVector4:
	case tUDecVector4:
		{
			return QString( "X %1 Y %2 Z %3 W %4" )
					.arg( NumOrMinMax( val.f32v4[0], 'f', VECTOR_DECIMALS ) )
					.arg( NumOrMinMax( val.f32v4[1], 'f', VECTOR_DECIMALS ) )
					.arg( NumOrMinMax( val.f32v4[2], 'f', VECTOR_DECIMALS ) )
					.arg( NumOrMinMax( val.f32v4[3], 'f', VECTOR_DECIMALS ) );
		}
	case tMatrix:
	case tQuat:
	case tQuatXYZW:
		{
			Matrix m;

			if ( typ == tMatrix )
				m = *( static_cast<Matrix *>( val.data ) );
			else
				m.fromQuat( Quat( val.f32v4[0], val.f32v4[1], val.f32v4[2], val.f32v4[3] ) );

			float x, y, z;
			QString pre, suf;

			if ( !m.toEuler( x, y, z ) ) {
				pre = "(";
				suf = ")";
			}

			return ( pre + QString( "R %1 P %2 Y %3" ) + suf )
					.arg( NumOrMinMax( rad2deg(x), 'f', ROTATION_COARSE ) )
					.arg( NumOrMinMax( rad2deg(y), 'f', ROTATION_COARSE ) )
					.arg( NumOrMinMax( rad2deg(z), 'f', ROTATION_COARSE ) );
		}
	case tMatrix4:
		{
			Matrix4 * m = static_cast<Matrix4 *>( val.data );
			Matrix r; Vector3 t, s;
			m->decompose( t, r, s );
			float xr, yr, zr;
			r.toEuler( xr, yr, zr );
			return QString( "Trans( X %1 Y %2 Z %3 ) Rot( R %4 P %5 Y %6 ) Scale( X %7 Y %8 Z %9 )" )
					.arg( t[0], 0, 'f', 3 )
					.arg( t[1], 0, 'f', 3 )
					.arg( t[2], 0, 'f', 3 )
					.arg( rad2deg(xr), 0, 'f', 3 )
					.arg( rad2deg(yr), 0, 'f', 3 )
					.arg( rad2deg(zr), 0, 'f', 3 )
					.arg( s[0], 0, 'f', 3 )
					.arg( s[1], 0, 'f', 3 )
					.arg( s[2], 0, 'f', 3 );
		}
	case tByteArray:
		return QString( "%1 bytes" ).arg( static_cast<QByteArray *>( val.data )->size() );
	case tStringPalette:
		{
			QByteArray * array = static_cast<QByteArray *>( val.data );
			QString s;

			while ( s.length() < array->size() ) {
				s += &array->data()[s.length()];
				s += QChar( '|' );
			}

			return s;
		}
	case tByteMatrix:
		{
			ByteMatrix * array = static_cast<ByteMatrix *>( val.data );
			return QString( "%1 bytes  [%2 x %3]" )
					.arg( array->count() )
					.arg( array->count( 0 ) )
					.arg( array->count( 1 ) );
		}
	case tFileVersion:
		return NifModel::version2string( val.u32 );
	case tTriangle:
		{
			return QString( "%1 %2 %3" ).arg( val.t.v1() ).arg( val.t.v2() ).arg( val.t.v3() );
		}
	case tBSVertexDesc:
		return reinterpret_cast<const BSVertexDesc *>( &(val.u08) )->toString();
	case tBlob:
		{
			QByteArray * array = static_cast<QByteArray *>( val.data );
			return QString( "%1 bytes" ).arg( array->size() );
		}
	default:
		return QString();
	}
}

void NifValue::reportModelError( const BaseModel * model, const NifItem * item, const QString & msg )
{
	if ( item )
		model->reportError( item, msg );
	else
		model->reportError( msg );
}

void NifValue::reportConvertToError( const BaseModel * model, const NifItem * item, const QString & toType ) const
{
	reportModelError( model, item, QString("Could not convert a value of type %1 to %2.").arg( getTypeDebugStr( type() ), toType ) );
}

void NifValue::reportConvertFromError( const BaseModel * model, const NifItem * item, const QString & fromType ) const
{
	reportModelError( model, item, QString("Could not assign %1 to a value of type %2.").arg( fromType, getTypeDebugStr( type() ) ) );
}

QString NifValue::getTypeDebugStr( NifValue::Type t )
{
	const char * typeStr;
	switch ( t ) {
	case tBool:             typeStr = "Bool"; break;
	case tByte:             typeStr = "Byte"; break;
	case tWord:             typeStr = "Word"; break;
	case tFlags:            typeStr = "Flags"; break;
	case tStringOffset:     typeStr = "StringOffset"; break;
	case tStringIndex:      typeStr = "StringIndex"; break;
	case tBlockTypeIndex:   typeStr = "BlockTypeIndex"; break;
	case tInt:              typeStr = "Int"; break;
	case tShort:            typeStr = "Short"; break;
	case tULittle32:        typeStr = "ULittle32"; break;
	case tInt64:            typeStr = "Int64"; break;
	case tUInt64:           typeStr = "UInt64"; break;
	case tUInt:             typeStr = "UInt"; break;
	case tLink:             typeStr = "Link"; break;
	case tUpLink:           typeStr = "UpLink"; break;
	case tFloat:            typeStr = "Float"; break;
	case tSizedString:      typeStr = "SizedString"; break;
	case tSizedString16:    typeStr = "SizedString16"; break;
	case tText:             typeStr = "Text"; break;
	case tShortString:      typeStr = "ShortString"; break;
	case tHeaderString:     typeStr = "HeaderString"; break;
	case tLineString:       typeStr = "LineString"; break;
	case tChar8String:      typeStr = "Char8String"; break;
	case tColor3:           typeStr = "Color3"; break;
	case tColor4:           typeStr = "Color4"; break;
	case tVector3:          typeStr = "Vector3"; break;
	case tQuat:             typeStr = "Quat"; break;
	case tQuatXYZW:         typeStr = "QuatXYZW"; break;
	case tMatrix:           typeStr = "Matrix"; break;
	case tMatrix4:          typeStr = "Matrix4"; break;
	case tVector2:          typeStr = "Vector2"; break;
	case tVector4:          typeStr = "Vector4"; break;
	case tByteVector4:      typeStr = "ByteVector4"; break;
	case tUDecVector4:      typeStr = "UDecVector4"; break;
	case tTriangle:         typeStr = "Triangle"; break;
	case tFileVersion:      typeStr = "FileVersion"; break;
	case tByteArray:        typeStr = "ByteArray"; break;
	case tStringPalette:    typeStr = "StringPalette"; break;
	case tByteMatrix:       typeStr = "ByteMatrix"; break;
	case tBlob:             typeStr = "Blob"; break;
	case tHfloat:           typeStr = "Hfloat"; break;
	case tHalfVector3:      typeStr = "HalfVector3"; break;
	case tShortVector3:     typeStr = "ShortVector3"; break;
	case tUshortVector3:    typeStr = "UshortVector3"; break;
	case tByteVector3:      typeStr = "ByteVector3"; break;
	case tHalfVector2:      typeStr = "HalfVector2"; break;
	case tByteColor4:       typeStr = "ByteColor4"; break;
	case tByteColor4BGRA:   typeStr = "ByteColor4BGRA"; break;
	case tBSVertexDesc:     typeStr = "BSVertexDesc"; break;
	case tNone:             typeStr = "None"; break;
	default:                typeStr = "UNKNOWN"; break;
	}

	return QString("%2 (%1)").arg( int(t) ).arg( typeStr );
}

QColor NifValue::toColor( const BaseModel * model, const NifItem * item ) const
{
	switch ( type() ) {
	case tColor3:
		return Color3( val.f32v4[0], val.f32v4[1], val.f32v4[2] ).toQColor();
	case tColor4:
	case tByteColor4:
	case tByteColor4BGRA:
		return Color4( val.f32v4[0], val.f32v4[1], val.f32v4[2], val.f32v4[3] ).toQColor();
	default:
		if ( model )
			reportConvertToError(model, item, "a color");
		return QColor();
	}
}

