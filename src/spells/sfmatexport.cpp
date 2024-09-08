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

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif && nif->getBSVersion() >= 170 && index.isValid() )
			return nif->blockInherits( index, "BSGeometry" ) || nif->blockInherits( index, "BSLightingShaderProperty" );
		return false;
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
	if ( nif->blockInherits( idx, "BSLightingShaderProperty" ) )
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
		if ( nif && nif->getBSVersion() >= 170 && index.isValid() )
			return nif->blockInherits( index, "BSGeometry" ) || nif->blockInherits( index, "BSLightingShaderProperty" );
		return false;
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
		if ( nif && nif->getBSVersion() >= 170 && index.isValid() )
			return nif->blockInherits( index, "BSGeometry" ) || nif->blockInherits( index, "BSLightingShaderProperty" );
		return false;
	}

	static std::string getBaseName( const std::string_view & fullPath );
	static void renameMaterial(
		std::string & matFileData, const std::string_view & matPath, const std::string_view & newPath );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

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
	if ( nif->blockInherits( idx, "BSLightingShaderProperty" ) )
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

			QString	dirName = nif->getFolder();
			if ( dirName.isEmpty() || dirName.contains( ".ba2/", Qt::CaseInsensitive ) || dirName.contains( ".bsa/", Qt::CaseInsensitive ) ) {
				QSettings	settings;
				dirName = settings.value( "Spells//Extract File/Last File Path", QString() ).toString();
			}
			if ( !dirName.isEmpty() && !dirName.endsWith( '/' ) )
				dirName.append( QChar('/') );
			dirName.append( QString::fromStdString( getBaseName( matFilePath ) ) );
			QString	fileName = QFileDialog::getSaveFileName( qApp->activeWindow(), tr("Choose a .mat file for export"), dirName, "Material (*.mat)" );
			if ( fileName.isEmpty() )
				return index;
			if ( !fileName.endsWith( ".mat", Qt::CaseInsensitive ) )
				fileName.append( ".mat" );

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

