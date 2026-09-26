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

#ifndef NIFITEM_H
#define NIFITEM_H

#include "data/nifvalue.h"
#include "xml/nifexpr.h"
#include <span>

#include <QSharedData> // Inherited
#include <QPointer>
#include <QVector>
#include <QMap>


//! @file nifitem.h NifItem, NifBlock, NifData, NifSharedData

/*! Shared data for NifData.
 *
 * @see QSharedDataPointer
 */
class NifSharedData final : public QSharedData
{
	friend class NifData;

public:
	enum DataFlag
	{
		None = 0x0,
		Abstract = 0x1,
		Binary = 0x2,
		Templated = 0x4,
		Compound = 0x8,
		Array = 0x10,
		MultiArray = 0x20,
		Conditionless = 0x40,
		Mixin = 0x80,
		TypeCondition = 0x100
	};

	typedef QFlags<DataFlag> DataFlags;

private:

	NifSharedData( const QString & n, const QString & t, const QString & tt, const QString & a, const QString & a1,
				   const QString & a2, const QString & c, quint32 v1, quint32 v2, NifSharedData::DataFlags f )
		: QSharedData(), name( n ), type( t ), templ( tt ), arg( a ), argexpr( a ), arr1( a1 ), arr2( a2 ),
		cond( c ), ver1( v1 ), ver2( v2 ), condexpr( c ), arr1expr( a1 ), flags( f )
	{
	}

	NifSharedData( const QString & n, const QString & t )
		: QSharedData(), name( n ), type( t ) {}

	NifSharedData( const QString & n, const QString & t, const QString & txt )
		: QSharedData(), name( n ), type( t ), text( txt ) {}

	NifSharedData()
		: QSharedData() {}

	//! Name.
	QString name;
	//! Type.
	QString type;
	//! Template type.
	QString templ;
	//! Argument.
	QString arg;
	//! Arg as an expression.
	NifExpr argexpr;
	//! First array length.
	QString arr1;
	//! Second array length.
	QString arr2;
	//! Condition.
	QString cond;
	//! Earliest version.
	quint32 ver1 = 0;
	//! Latest version.
	quint32 ver2 = 0;
	//! Description text.
	QString text;
	//! Condition as an expression.
	NifExpr condexpr;
	//! First array length as an expression.
	NifExpr arr1expr;
	//! Version condition.
	QString vercond;
	//! Version condition as an expression.
	NifExpr verexpr;

	DataFlags flags = None;
};

Q_DECLARE_OPERATORS_FOR_FLAGS( NifSharedData::DataFlags );

//! The data and NifValue stored by a NifItem
class NifData : public NifValue
{
public:
	NifData( const QString & name, const QString & type, const QString & temp, const NifValue & val, const QString & arg,
			 const QString & arr1 = QString(), const QString & arr2 = QString(), const QString & cond = QString(),
			 quint32 ver1 = 0, quint32 ver2 = 0, NifSharedData::DataFlags flag = NifSharedData::None )
		: NifValue( val ), d( new NifSharedData( name, type, temp, arg, arr1, arr2, cond, ver1, ver2, flag ) )
	{}

	NifData( const QString & name, const QString & type = QString(), const QString & text = QString() )
		: d( new NifSharedData( name, type, text ) ) {}

	NifData()
		: d( new NifSharedData() ) {}

	//! Get the name of the data.
	inline const QString & name() const { return d->name; }
	//! Get the type of the data.
	inline const QString & type() const { return d->type; }
	//! Get the template type of the data.
	inline const QString & templ() const { return d->templ; }
	//! Get the argument of the data.
	inline const QString & arg() const { return d->arg; }
	//! Get the first array length of the data.
	inline const QString & arr1() const { return d->arr1; }
	//! Get the second array length of the data.
	inline const QString & arr2() const { return d->arr2; }
	//! Get the condition attribute of the data.
	inline const QString & cond() const { return d->cond; }
	//! Get the earliest version of the data.
	inline quint32 ver1() const { return d->ver1; }
	//! Get the latest version of the data.
	inline quint32 ver2() const { return d->ver2; }
	//! Get the text description of the data.
	inline const QString & text() const { return d->text; }
	//! Get the arg attribute of the data, as an expression.
	inline const NifExpr & argexpr() const { return d->argexpr; }
	//! Get the condition attribute of the data, as an expression.
	inline const NifExpr & condexpr() const { return d->condexpr; }
	//! Get the first array length of the data, as an expression.
	inline const NifExpr & arr1expr() const { return d->arr1expr; }
	//! Get the version condition attribute of the data.
	inline const QString & vercond() const { return d->vercond; }
	//! Get the version condition attribute of the data, as an expression.
	inline const NifExpr & verexpr() const { return d->verexpr; }

	//! Get the abstract attribute of the data.
	inline bool isAbstract() const { return d->flags & NifSharedData::Abstract; }
	//! Is the data binary. Binary means the data is being treated as one blob.
	inline bool isBinary() const { return d->flags & NifSharedData::Binary; }
	//! Is the data templated. Templated means the type is dynamic.
	inline bool isTemplated() const { return d->flags & NifSharedData::Templated; }
	//! Is the data a compound. Compound means the data type is a compound block.
	inline bool isCompound() const { return d->flags & NifSharedData::Compound; }
	//! Is the data an array. Array means the data on this row repeats.
	inline bool isArray() const { return d->flags & NifSharedData::Array; }
	//! Is the data a multi-array. Multi-array means the item's children are also arrays.
	inline bool isMultiArray() const { return d->flags & NifSharedData::MultiArray; }
	//! Is the data conditionless. Conditionless means no expression evaluation is necessary.
	inline bool isConditionless() const { return d->flags & NifSharedData::Conditionless; }
	//! Does the data's condition checks only the type of the parent block.
	inline bool hasTypeCondition() const { return d->flags & NifSharedData::TypeCondition; }
	//! Is the data a mixin. Mixin is a specialized compound which creates no nesting.
	inline bool isMixin() const { return d->flags & NifSharedData::Mixin; }
	static bool compareStrings( const QChar * s, const char * t, size_t l );
	//! Does the name match testName?
	inline bool hasName( std::string_view testName ) const
	{
		size_t	l = size_t( d->name.length() );
		return ( l == testName.length() && compareStrings( d->name.constData(), testName.data(), l ) );
	}
	//! Does the string type match testType?
	inline bool hasStrType( std::string_view testType ) const
	{
		size_t	l = size_t( d->type.length() );
		return ( l == testType.length() && compareStrings( d->type.constData(), testType.data(), l ) );
	}

	//! Sets the name of the data.
	void setName( const QString & name ) { d->name = name; }
	//! Sets the type of the data.
	void setStrType( const QString & type ) { d->type = type; }
	//! Sets the template type of the data.
	void setTempl( const QString & templ ) { d->templ = templ; }
	//! Sets the argument of the data.
	void setArg( const QString & arg )
	{
		d->arg = arg;
		d->argexpr = NifExpr( arg );
	}
	//! Sets the first array length of the data.
	void setArr1( const QString & arr1 )
	{
		d->arr1 = arr1;
		d->arr1expr = NifExpr( arr1 );
	}
	//! Sets the second array length of the data.
	void setArr2( const QString & arr2 ) { d->arr2 = arr2; }
	//! Sets the condition attribute of the data.
	void setCond( const QString & cond )
	{
		d->cond = cond;
		d->condexpr = NifExpr( cond );
	}
	//! Sets the earliest version of the data.
	void setVer1( quint32 ver1 ) { d->ver1 = ver1; }
	//! Sets the latest version of the data.
	void setVer2( quint32 ver2 ) { d->ver2 = ver2; }
	//! Sets the text description of the data.
	void setText( const QString & text ) { d->text = text; }
	//! Sets the version condition attribute of the data.
	void setVerCond( const QString & cond )
	{
		d->vercond = cond;
		d->verexpr = NifExpr( cond );
	}

	inline void setFlag( NifSharedData::DataFlags flag, bool val )
	{
		(val) ? d->flags |= flag : d->flags &= ~flag;
	}
	//! Sets the abstract attribute of the data.
	inline void setAbstract( bool flag ) { setFlag( NifSharedData::Abstract, flag ); }
	//! Sets the binary data flag. Binary means the data is being treated as one blob.
	inline void setBinary( bool flag ) { setFlag( NifSharedData::Binary, flag ); }
	//! Sets the templated data flag. Templated means the type is dynamic.
	inline void setTemplated( bool flag ) { setFlag( NifSharedData::Templated, flag ); }
	//! Sets the compound data flag. Compound means the data type is a compound block.
	inline void setIsCompound( bool flag ) { setFlag( NifSharedData::Compound, flag ); }
	//! Sets the array data flag. Array means the data on this row repeats.
	inline void setIsArray( bool flag ) { setFlag( NifSharedData::Array, flag ); }
	//! Sets the multi-array data flag. Multi-array means the item's children are also arrays.
	inline void setIsMultiArray( bool flag ) { setFlag( NifSharedData::MultiArray, flag ); }
	//! Sets the conditionless data flag. Conditionless means no expression evaluation is necessary.
	inline void setIsConditionless( bool flag ) { setFlag( NifSharedData::Conditionless, flag ); }
	//! Sets the mixin data flag. Mixin is a specialized compound which creates no nesting.
	inline void setIsMixin( bool flag ) { setFlag( NifSharedData::Mixin, flag ); }
	//! Sets the type condition data flag (does the data's condition checks only the type of the parent block).
	inline void setHasTypeCondition( bool flag ) { setFlag( NifSharedData::TypeCondition, flag ); }

	//! Gets the data's value type (NifValue::Type).
	inline NifValue::Type valueType() const { return NifValue::type(); }

protected:
	//! The internal shared data.
	QSharedDataPointer<NifSharedData> d;
};

//! A block representing a niobject in XML.
struct NifBlock
{
	//! Identifier.
	QString id;
	//! Ancestor.
	QString ancestor;
	//! Description text.
	QString text;
	//! Abstract flag.
	bool abstract = false;
	//! Data present.
	QList<NifData> types;
};

//! An item which contains NifData
class NifItem
{
public:
	NifItem() = delete;

	NifItem( BaseModel * model, NifItem * parent )
		: parentModel( model ), parentItem( parent ) {}

	NifItem( BaseModel * model, const NifData & data, NifItem * parent )
		: itemData( data ), parentModel( model ), parentItem( parent ) {}

	~NifItem()
	{
		if ( childItems ) {
			deleteChildItems();
			std::free( childItems );
		}
		if ( linkRows )
			delete[] ( linkRows - 1 );
	}

	//! Return the parent model.
	const BaseModel * model() const { return parentModel; }

	//! Return the parent model.
	BaseModel * model() { return parentModel; }

	//! Return the parent item.
	const NifItem * parent() const { return parentItem; }

	//! Return the parent item.
	NifItem * parent() { return parentItem; }

	//! Return the row that this item is at.
	int row() const
	{
		if ( rowIdx < 0 ) [[unlikely]]
			return updateRowIndex();

		return rowIdx;
	}

	/*! Allocate memory to insert child items
	 *
	 * @param e The number of items to be inserted
	 */
	void prepareInsert( int e )
	{
		if ( childItemsCapacity < ( childItemsSize + e ) )
			reserveChildItems( childItemsSize + e );
	}

	//! Get a span of child items.
	std::span< NifItem * > children()
	{
		return std::span< NifItem * >( childItems, size_t( childItemsSize ) );
	}
	std::span< const NifItem * const > children() const
	{
		return std::span< const NifItem * const >( childItems, size_t( childItemsSize ) );
	}

	//! Return the number of child items.
	int childCount() const { return childItemsSize; }

	//! Checks if the item is testAncestor itself or its child or a child of a child, etc.
	bool isDescendantOf( const NifItem * testAncestor ) const;

	//! Checks if testDescendant is this item itself or its child or a child of a child, etc.
	bool isAncestorOf( const NifItem * testDescendant ) const { return testDescendant && testDescendant->isDescendantOf( this ); }

	//! Gets the ancestry level of the item relative to testAncestor.
	// 0 - this item is testAncestor; 1 - the item is a child of testAncestor; 2 - a child of a child, etc.
	// Returns -1 if the item is not a descendant of testAncestor.
	int ancestorLevel( const NifItem * testAncestor ) const;

	//! Gets the ancestor of the item at level testLevel,
	// where levels are: 0 - this item; 1 - the item's parent; 2 - the parent of the parent, etc.
	const NifItem * ancestorAt( int testLevel ) const;
	//! Gets the ancestor of the item at level testLevel,
	// where levels are: 0 - this item; 1 - the item's parent; 2 - the parent of the parent, etc.
	NifItem * ancestorAt( int testLevel ) { return const_cast<NifItem *>( const_cast<const NifItem *>(this)->ancestorAt(testLevel) ); }

private:
	void registerChild( NifItem * item, int at );

	NifItem * unregisterChild( int at );

public:
	/*! Insert child data item
	 *
	 * @param data	The data to insert
	 * @param at	The position to insert at; append if not specified
	 * @return		An item containing the inserted data
	 */
	NifItem * insertChild( const NifData & data, int at = -1 )
	{
		NifItem * item = new NifItem( parentModel, data, this );
		registerChild( item, at );
		return item;
	}

	/*! Insert child data item with forcing a value type
	 *
	 * @param data	The data to insert
	 * @param forceVType The value type to force
	 * @param at	The position to insert at; append if not specified
	 * @return		An item containing the inserted data
	 */
	NifItem * insertChild( const NifData & data, NifValue::Type forceVType, int at = -1 )
	{
		NifItem * item = new NifItem( parentModel, data, this );
		item->changeValueType( forceVType );
		registerChild( item, at );
		return item;
	}

	/*! Insert child item
	 *
	 * @param child The data to insert
	 * @param at	The position to insert at; append if not specified
	 * @return		The row the child was inserted at
	 */
	int insertChild( NifItem * child, int at = -1 )
	{
		if ( child ) {
			child->parentItem = this;
			child->onParentItemChange();
			registerChild( child, at );

			return child->row();
		}

		return -1;
	}

	/*! Take child item at row
	 *
	 * @param row	The row to take the item from
	 * @return		The child item that was removed
	 */
	NifItem * takeChild( int row )
	{
		NifItem * item = unregisterChild( row );
		if ( item ) {
			item->parentItem = nullptr;
			item->invalidateRow();
		}
		return item;
	}

	/*! Remove child item at row
	 *
	 * @param row The row to remove the item from
	 */
	void removeChild( int row )
	{
		delete unregisterChild( row );
	}

	/*! Remove several child items
	 *
	 * @param row	The row to start from
	 * @param count The number of rows to delete
	 */
	void removeChildren( int row, int count );

	//! Return the child item at the specified row
	NifItem * child( int row )
	{
		if ( (unsigned int) row >= (unsigned int) childItemsSize ) [[unlikely]]
			return nullptr;
		return childItems[row];
	}

	//! Return the child item at the specified row
	const NifItem * child( int row ) const
	{
		if ( (unsigned int) row >= (unsigned int) childItemsSize ) [[unlikely]]
			return nullptr;
		return childItems[row];
	}

	//! Remove all child items
	void killChildren()
	{
		if ( childItemsSize > 0 )
			deleteChildItems();

		if ( hasChildLinks() ) {
			linkRowsSize = 0;
			unregisterInParentLinkCache();
		}
	}

	std::span< const int > getLinkRows() const { return std::span< const int >( linkRows, linkRowsSize ); }

	//! Cached result of cond expression
	bool condition() const { return conditionStatus == 1; }

	//! Cached result of vercond expression
	bool versionCondition() const { return vercondStatus == 1; }

	//! Has the condition been cached
	bool isConditionCached() const { return conditionStatus != -1; }

	//! Has the version condition been cached
	bool isVersionConditionCached() const { return vercondStatus != -1; }

	//! Cache the cond expression
	void setCondition( bool status ) const { conditionStatus = status; }

	//! Cache the vercond expression
	void setVersionCondition( bool status ) const { vercondStatus = status; }

	//! Invalidate the cached cond expression in the item and its children.
	void invalidateCondition()
	{
		if ( isConditionCached() ) {
			conditionStatus = -1;

			for ( auto c : children() )
				c->invalidateCondition();
		}
	}

	//! Invalidate the cached vercond expression in the item and its children.
	void invalidateVersionCondition()
	{
		if ( isVersionConditionCached() ) {
			vercondStatus = -1;

			for ( auto c : children() )
				c->invalidateVersionCondition();
		}
	}

private:
	//! Invalidate the cached at index
	void invalidateRow() { rowIdx = -1; }

	size_t findLinkRow( int n ) const;
	void insertLinkRow( int n );
	void registerInParentLinkCache();
	void unregisterInParentLinkCache();
	void removeLinkRow( int n );
	void updateChildRows( int iStartChild = 0 );
	void updateLinkRows( int iStartChild = 0 );
	int updateRowIndex() const;
	void reserveChildItems( int n );
	void deleteChildItems();

	void onParentItemChange();

public:
	//! Does the item have any children of link type?
	bool hasChildLinks() const { return bool( linkRowsSize ); }

	//! Return the value of the item data (const version)
	inline const NifValue & value() const { return itemData; }
	//! Return the value of the item data
	inline NifValue & value() { return itemData; }

	//! Return the name of the data
	inline const QString & name() const { return itemData.name(); }
	//! Return the type of the data (the "type" attribute in the XML file).
	inline const QString & strType() const { return itemData.type(); }
	//! Return the template type of the data
	inline const QString & templ() const { return itemData.templ(); }
	//! Return the argument attribute of the data
	inline const QString & arg()  const { return itemData.arg();  }
	//! Return the first array length of the data
	inline const QString & arr1() const { return itemData.arr1(); }
	//! Return the second array length of the data
	inline const QString & arr2() const { return itemData.arr2(); }
	//! Return the condition attribute of the data
	inline const QString & cond() const { return itemData.cond(); }
	//! Return the earliest version attribute of the data
	inline quint32 ver1() const { return itemData.ver1(); }
	//! Return the latest version attribute of the data
	inline quint32 ver2() const { return itemData.ver2(); }
	//! Return the description text of the data
	inline const QString & text() const { return itemData.text(); }

	//! Return the condition attribute of the data, as an expression
	inline const NifExpr & argexpr() const { return itemData.argexpr(); }
	//! Return the condition attribute of the data, as an expression
	inline const NifExpr & condexpr() const { return itemData.condexpr(); }
	//! Return the arr1 attribute of the data, as an expression
	inline const NifExpr & arr1expr() const { return itemData.arr1expr(); }
	//! Return the version condition attribute of the data
	inline const QString & vercond() const { return itemData.vercond(); }
	//! Return the version condition attribute of the data, as an expression
	inline const NifExpr & verexpr() const { return itemData.verexpr(); }

	//! Return the abstract attribute of the data.
	inline bool isAbstract() const { return itemData.isAbstract(); }
	//! Is the item data binary. Binary means the data is being treated as one blob.
	inline bool isBinary() const { return itemData.isBinary(); }
	//! Is the item data templated. Templated means the type is dynamic.
	inline bool isTemplated() const { return itemData.isTemplated(); }
	//! Is the item data a compound. Compound means the data type is a compound block.
	inline bool isCompound() const { return itemData.isCompound(); }
	//! Is the item data an array. Array means the data on this row repeats.
	inline bool isArray() const { return itemData.isArray(); }
	//! Is the item data a multi-array. Multi-array means the item's children are also arrays.
	inline bool isMultiArray() const { return itemData.isMultiArray(); }
	//! Is the item data conditionless. Conditionless means no expression evaluation is necessary.
	inline bool isConditionless() const { return itemData.isConditionless(); }
	//! Does the items data's condition checks only the type of the parent block.
	inline bool hasTypeCondition() const { return itemData.hasTypeCondition(); }

	//! Does the item's name match testName?
	inline bool hasName( const QString & testName ) const { return itemData.name() == testName; }
	//! Does the item's name match testName?
	// item->hasName("Foo") is much faster than item->name() == "Foo"
	inline bool hasName( std::string_view testName ) const { return itemData.hasName( testName ); }
	inline bool hasName( const char * testName ) const { return itemData.hasName( std::string_view(testName) ); }

	//! Does the item's string type match testType?
	inline bool hasStrType( const QString & testType ) const { return itemData.type() == testType; }
	//! Does the item's string type match testType?
	// item->hasStrType("Foo") is much faster than item->strType() == "Foo"
	inline bool hasStrType( std::string_view testType ) const { return itemData.hasStrType( testType ); }
	inline bool hasStrType( const char * testType ) const { return itemData.hasStrType( std::string_view(testType) ); }

	//! Set the name
	inline void setName( const QString & name ) {   itemData.setName( name );   }
	//! Set the string type
	inline void setStrType( const QString & type ) { itemData.setStrType( type ); }
	//! Set the template type
	inline void setTempl( const QString & temp ) { itemData.setTempl( temp ); }
	//! Set the argument attribute
	inline void setArg( const QString & arg )   {   itemData.setArg( arg );     }
	//! Set the first array length
	inline void setArr1( const QString & arr1 ) {   itemData.setArr1( arr1 );   }
	//! Set the second array length
	inline void setArr2( const QString & arr2 ) {   itemData.setArr2( arr2 );   }
	//! Set the condition attribute
	inline void setCond( const QString & cond ) {   itemData.setCond( cond );   }

	//! Set the earliest version attribute
	inline void setVer1( int v1 ) {   itemData.setVer1( v1 );     }
	//! Set the latest version attribute
	inline void setVer2( int v2 ) {   itemData.setVer2( v2 );     }

	//! Set the description text
	inline void setText( const QString & text )    {   itemData.setText( text );    }
	//! Set the version condition attribute
	inline void setVerCond( const QString & cond ) {   itemData.setVerCond( cond ); }

	inline void setIsConditionless( bool flag ) { itemData.setIsConditionless( flag ); }

	//! Determine if this item is present in the specified version
	inline bool evalVersion( quint32 v ) const
	{
		return ( ( ver1() == 0 || ver1() <= v ) && ( ver2() == 0 || v <= ver2() ) );
	}

	//! Gets the item's value type (NifValue::Type).
	inline NifValue::Type valueType() const { return itemData.valueType(); }
	//! Check if the item's value is of testType.
	inline bool hasValueType( NifValue::Type testType ) const { return valueType() == testType; }
	//! Check if the type of the item's value is a color type (Color3 or Color4 in xml).
	inline bool isColor() const { return itemData.isColor(); }
	//! Check if the type of the item's value is a count.
	inline bool isCount() const { return itemData.isCount(); }
	//! Check if the type of the item's value is a flag type (Flags in xml).
	inline bool isFlags() const { return itemData.isFlags(); }
	//! Check if the type of the item's value is a float type (Float in xml).
	inline bool isFloat() const { return itemData.isFloat(); }
	//! Check if the type of the item's value is of a link type (Ref or Ptr in xml).
	inline bool isLink() const { return itemData.isLink(); }
	//! Check if the type of the item's value is a 3x3 matrix type (Matrix33 in xml).
	inline bool isMatrix() const { return itemData.isMatrix(); }
	//! Check if the type of the item's value is a 4x4 matrix type (Matrix44 in xml).
	inline bool isMatrix4() const { return itemData.isMatrix4(); }
	//! Check if the type of the item's value is a byte matrix.
	inline bool isByteMatrix() const { return itemData.isByteMatrix(); }
	//! Check if the type of the item's value is a quaternion type.
	inline bool isQuat() const { return itemData.isQuat(); }
	//! Check if the type of the item's value is a string type.
	inline bool isString() const { return itemData.isString(); }
	//! Check if the type of the item's value is a Vector2.
	inline bool isVector2() const { return itemData.isVector2(); }
	//! Check if the type of the item's value is a HalfVector2.
	inline bool isHalfVector2() const { return itemData.isHalfVector2(); }
	//! Check if the type of the item's value is a Vector3.
	inline bool isVector3() const { return itemData.isVector3(); }
	//! Check if the type of the item's value is a Vector3 used as a color.
	bool isVector3Color() const;
	//! Check if the type of the item's value is a Half Vector3.
	inline bool isHalfVector3() const { return itemData.isHalfVector3(); }
	//! Check if the type of the item's value is a Byte Vector3.
	inline bool isByteVector3() const { return itemData.isByteVector3(); }
	//! Check if the type of the item's value is a Vector4.
	inline bool isVector4() const { return itemData.isVector4(); }
	//! Check if the type of the item's value is a triangle type.
	inline bool isTriangle() const { return itemData.isTriangle(); }
	//! Check if the type of the item's value is a byte array.
	inline bool isByteArray() const { return itemData.isByteArray(); }
	//! Check if the type of the item's value is a File Version.
	inline bool isFileVersion() const { return itemData.isFileVersion(); }

	//! Return the item's value as a count if applicable.
	inline quint64 getCountValue() const { return itemData.toCount( parentModel, this ); }
	//! Return the item's value as a float if applicable.
	inline float getFloatValue() const { return itemData.toFloat( parentModel, this ); }
	//! Return the item's value as a link if applicable.
	inline qint32 getLinkValue() const { return itemData.toLink( parentModel, this ); }
	//! Return the item's value as a QColor if applicable.
	inline QColor getColorValue() const { return itemData.toColor( parentModel, this ); }
	//! Return the item's value as a file version if applicable.
	inline quint32 getFileVersionValue() const { return itemData.toFileVersion( parentModel, this ); }

	//! Return a string which represents the item's value.
	inline QString getValueAsString() const { return itemData.toString(); }
	//! Return the item's value as a QVariant.
	inline QVariant getValueAsVariant() const { return itemData.toVariant(); }

	//! Get the value of the item
	template <typename T> inline T get() const { return itemData.get<T>( parentModel, this ); }
	//! Get the value of an item if it's not nullptr
	template <typename T> static inline T get( const NifItem * item ) { return item ? item->get<T>() : T(); }

	//! Get the child items' values as an array.
	template <typename T> QVector<T> getArray() const
	{
		QVector<T> array;
		qsizetype nSize = childItemsSize;
		if ( nSize > 0 ) {
			array.reserve( nSize );
			for ( auto child : children() )
				array.append( child->get<T>() );
		}
		return array;
	}
	//! Get the values of the child items of arrayRoot as an array if arrayRoot is not nullptr.
	template <typename T> static inline QVector<T> getArray( const NifItem * arrayRoot )
	{
		return arrayRoot ? arrayRoot->getArray<T>() : QVector<T>();
	}

	//! Set the value of the item.
	template <typename T> inline bool set( const T & v ) { return itemData.set<T>( v, parentModel, this ); }
	//! Set the value of an item if it's not nullptr.
	template <typename T> static inline bool set( NifItem * item, const T & v ) { return item ? item->set<T>(v) : false; }

	//! Set the child items' values from an array.
	template <typename T> bool setArray( const QVector<T> & array )
	{
		qsizetype nSize = childItemsSize;
		if ( array.size() != nSize ) {
			reportError(
				__func__,
				QString( "The input QVector's size (%1) does not match the array's size (%2)." ).arg( array.size() ).arg( nSize )
			);
			return false;
		}
		for ( qsizetype i = 0; i < nSize; i++ ) {
			if ( !childItems[i]->set<T>( array.at( i ) ) )
				return false;
		}

		return true;
	}

	//! Set the child items' values from a single value.
	template <typename T> bool fillArray( const T & val )
	{
		for ( auto child : children() ) {
			if ( !child->set<T>( val ) )
				return false;
		}

		return true;
	}

	//! Set the item's value to a count.
	inline bool setCountValue( quint64 c ) { return itemData.setCount( c, parentModel, this ); }
	//! Set the item's value to a float.
	inline bool setFloatValue( float f ) { return itemData.setFloat( f, parentModel, this ); }
	//! Set the item's value to a link (block number).
	inline bool setLinkValue( qint32 link ) { return itemData.setLink( link, parentModel, this ); }
	//! Set the item's value to a file version.
	inline bool setFileVersionValue( quint32 v ) { return itemData.setFileVersion( v, parentModel, this ); }

	//! Set the item's value from a string.
	inline bool setValueFromString( const QString & str ) { return itemData.setFromString( str, parentModel, this ); }
	//! Set the item's value from a QVariant.
	inline bool setValueFromVariant( const QVariant & v ) { return itemData.setFromVariant( v ); }

	//! Change the type of value stored.
	inline void changeValueType( NifValue::Type t ) { itemData.changeType( t ); }

	//! Return string representation ("path") of an item within its model (e.g., "NiTriShape [0]\Vertex Data [3]\Vertex colors").
	QString repr() const;

	void reportError( const QString & msg ) const;
	void reportError( const QString & funcName, const QString & msg ) const;

private:
	//! The data held by the item
	NifData itemData;
	BaseModel * parentModel = nullptr;
	//! The parent of this item
	NifItem * parentItem = nullptr;
	//! The child items
	NifItem ** childItems = nullptr;
	int childItemsSize = 0;
	int childItemsCapacity = 0;

	//! Item's row index, -1 is not cached, otherwise 0+
	mutable int rowIdx = -1;
	//! Item's condition status, -1 is not cached, otherwise 0/1
	mutable signed char conditionStatus = -1;
	//! Item's vercond status, -1 is not cached, otherwise 0/1
	mutable signed char vercondStatus = -1;

	//! Rows which are links or have links under them at any level
	unsigned short linkRowsSize = 0;
	int * linkRows = nullptr;	// linkRows[-1] is the capacity of the array
};

#endif
