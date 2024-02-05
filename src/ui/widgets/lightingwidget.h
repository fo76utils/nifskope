#ifndef LIGHTINGWIDGET_H
#define LIGHTINGWIDGET_H

#include <QWidget>
#include <QAction>

#include <memory>

class GLView;

namespace Ui {
class LightingWidget;
}

class LightingWidget : public QWidget
{
	Q_OBJECT

public:
	LightingWidget( GLView * ogl, QWidget * parent = nullptr);
	~LightingWidget();

	void setDefaults();
	void setActions( QVector<QAction *> actions );

private:
	std::unique_ptr<Ui::LightingWidget> ui;

	enum
	{
		BRIGHT = 1440,
		POS = 720,

		DirMin = 0,
		DirMax = BRIGHT,
		AmbientMin = 0,
		AmbientMax = BRIGHT,
		DeclinationMin = -POS,
		DeclinationMax = POS,
		PlanarAngleMin = -POS,
		PlanarAngleMax = POS,

		DirDefault = DirMax / 2,
		AmbientDefault = AmbientMax / 2,
		DeclinationDefault = (DeclinationMax + DeclinationMin),
		PlanarAngleDefault = (PlanarAngleMax + PlanarAngleMin),

		LightLevelMin = 0,
		LightLevelMax = BRIGHT,
		LightLevelDefault = POS,
		LightColorMin = 0,
		LightColorMax = BRIGHT,
		LightColorDefault = POS,
		ToneMappingMin = 0,
		ToneMappingMax = BRIGHT,
		ToneMappingDefault = POS
	};
};

#endif // LIGHTINGWIDGET_H
