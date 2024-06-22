#ifndef GAMEMANAGER_H
#define GAMEMANAGER_H

#include "libfo76utils/src/common.hpp"

#include <unordered_map>
#include <QString>
#include <QStringList>

class QProgressDialog;
class NifModel;
class BA2File;
class CE2MaterialDB;

namespace Game
{

enum GameMode : int
{
	OTHER,
	MORROWIND,
	OBLIVION,
	// Fallout 3 and Fallout NV cannot be differentiated by version
	FALLOUT_3NV,
	SKYRIM,
	SKYRIM_SE,
	FALLOUT_4,
	FALLOUT_76,
	STARFIELD,

	NUM_GAMES
};

enum BSVersion
{
	BSSTREAM_1 = 1,
	BSSTREAM_3 = 3,
	BSSTREAM_4 = 4,
	BSSTREAM_5 = 5,
	BSSTREAM_6 = 6,
	BSSTREAM_7 = 7,
	BSSTREAM_8 = 8,
	BSSTREAM_9 = 9,
	BSSTREAM_11 = 11,
	BSSTREAM_14 = 14,
	BSSTREAM_16 = 16,
	BSSTREAM_21 = 21,
	BSSTREAM_24 = 24,
	BSSTREAM_25 = 25,
	BSSTREAM_26 = 26,
	BSSTREAM_27 = 27,
	BSSTREAM_28 = 28,
	BSSTREAM_30 = 30,
	BSSTREAM_31 = 31,
	BSSTREAM_32 = 32,
	BSSTREAM_33 = 33,
	BSSTREAM_34 = 34,
	BSSTREAM_83 = 83,
	BSSTREAM_100 = 100,
	BSSTREAM_130 = 130,
	BSSTREAM_155 = 155,
	BSSTREAM_170 = 170,
	BSSTREAM_172 = 172,
	BSSTREAM_173 = 173
};

QString StringForMode(GameMode game);
GameMode ModeForString(QString game);

class GameManager
{
	GameManager();
public:
	GameManager( const GameManager & ) = delete;
	GameManager & operator= ( const GameManager ) = delete;

	// OTHER is returned if 'nif' is nullptr
	static GameMode get_game( const NifModel * nif );

	static GameManager * get();

	//! Game installation path
	static QString path( const GameMode game );
	//! Game data path
	static QString data( const GameMode game );
	//! Game folders managed by the GameManager
	static QStringList folders( const GameMode game );
	//! Game enabled status in the GameManager
	static bool status( const GameMode game );

	struct GameResources
	{
		GameMode	game = OTHER;
		std::int32_t	refCnt = 0;
		BA2File *	ba2File = nullptr;
		CE2MaterialDB *	sfMaterials = nullptr;
		std::uint64_t	sfMaterialDB_ID = 0;
		GameResources *	parent = nullptr;
		// list of data paths, empty for archived NIFs
		QStringList	dataPaths;
		~GameResources();
		void init_archives();
		CE2MaterialDB * init_materials();
		void close_archives();
		void close_materials();
		QString find_file( const std::string_view & fullPath );
		bool get_file( QByteArray & data, const std::string_view & fullPath );
		void list_files(
			std::set< std::string_view > & fileSet,
			bool (*fileListFilterFunc)( void * p, const std::string_view & fileName ), void * fileListFilterFuncData );
	};

	static GameResources * addNIFResourcePath( const NifModel * nif, const QString & dataPath );
	static void removeNIFResourcePath( const NifModel * nif );
	static inline GameResources & getNIFResources( const NifModel * nif );

	//! Convert 'name' to lower case, replace backslashes with forward slashes, and make sure that the path
	// begins with 'archive_folder' and ends with 'extension' (e.g. "textures" and ".dds").
	static std::string get_full_path( const QString & name, const char * archive_folder, const char * extension );
	//! Search for file 'path' in the resource archives and folders, and return the full path if the file is found,
	// or an empty string otherwise.
	static QString find_file(
		const GameMode game, const QString & path, const char * archiveFolder, const char * extension );
	//! Find and load resource file to 'data'. The return value is true on success.
	static bool get_file( QByteArray & data, const GameMode game, const std::string_view & fullPath );
	static bool get_file(
		QByteArray & data, const GameMode game,
		const QString & path, const char * archiveFolder, const char * extension );
	//! Return pointer to Starfield material database, loading it first if necessary.
	// On error, nullptr is returned.
	static CE2MaterialDB * materials( const GameMode game );
	//! Returns a unique ID for the currently loaded material database (0 if none).
	// Previously returned material pointers become invalid when this value changes.
	static std::uint64_t get_material_db_id( const GameMode game );
	//! Close all currently opened resource archives, files and materials. If 'nifResourcesFirst' is true,
	// then only the resources associated with loose NIF files are closed, if there are any.
	static void close_resources( bool nifResourcesFirst = false );
	//! List resource files available for 'game' on the archive filesystem, as a set of null-terminated strings.
	// The file list can be optionally filtered by a function that returns false if the file should be excluded.
	static void list_files(
		std::set< std::string_view > & fileSet, const GameMode game,
		bool (*fileListFilterFunc)( void * p, const std::string_view & fileName ) = nullptr,
		void * fileListFilterFuncData = nullptr );

	//! Find applicable data folders at the game installation path
	static QStringList find_folders( const GameMode game );

	//! Game installation path
	static inline QString path( const QString & game );
	//! Game data path
	static inline QString data( const QString & game );
	//! Game folders managed by the GameManager
	static inline QStringList folders( const QString & game );
	//! Game enabled status in the GameManager
	static inline bool status( const QString & game );
	//! Find applicable data folders at the game installation path
	static inline QStringList find_folders( const QString & game );

	static inline void update_game( const GameMode game, const QString & path );
	static inline void update_game( const QString & game, const QString & path );
	static inline void update_folders( const GameMode game, const QStringList & list );
	static inline void update_folders( const QString & game, const QStringList & list );
	static inline void update_status( const GameMode game, bool status );
	static inline void update_status( const QString & game, bool status );
	static inline void update_other_games_fallback( bool status );

	static void init_settings( int & manager_version, QProgressDialog * dlg = nullptr );
	static void update_settings( int & manager_version, QProgressDialog * dlg = nullptr );

	//! Save the manager to settings
	static void save();
	//! Load the manager from settings
	static void load();
	//! Reset the manager
	static void clear();

private:
	static void insert_game( const GameMode game, const QString & path );
	static void insert_folders( const GameMode game, const QStringList & list );
	static void insert_status( const GameMode game, bool status );

	static GameResources	archives[NUM_GAMES];
	// resources associated with loose NIF files
	static std::unordered_map< const NifModel *, GameResources * >	nifResourceMap;
	static std::uint64_t	material_db_prv_id;
	static QString	gamePaths[NUM_GAMES];
	static bool	gameStatus[NUM_GAMES];
	static bool	otherGamesFallback;
};

inline GameManager::GameResources & GameManager::getNIFResources( const NifModel * nif )
{
	auto	i = nifResourceMap.find( nif );
	if ( i != nifResourceMap.end() ) [[likely]]
		return *(i->second);
	return archives[get_game(nif)];
}

QString GameManager::path( const QString & game )
{
	return path( ModeForString(game) );
}

QString GameManager::data( const QString & game )
{
	return data( ModeForString(game) );
}

QStringList GameManager::folders( const QString & game )
{
	return folders( ModeForString(game) );
}

bool GameManager::status( const QString & game )
{
	return status( ModeForString(game) );
}

QStringList GameManager::find_folders( const QString & game )
{
	return find_folders( ModeForString(game) );
}

void GameManager::update_game( const GameMode game, const QString & path )
{
	insert_game( game, path );
}

void GameManager::update_game( const QString & game, const QString & path )
{
	update_game( ModeForString(game), path );
}

void GameManager::update_folders( const GameMode game, const QStringList & list )
{
	insert_folders( game, list );
}

void GameManager::update_folders( const QString & game, const QStringList & list )
{
	update_folders( ModeForString(game), list );
}

void GameManager::update_status( const GameMode game, bool status )
{
	insert_status( game, status );
}

void GameManager::update_status( const QString & game, bool status )
{
	update_status( ModeForString(game), status );
}

void GameManager::update_other_games_fallback( bool status )
{
	otherGamesFallback = status;
}

} // end namespace Game

#endif // GAMEMANAGER_H
