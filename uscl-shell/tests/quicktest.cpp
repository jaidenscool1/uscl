#include <QtQuickTest/quicktest.h>
#include <QFontDatabase>
#include <QGuiApplication>

class ThemeTestSetup : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        for (const char* font : {"NotoSans-Regular.ttf", "NotoSans-Light.ttf", "Roboto-Thin.ttf"})
            QFontDatabase::addApplicationFont(QString(":/qt/qml/Uscl/fonts/") + font);
        QGuiApplication::setFont(QFont("Noto Sans"));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(uscl_themes, ThemeTestSetup)
#include "quicktest.moc"
