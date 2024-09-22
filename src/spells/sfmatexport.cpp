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
	static void createComponent( QJsonArray & components, const QJsonObject & data, const char * type, int n = 0 );
	static QJsonObject createColor( FloatVector4 c );
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

void CE2MaterialToJSON::createComponent( QJsonArray & components, const QJsonObject & data, const char * type, int n )
{
	QJsonObject	componentObject;
	componentObject.insert( "Data", data );
	componentObject.insert( "Index", n );
	componentObject.insert( "Type", type );
	components.append( componentObject );
}

QJsonObject CE2MaterialToJSON::createColor( FloatVector4 c )
{
	QJsonObject	xmfloatData;
	xmfloatData.insert( "x", QString::number( c[0] ) );
	xmfloatData.insert( "y", QString::number( c[1] ) );
	xmfloatData.insert( "z", QString::number( c[2] ) );
	xmfloatData.insert( "w", QString::number( c[3] ) );
	QJsonObject	xmfloatValue;
	xmfloatValue.insert( "Data", xmfloatData );
	xmfloatValue.insert( "Type", "XMFLOAT4" );

	QJsonObject	colorData;
	colorData.insert( "Value", xmfloatValue );
	QJsonObject	colorObject;
	colorObject.insert( "Data", colorData );
	colorObject.insert( "Type", "BSMaterial::Color" );

	return colorObject;
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
		if ( !( ( o->layerMask & ( 1U << i ) ) && o->layers[i] ) )
			continue;

		createLink( components, o->layers[i], i );

		if ( i > 0 && i <= CE2Material::maxBlenders && o->blenders[i - 1] )
			createLink( components, o->blenders[i - 1], i - 1 );
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
	if ( o->flags & CE2Material::Flag_TwoSided ) {
		QJsonObject	paramBool;
		paramBool.insert( "Value", "true" );
		createComponent( components, paramBool, "BSMaterial::ParamBool" );
	}
	// TODO
}

void CE2MaterialToJSON::createBlender( QJsonArray & components, const CE2Material::Blender * o )
{
	if ( o->uvStream )
		createLink( components, o->uvStream );
	// TODO
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
	// TODO
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
	// TODO
}

void CE2MaterialToJSON::createUVStream( QJsonArray & components, const CE2Material::UVStream * o )
{
	// TODO
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

