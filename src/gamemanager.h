#ifndef GAMEMANAGER_H
#define GAMEMANAGER_H

#include <cstdint>
#include <memory>

#include <QMap>
#include <QString>
#include <QStringBuilder>
#include <QMutex>
#include <QAbstractItemModel>

#include "ba2file.hpp"
#include "material.hpp"

class QProgressDialog;

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

using GameMap = QMap<GameMode, QString>;
using GameEnabledMap = QMap<GameMode, bool>;
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
	BSSTREAM_172 = 172,
	BSSTREAM_173 = 173
};

QString StringForMode(GameMode game);
GameMode ModeForString(QString game);

class GameManager
{
	GameManager();
public:
	GameManager(const GameManager&) = delete;
	GameManager& operator= (const GameManager) = delete;

	static GameMode get_game(uint32_t version, uint32_t user, uint32_t bsver);

	static GameManager* get();

	//! Game installation path
	static QString path(const GameMode game);
	//! Game data path
	static QString data(const GameMode game);
	//! Game folders managed by the GameManager
	static QStringList folders(const GameMode game);
	//! Game enabled status in the GameManager
	static bool status(const GameMode game);

	//! Convert 'name' to lower case, replace backslashes with forward slashes, and make sure that the path begins with 'archive_folder' and ends with 'extension' (e.g. "textures" and ".dds").
	static std::string get_full_path(const QString& name, const char* archive_folder, const char* extension);
	//! Search for file 'path' in the resource archives and folders, and return the full path if the file is found, or an empty string otherwise.
	static QString find_file(const GameMode game, const QString& path, const char* archiveFolder, const char* extension);
	//! Find and load resource file to 'data'. The return value is true on success.
	static bool get_file(std::vector< unsigned char >& data, const GameMode game, const std::string& fullPath);
	static bool get_file(std::vector< unsigned char >& data, const GameMode game, const QString& path, const char* archiveFolder, const char* extension);
	static bool get_file(QByteArray& data, const GameMode game, const QString& path, const char* archiveFolder, const char* extension);
	//! Return pointer to Starfield material database, loading it first if necessary. On error, nullptr is returned.
	static CE2MaterialDB* materials(const GameMode game);
	//! Returns a non-zero ID unique to the currently loaded material database. Previously returned material pointers become invalid when this value changes.
	static std::uintptr_t get_material_db_id();
	//! Close all currently opened resource archives and files.
	static void close_archives(bool tempPathsFirst = false);
	//! Deallocate Starfield material database if it is currently loaded.
	static void close_materials();
	//! Open a folder or archive without adding it to the list of data paths.
	static bool set_temp_path(const GameMode game, const char* pathName, bool ignoreErrors);
	//! List resource files available for 'game' on the archive filesystem, as a set of null-terminated strings.
	// The file list can be optionally filtered by a function that returns false if the file should be excluded.
	static void list_files(std::set< std::string_view >& fileSet, const GameMode game, bool (*fileListFilterFunc)(void* p, const std::string_view& fileName) = nullptr, void* fileListFilterFuncData = nullptr);

	//! Find applicable data folders at the game installation path
	static QStringList find_folders(const GameMode game);

	//! Game installation path
	static inline QString path(const QString& game);
	//! Game data path
	static inline QString data(const QString& game);
	//! Game folders managed by the GameManager
	static inline QStringList folders(const QString& game);
	//! Game enabled status in the GameManager
	static inline bool status(const QString& game);
	//! Find applicable data folders at the game installation path
	static inline QStringList find_folders(const QString& game);

	static inline void update_game(const GameMode game, const QString& path);
	static inline void update_game(const QString& game, const QString& path);
	static inline void update_folders(const GameMode game, const QStringList& list);
	static inline void update_folders(const QString& game, const QStringList& list);
	static inline void update_status(const GameMode game, bool status);
	static inline void update_status(const QString& game, bool status);

	void init_settings(int& manager_version, QProgressDialog* dlg = nullptr) const;
	void update_settings(int& manager_version, QProgressDialog* dlg = nullptr) const;

	//! Save the manager to settings
	void save() const;
	//! Load the manager from settings
	void load();
	//! Reset the manager
	void clear();

	struct GameInfo
	{
		GameMode id = OTHER;
		QString name;
		QString path;
		bool status = false;
	};

private:
	void insert_game(const GameMode game, const QString& path);
	void insert_folders(const GameMode game, const QStringList& list);
	void insert_status(const GameMode game, bool status);

	mutable QMutex mutex;

	GameMap game_paths;
	GameEnabledMap game_status;
	ResourceListMap game_folders;
};

QString GameManager::path(const QString& game)
{
	return path(ModeForString(game));
}

QString GameManager::data(const QString& game)
{
	return data(ModeForString(game));
}

QStringList GameManager::folders(const QString& game)
{
	return folders(ModeForString(game));
}

bool GameManager::status(const QString& game)
{
	return status(ModeForString(game));
}

QStringList GameManager::find_folders(const QString & game)
{
	return find_folders(ModeForString(game));
}

void GameManager::update_game(const GameMode game, const QString & path)
{
	get()->insert_game(game, path);
}

void GameManager::update_game(const QString& game, const QString& path)
{
	update_game(ModeForString(game), path);
}

void GameManager::update_folders(const GameMode game, const QStringList & list)
{
	get()->insert_folders(game, list);
}

void GameManager::update_folders(const QString& game, const QStringList& list)
{
	update_folders(ModeForString(game), list);
}

void GameManager::update_status(const GameMode game, bool status)
{
	get()->insert_status(game, status);
}

void GameManager::update_status(const QString& game, bool status)
{
	update_status(ModeForString(game), status);
}

} // end namespace Game

template < typename T >
static inline QModelIndex QModelIndex_child( const T & m, int arow = 0, int acolumn = 0 )
{
	const QAbstractItemModel *	model = m.model();
	if ( !model )
		return QModelIndex();
	return model->index( arow, acolumn, m );
}

#endif // GAMEMANAGER_H
