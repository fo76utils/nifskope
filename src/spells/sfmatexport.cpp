#include "spellbook.h"
#include "libfo76utils/src/material.hpp"
#include "model/nifmodel.h"

#include <cctype>
#include <random>
#include <chrono>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>

//! Export Starfield material as JSON format .mat file
class spStarfieldMaterialExport final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Copy JSON to Clipboard" ); }
	QString page() const override final { return Spell::tr( "Material" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	static bool canExportMaterial( const NifModel * nif, const QModelIndex & index, bool isModified = false )
	{
		if ( !( nif && nif->getBSVersion() >= 170 && index.isValid() ) )
			return false;
		auto	iBlock = nif->getBlockIndex( index );
		if ( nif->isNiBlock( iBlock, "BSGeometry" ) )
			iBlock = nif->getBlockIndex( nif->getLink( iBlock, "Shader Property" ) );
		if ( !( nif->isNiBlock( iBlock, "BSLightingShaderProperty" )
				|| nif->isNiBlock( iBlock, "BSEffectShaderProperty" ) ) ) {
			return false;
		}
		const NifItem *	i = nif->getItem( iBlock, "Material" );
		if ( !i )
			return false;
		return ( nif->get<bool>( i, "Is Modified" ) == isModified );
	}

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return canExportMaterial( nif, index );
	}

	static std::mt19937_64	rndGen;
	static bool	rndGenInitFlag;

	static BSMaterialsCDB::BSResourceID generateResourceID(
		const BSMaterialsCDB::BSResourceID * id = nullptr, std::set< BSMaterialsCDB::BSResourceID > * idsUsed = nullptr,
		const BSMaterialsCDB * matDB = nullptr );
	static inline bool readResourceID( BSMaterialsCDB::BSResourceID & id, const std::string_view & s );
	static void generateResourceIDs( std::string & matFileData, const BSMaterialsCDB * matDB = nullptr );
	static void processItem( NifModel * nif, const QModelIndex & index, bool generateIDs = false );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		processItem( nif, index, false );
		return index;
	}
};

std::mt19937_64	spStarfieldMaterialExport::rndGen;
bool	spStarfieldMaterialExport::rndGenInitFlag = false;

BSMaterialsCDB::BSResourceID spStarfieldMaterialExport::generateResourceID(
	const BSMaterialsCDB::BSResourceID * id, std::set< BSMaterialsCDB::BSResourceID > * idsUsed,
	const BSMaterialsCDB * matDB )
{
	BSMaterialsCDB::BSResourceID	newID( 0x00040000U, 0U, 0U );
	if ( id ) {
		newID = *id;
		if ( !( newID.ext & 0x80808080U ) )
			return newID;
	}
	if ( !rndGenInitFlag ) [[unlikely]] {
		rndGenInitFlag = true;
		unsigned long long	s1, s2;
#if ENABLE_X86_64_SIMD >= 4
		__builtin_ia32_rdrand64_step( &s1 );
		__builtin_ia32_rdrand64_step( &s2 );
#else
		auto	t = std::chrono::steady_clock::now().time_since_epoch();
		s1 = (unsigned long long) std::chrono::duration_cast< std::chrono::microseconds >( t ).count();
		s2 = timerFunctionRDTSC();
#endif
		std::seed_seq	s{ std::uint32_t(s1), std::uint32_t(s1 >> 32), std::uint32_t(s2), std::uint32_t(s2 >> 32) };
		rndGen.seed( s );
	}
	while ( true ) {
		std::uint64_t	tmp1 = rndGen();
		std::uint64_t	tmp2 = rndGen();
		tmp1 = ( tmp1 & 0x07FFFFFF0003FFFFULL ) | 0xA000000000040000ULL;
		newID.file = std::uint32_t( tmp1 );
		newID.ext = std::uint32_t( tmp1 >> 32 );
		newID.dir = std::uint32_t( tmp2 );
		if ( !( ( newID.dir + 1U ) & 0xFFFFFFFEU ) ) [[unlikely]]
			continue;
		if ( matDB && matDB->getMaterial( newID ) ) [[unlikely]]
			continue;
		if ( idsUsed && !idsUsed->insert( newID ).second ) [[unlikely]]
			continue;
		break;
	}
	return newID;
}

inline bool spStarfieldMaterialExport::readResourceID( BSMaterialsCDB::BSResourceID & id, const std::string_view & s )
{
	id = BSMaterialsCDB::BSResourceID( 0, 0, 0 );
	if ( !( s.length() == 30 && s.starts_with( "res:" ) && s[12] == ':' && s[21] == ':' ) )
		return false;
	for ( size_t i = 4; i < 30; i++ ) {
		if ( i == 12 || i == 21 )
			continue;
		char	c = s[i];
		if ( ( c >= 'A' && c <= 'F' ) || ( c >= 'a' && c <= 'f' ) )
			c = c + 9;
		else if ( !( c >= '0' && c <= '9' ) )
			return false;
		id.dir = ( id.dir << 4 ) | ( id.file >> 28 );
		id.file = ( id.file << 4 ) | ( id.ext >> 28 );
		id.ext = ( id.ext << 4 ) | std::uint32_t( c & 0x0F );
	}
	return bool( id );
}

void spStarfieldMaterialExport::generateResourceIDs( std::string & matFileData, const BSMaterialsCDB * matDB )
{
	std::set< BSMaterialsCDB::BSResourceID >	idsUsed;
	std::map< BSMaterialsCDB::BSResourceID, BSMaterialsCDB::BSResourceID >	idsDefined;
	for ( size_t i = 0; ( i + 32 ) <= matFileData.length(); i++ ) {
		if ( !( matFileData[i] == '"' && matFileData[i + 31] == '"' ) )
			continue;
		std::string_view	s( matFileData.c_str() + ( i + 1 ), 30 );
		BSMaterialsCDB::BSResourceID	tmp;
		if ( !readResourceID( tmp, s ) )
			continue;
		idsUsed.insert( tmp );
		if ( i >= 13 && std::string_view( matFileData.c_str() + ( i - 13 ), 13 ) == "\n      \"ID\": " )
			idsDefined.emplace( tmp, tmp );
	}
	for ( auto & i : idsDefined )
		i.second = generateResourceID( &(i.first), &idsUsed, matDB );
	bool	warningFlag = false;
	for ( size_t i = 0; ( i + 32 ) <= matFileData.length(); i++ ) {
		if ( !( matFileData[i] == '"' && matFileData[i + 31] == '"' ) )
			continue;
		std::string_view	s( matFileData.c_str() + ( i + 1 ), 30 );
		BSMaterialsCDB::BSResourceID	tmp;
		if ( !readResourceID( tmp, s ) )
			continue;
		auto	j = idsDefined.find( tmp );
		if ( j == idsDefined.end() || j->second == tmp ) {
			if ( j == idsDefined.end() )
				warningFlag = true;
			continue;
		}
		tmp = j->second;
		char *	t = matFileData.data() + ( i + 5 );
		for ( size_t k = 0; k < 26; k++ ) {
			if ( k == 8 || k == 17 ) {
				t[k] = ':';
				continue;
			}
			char	c = char( tmp.dir >> 28 );
			tmp.dir = ( tmp.dir << 4 ) | ( tmp.file >> 28 );
			tmp.file = ( tmp.file << 4 ) | ( tmp.ext >> 28 );
			tmp.ext = tmp.ext << 4;
			t[k] = c + ( (unsigned char) c < 10 ? '0' : '7' );
		}
	}
	if ( warningFlag ) {
		QMessageBox::warning( nullptr, "NifSkope warning", QString("The material references undefined or external resource IDs") );
	}
}

void spStarfieldMaterialExport::processItem( NifModel * nif, const QModelIndex & index, bool generateIDs )
{
	QModelIndex	idx = nif->getBlockIndex( index );
	if ( nif->blockInherits( idx, "BSGeometry" ) )
		idx = nif->getBlockIndex( nif->getLink( idx, "Shader Property" ) );
	if ( nif->blockInherits( idx, "BSShaderProperty" ) )
		idx = nif->getIndex( idx, "Name" );
	else
		return;
	if ( !idx.isValid() )
		return;

	QString	materialPath( nif->resolveString( nif->getItem( idx ) ) );
	if ( !materialPath.isEmpty() ) {
		std::string	matFilePath( Game::GameManager::get_full_path( materialPath, "materials/", ".mat" ) );
		std::string	matFileData;
		try {
			CE2MaterialDB *	materials = nif->getCE2Materials();
			if ( materials ) {
				(void) materials->loadMaterial( matFilePath );
				materials->getJSONMaterial( matFileData, matFilePath );
				if ( generateIDs )
					generateResourceIDs( matFileData, materials );
				QClipboard	*clipboard;
				if ( !matFileData.empty() && ( clipboard = QGuiApplication::clipboard() ) != nullptr )
					clipboard->setText( QString::fromStdString( matFileData ) );
			}
		} catch ( std::exception& e ) {
			QMessageBox::critical( nullptr, "NifSkope error", QString("Error loading material '%1': %2" ).arg( materialPath ).arg( e.what() ) );
		}
	}
}

REGISTER_SPELL( spStarfieldMaterialExport )

//! Clone Starfield material with new resource IDs as JSON format .mat file
class spStarfieldMaterialClone final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Clone and Copy to Clipboard" ); }
	QString page() const override final { return Spell::tr( "Material" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return spStarfieldMaterialExport::canExportMaterial( nif, index );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		spStarfieldMaterialExport::processItem( nif, index, true );
		return index;
	}
};

REGISTER_SPELL( spStarfieldMaterialClone )

//! Save Starfield material with new name and resource IDs as JSON format .mat file
class spStarfieldMaterialSaveAs final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Save as New..." ); }
	QString page() const override final { return Spell::tr( "Material" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return spStarfieldMaterialExport::canExportMaterial( nif, index );
	}

	static QString getOutputFileName( const NifModel * nif, const std::string_view & matFilePath );
	static std::string getBaseName( const std::string_view & fullPath );
	static void renameMaterial(
		std::string & matFileData, const std::string_view & matPath, const std::string_view & newPath );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

QString spStarfieldMaterialSaveAs::getOutputFileName( const NifModel * nif, const std::string_view & matFilePath )
{
	QString	dirName = nif->getFolder();
	bool	setLastFilePath = false;
	if ( dirName.isEmpty() || dirName.contains( ".ba2/", Qt::CaseInsensitive ) || dirName.contains( ".bsa/", Qt::CaseInsensitive ) ) {
		QSettings	settings;
		dirName = settings.value( "Spells//Extract File/Last File Path", QString() ).toString();
		setLastFilePath = true;
	}
	if ( !dirName.isEmpty() && !dirName.endsWith( '/' ) )
		dirName.append( QChar('/') );
	dirName.append( QString::fromStdString( getBaseName( matFilePath ) ) );
	QString	fileName = QFileDialog::getSaveFileName( qApp->activeWindow(), tr("Choose a .mat file for export"), dirName, "Material (*.mat)" );
	if ( fileName.isEmpty() )
		return QString();
	if ( !fileName.endsWith( ".mat", Qt::CaseInsensitive ) )
		fileName.append( ".mat" );
	if ( setLastFilePath ) {
		QSettings	settings;
		settings.setValue( "Spells//Extract File/Last File Path", QFileInfo( fileName ).dir().absolutePath() );
	}
	return fileName;
}

std::string spStarfieldMaterialSaveAs::getBaseName( const std::string_view & fullPath )
{
	size_t	p1 = fullPath.rfind( '/' );
	size_t	p2 = fullPath.rfind( '\\' );
	size_t	p3 = fullPath.rfind( '.' );
	if ( p1 == std::string_view::npos )
		p1 = p2;
	else if ( p2 != std::string_view::npos )
		p1 = std::max( p1, p2 );
	if ( p1 == std::string_view::npos )
		p1 = 0;
	else
		p1++;
	if ( p3 == std::string_view::npos || p3 < p1 )
		p3 = fullPath.length();
	std::string	s( fullPath.data() + p1, p3 - p1 );
	for ( auto & c : s ) {
		if ( !( (unsigned char) c >= 0x20 && (unsigned char) c < 0x7F ) )
			c = '_';
	}
	return s;
}

void spStarfieldMaterialSaveAs::renameMaterial(
	std::string & matFileData, const std::string_view & matPath, const std::string_view & newPath )
{
	std::string	matBaseName( getBaseName( matPath ) );
	std::string	newBaseName( getBaseName( newPath ) );
	std::string_view	s( matFileData );
	std::string	newFileData;
	while ( true ) {
		size_t	n = s.find( '\n' );
		if ( n == std::string_view::npos )
			break;
		n++;
		std::string_view	t( s.data(), n );
		s = std::string_view( s.data() + n, s.length() - n );
		bool	foundMatch = false;
		if ( s.starts_with( "          },\n          \"Index\": 0,\n          \"Type\": \"BSComponentDB::CTName\"" ) ) {
			if ( t.starts_with( "            \"Name\": \"" ) ) {
				for ( size_t i = 0; true; i++ ) {
					if ( ( i + 21 ) >= t.length() )
						break;
					char	c = t[i + 21];
					if ( i >= matBaseName.length() ) {
						foundMatch = ( c == '"' || c == '_' || c == ' ' );
						if ( foundMatch )
							t = std::string_view( t.data() + ( i + 21 ), t.length() - ( i + 21 ) );
						break;
					}
					if ( std::tolower( c ) != std::tolower( matBaseName[i] ) )
						break;
				}
			}
		}
		if ( foundMatch )
			printToString( newFileData, "            \"Name\": \"%s", newBaseName.c_str() );
		newFileData += t;
	}
	matFileData = newFileData;
}

QModelIndex spStarfieldMaterialSaveAs::cast( NifModel * nif, const QModelIndex & index )
{
	QModelIndex	idx = nif->getBlockIndex( index );
	if ( nif->blockInherits( idx, "BSGeometry" ) )
		idx = nif->getBlockIndex( nif->getLink( idx, "Shader Property" ) );
	if ( nif->blockInherits( idx, "BSShaderProperty" ) )
		idx = nif->getIndex( idx, "Name" );
	else
		return index;
	if ( !idx.isValid() )
		return index;

	QString	materialPath( nif->resolveString( nif->getItem( idx ) ) );
	if ( !materialPath.isEmpty() ) {
		std::string	matFilePath( Game::GameManager::get_full_path( materialPath, "materials/", ".mat" ) );
		std::string	matFileData;
		try {
			CE2MaterialDB *	materials = nif->getCE2Materials();
			if ( !materials )
				return index;
			(void) materials->loadMaterial( matFilePath );
			materials->getJSONMaterial( matFileData, matFilePath );
			if ( matFileData.empty() )
				return index;

			QString	fileName = getOutputFileName( nif, matFilePath );
			if ( fileName.isEmpty() )
				return index;

			spStarfieldMaterialExport::generateResourceIDs( matFileData, materials );
			matFileData += '\n';
			renameMaterial( matFileData, matFilePath, fileName.toStdString() );

			QFile	matFile( fileName );
			if ( matFile.open( QIODevice::WriteOnly ) )
				matFile.write( matFileData.c_str(), qsizetype( matFileData.length() ) );
			else
				throw FO76UtilsError( "could not open output file" );
		} catch ( std::exception& e ) {
			QMessageBox::critical( nullptr, "NifSkope error", QString("Error exporting material '%1': %2" ).arg( materialPath ).arg( e.what() ) );
		}
	}

	return index;
}

REGISTER_SPELL( spStarfieldMaterialSaveAs )

class CE2MaterialToJSON
{
protected:
	struct MatObject
	{
		QJsonObject	jsonObject;
		const CE2MaterialObject *	o;
		BSMaterialsCDB::BSResourceID	objectID;
		QString getResourceID() const;
	};
	QMap< const CE2MaterialObject *, MatObject >	matObjectMap;
	CE2MaterialDB *	materials;
	QString	matName;
	unsigned char	objTypeIndex[8];
	QVector< MatObject * >	matObjects;

	MatObject * getObject( const CE2MaterialObject * o );
	void createObject( const CE2MaterialObject * o );
	static QJsonObject createStructure( const QJsonObject & data, const char * type );
	static QJsonObject createList( const QJsonArray & data, const char * elementType );
	static void createComponent( QJsonArray & components, const QJsonObject & data, const char * type, int n = 0 );
	static inline void insertBool( QJsonObject & data, const char * fieldName, bool value )
	{
		data.insert( fieldName, ( !value ? "false" : "true" ) );
	}
	static inline void insertBool( QJsonObject & data, const char * fieldName, std::uint32_t flags, std::uint32_t m )
	{
		data.insert( fieldName, ( !( flags & m ) ? "false" : "true" ) );
	}
	static QJsonObject createBool( bool v, const char * fieldName, const char * componentType = nullptr, int n = -1 );
	static QJsonObject createFloat( float v, const char * fieldName, const char * componentType = nullptr, int n = -1 );
	static QJsonObject createXMFLOAT( FloatVector4 v, int channels,
										const char * fieldName, const char * componentType = nullptr, int n = -1 );
	static inline QJsonObject createColor( FloatVector4 c, int n = -1 )
	{
		return createXMFLOAT( c, 4, "Value", "BSMaterial::Color", n );
	}
	void createLink( QJsonArray & components, const CE2MaterialObject * o, int n = 0 );
	void createLayeredMaterial( QJsonArray & components, const CE2Material * o );
	void createBlender( QJsonArray & components, const CE2Material::Blender * o );
	void createLayer( QJsonArray & components, const CE2Material::Layer * o );
	void createMaterial( QJsonArray & components, const CE2Material::Material * o );
	void createTextureSet( QJsonArray & components, const CE2Material::TextureSet * o );
	void createUVStream( QJsonArray & components, const CE2Material::UVStream * o );
public:
	CE2MaterialToJSON( NifModel * nif, const QString & matFilePath, const CE2MaterialObject * o );
	~CE2MaterialToJSON();
	QByteArray getJSONData() const;
};

QString CE2MaterialToJSON::MatObject::getResourceID() const
{
	if ( !o )
		return QString();
	if ( o->type == 1 && !o->parent )
		return "<this>";

	char	tmpBuf[64];
	int	len = std::snprintf( tmpBuf, 64, "res:%08X:%08X:%08X",
							(unsigned int) objectID.dir, (unsigned int) objectID.file, (unsigned int) objectID.ext );
	return QString::fromLatin1( tmpBuf, len );
}

CE2MaterialToJSON::MatObject * CE2MaterialToJSON::getObject( const CE2MaterialObject * o )
{
	if ( !o )
		return nullptr;

	auto	i = matObjectMap.find( o );
	if ( i == matObjectMap.end() ) {
		MatObject	m;
		m.o = o;
		do {
			m.objectID = spStarfieldMaterialExport::generateResourceID( nullptr, nullptr, materials );
			for ( auto j = matObjectMap.begin(); j != matObjectMap.end(); j++ ) {
				if ( j.value().objectID == m.objectID ) {
					m.objectID = BSMaterialsCDB::BSResourceID( 0, 0, 0 );
					break;
				}
			}
		} while ( !m.objectID );
		i = matObjectMap.insert( o, m );
		matObjects.append( &( i.value() ) );
		createObject( o );
	}
	return &( i.value() );
}

void CE2MaterialToJSON::createObject( const CE2MaterialObject * o )
{
	if ( !( o && o->type >= 1 && o->type <= 6 ) )
		return;

	static const char *	baseObjectPaths[6] = {
		"materials\\layered\\root\\layeredmaterials.mat",
		"materials\\layered\\root\\blenders.mat",
		"materials\\layered\\root\\layers.mat",
		"materials\\layered\\root\\materials.mat",
		"materials\\layered\\root\\texturesets.mat",
		"materials\\layered\\root\\uvstreams.mat"
	};

	MatObject &	m = matObjectMap.find( o ).value();
	QJsonObject &	jsonObject = m.jsonObject;
	jsonObject.insert( "Parent", baseObjectPaths[o->type - 1] );
	if ( !( o->type == 1 && !o->parent ) )
		jsonObject.insert( "ID", m.getResourceID() );
	if ( o->parent ) {
		QJsonArray	edges;
		QJsonObject	outerEdge;
		outerEdge.insert( "EdgeIndex", 0 );
		outerEdge.insert( "To", getObject( o->parent )->getResourceID() );
		outerEdge.insert( "Type", "BSComponentDB2::OuterEdge" );
		edges.append( outerEdge );
		jsonObject.insert( "Edges", edges );
	}
	if ( o->type != 1 || o->parent )
		objTypeIndex[o->type - 1] = objTypeIndex[o->type - 1] + 1;

	QJsonArray	components;
	{
		static const char *	objNames[6] = { "LOD", "Blender", "Layer", "Material", "TextureSet", "UVStream" };
		QJsonObject	ctNameData;
		QString	name;
		if ( o->type == 1 && !o->parent )
			name = matName;
		else if ( o->name && o->name[0] )
			name = QString::fromUtf8( o->name, -1 );
		else
			name = matName + QString( "_%1%2" ).arg( objNames[o->type - 1] ).arg( objTypeIndex[o->type - 1] );
		ctNameData.insert( "Name", name );
		createComponent( components, ctNameData, "BSComponentDB::CTName" );
	}
	if ( o->type == 1 )
		createLayeredMaterial( components, static_cast< const CE2Material * >( o ) );
	else if ( o->type == 2 )
		createBlender( components, static_cast< const CE2Material::Blender * >( o ) );
	else if ( o->type == 3 )
		createLayer( components, static_cast< const CE2Material::Layer * >( o ) );
	else if ( o->type == 4 )
		createMaterial( components, static_cast< const CE2Material::Material * >( o ) );
	else if ( o->type == 5 )
		createTextureSet( components, static_cast< const CE2Material::TextureSet * >( o ) );
	else if ( o->type == 6 )
		createUVStream( components, static_cast< const CE2Material::UVStream * >( o ) );
	jsonObject.insert( "Components", components );
}

QJsonObject CE2MaterialToJSON::createStructure( const QJsonObject & data, const char * type )
{
	QJsonObject	o;
	o.insert( "Data", data );
	o.insert( "Type", type );
	return o;
}

QJsonObject CE2MaterialToJSON::createList( const QJsonArray & data, const char * elementType )
{
	QJsonObject	o;
	o.insert( "Data", data );
	o.insert( "ElementType", elementType );
	o.insert( "Type", "<collection>" );
	return o;
}

void CE2MaterialToJSON::createComponent( QJsonArray & components, const QJsonObject & data, const char * type, int n )
{
	QJsonObject	componentObject = createStructure( data, type );
	componentObject.insert( "Index", n );
	components.append( componentObject );
}

QJsonObject CE2MaterialToJSON::createBool( bool v, const char * fieldName, const char * componentType, int n )
{
	QJsonObject	boolData;
	insertBool( boolData, fieldName, v );
	if ( !componentType )
		return boolData;
	QJsonObject	boolComponent = createStructure( boolData, componentType );
	if ( n >= 0 )
		boolComponent.insert( "Index", n );
	return boolComponent;
}

QJsonObject CE2MaterialToJSON::createFloat( float v, const char * fieldName, const char * componentType, int n )
{
	QJsonObject	floatData;
	floatData.insert( fieldName, QString::number( v ) );
	if ( !componentType )
		return floatData;
	QJsonObject	floatComponent = createStructure( floatData, componentType );
	if ( n >= 0 )
		floatComponent.insert( "Index", n );
	return floatComponent;
}

QJsonObject CE2MaterialToJSON::createXMFLOAT( FloatVector4 v, int channels,
												const char * fieldName, const char * componentType, int n )
{
	QJsonObject	xmfloatData;
	xmfloatData.insert( "x", QString::number( v[0] ) );
	xmfloatData.insert( "y", QString::number( v[1] ) );
	if ( channels >= 3 )
		xmfloatData.insert( "z", QString::number( v[2] ) );
	if ( channels >= 4 )
		xmfloatData.insert( "w", QString::number( v[3] ) );
	QJsonObject	xmfloatValue =
		createStructure( xmfloatData, ( channels < 3 ? "XMFLOAT2" : ( channels < 4 ? "XMFLOAT3" : "XMFLOAT4" ) ) );
	if ( !fieldName )
		return xmfloatValue;

	QJsonObject	xmfloatObject;
	xmfloatObject.insert( fieldName, xmfloatValue );
	if ( !componentType )
		return xmfloatObject;

	QJsonObject	xmfloatComponent = createStructure( xmfloatObject, componentType );
	if ( n >= 0 )
		xmfloatComponent.insert( "Index", n );

	return xmfloatComponent;
}

void CE2MaterialToJSON::createLink( QJsonArray & components, const CE2MaterialObject * o, int n )
{
	if ( !( o && o->type >= 1 && o->type <= 6 ) )
		return;
	QJsonObject	linkData;
	linkData.insert( "ID", getObject( o )->getResourceID() );
	const char *	t = "BSMaterial::LODMaterialID";
	if ( o->type == 2 )
		t = "BSMaterial::BlenderID";
	else if ( o->type == 3 )
		t = "BSMaterial::LayerID";
	else if ( o->type == 4 )
		t = "BSMaterial::MaterialID";
	else if ( o->type == 5 )
		t = "BSMaterial::TextureSetID";
	else if ( o->type == 6 )
		t = "BSMaterial::UVStreamID";
	createComponent( components, linkData, t, n );
}

#define getCE2MatString( t, n ) t[std::min< unsigned int >( n, ( sizeof( t ) / sizeof( char * ) ) - 1U )]

void CE2MaterialToJSON::createLayeredMaterial( QJsonArray & components, const CE2Material * o )
{
	for ( int i = 0; i < CE2Material::maxLayers; i++ ) {
		if ( ( o->layerMask & ( 1U << i ) ) && o->layers[i] )
			createLink( components, o->layers[i], i );
	}
	for ( int i = 0; i < CE2Material::maxBlenders; i++ ) {
		if ( o->blenders[i] )
			createLink( components, o->blenders[i], i );
	}
	for ( int i = 0; i < CE2Material::maxLODMaterials; i++ ) {
		if ( o->lodMaterials[i] )
			createLink( components, o->lodMaterials[i], i );
	}

	{
		QJsonObject	shaderRoute;
		shaderRoute.insert( "Route", getCE2MatString( CE2Material::shaderRouteNames, o->shaderRoute ) );
		createComponent( components, shaderRoute, "BSMaterial::ShaderRouteComponent" );
	}
	{
		QJsonObject	shaderModel;
		shaderModel.insert( "FileName", getCE2MatString( CE2Material::shaderModelNames, o->shaderModel ) );
		createComponent( components, shaderModel, "BSMaterial::ShaderModelComponent" );
	}
	if ( o->flags & CE2Material::Flag_TwoSided )
		components.append( createBool( true, "Value", "BSMaterial::ParamBool", 0 ) );

	if ( o->physicsMaterialType ) {
		const char *	physMatName = getCE2MatString( CE2Material::physicsMaterialNames, o->physicsMaterialType );
		std::uint32_t	physMatHash = 0;
		for ( ; *physMatName; physMatName++ )
			hashFunctionCRC32( physMatHash, (unsigned char) ( *physMatName | 0x20 ) );
		QJsonObject	physMatData;
		physMatData.insert( "Value", QString::number( physMatHash ) );
		QJsonObject	collisionComponent;
		collisionComponent.insert( "MaterialTypeOverride",
									createStructure( physMatData, "BSMaterial::PhysicsMaterialType" ) );
		createComponent( components, collisionComponent, "BSMaterial::CollisionComponent" );
	}

	if ( o->flags & CE2Material::Flag_HasOpacity ) {
		QJsonObject	alphaSettings;
		alphaSettings.insert( "HasOpacity", "true" );
		alphaSettings.insert( "AlphaTestThreshold", QString::number( o->alphaThreshold ) );
		alphaSettings.insert( "OpacitySourceLayer", QString( "MATERIAL_LAYER_%1" ).arg( o->alphaSourceLayer ) );
		insertBool( alphaSettings, "UseDitheredTransparency", o->flags, CE2Material::Flag_DitheredTransparency );
		QJsonObject	alphaBlenderSettings;
		alphaBlenderSettings.insert( "Mode", getCE2MatString( CE2Material::alphaBlendModeNames, o->alphaBlendMode ) );
		insertBool( alphaBlenderSettings, "UseDetailBlendMask", o->flags, CE2Material::Flag_AlphaDetailBlendMask );
		insertBool( alphaBlenderSettings, "UseVertexColor", o->flags, CE2Material::Flag_AlphaVertexColor );
		if ( o->flags & CE2Material::Flag_AlphaVertexColor ) {
			alphaBlenderSettings.insert( "VertexColorChannel",
										getCE2MatString( CE2Material::colorChannelNames, o->alphaVertexColorChannel ) );
		}
		if ( o->alphaUVStream ) {
			QJsonObject	linkData;
			linkData.insert( "ID", getObject( o->alphaUVStream )->getResourceID() );
			alphaBlenderSettings.insert( "OpacityUVStream", createStructure( linkData, "BSMaterial::UVStreamID" ) );
		}
		if ( o->flags & CE2Material::Flag_IsDecal ) {
			alphaBlenderSettings.insert( "HeightBlendThreshold", QString::number( o->alphaHeightBlendThreshold ) );
			alphaBlenderSettings.insert( "HeightBlendFactor", QString::number( o->alphaHeightBlendFactor ) );
		}
		if ( o->alphaBlendMode == 2 ) {
			alphaBlenderSettings.insert( "Position", QString::number( o->alphaPosition ) );
			alphaBlenderSettings.insert( "Contrast", QString::number( o->alphaContrast ) );
		}
		alphaSettings.insert( "Blender", createStructure( alphaBlenderSettings, "BSMaterial::AlphaBlenderSettings" ) );
		createComponent( components, alphaSettings, "BSMaterial::AlphaSettingsComponent" );
	}

	if ( o->flags & CE2Material::Flag_HasOpacityComponent ) {
		QJsonObject	opacityComponent;
		opacityComponent.insert( "FirstLayerIndex", QString( "MATERIAL_LAYER_%1" ).arg( o->opacityLayer1 ) );
		insertBool( opacityComponent, "SecondLayerActive", o->flags, CE2Material::Flag_OpacityLayer2Active );
		if ( o->flags & CE2Material::Flag_OpacityLayer2Active ) {
			opacityComponent.insert( "SecondLayerIndex", QString( "MATERIAL_LAYER_%1" ).arg( o->opacityLayer2 ) );
			opacityComponent.insert( "FirstBlenderIndex", QString( "BLEND_LAYER_%1" ).arg( o->opacityBlender1 ) );
			opacityComponent.insert( "FirstBlenderMode",
									getCE2MatString( CE2Material::blenderModeNames, o->opacityBlender1Mode ) );
		}
		insertBool( opacityComponent, "ThirdLayerActive", o->flags, CE2Material::Flag_OpacityLayer3Active );
		if ( o->flags & CE2Material::Flag_OpacityLayer3Active ) {
			opacityComponent.insert( "ThirdLayerIndex", QString( "MATERIAL_LAYER_%1" ).arg( o->opacityLayer3 ) );
			opacityComponent.insert( "SecondBlenderIndex", QString( "BLEND_LAYER_%1" ).arg( o->opacityBlender2 ) );
			opacityComponent.insert( "SecondBlenderMode",
									getCE2MatString( CE2Material::blenderModeNames, o->opacityBlender2Mode ) );
		}
		opacityComponent.insert( "SpecularOpacityOverride", QString::number( o->specularOpacityOverride ) );
		createComponent( components, opacityComponent, "BSMaterial::OpacityComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_IsEffect ) && o->effectSettings ) {
		static const std::uint32_t	effectFlags[19] = {
			CE2Material::EffectFlag_UseFalloff, CE2Material::EffectFlag_UseRGBFalloff,
			CE2Material::EffectFlag_VertexColorBlend, CE2Material::EffectFlag_IsAlphaTested,
			CE2Material::EffectFlag_NoHalfResOpt, CE2Material::EffectFlag_SoftEffect,
			CE2Material::EffectFlag_EmissiveOnly, CE2Material::EffectFlag_EmissiveOnlyAuto,
			CE2Material::EffectFlag_DirShadows, CE2Material::EffectFlag_NonDirShadows,
			CE2Material::EffectFlag_IsGlass, CE2Material::EffectFlag_Frosting,
			CE2Material::EffectFlag_ZTest, CE2Material::EffectFlag_ZWrite,
			CE2Material::EffectFlag_BacklightEnable, CE2Material::EffectFlag_RenderBeforeClouds,
			CE2Material::EffectFlag_MVFixup, CE2Material::EffectFlag_MVFixupEdgesOnly,
			CE2Material::EffectFlag_RenderBeforeOIT
		};
		static const char *	effectFlagNames[19] = {
			"UseFallOff", "UseRGBFallOff",
			"VertexColorBlend", "IsAlphaTested",
			"NoHalfResOptimization", "SoftEffect",
			"EmissiveOnlyEffect", "EmissiveOnlyAutomaticallyApplied",
			"ReceiveDirectionalShadows", "ReceiveNonDirectionalShadows",
			"IsGlass", "Frosting",
			"ZTest", "ZWrite",
			"BackLightingEnable", "ForceRenderBeforeClouds",
			"DepthMVFixup", "DepthMVFixupEdgesOnly",
			"ForceRenderBeforeOIT"
		};
		const CE2Material::EffectSettings *	sp = o->effectSettings;
		QJsonObject	effectSettings;
		for ( int i = 0; i < 19; i++ )
			insertBool( effectSettings, effectFlagNames[i], sp->flags, effectFlags[i] );
		if ( sp->flags & ( CE2Material::EffectFlag_UseFalloff | CE2Material::EffectFlag_UseRGBFalloff ) ) {
			effectSettings.insert( "FalloffStartAngle", QString::number( sp->falloffStartAngle ) );
			effectSettings.insert( "FalloffStopAngle", QString::number( sp->falloffStopAngle ) );
			effectSettings.insert( "FalloffStartOpacity", QString::number( sp->falloffStartOpacity ) );
			effectSettings.insert( "FalloffStopOpacity", QString::number( sp->falloffStopOpacity ) );
		}
		if ( sp->flags & CE2Material::EffectFlag_IsAlphaTested )
			effectSettings.insert( "AlphaTestThreshold", QString::number( sp->alphaThreshold ) );
		if ( sp->flags & CE2Material::EffectFlag_SoftEffect )
			effectSettings.insert( "SoftFalloffDepth", QString::number( sp->softFalloffDepth ) );
		if ( sp->flags & CE2Material::EffectFlag_Frosting ) {
			effectSettings.insert( "FrostingUnblurredBackgroundAlphaBlend", QString::number( sp->frostingBgndBlend ) );
			effectSettings.insert( "FrostingBlurBias", QString::number( sp->frostingBlurBias ) );
		}
		effectSettings.insert( "MaterialOverallAlpha", QString::number( sp->materialAlpha ) );
		effectSettings.insert( "BlendingMode", getCE2MatString( CE2Material::effectBlendModeNames, sp->blendMode ) );
		if ( sp->flags & CE2Material::EffectFlag_BacklightEnable ) {
			effectSettings.insert( "BacklightingScale", QString::number( sp->backlightScale ) );
			effectSettings.insert( "BacklightingSharpness", QString::number( sp->backlightSharpness ) );
			effectSettings.insert( "BacklightingTransparencyFactor", QString::number( sp->backlightTransparency ) );
			effectSettings.insert( "BackLightingTintColor", createColor( sp->backlightTintColor ) );
		}
		effectSettings.insert( "DepthBiasInUlp", QString::number( sp->depthBias ) );
		createComponent( components, effectSettings, "BSMaterial::EffectSettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_LayeredEdgeFalloff ) && o->layeredEdgeFalloff ) {
		const CE2Material::LayeredEdgeFalloff *	sp = o->layeredEdgeFalloff;
		QJsonObject	layeredEdgeFalloff;
		for ( int i = 0; i < 4; i++ ) {
			const char *	fieldName = "FalloffStartAngles";
			const float *	fieldSrc = sp->falloffStartAngles;
			if ( i == 1 ) {
				fieldName = "FalloffStopAngles";
				fieldSrc = sp->falloffStopAngles;
			} else if ( i == 2 ) {
				fieldName = "FalloffStartOpacities";
				fieldSrc = sp->falloffStartOpacities;
			} else if ( i == 3 ) {
				fieldName = "FalloffStopOpacities";
				fieldSrc = sp->falloffStopOpacities;
			}
			QJsonArray	fieldData;
			for ( int j = 0; j < 3; j++ )
				fieldData.append( QString::number( fieldSrc[j] ) );
			layeredEdgeFalloff.insert( fieldName, createList( fieldData, "float" ) );
		}
		layeredEdgeFalloff.insert( "ActiveLayersMask", QString::number( sp->activeLayersMask ) );
		insertBool( layeredEdgeFalloff, "UseRGBFallOff", sp->useRGBFalloff );
		createComponent( components, layeredEdgeFalloff, "BSMaterial::LayeredEdgeFalloffComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_IsDecal ) && o->decalSettings && o->decalSettings->isDecal ) {
		const CE2Material::DecalSettings *	sp = o->decalSettings;
		QJsonObject	decalSettings;
		decalSettings.insert( "IsDecal", "true" );
		decalSettings.insert( "MaterialOverallAlpha", QString::number( sp->decalAlpha ) );
		decalSettings.insert( "WriteMask", QString::number( sp->writeMask & 0x00F00737 ) );
		insertBool( decalSettings, "IsPlanet", sp->isPlanet );
		insertBool( decalSettings, "IsProjected", sp->isProjected );
		if ( sp->isProjected ) {
			QJsonObject	projectedDecalData;
			insertBool( projectedDecalData, "UseParallaxOcclusionMapping", sp->useParallaxMapping );
			if ( sp->useParallaxMapping ) {
				QJsonObject	heightTextureData;
				heightTextureData.insert( "FileName", QString::fromUtf8( sp->surfaceHeightMap->data(),
																		sp->surfaceHeightMap->length() ) );
				projectedDecalData.insert( "SurfaceHeightMap",
											createStructure( heightTextureData, "BSMaterial::TextureFile" ) );
				projectedDecalData.insert( "ParallaxOcclusionScale", QString::number( sp->parallaxOcclusionScale ) );
				insertBool( projectedDecalData, "ParallaxOcclusionShadows", sp->parallaxOcclusionShadows );
				projectedDecalData.insert( "MaxParralaxOcclusionSteps", QString::number( sp->maxParallaxSteps ) );
			}
			projectedDecalData.insert( "RenderLayer",
										getCE2MatString( CE2Material::decalRenderLayerNames, sp->renderLayer ) );
			insertBool( projectedDecalData, "UseGBufferNormals", sp->useGBufferNormals );
			decalSettings.insert( "ProjectedDecalSetting",
									createStructure( projectedDecalData, "BSMaterial::ProjectedDecalSettings" ) );
		}
		decalSettings.insert( "BlendMode", getCE2MatString( CE2Material::decalBlendModeNames, sp->blendMode ) );
		insertBool( decalSettings, "AnimatedDecalIgnoresTAA", sp->animatedDecalIgnoresTAA );
		createComponent( components, decalSettings, "BSMaterial::DecalSettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_UseDetailBlender )
		&& o->detailBlenderSettings && o->detailBlenderSettings->isEnabled ) {
		const CE2Material::DetailBlenderSettings *	sp = o->detailBlenderSettings;
		QJsonObject	detailBlenderSettings;
		detailBlenderSettings.insert( "IsDetailBlendMaskSupported", "true" );
		QJsonObject	maskTexture;
		QJsonObject	texturePath;
		texturePath.insert( "FileName", QString::fromUtf8( sp->texturePath->data(), sp->texturePath->length() ) );
		maskTexture.insert( "Texture", createStructure( texturePath, "BSMaterial::MRTextureFile" ) );
		if ( sp->textureReplacementEnabled ) {
			QJsonObject	textureReplacement;
			textureReplacement.insert( "Enabled", "true" );
			textureReplacement.insert( "Color", createColor( FloatVector4( sp->textureReplacement ) / 255.0f ) );
			maskTexture.insert( "Replacement",
								createStructure( textureReplacement, "BSMaterial::TextureReplacement" ) );
		}
		detailBlenderSettings.insert( "DetailBlendMask",
										createStructure( maskTexture, "BSMaterial::SourceTextureWithReplacement" ) );
		if ( sp->uvStream ) {
			QJsonObject	linkData;
			linkData.insert( "ID", getObject( sp->uvStream )->getResourceID() );
			detailBlenderSettings.insert( "DetailBlendMaskUVStream",
											createStructure( linkData, "BSMaterial::UVStreamID" ) );
		}
		QJsonObject	detailBlenderComponent;
		detailBlenderComponent.insert( "DetailBlenderSettings",
										createStructure( detailBlenderSettings, "BSMaterial::DetailBlenderSettings" ) );
		createComponent( components, detailBlenderComponent, "BSMaterial::DetailBlenderSettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_Emissive ) && o->emissiveSettings && o->emissiveSettings->isEnabled ) {
		const CE2Material::EmissiveSettings *	sp = o->emissiveSettings;
		QJsonObject	emittanceData;
		emittanceData.insert( "EmissiveSourceLayer", QString( "MATERIAL_LAYER_%1" ).arg( sp->sourceLayer ) );
		emittanceData.insert( "EmissiveTint", createColor( sp->emissiveTint ) );
		emittanceData.insert( "EmissiveMaskSourceBlender",
								getCE2MatString( CE2Material::maskSourceBlenderNames, sp->maskSourceBlender ) );
		emittanceData.insert( "EmissiveClipThreshold", QString::number( sp->clipThreshold ) );
		insertBool( emittanceData, "AdaptiveEmittance", sp->adaptiveEmittance );
		if ( !sp->adaptiveEmittance ) {
			emittanceData.insert( "LuminousEmittance", QString::number( sp->luminousEmittance ) );
		} else {
			emittanceData.insert( "ExposureOffset", QString::number( sp->exposureOffset ) );
			insertBool( emittanceData, "EnableAdaptiveLimits", sp->enableAdaptiveLimits );
			if ( sp->enableAdaptiveLimits ) {
				emittanceData.insert( "MaxOffsetEmittance", QString::number( sp->maxOffset ) );
				emittanceData.insert( "MinOffsetEmittance", QString::number( sp->minOffset ) );
			}
		}
		QJsonObject	emissiveSettings;
		emissiveSettings.insert( "Enabled", "true" );
		emissiveSettings.insert( "Settings", createStructure( emittanceData, "BSMaterial::EmittanceSettings" ) );
		createComponent( components, emissiveSettings, "BSMaterial::EmissiveSettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_LayeredEmissivity )
		&& o->layeredEmissiveSettings && o->layeredEmissiveSettings->isEnabled ) {
		const CE2Material::LayeredEmissiveSettings *	sp = o->layeredEmissiveSettings;
		QJsonObject	layeredEmissivity;
		layeredEmissivity.insert( "Enabled", "true" );
		layeredEmissivity.insert( "FirstLayerIndex", QString( "MATERIAL_LAYER_%1" ).arg( sp->layer1Index ) );
		layeredEmissivity.insert( "FirstLayerTint", createColor( FloatVector4( sp->layer1Tint ) / 255.0f ) );
		layeredEmissivity.insert( "FirstLayerMaskIndex",
									getCE2MatString( CE2Material::maskSourceBlenderNames, sp->layer1MaskIndex ) );
		insertBool( layeredEmissivity, "SecondLayerActive", sp->layer2Active );
		if ( sp->layer2Active ) {
			layeredEmissivity.insert( "SecondLayerIndex", QString( "MATERIAL_LAYER_%1" ).arg( sp->layer2Index ) );
			layeredEmissivity.insert( "SecondLayerTint", createColor( FloatVector4( sp->layer2Tint ) / 255.0f ) );
			layeredEmissivity.insert( "SecondLayerMaskIndex",
										getCE2MatString( CE2Material::maskSourceBlenderNames, sp->layer2MaskIndex ) );
			layeredEmissivity.insert( "FirstBlenderIndex", QString( "BLEND_LAYER_%1" ).arg( sp->blender1Index ) );
			layeredEmissivity.insert( "FirstBlenderMode",
										getCE2MatString( CE2Material::blenderModeNames, sp->blender1Mode ) );
		}
		insertBool( layeredEmissivity, "ThirdLayerActive", sp->layer3Active );
		if ( sp->layer3Active ) {
			layeredEmissivity.insert( "ThirdLayerIndex", QString( "MATERIAL_LAYER_%1" ).arg( sp->layer3Index ) );
			layeredEmissivity.insert( "ThirdLayerTint", createColor( FloatVector4( sp->layer3Tint ) / 255.0f ) );
			layeredEmissivity.insert( "ThirdLayerMaskIndex",
										getCE2MatString( CE2Material::maskSourceBlenderNames, sp->layer3MaskIndex ) );
			layeredEmissivity.insert( "SecondBlenderIndex", QString( "BLEND_LAYER_%1" ).arg( sp->blender2Index ) );
			layeredEmissivity.insert( "SecondBlenderMode",
										getCE2MatString( CE2Material::blenderModeNames, sp->blender2Mode ) );
		}
		layeredEmissivity.insert( "EmissiveClipThreshold", QString::number( sp->clipThreshold ) );
		insertBool( layeredEmissivity, "AdaptiveEmittance", sp->adaptiveEmittance );
		if ( !sp->adaptiveEmittance ) {
			layeredEmissivity.insert( "LuminousEmittance", QString::number( sp->luminousEmittance ) );
		} else {
			layeredEmissivity.insert( "ExposureOffset", QString::number( sp->exposureOffset ) );
			insertBool( layeredEmissivity, "EnableAdaptiveLimits", sp->enableAdaptiveLimits );
			if ( sp->enableAdaptiveLimits ) {
				layeredEmissivity.insert( "MaxOffsetEmittance", QString::number( sp->maxOffset ) );
				layeredEmissivity.insert( "MinOffsetEmittance", QString::number( sp->minOffset ) );
			}
		}
		insertBool( layeredEmissivity, "IgnoresFog", sp->ignoresFog );
		createComponent( components, layeredEmissivity, "BSMaterial::LayeredEmissivityComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_Translucency )
		&& o->translucencySettings && o->translucencySettings->isEnabled ) {
		const CE2Material::TranslucencySettings *	sp = o->translucencySettings;
		QJsonObject	translucencyData;
		insertBool( translucencyData, "Thin", sp->isThin );
		insertBool( translucencyData, "FlipBackFaceNormalsInViewSpace", sp->flipBackFaceNormalsInVS );
		insertBool( translucencyData, "UseSSS", sp->useSSS );
		if ( sp->useSSS ) {
			translucencyData.insert( "SSSWidth", QString::number( sp->sssWidth ) );
			translucencyData.insert( "SSSStrength", QString::number( sp->sssStrength ) );
		}
		translucencyData.insert( "TransmissiveScale", QString::number( sp->transmissiveScale ) );
		translucencyData.insert( "TransmittanceWidth", QString::number( sp->transmittanceWidth ) );
		translucencyData.insert( "SpecLobe0RoughnessScale", QString::number( sp->specLobe0RoughnessScale ) );
		translucencyData.insert( "SpecLobe1RoughnessScale", QString::number( sp->specLobe1RoughnessScale ) );
		translucencyData.insert( "TransmittanceSourceLayer", QString( "MATERIAL_LAYER_%1" ).arg( sp->sourceLayer ) );
		QJsonObject	translucencySettings;
		translucencySettings.insert( "Enabled", "true" );
		translucencySettings.insert( "Settings",
									createStructure( translucencyData, "BSMaterial::TranslucencySettings" ) );
		createComponent( components, translucencySettings, "BSMaterial::TranslucencySettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_IsVegetation ) && o->vegetationSettings && o->vegetationSettings->isEnabled ) {
		const CE2Material::VegetationSettings *	sp = o->vegetationSettings;
		QJsonObject	vegetationSettings;
		vegetationSettings.insert( "Enabled", "true" );
		vegetationSettings.insert( "LeafFrequency", QString::number( sp->leafFrequency ) );
		vegetationSettings.insert( "LeafAmplitude", QString::number( sp->leafAmplitude ) );
		vegetationSettings.insert( "BranchFlexibility", QString::number( sp->branchFlexibility ) );
		vegetationSettings.insert( "TrunkFlexibility", QString::number( sp->trunkFlexibility ) );
#if 0
		vegetationSettings.insert( "DEPRECATEDTerrainBlendStrength", QString::number( sp->terrainBlendStrength ) );
		vegetationSettings.insert( "DEPRECATEDTerrainBlendGradientFactor",
									QString::number( sp->terrainBlendGradientFactor ) );
#endif
		createComponent( components, vegetationSettings, "BSMaterial::VegetationSettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_IsWater ) && o->waterSettings ) {
		const CE2Material::WaterSettings *	sp = o->waterSettings;
		QJsonObject	waterSettings;
		waterSettings.insert( "WaterEdgeFalloff", QString::number( sp->waterEdgeFalloff ) );
		waterSettings.insert( "WaterWetnessMaxDepth", QString::number( sp->waterWetnessMaxDepth ) );
		waterSettings.insert( "WaterEdgeNormalFalloff", QString::number( sp->waterEdgeNormalFalloff ) );
		waterSettings.insert( "WaterDepthBlur", QString::number( sp->waterDepthBlur ) );
		waterSettings.insert( "WaterRefractionMagnitude", QString::number( sp->reflectance[3] ) );
		waterSettings.insert( "PhytoplanktonReflectanceColorR", QString::number( sp->phytoplanktonReflectance[0] ) );
		waterSettings.insert( "PhytoplanktonReflectanceColorG", QString::number( sp->phytoplanktonReflectance[1] ) );
		waterSettings.insert( "PhytoplanktonReflectanceColorB", QString::number( sp->phytoplanktonReflectance[2] ) );
		waterSettings.insert( "SedimentReflectanceColorR", QString::number( sp->sedimentReflectance[0] ) );
		waterSettings.insert( "SedimentReflectanceColorG", QString::number( sp->sedimentReflectance[1] ) );
		waterSettings.insert( "SedimentReflectanceColorB", QString::number( sp->sedimentReflectance[2] ) );
		waterSettings.insert( "YellowMatterReflectanceColorR", QString::number( sp->yellowMatterReflectance[0] ) );
		waterSettings.insert( "YellowMatterReflectanceColorG", QString::number( sp->yellowMatterReflectance[1] ) );
		waterSettings.insert( "YellowMatterReflectanceColorB", QString::number( sp->yellowMatterReflectance[2] ) );
		waterSettings.insert( "MaxConcentrationPlankton", QString::number( sp->phytoplanktonReflectance[3] ) );
		waterSettings.insert( "MaxConcentrationSediment", QString::number( sp->sedimentReflectance[3] ) );
		waterSettings.insert( "MaxConcentrationYellowMatter", QString::number( sp->yellowMatterReflectance[3] ) );
		waterSettings.insert( "ReflectanceR", QString::number( sp->reflectance[0] ) );
		waterSettings.insert( "ReflectanceG", QString::number( sp->reflectance[1] ) );
		waterSettings.insert( "ReflectanceB", QString::number( sp->reflectance[2] ) );
		insertBool( waterSettings, "LowLOD", sp->lowLOD );
		insertBool( waterSettings, "PlacedWater", sp->placedWater );
		createComponent( components, waterSettings, "BSMaterial::WaterSettingsComponent" );
	}

	if ( ( o->flags & CE2Material::Flag_IsHair ) && o->hairSettings && o->hairSettings->isEnabled ) {
		const CE2Material::HairSettings *	sp = o->hairSettings;
		QJsonObject	hairSettings;
		hairSettings.insert( "Enabled", "true" );
		insertBool( hairSettings, "IsSpikyHair", sp->isSpikyHair );
		hairSettings.insert( "SpecScale", QString::number( sp->specScale ) );
		hairSettings.insert( "SpecularTransmissionScale", QString::number( sp->specularTransmissionScale ) );
		hairSettings.insert( "DirectTransmissionScale", QString::number( sp->directTransmissionScale ) );
		hairSettings.insert( "DiffuseTransmissionScale", QString::number( sp->diffuseTransmissionScale ) );
		hairSettings.insert( "Roughness", QString::number( sp->roughness ) );
		hairSettings.insert( "ContactShadowSoftening", QString::number( sp->contactShadowSoftening ) );
		hairSettings.insert( "BackscatterStrength", QString::number( sp->backscatterStrength ) );
		hairSettings.insert( "BackscatterWrap", QString::number( sp->backscatterWrap ) );
		hairSettings.insert( "VariationStrength", QString::number( sp->variationStrength ) );
		hairSettings.insert( "IndirectSpecularScale", QString::number( sp->indirectSpecularScale ) );
		hairSettings.insert( "IndirectSpecularTransmissionScale",
							QString::number( sp->indirectSpecularTransmissionScale ) );
		hairSettings.insert( "IndirectSpecRoughness", QString::number( sp->indirectSpecRoughness ) );
		hairSettings.insert( "EdgeMaskContrast", QString::number( sp->edgeMaskContrast ) );
		hairSettings.insert( "EdgeMaskMin", QString::number( sp->edgeMaskMin ) );
		hairSettings.insert( "EdgeMaskDistanceMin", QString::number( sp->edgeMaskDistanceMin ) );
		hairSettings.insert( "EdgeMaskDistanceMax", QString::number( sp->edgeMaskDistanceMax ) );
		hairSettings.insert( "MaxDepthOffset", QString::number( sp->maxDepthOffset ) );
		hairSettings.insert( "DitherScale", QString::number( sp->ditherScale ) );
		hairSettings.insert( "DitherDistanceMin", QString::number( sp->ditherDistanceMin ) );
		hairSettings.insert( "DitherDistanceMax", QString::number( sp->ditherDistanceMax ) );
		hairSettings.insert( "Tangent", createXMFLOAT( sp->tangent, 3, nullptr ) );
		hairSettings.insert( "TangentBend", QString::number( sp->tangent[3] ) );
		hairSettings.insert( "DepthOffsetMaskVertexColorChannel",
							getCE2MatString( CE2Material::colorChannelNames, sp->depthOffsetMaskVertexColorChannel ) );
		hairSettings.insert( "AOVertexColorChannel",
							getCE2MatString( CE2Material::colorChannelNames, sp->aoVertexColorChannel ) );
		createComponent( components, hairSettings, "BSMaterial::HairSettingsComponent" );
	}

	// TODO: implement all supported material components
	if ( ( o->flags & CE2Material::Flag_GlobalLayerData ) && o->globalLayerData ) {
		QMessageBox::warning( nullptr, "NifSkope warning",
								QString( "Saving global layer data is not implemented yet" ) );
	}
}

void CE2MaterialToJSON::createBlender( QJsonArray & components, const CE2Material::Blender * o )
{
	if ( o->uvStream )
		createLink( components, o->uvStream );
	if ( o->texturePath && !o->texturePath->empty() ) {
		QJsonObject	textureFile;
		textureFile.insert( "FileName", QString::fromUtf8( o->texturePath->data(), o->texturePath->length() ) );
		createComponent( components, textureFile, "BSMaterial::MRTextureFile" );
	}
	if ( o->textureReplacementEnabled ) {
		QJsonObject	textureReplacement;
		textureReplacement.insert( "Enabled", "true" );
		textureReplacement.insert( "Color", createColor( FloatVector4( o->textureReplacement ) / 255.0f ) );
		createComponent( components, textureReplacement, "BSMaterial::TextureReplacement" );
	}
	QJsonObject	blendMode;
	blendMode.insert( "Value", getCE2MatString( CE2Material::alphaBlendModeNames, o->blendMode ) );
	createComponent( components, blendMode, "BSMaterial::BlendModeComponent" );
	if ( o->boolParams[5] ) {
		QJsonObject	colorChannel;
		colorChannel.insert( "Value", getCE2MatString( CE2Material::colorChannelNames, o->colorChannel ) );
		createComponent( components, colorChannel, "BSMaterial::ColorChannelTypeComponent" );
	}
	for ( int i = 0; i < CE2Material::Blender::maxFloatParams; i++ ) {
		if ( ( i <= 1 && o->blendMode != 0 ) || ( ( i == 2 || i == 3 ) && o->blendMode != 2 ) )
			continue;
		components.append( createFloat( o->floatParams[i], "Value", "BSMaterial::MaterialParamFloat", i ) );
	}
	for ( int i = 0; i < CE2Material::Blender::maxBoolParams; i++ )
		components.append( createBool( o->boolParams[i], "Value", "BSMaterial::ParamBool", i ) );
}

void CE2MaterialToJSON::createLayer( QJsonArray & components, const CE2Material::Layer * o )
{
	if ( o->material )
		createLink( components, o->material );
	if ( o->uvStream )
		createLink( components, o->uvStream );
}

void CE2MaterialToJSON::createMaterial( QJsonArray & components, const CE2Material::Material * o )
{
	if ( o->textureSet )
		createLink( components, o->textureSet );
	components.append( createColor( o->color, 0 ) );
	{
		QJsonObject	colorModeData;
		colorModeData.insert( "Value", CE2Material::colorModeNames[o->colorModeFlags & 1] );
		createComponent( components, colorModeData, "BSMaterial::MaterialOverrideColorTypeComponent" );
	}
	components.append( createBool( bool( o->colorModeFlags & 2 ), "Value", "BSMaterial::ParamBool", 0 ) );
	if ( o->flipbookFlags & 1 ) {
		QJsonObject	flipbookData;
		flipbookData.insert( "IsAFlipbook", "true" );
		flipbookData.insert( "Columns", QString::number( o->flipbookColumns ) );
		flipbookData.insert( "Rows", QString::number( o->flipbookRows ) );
		flipbookData.insert( "FPS", QString::number( o->flipbookFPS ) );
		insertBool( flipbookData, "Loops", o->flipbookFlags, 2 );
		createComponent( components, flipbookData, "BSMaterial::FlipbookComponent" );
	}
}

void CE2MaterialToJSON::createTextureSet( QJsonArray & components, const CE2Material::TextureSet * o )
{
	for ( int i = 0; i < CE2Material::TextureSet::maxTexturePaths; i++ ) {
		if ( ( o->texturePathMask & ( 1U << i ) ) && o->texturePaths[i] && !o->texturePaths[i]->empty() ) {
			QJsonObject	textureFile;
			textureFile.insert( "FileName",
								QString::fromUtf8( o->texturePaths[i]->data(), o->texturePaths[i]->length() ) );
			createComponent( components, textureFile, "BSMaterial::MRTextureFile", i );
		}
		if ( o->textureReplacementMask & ( 1U << i ) ) {
			QJsonObject	textureReplacement;
			textureReplacement.insert( "Enabled", "true" );
			textureReplacement.insert( "Color", createColor( FloatVector4( o->textureReplacements[i] ) / 255.0f ) );
			createComponent( components, textureReplacement, "BSMaterial::TextureReplacement", i );
		}
	}
	components.append( createFloat( o->floatParam, "Value", "BSMaterial::MaterialParamFloat", 0 ) );
	{
		QJsonObject	resolutionHint;
		resolutionHint.insert( "ResolutionHint",
								getCE2MatString( CE2Material::resolutionSettingNames, o->resolutionHint ) );
		createComponent( components, resolutionHint, "BSMaterial::TextureResolutionSetting" );
	}
	components.append( createBool( o->disableMipBiasHint, "DisableMipBiasHint", "BSMaterial::MipBiasSetting", 0 ) );
}

void CE2MaterialToJSON::createUVStream( QJsonArray & components, const CE2Material::UVStream * o )
{
	components.append( createXMFLOAT( o->scaleAndOffset, 2, "Value", "BSMaterial::Scale", 0 ) );
	components.append( createXMFLOAT( FloatVector4( o->scaleAndOffset ).shuffleValues( 0xEE ), 2,
										"Value", "BSMaterial::Offset", 0 ) );
	QJsonObject	addressMode;
	addressMode.insert( "Value", getCE2MatString( CE2Material::textureAddressModeNames, o->textureAddressMode ) );
	createComponent( components, addressMode, "BSMaterial::TextureAddressModeComponent" );
	QJsonObject	channel;
	channel.insert( "Value", getCE2MatString( CE2Material::channelNames, o->channel ) );
	createComponent( components, channel, "BSMaterial::Channel" );
}

CE2MaterialToJSON::CE2MaterialToJSON( NifModel * nif, const QString & matFilePath, const CE2MaterialObject * o )
  : materials( nif->getCE2Materials() ),
	matName( QString::fromStdString( spStarfieldMaterialSaveAs::getBaseName( matFilePath.toStdString() ) ) )
{
	for ( int i = 0; i < 8; i++ )
		objTypeIndex[i] = 0;
	(void) getObject( o );
}

CE2MaterialToJSON::~CE2MaterialToJSON()
{
}

QByteArray CE2MaterialToJSON::getJSONData() const
{
	QJsonObject	matFileObject;
	matFileObject.insert( "Version", 1 );

	QJsonArray	jsonObjects;
	for ( auto m : matObjects )
		jsonObjects.append( m->jsonObject );
	matFileObject.insert( "Objects", jsonObjects );

	QJsonDocument	matFileData( matFileObject );
	return matFileData.toJson( QJsonDocument::Indented );
}

//! Save edited Starfield material as JSON format .mat file
class spStarfieldEditedMaterialSaveAs final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Save Edited Material..." ); }
	QString page() const override final { return Spell::tr( "Material" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return spStarfieldMaterialExport::canExportMaterial( nif, index, true );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

QModelIndex spStarfieldEditedMaterialSaveAs::cast( NifModel * nif, const QModelIndex & index )
{
	QModelIndex	idx = nif->getBlockIndex( index );
	if ( nif->blockInherits( idx, "BSGeometry" ) )
		idx = nif->getBlockIndex( nif->getLink( idx, "Shader Property" ) );
	QString	matFilePath;
	if ( nif->blockInherits( idx, "BSShaderProperty" ) ) {
		matFilePath = nif->get<QString>( idx, "Name" );
		idx = nif->getIndex( idx, "Material" );
	} else {
		return index;
	}
	if ( !idx.isValid() || matFilePath.isEmpty() )
		return index;

	AllocBuffers	matBuf;
	const CE2Material *	mat = reinterpret_cast< const CE2Material * >( nif->updateSFMaterial( matBuf, idx ) );
	if ( !mat )
		return index;

	matFilePath = spStarfieldMaterialSaveAs::getOutputFileName(
						nif, Game::GameManager::get_full_path( matFilePath, "materials/", ".mat" ) );
	if ( matFilePath.isEmpty() )
		return index;

	CE2MaterialToJSON	matConv( nif, matFilePath, mat );

	QFile	matFile( matFilePath );
	if ( matFile.open( QIODevice::WriteOnly ) )
		matFile.write( matConv.getJSONData() );
	else
		QMessageBox::critical( nullptr, "NifSkope error", QString( "Could not open output file" ) );

	return index;
}

REGISTER_SPELL( spStarfieldEditedMaterialSaveAs )

