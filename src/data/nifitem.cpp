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

#include "nifitem.h"
#include "model/basemodel.h"
#include "model/nifmodel.h"

#include <new>

bool NifData::compareStrings( const QChar * s, const char * t, size_t l )
{
	for ( size_t i = 0; i < l; i++ ) {
		if ( s[i] != t[i] )
			return false;
	}
	return true;
}

bool NifItem::isDescendantOf( const NifItem * testAncestor ) const
{
	if ( testAncestor ) {
		const NifItem * ancestor = this;
		do {
			if ( ancestor == testAncestor )
				return true;
			ancestor = ancestor->parent();
		} while ( ancestor );
	}

	return false;
}

bool NifItem::isVector3Color() const
{
	if ( this->isVector3() && this->hasName( "Value" ) ) {
		NifModel * nif = qobject_cast<NifModel *>( this->parentModel );
		QModelIndex parent = nif->getBlockIndex( nif->getParent( nif->getBlockIndex( this ) ) );
		QModelIndex grandparent = nif->getBlockIndex( nif->getParent( parent ) );
		if ( nif->isNiBlock( grandparent , "BSLightingShaderPropertyColorController" ) ) {
			return true;
		} else if ( nif->isNiBlock( grandparent , "NiControllerSequence" ) ) {
			QModelIndex iCtrlBlcks = nif->getIndex( grandparent, "Controlled Blocks");
			for ( int r = 0; r < nif->rowCount( iCtrlBlcks ); r++ ) {
				QModelIndex iCB = nif->getIndex( iCtrlBlcks, r );
				QModelIndex iInterp = nif->getBlockIndex( nif->getLink( iCB, "Interpolator" ), "NiInterpolator" );
				if ( parent == iInterp ) {
					QModelIndex iController = nif->getBlockIndex( nif->getLink( iCB, "Controller" ), "NiTimeController" );
					if ( nif->isNiBlock( iController, "BSEffectShaderPropertyColorController") ) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

int NifItem::ancestorLevel( const NifItem * testAncestor ) const
{
	if ( testAncestor ) {
		const NifItem * ancestor = this;
		for ( int level = 0; ; level++ ) {
			if ( ancestor == testAncestor )
				return level;
			ancestor = ancestor->parent();
			if ( !ancestor )
				break;
		}
	}

	return -1;
}

const NifItem * NifItem::ancestorAt( int testLevel ) const
{
	if ( testLevel >= 0 ) {
		const NifItem * ancestor = this;
		for ( int level = 0; ; level++ ) {
			if ( level == testLevel )
				return ancestor;
			ancestor = ancestor->parent();
			if ( !ancestor )
				break;
		}
	}

	return nullptr;
}

void NifItem::registerChild( NifItem * item, int at )
{
	int nOldChildren = childItemsSize;
	if ( nOldChildren >= childItemsCapacity ) [[unlikely]]
		reserveChildItems( nOldChildren + 1 );
	if ( at < 0 || at >= nOldChildren ) {
		at = nOldChildren;
		childItems[at] = item;
		childItemsSize++;
		item->rowIdx = at;
	} else {
		std::memmove( childItems + ( at + 1 ), childItems + at, size_t( nOldChildren - at ) * sizeof( NifItem * ) );
		childItems[at] = item;
		childItemsSize++;
		item->rowIdx = at;
		updateChildRows( at + 1 );
	}
	if ( item->isLink() || item->hasChildLinks() ) [[unlikely]]
		item->registerInParentLinkCache();
}

NifItem * NifItem::unregisterChild( int at )
{
	if ( !( at >= 0 && at < childItemsSize ) ) [[unlikely]]
		return nullptr;

	NifItem * item = childItems[at];

	int n = childItemsSize - ( at + 1 );
	if ( n > 0 )
		std::memmove( childItems + at, childItems + ( at + 1 ), size_t( n ) * sizeof( NifItem * ) );
	childItemsSize = at + n;
	updateChildRows( at );

	return item;
}

void NifItem::removeChildren( int row, int count )
{
	int iStart = std::max( row, 0 );
	int iEnd = std::min( row + count, childItemsSize );
	if ( iStart < iEnd ) [[likely]] {
		for ( int i = iStart; i < iEnd; i++ )
			delete childItems[i];
		int n = childItemsSize - iEnd;
		if ( n > 0 )
			std::memmove( childItems + iStart, childItems + iEnd, size_t( n ) * sizeof( NifItem * ) );
		childItemsSize = iStart + n;
		updateChildRows( iStart );
	}
}

size_t NifItem::findLinkRow( int n ) const
{
	size_t	i0 = 0;
	size_t	i2 = linkRowsSize;
	while ( i2 > i0 ) {
		size_t	i1 = ( i0 + i2 ) >> 1;
		if ( linkRows[i1] < n )
			i0 = i1 + 1;
		else
			i2 = i1;
	}
	// return the index of the first element of linkRows not less than 'n'
	return i0;
}

void NifItem::insertLinkRow( int n )
{
	if ( !linkRows || linkRowsSize >= (unsigned int) *( linkRows - 1 ) ) [[unlikely]] {
		size_t	linkRowsCapacity = size_t( linkRowsSize ) + 1;
		linkRowsCapacity = ( linkRowsCapacity + ( linkRowsCapacity >> 1 ) ) | 3;
		int *	tmp = new int[linkRowsCapacity + 1];
		tmp[0] = int( linkRowsCapacity );
		if ( linkRowsSize > 0 )
			std::memcpy( tmp + 1, linkRows, size_t( linkRowsSize ) * sizeof( int ) );
		if ( linkRows )
			delete[] ( linkRows - 1 );
		linkRows = tmp + 1;
	}

	size_t	i = linkRowsSize;
	if ( i && linkRows[i - 1] >= n ) {
		i = findLinkRow( n );
		if ( linkRows[i] == n )
			return;
		std::memmove( linkRows + ( i + 1 ), linkRows + i, ( size_t( linkRowsSize ) - i ) * sizeof( int ) );
	}
	if ( linkRowsSize >= 65535U )
		throw std::bad_alloc();
	linkRows[i] = n;
	linkRowsSize++;
}

void NifItem::registerInParentLinkCache()
{
	NifItem * c = this;
	for ( NifItem * p = parentItem; p; p = c->parentItem ) {
		bool bOldHasChildLinks = p->hasChildLinks();
		int	i = c->row();
		if ( i >= 0 )
			p->insertLinkRow( i );
		if ( bOldHasChildLinks )
			break; // Do NOT register p in its parent (again) if c is NOT a first registered child link for p
		c = p;
	}
}

void NifItem::unregisterInParentLinkCache()
{
	NifItem * c = this;
	for ( NifItem * p = parentItem; p; p = c->parentItem ) {
		p->removeLinkRow( c->row() );
		if ( p->linkRowsSize )
			break; // Do NOT unregister p in its parent if p still has other registered child links
		c = p;
	}
}

void NifItem::removeLinkRow( int n )
{
	size_t	i = findLinkRow( n );
	if ( i < linkRowsSize && linkRows[i] == n ) {
		if ( ( i + 1 ) < linkRowsSize )
			std::memmove( linkRows + i, linkRows + ( i + 1 ), ( size_t( linkRowsSize ) - ( i + 1 ) ) * sizeof( int ) );
		linkRowsSize--;
	}
}

void NifItem::updateChildRows( int iStartChild )
{
	for ( int i = iStartChild; i < childItemsSize; i++ ) {
		NifItem *	c = childItems[i];
		if ( c ) [[likely]]
			c->rowIdx = i;
	}
	if ( !hasChildLinks() ) [[likely]]
		return;
	updateLinkRows( iStartChild );
}

void NifItem::updateLinkRows( int iStartChild )
{
	size_t	i = linkRowsSize;
	while ( i > 0 && linkRows[i - 1] >= iStartChild )
		i--;
	if ( i >= linkRowsSize )
		return;
	linkRowsSize = i;
	for ( int n = iStartChild; n < childItemsSize; n++ ) {
		NifItem *	c = childItems[n];
		if ( c && ( c->isLink() || c->hasChildLinks() ) )
			insertLinkRow( n );
	}
	if ( !( isLink() || hasChildLinks() ) )
		unregisterInParentLinkCache();
}

int NifItem::updateRowIndex() const
{
	if ( !parentItem ) {
		rowIdx = 0;
		return 0;
	}
	const NifItem * const *	p = parentItem->childItems;
	qsizetype	n = parentItem->childItemsSize;
	for ( qsizetype i = 0; i < n; i++ ) {
		if ( p[i] == this ) {
			rowIdx = int( i );
			return rowIdx;
		}
	}
	rowIdx = -1;
	return -1;
}

void NifItem::reserveChildItems( int n )
{
	if ( n <= childItemsCapacity ) [[unlikely]]
		return;
	n = std::max< int >( n, ( ( childItemsCapacity + ( childItemsCapacity >> 1 ) ) | 1 ) + 1 );
	void *	tmp = std::realloc( childItems, size_t( n ) * sizeof( NifItem * ) );
	if ( !tmp )
		throw std::bad_alloc();
	childItems = reinterpret_cast< NifItem ** >( tmp );
	childItemsCapacity = n;
}

void NifItem::deleteChildItems()
{
	for ( auto c : children() )
		delete c;
	childItemsSize = 0;
}

void NifItem::onParentItemChange()
{
	parentModel     = parentItem->parentModel;
	vercondStatus   = -1;
	conditionStatus = -1;

	for ( auto c : children() )
		c->onParentItemChange();
}

QString NifItem::repr() const
{
	return parentModel->itemRepr( this );
}

void NifItem::reportError( const QString & msg ) const
{
	parentModel->reportError( this, msg );
}

void NifItem::reportError( const QString & funcName, const QString & msg ) const
{
	parentModel->reportError( this, funcName, msg );
}
