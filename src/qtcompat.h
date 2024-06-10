#ifndef QTCOMPAT_H_INCLUDED
#define QTCOMPAT_H_INCLUDED

#include <QAbstractItemModel>
#include <QModelIndex>

// implementation of QModelIndex::child()

template < typename T >
static inline QModelIndex QModelIndex_child( const T & m, int arow = 0, int acolumn = 0 )
{
	const QAbstractItemModel *	model = m.model();
	if ( !model )
		return QModelIndex();
	return model->index( arow, acolumn, m );
}

#endif
