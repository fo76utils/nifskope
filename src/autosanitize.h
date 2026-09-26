#ifndef AUTOSANITIZE_H
#define AUTOSANITIZE_H

#include <QMap>
#include <QModelIndex>
#include <QStringList>

class NifModel;

//! Per-operation exclusions for the automatic sanitize pipeline.
class AutoSanitizePolicy
{
public:
	enum Operation { FixNames, ReorderLinks, CollapseLinks, AdjustTextures, FixGeometryData, OperationCount };

	static QString operationId( Operation operation );
	static QString operationName( Operation operation );
	//! Empty scope means all games. Other scopes use stable, untranslated identifiers.
	static QStringList scopes();
	static QString scopeName( const QString & scope );
	static QString configPath();
	static QString ruleKey( Operation operation, const QString & scope, bool derived );

	void load( const QString & path );
	bool save( const QString & path, QString & error ) const;
	bool excludes( Operation operation, const NifModel * nif, const QModelIndex & block ) const;
	static bool protectsCamera( const NifModel * nif, const QModelIndex & block );

	// Retain unknown types for editing and forward compatibility; they never match a block.
	QMap<QString, QStringList> rules;
	QStringList diagnostics;

private:
	bool formatError = false;
};

#endif // AUTOSANITIZE_H
