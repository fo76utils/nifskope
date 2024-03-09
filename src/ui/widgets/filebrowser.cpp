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

#include <cstdlib>

#include "filebrowser.h"

QTreeWidgetItem *	FileBrowserWidget::findDirectory( std::map< std::string, QTreeWidgetItem * > & dirMap, const std::string& d )
{
	std::map< std::string, QTreeWidgetItem * >::iterator	i = dirMap.find( d );
	if ( i != dirMap.end() )
		return i->second;
	size_t	n = std::string::npos;
	if ( d.length() >= 2 )
		n = d.rfind( '/', d.length() - 2 );
	if ( n == std::string::npos )
		n = 0;
	else
		n++;
	QTreeWidgetItem *	parent = nullptr;
	if ( n )
		parent = findDirectory( dirMap, std::string( d, 0, n ) );
	QTreeWidgetItem *	tmp;
	if ( !parent )
		tmp = new QTreeWidgetItem( treeWidget, -2 );
	else
		tmp = new QTreeWidgetItem( parent, -2 );
	dirMap.emplace( d, tmp );
	tmp->setText( 0, QString::fromStdString( std::string( d, n, d.length() - n ) ) );
	return tmp;
}

void FileBrowserWidget::updateTreeWidget()
{
	treeWidget->clear();
	filesShown.clear();
	std::string	filterString( filter->text().trimmed().toStdString() );
	int	curFileIndex = -1;
	for ( std::set< std::string >::const_iterator i = fileSet.begin(); i != fileSet.end(); i++ ) {
		if ( currentFile && *i == *currentFile ) {
			curFileIndex = int( filesShown.size() );
		} else if ( !filterString.empty() && i->find( filterString ) == std::string::npos ) {
			continue;
		}
		filesShown.push_back( &(*i) );
	}

	std::map< std::string, QTreeWidgetItem * >	dirMap;
	std::string	d;
	for ( size_t i = 0; i < filesShown.size(); i++ ) {
		const std::string &	fullPath( *(filesShown[i]) );
		size_t	n = std::string::npos;
		if ( filesShown.size() > 100 )
			n = fullPath.rfind( '/' );
		if ( n == std::string::npos )
			n = 0;
		else
			n++;
		QTreeWidgetItem *	parent = nullptr;
		if ( n ) {
			d = std::string( fullPath, 0, n );
			parent = findDirectory( dirMap, d );
		}
		QTreeWidgetItem *	tmp;
		if ( !parent )
			tmp = new QTreeWidgetItem( treeWidget, int(i) );
		else
			tmp = new QTreeWidgetItem( parent, int(i) );
		tmp->setText( 0, QString::fromStdString( std::string( *(filesShown[i]), n, filesShown[i]->length() - n ) ) );
		if ( i == size_t(curFileIndex) )
			treeWidget->setCurrentItem( tmp );
	}
}

void FileBrowserWidget::checkItemActivated()
{
	if ( getItemSelected() )
		dlg.accept();
}

FileBrowserWidget::FileBrowserWidget( int w, int h, const char * titleString, const std::set< std::string > & files, const std::string& fileSelected )
	: fileSet( files ), currentFile( nullptr )
{
	layout = new QGridLayout( &dlg );
	layout->setColumnMinimumWidth( 0, w );
	layout->setRowMinimumHeight( 1, h );
	title = new QLabel( &dlg );
	title->setText( titleString );
	layout->addWidget( title, 0, 0 );
	treeWidget = new QTreeWidget( &dlg );
	treeWidget->setHeaderLabel( "Path" );
	layout->addWidget( treeWidget, 1, 0 );
	layout2 = new QGridLayout();
	layout->addLayout( layout2, 2, 0 );
	layout2->setColumnMinimumWidth( 0, w - ( w >> 2 ) );
	layout2->setColumnMinimumWidth( 1, w >> 2 );
	filter = new QLineEdit( &dlg );
	layout2->addWidget( filter, 0, 0 );
	filterTitle = new QLabel( &dlg );
	filterTitle->setText( "Path Filter" );
	layout2->addWidget( filterTitle, 0, 1 );

	if ( !fileSelected.empty() )
		currentFile = &fileSelected;
	QObject::connect( filter, &QLineEdit::returnPressed, filter, [this]() { updateTreeWidget(); } );
	QObject::connect( treeWidget, &QTreeWidget::itemDoubleClicked, treeWidget, [this]() { checkItemActivated(); } );
	updateTreeWidget();
}

FileBrowserWidget::~FileBrowserWidget()
{
}

const std::string * FileBrowserWidget::getItemSelected() const
{
	QList< QTreeWidgetItem * >	tmp = treeWidget->selectedItems();
	if ( tmp.size() > 0 ) {
		int	n = tmp[0]->type();
		if ( n >= 0 && size_t(n) < filesShown.size() )
			return filesShown[n];
	}
	return nullptr;
}
