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

#include "bsamodel.h"
#include "gamemanager.h"
#include "libfo76utils/src/ba2file.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QStringBuilder>


BSAModel::BSAModel( QObject * parent )
	: QStandardItemModel( parent )
{

}

void BSAModel::init()
{
	setColumnCount( 3 );
	setHorizontalHeaderLabels( { "File", "Path", "Size" } );
}

Qt::ItemFlags BSAModel::flags( const QModelIndex & index ) const
{
	return QStandardItemModel::flags( index ) ^ Qt::ItemIsEditable;
}

static bool fileListFilterFunction( void * p, const std::string & s )
{
	const std::string &	path = *( reinterpret_cast< std::string * >( p ) );
	return ( s.length() > path.length() && ( path.empty() || s.starts_with( path ) ) );
}

bool BSAModel::fillModel( const BA2File * bsa, const QString & folder )
{
	if ( !bsa )
		return false;

	std::string	path( folder.toStdString() );
	if ( !path.empty() && !path.ends_with( '/' ) )
		path += '/';
	std::vector< std::string >	fileList;
	bsa->getFileList( fileList, false, &fileListFilterFunction, &path );
	if ( fileList.size() < 1 )
		return false;

	QMap< QString, QStandardItem * >	folderMap;
	bool	foundFiles = false;
	for ( const auto & i : fileList ) {
		// List files
		auto	fd = bsa->findFile( i );
		if ( !fd )
			continue;
		foundFiles = true;
		qsizetype	bytes = qsizetype( fd->archiveType < 64 || fd->packedSize == 0 ? fd->unpackedSize : fd->packedSize );
		QString	fileSize( (bytes > 1024) ? QString::number( bytes / 1024 ) + "KB" : QString::number( bytes ) + "B" );

		QString	fullPath( QString::fromStdString( i ) );
		QString	baseName( fullPath.mid( fullPath.lastIndexOf( QChar( '/' ) ) + 1 ) );
		auto	folderItem = insertFolder( fullPath, path.length(), folderMap );

		auto	fileItem = new QStandardItem( baseName );
		auto	pathItem = new QStandardItem( fullPath );
		auto	sizeItem = new QStandardItem( fileSize );

		folderItem->appendRow( { fileItem, pathItem, sizeItem } );
	}
	return foundFiles;
}

QStandardItem * BSAModel::insertFolder( const QString & path, qsizetype pos, QMap< QString, QStandardItem * > & folderMap, QStandardItem * parent )
{
	if ( !parent )
		parent = invisibleRootItem();
	if ( path.length() <= pos )
		return parent;

	qsizetype	i1 = path.indexOf( QChar('/'), pos );
	if ( i1 < 0 )
		return parent;

	QStandardItem *	folderItem = nullptr;
	QString	key( path.left( i1 ) );
	for ( auto i = folderMap.find( key ); i != folderMap.end(); ) {
		folderItem = i.value();
		break;
	}
	if ( !folderItem ) {
		folderItem = new QStandardItem( path.mid( pos, i1 - pos ) );
		auto pathDummy = new QStandardItem( "" );
		auto sizeDummy = new QStandardItem( "" );

		parent->appendRow( { folderItem, pathDummy, sizeDummy } );
		folderMap.insert( key, folderItem );
	}
	if ( path.indexOf( QChar('/'), i1 + 1 ) < 0 )
		return folderItem;

	// Recurse through folders
	return insertFolder( path, i1 + 1, folderMap, folderItem );
}


BSAProxyModel::BSAProxyModel( QObject * parent )
	: QSortFilterProxyModel( parent )
{

}

void BSAProxyModel::setFiletypes( QStringList types )
{
	filetypes = types;
}

void BSAProxyModel::setFilterByNameOnly( bool nameOnly )
{
	filterByNameOnly = nameOnly;

	setFilterRegExp( filterRegExp() );
}

void BSAProxyModel::resetFilter()
{
	setFilterRegExp( QRegExp( "*", Qt::CaseInsensitive, QRegExp::Wildcard ) );
}

bool BSAProxyModel::filterAcceptsRow( int sourceRow, const QModelIndex & sourceParent ) const
{
	if ( !filterRegExp().isEmpty() ) {

		QModelIndex sourceIndex0 = sourceModel()->index( sourceRow, 0, sourceParent );
		QModelIndex sourceIndex1 = sourceModel()->index( sourceRow, 1, sourceParent );
		if ( sourceIndex0.isValid() ) {
			// If children match, parent matches
			int c = sourceModel()->rowCount( sourceIndex0 );
			for ( int i = 0; i < c; ++i ) {
				if ( filterAcceptsRow( i, sourceIndex0 ) )
					return true;
			}

			QString key0 = sourceModel()->data( sourceIndex0, filterRole() ).toString();
			QString key1 = sourceModel()->data( sourceIndex1, filterRole() ).toString();

			bool typeMatch = true;
			if ( filetypes.count() ) {
				typeMatch = false;
				for ( auto f : filetypes ) {
					typeMatch |= key1.endsWith( f, Qt::CaseInsensitive );
				}
			}

			bool stringMatch = (filterByNameOnly) ? key0.contains( filterRegExp() ) : key1.contains( filterRegExp() );

			return typeMatch && stringMatch;
		}
	}

	return QSortFilterProxyModel::filterAcceptsRow( sourceRow, sourceParent );
}

bool BSAProxyModel::lessThan( const QModelIndex & left, const QModelIndex & right ) const
{
	QString leftString = sourceModel()->data( left ).toString();
	QString rightString = sourceModel()->data( right ).toString();

	QModelIndex leftChild = QModelIndex_child( left );
	QModelIndex rightChild = QModelIndex_child( right );

	if ( !leftChild.isValid() && rightChild.isValid() )
		return false;

	if ( leftChild.isValid() && !rightChild.isValid() )
		return true;

	return leftString < rightString;
}
