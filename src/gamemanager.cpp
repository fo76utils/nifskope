#include "gamemanager.h"

#include "ba2file.hpp"
#include "bsrefl.hpp"
#include "material.hpp"
#include "message.h"
#include "model/nifmodel.h"

#include <QSettings>
#include <QCoreApplication>
#include <QProgressDialog>
#include <QDir>
#include <QMessageBox>
#include <QStringBuilder>

namespace Game
{

using GameMap = QMap<GameMode, QString>;
using ResourceListMap = QMap<GameMode, QStringList>;

using namespace std::string_literals;

static const auto beth = QString("HKEY_LOCAL_MACHINE\\SOFTWARE\\Bethesda Softworks\\%1");
static const auto msft = QString("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1");


static const GameMap STRING = {
	{MORROWIND, "Morrowind"},
	{OBLIVION, "Oblivion"},
	{FALLOUT_3NV, "Fallout 3 / New Vegas"},
	{SKYRIM, "Skyrim"},
	{SKYRIM_SE, "Skyrim SE"},
	{FALLOUT_4, "Fallout 4"},
	{FALLOUT_76, "Fallout 76"},
	{STARFIELD, "Starfield"},
	{OTHER, "Other Games"}
};

static const GameMap KEY = {
	{MORROWIND, beth.arg("Morrowind")},
	{OBLIVION, beth.arg("Oblivion")},
	{FALLOUT_3NV, beth.arg("FalloutNV")},
	{SKYRIM, beth.arg("Skyrim")},
	{SKYRIM_SE, beth.arg("Skyrim Special Edition")},
	{FALLOUT_4, beth.arg("Fallout4")},
	{FALLOUT_76, msft.arg("Fallout 76")},
	{OTHER, ""}
};

static const GameMap DATA = {
	{MORROWIND, "Data Files"},
	{OBLIVION, "Data"},
	{FALLOUT_3NV, "Data"},
	{SKYRIM, "Data"},
	{SKYRIM_SE, "Data"},
	{FALLOUT_4, "Data"},
	{FALLOUT_76, "Data"},
	{STARFIELD, "Data"},
	{OTHER, ""}
};

static const ResourceListMap FOLDERS = {
	{MORROWIND, {"."}},
	{OBLIVION, {"."}},
	{FALLOUT_3NV, {"."}},
	{SKYRIM, {"."}},
	{SKYRIM_SE, {"."}},
	{FALLOUT_4, {".", "Textures"}},
	{FALLOUT_76, {".", "Textures"}},
	{STARFIELD, {".", "Textures"}},
	{OTHER, {}}
};

std::uint64_t	GameManager::material_db_prv_id = 0;
GameManager::GameResources	GameManager::archives[NUM_GAMES];
std::unordered_map< const NifModel *, GameManager::GameResources * >	GameManager::nifResourceMap;
QString	GameManager::gamePaths[NUM_GAMES];
bool	GameManager::gameStatus[NUM_GAMES] = { true, true, true, true, true, true, true, true, true };
bool	GameManager::otherGamesFallback = false;

static bool archiveFilterFunction_1( [[maybe_unused]] void * p, const std::string_view & s )
{
	return !( s.ends_with( ".mp3" ) || s.ends_with( ".ogg" ) || s.ends_with( ".wav" ) );
}

static bool archiveFilterFunction_2( [[maybe_unused]] void * p, const std::string_view & s )
{
	return !( s.ends_with( ".nif" ) || s.ends_with( ".fuz" ) || s.ends_with( ".lip" ) );
}

static bool archiveFilterFunction_3( [[maybe_unused]] void * p, const std::string_view & s )
{
	return !( s.ends_with( ".nif" ) || s.ends_with( ".wem" ) || s.ends_with( ".ffxanim" ) );
}

typedef bool (*ArchiveFilterFuncType)( void *, const std::string_view & );
static const ArchiveFilterFuncType archiveFilterFuncTable[NUM_GAMES] =
{
	&archiveFilterFunction_1, &archiveFilterFunction_1,	// other, Morrowind
	&archiveFilterFunction_1, &archiveFilterFunction_1,	// Oblivion, Fallout_3NV
	&archiveFilterFunction_2, &archiveFilterFunction_2,	// Skyrim, Skyrim_SE
	&archiveFilterFunction_2, &archiveFilterFunction_2,	// Fallout_4, Fallout_76
	&archiveFilterFunction_3	// Starfield
};

static const auto GAME_PATHS = QString("Game Paths");
static const auto GAME_FOLDERS = QString("Game Folders");
static const auto GAME_STATUS = QString("Game Status");
static const auto GAME_MGR_VER = QString("Game Manager Version");

QString registry_game_path( const QString& key )
{
#ifdef Q_OS_WIN32
	QString data_path;
	QSettings cfg(key, QSettings::Registry32Format);
	data_path = cfg.value("Installed Path").toString(); // Steam
	if ( data_path.isEmpty() )
		data_path = cfg.value("Path").toString(); // Microsoft Uninstall
	// Remove encasing quotes
	data_path.remove('"');
	if ( data_path.isEmpty() )
		return {};

	QDir data_path_dir(data_path);
	if ( data_path_dir.exists() )
		return QDir::cleanPath(data_path);

#else
	(void) key;
#endif
	return {};
}

QStringList existing_folders(GameMode game, QString path)
{
	// TODO: Restore the minimal previous support for detecting Civ IV, etc.
	if ( game == OTHER )
		return {};

	QStringList folders;
	for ( const auto& f : FOLDERS.value(game, {}) ) {
		QDir dir(QString("%1/%2").arg(path).arg(DATA.value(game, "")));
		if ( dir.exists(f) )
			folders.append(QFileInfo(dir, f).absoluteFilePath());
	}

	return folders;
}

QString StringForMode( GameMode game )
{
	if ( game >= NUM_GAMES )
		return {};

	return STRING.value(game, "");
}

GameMode ModeForString( QString game )
{
	return STRING.key(game, OTHER);
}

QProgressDialog* prog_dialog( QString title )
{
	QProgressDialog* dlg = new QProgressDialog(title, {}, 0, NUM_GAMES);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
	return dlg;
}

void process( QProgressDialog* dlg, int i )
{
	if ( dlg ) {
		dlg->setValue(i);
		QCoreApplication::processEvents();
	}
}

GameManager::GameResources::~GameResources()
{
	if ( sfMaterials && !( parent && sfMaterials == parent->sfMaterials ) )
		delete sfMaterials;
	if ( ba2File )
		delete ba2File;
}

void GameManager::GameResources::init_archives()
{
	if ( sfMaterialDB_ID )
		close_materials();
	if ( ba2File ) {
		delete ba2File;
		ba2File = nullptr;
	}

	if ( parent && !parent->ba2File )
		parent->init_archives();

	QStringList	tmp;
	if ( gameStatus[game] ) {
		tmp = dataPaths;
		if ( !parent && otherGamesFallback && game != OTHER && gameStatus[OTHER] )
			tmp.append( archives[OTHER].dataPaths );
	}
	if ( tmp.isEmpty() )
		return;
	ba2File = new BA2File();
	for ( const auto & i : tmp ) {
		try {
			ba2File->loadArchivePath( i.toStdString().c_str(), archiveFilterFuncTable[game] );
		} catch ( FO76UtilsError & e ) {
			QMessageBox::critical( nullptr, "NifSkope error", QString("Error opening resource path '%1': %2").arg(i).arg(e.what()) );
		}
	}
}

static bool archiveScanFunctionMat( [[maybe_unused]] void * p, const BA2File::FileInfo & fd )
{
	if ( fd.fileName.ends_with( ".mat" ) || fd.fileName.ends_with( ".cdb" ) )
		return fd.fileName.starts_with( "materials/" );
	return false;
}

CE2MaterialDB * GameManager::GameResources::init_materials()
{
	if ( game != STARFIELD )
		return nullptr;

	close_materials();

	if ( parent && !parent->sfMaterialDB_ID )
		parent->init_materials();

	if ( !ba2File )
		init_archives();
	bool	haveMaterials = ( ba2File && ba2File->scanFileList( &archiveScanFunctionMat ) );

	if ( !haveMaterials || ( parent && !parent->sfMaterialDB_ID ) ) {
		if ( parent && parent->sfMaterialDB_ID ) {
			sfMaterials = parent->sfMaterials;
			sfMaterialDB_ID = parent->sfMaterialDB_ID;
			return sfMaterials;
		}
		return nullptr;
	}
	sfMaterials = new CE2MaterialDB();
	sfMaterialDB_ID = ++GameManager::material_db_prv_id;
	if ( parent )
		sfMaterials->copyFrom( *(parent->sfMaterials) );
	try {
		sfMaterials->loadArchives( *ba2File );
	} catch ( FO76UtilsError & e ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error loading Starfield material database: %1").arg(e.what()) );
	}

	return sfMaterials;
}

void GameManager::GameResources::close_archives()
{
	if ( sfMaterialDB_ID )
		close_materials();
	if ( ba2File ) {
		delete ba2File;
		ba2File = nullptr;
	}
}

void GameManager::GameResources::close_materials()
{
	if ( sfMaterialDB_ID && !parent ) {
		for ( auto i = GameManager::nifResourceMap.begin(); i != GameManager::nifResourceMap.end(); i++ ) {
			if ( i->second->parent == this )
				i->second->close_materials();
		}
	}
	if ( sfMaterials && !( parent && sfMaterials == parent->sfMaterials ) )
		delete sfMaterials;
	sfMaterials = nullptr;
	sfMaterialDB_ID = 0;
}

QString GameManager::GameResources::find_file( const std::string_view & fullPath )
{
	if ( !ba2File && !dataPaths.isEmpty() )
		init_archives();
	if ( ba2File && ba2File->findFile( fullPath ) )
		return QString::fromUtf8( fullPath.data(), qsizetype(fullPath.length()) );
	if ( parent )
		return parent->find_file( fullPath );
	return QString();
}

static unsigned char * byteArrayAllocFunc( void * bufPtr, size_t nBytes )
{
	QByteArray *	p = reinterpret_cast< QByteArray * >( bufPtr );
	p->resize( qsizetype(nBytes) );
	return reinterpret_cast< unsigned char * >( p->data() );
}

bool GameManager::GameResources::get_file( QByteArray & data, const std::string_view & fullPath )
{
	if ( !ba2File && !dataPaths.isEmpty() )
		init_archives();
	const BA2File::FileInfo *	fd = nullptr;
	if ( ba2File )
		fd = ba2File->findFile( fullPath );
	if ( !fd ) {
		if ( parent )
			return parent->get_file( data, fullPath );
		qWarning() << "File '" << QLatin1String( fullPath.data(), qsizetype(fullPath.length()) ) << "' not found in archives";
		data.resize( 0 );
		return false;
	}
	try {
		ba2File->extractFile( &data, &byteArrayAllocFunc, *fd );
	} catch ( FO76UtilsError & e ) {
		if ( std::string_view(e.what()).starts_with( "BA2File: unexpected change to size of loose file" ) ) {
			close_archives();
			return get_file( data, fullPath );
		}
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error loading resource file '%1': %2").arg( QLatin1String( fullPath.data(), qsizetype(fullPath.length()) ) ).arg( e.what() ) );
		data.resize( 0 );
		return false;
	}
	return true;
}

struct list_files_scan_function_data {
	std::set< std::string_view > * fileSet;
	bool (*filterFunc)( void * p, const std::string_view & fileName );
	void * filterFuncData;
};

static bool list_files_scan_function( void * p, const BA2File::FileInfo & fd )
{
	list_files_scan_function_data & o = *( reinterpret_cast< list_files_scan_function_data * >( p ) );
	if ( !o.filterFunc || o.filterFunc( o.filterFuncData, fd.fileName ) )
		o.fileSet->insert( fd.fileName );
	return false;
}

void GameManager::GameResources::list_files(
	std::set< std::string_view > & fileSet,
	bool (*fileListFilterFunc)( void * p, const std::string_view & fileName ), void * fileListFilterFuncData )
{
	if ( parent )
		parent->list_files( fileSet, fileListFilterFunc, fileListFilterFuncData );
	// make sure that archives are loaded
	if ( !ba2File )
		init_archives();
	if ( !( ba2File && ba2File->size() > 0 ) )
		return;
	list_files_scan_function_data	tmp;
	tmp.fileSet = &fileSet;
	tmp.filterFunc = fileListFilterFunc;
	tmp.filterFuncData = fileListFilterFuncData;
	ba2File->scanFileList( &list_files_scan_function, &tmp );
}

GameManager::GameManager()
{
	for ( size_t game = size_t(OTHER); game < size_t(NUM_GAMES); game++ )
		archives[game].game = GameMode(game);

	QSettings settings;
	int manager_version = settings.value( GAME_MGR_VER, 0 ).toInt();
	if ( manager_version == 0 ) {
		auto dlg = prog_dialog( "Initializing the Game Manager" );
		// Initial game manager settings
		init_settings( manager_version, dlg );
		dlg->close();
	}

	if ( manager_version == 1 ) {
		update_settings( manager_version );
	}

	load();
}

GameMode GameManager::get_game( const NifModel * nif )
{
	if ( !nif ) [[unlikely]]
		return OTHER;

	quint32	bsver = nif->getBSVersion();

	switch ( bsver ) {
	case 0:
		break;
	case BSSTREAM_1:
	case BSSTREAM_3:
	case BSSTREAM_4:
	case BSSTREAM_5:
	case BSSTREAM_6:
	case BSSTREAM_7:
	case BSSTREAM_8:
	case BSSTREAM_9:
		return OBLIVION;
	case BSSTREAM_11:
		{
			quint32	user = nif->getUserVersion();
			if ( user == 10 || nif->getVersionNumber() <= 0x14000005 )	// TODO: Enumeration
				return OBLIVION;
			else if ( user == 11 )
				return FALLOUT_3NV;
		}
		return OTHER;
	case BSSTREAM_14:
	case BSSTREAM_16:
	case BSSTREAM_21:
	case BSSTREAM_24:
	case BSSTREAM_25:
	case BSSTREAM_26:
	case BSSTREAM_27:
	case BSSTREAM_28:
	case BSSTREAM_30:
	case BSSTREAM_31:
	case BSSTREAM_32:
	case BSSTREAM_33:
	case BSSTREAM_34:
		return FALLOUT_3NV;
	case BSSTREAM_83:
		return SKYRIM;
	case BSSTREAM_100:
		return SKYRIM_SE;
	case BSSTREAM_130:
		return FALLOUT_4;
	case BSSTREAM_155:
		return FALLOUT_76;
	case BSSTREAM_170:
	case BSSTREAM_172:
	case BSSTREAM_173:
		return STARFIELD;
	default:
		break;
	};

	// NOTE: Morrowind shares a version with other games (Freedom Force, etc.)
	if ( nif->getVersionNumber() == 0x04000002 )
		return MORROWIND;

	return OTHER;
}

GameManager * GameManager::get()
{
	static auto instance{new GameManager{}};
	return instance;
}

static QString getGamePathFromRegistry( GameMode game )
{
	QString	path = registry_game_path( KEY.value(game, {}) );
	if ( path.isEmpty() && game == FALLOUT_3NV )
		path = registry_game_path( beth.arg("Fallout3") );
	return path;
}

void GameManager::init_settings( int & manager_version, QProgressDialog * dlg )
{
	QSettings settings;
	QVariantMap paths, folders, status;
	for ( int g = 0; g < NUM_GAMES; g++ ) {
		process( dlg, g );
		GameMode	gameID = GameMode( g );
		QString	gamePath = getGamePathFromRegistry( gameID );
		QString	gameName = StringForMode( gameID );
		if ( !gamePath.isEmpty() ) {
			paths.insert( gameName, gamePath );
			folders.insert( gameName, existing_folders( gameID, gamePath ) );
		}

		// Game Enabled Status
		status.insert( gameName, true );
	}

	settings.setValue( GAME_PATHS, paths );
	settings.setValue( GAME_FOLDERS, folders );
	settings.setValue( GAME_STATUS, status );
	settings.setValue( "Settings/Resources/Other Games Fallback", QVariant(false) );
	settings.setValue( GAME_MGR_VER, ++manager_version );
}

void GameManager::update_settings( int & manager_version, QProgressDialog * dlg )
{
	QSettings settings;
	QVariantMap folders;

	for ( int g = 0; g < NUM_GAMES; g++ ) {
		process( dlg, g );
		GameMode	gameID = GameMode( g );
		QString	gamePath = getGamePathFromRegistry( gameID );
		if ( gamePath.isEmpty() || manager_version != 1 )
			continue;

		QString	gameName = StringForMode( gameID );
		folders.insert( gameName, existing_folders( gameID, gamePath ) );
	}

	if ( manager_version == 1 ) {
		settings.setValue( GAME_FOLDERS, folders );
		manager_version++;
	}

	settings.setValue( GAME_MGR_VER, manager_version );
}

QString GameManager::path( const GameMode game )
{
	if ( game >= OTHER && game < NUM_GAMES )
		return gamePaths[game];
	return QString();
}

QString GameManager::data( const GameMode game )
{
	return path(game) + "/" + DATA[game];
}

QStringList GameManager::folders( const GameMode game )
{
	if ( game >= OTHER && game < NUM_GAMES && gameStatus[game] )
		return archives[game].dataPaths;
	return {};
}

bool GameManager::status( const GameMode game )
{
	if ( game >= OTHER && game < NUM_GAMES )
		return gameStatus[game];
	return false;
}

GameManager::GameResources * GameManager::addNIFResourcePath( const NifModel * nif, const QString & dataPath )
{
	if ( !nif ) [[unlikely]]
		return &(GameManager::archives[OTHER]);

	GameMode	game = get_game( nif );
	auto	i = nifResourceMap.find( nif );
	if ( i != nifResourceMap.end() ) {
		if ( ( dataPath.isEmpty() && i->second->dataPaths.isEmpty() ) || i->second->dataPaths.startsWith( dataPath ) ) {
			if ( i->second->game == game )
				return i->second;
		}
		removeNIFResourcePath( nif );
	}
	GameResources *	r = nullptr;
	for ( auto i = nifResourceMap.begin(); i != nifResourceMap.end(); i++ ) {
		if ( ( dataPath.isEmpty() && i->second->dataPaths.isEmpty() ) || i->second->dataPaths.startsWith( dataPath ) ) {
			if ( i->second->game == game ) {
				// the same data path is already in use by another window
				r = i->second;
				r->refCnt++;
				break;
			}
		}
	}
	if ( !r ) {
		r = new GameResources();
		r->game = game;
		r->parent = &(archives[game]);
		if ( !dataPath.isEmpty() )
			r->dataPaths.append( dataPath );
	}
	nifResourceMap.emplace( nif, r );
	return r;
}

void GameManager::removeNIFResourcePath( const NifModel * nif )
{
	auto	i = nifResourceMap.find( nif );
	if ( i == nifResourceMap.end() )
		return;
	GameResources *	r = i->second;
	nifResourceMap.erase( i );
	r->refCnt--;
	if ( r->refCnt < 0 )
		delete r;
}

std::string GameManager::get_full_path( const QString & name, const char * archive_folder, const char * extension )
{
	if ( name.isEmpty() )
		return std::string();
	std::string	s = name.toLower().replace( '\\', '/' ).toStdString();
	if ( archive_folder && *archive_folder ) {
		std::string	d( archive_folder );
		if ( !d.ends_with('/') )
			d += '/';
		size_t	n = 0;
		for ( ; n < s.length(); n = n + d.length() ) {
			n = s.find( d, n );
			if ( n == 0 || n == std::string::npos || s[n - 1] == '/' )
				break;
		}
		if ( n == std::string::npos || n >= s.length() )
			s.insert( 0, d );
		else if ( n )
			s.erase( 0, n );
	}
	if ( extension && *extension && !s.ends_with(extension) ) {
		size_t	n = s.rfind( '.' );
		if ( n != std::string::npos ) {
			size_t	d = s.rfind( '/' );
			if ( d == std::string::npos || d < n )
				s.resize( n );
		}
		s += extension;
	}
	return s;
}

QString GameManager::find_file(
	const GameMode game, const QString & path, const char * archiveFolder, const char * extension )
{
	if ( !( game >= OTHER && game < NUM_GAMES ) )
		return QString();
	std::string	fullPath( get_full_path(path, archiveFolder, extension) );
	return archives[game].find_file( fullPath );
}

bool GameManager::get_file( QByteArray & data, const GameMode game, const std::string_view & fullPath )
{
	if ( !( game >= OTHER && game < NUM_GAMES ) )
		return false;
	return archives[game].get_file( data, fullPath );
}

bool GameManager::get_file(
	QByteArray & data, const GameMode game, const QString & path, const char * archiveFolder, const char * extension )
{
	std::string	fullPath( get_full_path(path, archiveFolder, extension) );
	return archives[game].get_file( data, fullPath );
}

CE2MaterialDB * GameManager::materials( const GameMode game )
{
	if ( game != STARFIELD )
		return nullptr;
	if ( archives[game].sfMaterialDB_ID ) [[likely]]
		return archives[game].sfMaterials;
	return archives[game].init_materials();
}

std::uint64_t GameManager::get_material_db_id( const GameMode game )
{
	if ( !( game >= OTHER && game < NUM_GAMES ) ) [[unlikely]]
		return 0;
	return archives[game].sfMaterialDB_ID;
}

void GameManager::close_resources( bool nifResourcesFirst )
{
	bool	haveNIFResources = false;

	for ( auto i = nifResourceMap.begin(); i != nifResourceMap.end(); i++ ) {
		if ( i->second->ba2File && i->second->ba2File->size() > 0 )
			haveNIFResources = true;
		i->second->close_materials();
		i->second->close_archives();
	}

	if ( !( nifResourcesFirst && haveNIFResources ) ) {
		for ( size_t game = size_t(OTHER); game < size_t(NUM_GAMES); game++ ) {
			archives[game].close_materials();
			archives[game].close_archives();
		}
	}
}

void GameManager::list_files(
	std::set< std::string_view > & fileSet, const GameMode game,
	bool (*fileListFilterFunc)( void * p, const std::string_view & fileName ), void * fileListFilterFuncData )
{
	if ( !( game >= OTHER && game < NUM_GAMES ) )
		return;
	archives[game].list_files( fileSet, fileListFilterFunc, fileListFilterFuncData );
}

QStringList GameManager::find_folders( const GameMode game )
{
	if ( game >= OTHER && game < NUM_GAMES )
		return existing_folders( game, gamePaths[game] );
	return QStringList();
}

void GameManager::save()
{
	QVariantMap paths, folders, status;
	for ( size_t i = size_t(OTHER); i < size_t(NUM_GAMES); i++ ) {
		GameMode	game = GameMode(i);
		QString	gameName = StringForMode(game);
		if ( !gamePaths[i].isEmpty() )
			paths.insert( gameName, gamePaths[i] );
		if ( !archives[i].dataPaths.isEmpty() )
			folders.insert( gameName, archives[i].dataPaths );
		status.insert( gameName, gameStatus[i] );
	}

	QSettings settings;
	settings.setValue( GAME_PATHS, paths );
	settings.setValue( GAME_FOLDERS, folders );
	settings.setValue( GAME_STATUS, status );
	settings.setValue( "Settings/Resources/Other Games Fallback", QVariant(otherGamesFallback) );
}

void GameManager::load()
{
	QSettings	settings;
	auto	paths = settings.value(GAME_PATHS).toMap();
	auto	folders = settings.value(GAME_FOLDERS).toMap();
	auto	status = settings.value(GAME_STATUS).toMap();
	bool	useOther = settings.value( "Settings/Resources/Other Games Fallback", false ).toBool();

	clear();

	otherGamesFallback = useOther;
	for ( auto i = paths.constBegin(); i != paths.constEnd(); i++ )
		insert_game( ModeForString( i.key() ), i.value().toString() );
	for ( auto i = folders.constBegin(); i != folders.constEnd(); i++ )
		insert_folders( ModeForString( i.key() ), i.value().toStringList() );
	for ( auto i = status.constBegin(); i != status.constEnd(); i++ )
		insert_status( ModeForString( i.key() ), i.value().toBool() );
}

void GameManager::clear()
{
	for ( size_t i = size_t(OTHER); i < size_t(NUM_GAMES); i++ ) {
		gamePaths[i].clear();
		archives[i].dataPaths.clear();
		gameStatus[i] = true;
	}
	otherGamesFallback = false;
}

void GameManager::insert_game( const GameMode game, const QString & path )
{
	if ( game >= OTHER && game < NUM_GAMES )
		gamePaths[game] = path;
}

void GameManager::insert_folders( const GameMode game, const QStringList & list )
{
	if ( !( game >= OTHER && game < NUM_GAMES ) )
		return;
	archives[game].dataPaths.clear();
	for ( const auto & i : list ) {
		if ( !i.isEmpty() )
			archives[game].dataPaths.append( i );
	}
}

void GameManager::insert_status( const GameMode game, bool status )
{
	if ( game >= OTHER && game < NUM_GAMES )
		gameStatus[game] = status;
}

} // end namespace Game
