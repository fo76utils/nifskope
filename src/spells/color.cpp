#include "spellbook.h"

#include "ui/widgets/colorwheel.h"


// Brief description is deliberately not autolinked to class Spell
/*! \file color.cpp
 * \brief Color editing spells (spChooseColor)
 *
 * All classes here inherit from the Spell class.
 */

//! Choose a color using a ColorWheel
class spChooseColor final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Choose" ); }
	QString page() const override final { return Spell::tr( "Color" ); }
	QIcon icon() const override final { return ColorWheel::getIcon(); }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return nif->getValue( index ).isColor();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		auto typ = nif->getValue( index ).type();
		if ( typ == NifValue::tColor3 ) {
			nif->set<Color3>( index, ColorWheel::choose( nif->get<Color3>( index ) ) );
		} else if ( typ == NifValue::tColor4 ) {
			nif->set<Color4>( index, ColorWheel::choose( nif->get<Color4>( index ) ) );
		} else if ( typ == NifValue::tByteColor4 ) {
			auto col = ColorWheel::choose( nif->get<ByteColor4>( index ) );
			nif->set<ByteColor4>( index, *static_cast<ByteColor4 *>(&col) );
		} else if ( typ == NifValue::tByteColor4BGRA ) {
			auto col = ColorWheel::choose( nif->get<ByteColor4BGRA>( index ) );
			nif->set<ByteColor4BGRA>( index, *static_cast<ByteColor4BGRA *>(&col) );
		}


		return index;
	}
};

REGISTER_SPELL( spChooseColor )

//! Set an array of Colors
class spSetAllColor final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Set All" ); }
	QString page() const override final { return Spell::tr( "Color" ); }
	QIcon icon() const override final { return ColorWheel::getIcon(); }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !nif->isArray( index ) )
			return false;
		const NifItem * i = nif->getItem( nif->getIndex( index, 0 ), false );
		if ( !i )
			return false;
		if ( i->isColor() )
			return true;
		if ( !( i->hasStrType( "BSVertexData" ) || i->hasStrType( "BSVertexDataSSE" ) ) )
			return false;
		return nif->getIndex( i, "Vertex Colors" ).isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		NifItem * colorItem = nif->getItem( ( nif->isArray( index ) ? nif->getIndex( index, 0 ) : index ), false );
		if ( !colorItem )
			return index;

		if ( colorItem->isColor() ) {
			auto typ = colorItem->valueType();
			switch ( typ ) {
			case NifValue::tColor3:
				nif->fillArray<Color3>( index, ColorWheel::choose( nif->get<Color3>( colorItem ) ) );
				break;
			case NifValue::tColor4:
				nif->fillArray<Color4>( index, ColorWheel::choose( nif->get<Color4>( colorItem ) ) );
				break;
			case NifValue::tByteColor4BGRA:
				nif->fillArray<ByteColor4BGRA>( index,
												FloatVector4( ColorWheel::choose( nif->get<Color4>( colorItem ) ) ) );
				break;
			default:
				break;
			}
		} else if ( QModelIndex colorIdx = nif->getIndex( colorItem, "Vertex Colors" ); colorIdx.isValid() ) {
			FloatVector4 c = FloatVector4( ColorWheel::choose( nif->get<Color4>( colorIdx ) ) );
			nif->setState( BaseModel::Processing );
			int n = nif->rowCount( index );
			for ( int i = 0; i < n; i++ ) {
				if ( auto v = nif->getIndex( index, i ); v.isValid() ) {
					if ( colorIdx = nif->getIndex( v, "Vertex Colors" ); colorIdx.isValid() )
						nif->set<ByteColor4>( colorIdx, c );
				}
			}
			nif->restoreState();
		}

		return index;
	}
};

REGISTER_SPELL( spSetAllColor )
