#include "spellbook.h"
#include "gamemanager.h"
#include "libfo76utils/src/material.hpp"

#include <QClipboard>

//! Export Starfield material as JSON format .mat file
class spStarfieldMaterialExport final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Export Starfield Material" ); }
	QString page() const override final { return Spell::tr( "" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( nif && nif->getBSVersion() >= 160 ) {
			const NifItem * item = nif->getItem( index );
			if ( item && item->parent() && item->name() == "Name" && item->parent()->name() == "BSLightingShaderProperty" ) {
				return true;
			}
		}
		return false;
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QString	materialPath( nif->resolveString( nif->getItem( index ) ) );
		if ( !materialPath.isEmpty() ) {
			std::string	matFilePath( materialPath.toStdString() );
			std::string	matFileData;
			try {
				CE2MaterialDB *	materials = Game::GameManager::materials( Game::STARFIELD );
				if ( materials ) {
					(void) materials->loadMaterial( matFilePath );
					materials->getJSONMaterial( matFileData, matFilePath );
					QClipboard	*clipboard;
					if ( !matFileData.empty() && ( clipboard = QGuiApplication::clipboard() ) != nullptr )
						clipboard->setText( QString::fromStdString( matFileData ) );
				}
			} catch ( std::exception& e ) {
				QMessageBox::critical( nullptr, "NifSkope error", QString("Error loading material '%1': %2" ).arg( materialPath ).arg( e.what() ) );
			}
		}
		return index;
	}
};

REGISTER_SPELL( spStarfieldMaterialExport )

