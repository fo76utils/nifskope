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

bool BSAModel::fillModel( const BA2File * bsa, const QString & folder )
{
	if ( !bsa )
		return false;

	FileScanFuncData	data;
	data.p = this;
	data.bsa = bsa;
	data.path = folder.toStdString();
	if ( !data.path.empty() && !data.path.ends_with( '/' ) )
		data.path += '/';

	// List files
	bsa->scanFileList( &fileListScanFunction, &data );

	return ( rowCount() > 0 );
}

bool BSAModel::fileListScanFunction( void * p, const BA2File::FileInfo & fd )
{
	FileScanFuncData & o = *( reinterpret_cast< FileScanFuncData * >( p ) );

	if ( fd.fileName.length() <= o.path.length() || !( o.path.empty() || fd.fileName.starts_with( o.path ) ) )
		return false;

	qsizetype	bytes = qsizetype( fd.archiveType < 64 || fd.packedSize == 0 ? fd.unpackedSize : fd.packedSize );
	QString	fileSize( (bytes > 1024) ? QString::number( bytes / 1024 ) + "KB" : QString::number( bytes ) + "B" );

	QString	fullPath( QString::fromLatin1( fd.fileName.data(), qsizetype(fd.fileName.length()) ) );
	qsizetype	dirNameLen = fullPath.lastIndexOf( QChar( '/' ) );
	QString	baseName( fullPath.mid( dirNameLen + 1 ) );
	auto	folderItem = o.p->insertFolder( fullPath, o.path.length(), dirNameLen, o.folderMap );

	auto	fileItem = new QStandardItem( baseName );
	auto	pathItem = new QStandardItem( fullPath );
	auto	sizeItem = new QStandardItem( fileSize );

	folderItem->appendRow( { fileItem, pathItem, sizeItem } );

	return false;
}

QStandardItem * BSAModel::insertFolder( const QString & path, qsizetype pos1, qsizetype pos2, QHash< QString, QStandardItem * > & folderMap )
{
	if ( pos2 <= pos1 )
		return invisibleRootItem();

	QString	key( path.left( pos2 ) );
	for ( auto i = folderMap.find( key ); i != folderMap.end(); )
		return i.value();

	qsizetype	i1 = path.lastIndexOf( QChar('/'), pos2 - 1 );
	QStandardItem *	parent;
	// Recurse through folders
	if ( i1 > pos1 )
		parent = insertFolder( path, pos1, i1, folderMap );
	else
		parent = invisibleRootItem();
	QStandardItem *	folderItem = new QStandardItem( path.mid( i1 + 1, pos2 - ( i1 + 1 ) ) );
	auto pathDummy = new QStandardItem();
	auto sizeDummy = new QStandardItem();

	parent->appendRow( { folderItem, pathDummy, sizeDummy } );
	folderMap.insert( key, folderItem );

	return folderItem;
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
