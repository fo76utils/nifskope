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

#ifndef BSAMODEL_H
#define BSAMODEL_H

#include "libfo76utils/src/ba2file.hpp"

#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QHash>

class BSAModel : public QStandardItemModel
{
	Q_OBJECT

public:
	BSAModel( QObject * parent = nullptr );

	void init();

	Qt::ItemFlags flags( const QModelIndex & index ) const override;
	bool fillModel( const BA2File * bsa, const QString & folder );
protected:
	struct FileScanFuncData {
		BSAModel * p;
		const BA2File * bsa;
		std::string path;
		QHash< QString, QStandardItem * > folderMap;
	};
	static bool fileListScanFunction( void * p, const BA2File::FileDeclaration & fd );
	QStandardItem * insertFolder( const QString & path, qsizetype pos, QHash< QString, QStandardItem * > & folderMap, QStandardItem * parent = nullptr );
};


class BSAProxyModel : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	BSAProxyModel( QObject * parent = nullptr );

	void setFiletypes( QStringList types );

	void resetFilter();

public slots:
	void setFilterByNameOnly( bool nameOnly );

protected:
	bool filterAcceptsRow( int sourceRow, const QModelIndex & sourceParent ) const;
	bool lessThan( const QModelIndex & left, const QModelIndex & right ) const;

private:
	QStringList filetypes;
	bool filterByNameOnly = false;
};

#endif
