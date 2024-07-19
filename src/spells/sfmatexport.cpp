#include "spellbook.h"
#include "libfo76utils/src/material.hpp"
#include "model/nifmodel.h"

#include <random>
#include <chrono>

#include <QClipboard>

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
		newID.file = ( newID.file & 0xFFFC0000U ) | ( std::uint32_t(tmp1) & 0x0003FFFFU );
		newID.ext = 0xA0000000U | std::uint32_t( tmp1 >> 37 );
		newID.dir = std::uint32_t( tmp2 );
		if ( matDB && matDB->getMaterial( newID ) ) [[unlikely]]
			continue;
		if ( idsUsed && !idsUsed->insert( newID ).second ) [[unlikely]]
			continue;
		break;
	}
	return newID;
}

void spStarfieldMaterialExport::generateResourceIDs( std::string & matFileData, const BSMaterialsCDB * matDB )
{
	std::set< BSMaterialsCDB::BSResourceID >	idsUsed;
	std::map< BSMaterialsCDB::BSResourceID, BSMaterialsCDB::BSResourceID >	idsDefined;
	for ( size_t i = 0; ( i + 32 ) <= matFileData.length(); i++ ) {
		if ( !( matFileData[i] == '"' && matFileData[i + 31] == '"' ) )
			continue;
		std::string_view	s( matFileData.c_str() + ( i + 1 ), 30 );
		if ( !( s.starts_with( "res:" ) && s[12] == ':' && s[21] == ':' ) )
			continue;
		BSMaterialsCDB::BSResourceID	tmp( 0, 0, 0 );
		tmp.fromJSONString( s );
		if ( tmp ) {
			idsUsed.insert( tmp );
			if ( i >= 13 && std::string_view( matFileData.c_str() + ( i - 13 ), 13 ) == "\n      \"ID\": " )
				idsDefined.emplace( tmp, tmp );
		}
	}
	for ( auto & i : idsDefined )
		i.second = generateResourceID( &(i.first), &idsUsed, matDB );
	bool	warningFlag = false;
	for ( size_t i = 0; ( i + 32 ) <= matFileData.length(); i++ ) {
		if ( !( matFileData[i] == '"' && matFileData[i + 31] == '"' ) )
			continue;
		std::string_view	s( matFileData.c_str() + ( i + 1 ), 30 );
		if ( !( s.starts_with( "res:" ) && s[12] == ':' && s[21] == ':' ) )
			continue;
		BSMaterialsCDB::BSResourceID	tmp( 0, 0, 0 );
		tmp.fromJSONString( s );
		if ( !tmp )
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
	QModelIndex	idx = index;
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

