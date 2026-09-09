#ifndef SETTINGSSANITIZE_H
#define SETTINGSSANITIZE_H

#include "settingspane.h"
#include "autosanitize.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;

class SettingsSanitize final : public SettingsPane
{
	Q_OBJECT

public:
	explicit SettingsSanitize( QWidget * parent = nullptr );
	void read() override;
	void write() override;
	void setDefault() override;
	bool saveChanges();

private:
	void refreshRules();
	QString currentKey( bool derived ) const;

	AutoSanitizePolicy policy;
	QString path;
	QByteArray loadedContents;
	QComboBox * operation;
	QComboBox * scope;
	QComboBox * blockType;
	QCheckBox * includeDerived;
	QListWidget * exclusions;
	QLabel * configLocation;
	QPlainTextEdit * diagnostics;
};

#endif // SETTINGSSANITIZE_H
