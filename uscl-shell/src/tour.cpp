// uscl --qml-tour <folder>: a screenshot tour of every menu in every theme, for reviewing the UI by eye.
// Runs in the smoke test's scratch settings (main.cpp), so nothing the tour does reaches the player's setup.
// Each step runs a bit of JavaScript in Main.qml, waits for the animations, saves <theme>-<NN>-<step>.png and
// reports QML warnings under the step that caused them.
#include "tour.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <memory>

namespace
{
	QString g_step;
	int g_warnings = 0;

	void onMessage(QtMsgType type, const QMessageLogContext&, const QString& msg)
	{
		if (type == QtDebugMsg || msg.contains("QSoundEffect") || msg.contains("qt.multimedia"))
			return;
		g_warnings++;
		std::fprintf(stderr, "TOUR WARN [%s] %s\n", qPrintable(g_step), qPrintable(msg));
	}

	struct Step
	{
		QString name, js;
		int wait = 450;
	};

	QList<Step> steps(const QString& theme)
	{
		const QString reset = "dialog.close(); options.close(); profileCard.close(); if (prompt.open) prompt.close(); closePages(); dashboard.guideOpen = false; ";
		const bool ps3 = theme == "ps3";
		QList<Step> s;
		s << Step{"home", reset + (ps3 ? "xmb.cat = 3" : "dashboard.reset()"), 900}
		  << Step{"home-right", "input('right'); input('right')"}
		  << Step{"home-up", "input('up')"}
		  << Step{"home-down", reset + (ps3 ? "xmb.cat = 3; " : "dashboard.reset(); ") + "input('down')"}
		  << Step{"home-quick", reset + (ps3 ? "" : "dashboard.reset(); ") + "input('home')"};
		if (ps3)
			for (int c = 0; c <= 5; c++)
				s << Step{QString("xmb-column%1").arg(c), reset + QString("xmb.cat = %1").arg(c), c == 5 ? 2500 : 450};
		for (const QString& k : {"library", "settings", "profile", "online", "party", "friends", "updates", "add", "trophies"})
			s << Step{"section-" + QString(k), reset + "openConsoleSection('" + k + "')", QString(k) == "friends" ? 3000 : 500};
		s << Step{"library-options", reset + "openConsoleSection('library'); input('options')"}
		  << Step{"library-info", "options.close(); input('info')"}
		  << Step{"library-filters", reset + "openConsoleSection('library'); input('left'); input('down')"}
		  << Step{"library-sort", reset + "openConsoleSection('library'); input('up'); input('accept')"};
		for (int i = 0; i <= 10; i++)
			s << Step{QString("settings-%1").arg(i, 2, 10, QChar('0')), reset + QString("openConsoleSection('settings'); cats[1].items[%1].action()").arg(i), 500};
		s << Step{"settings-theme-choice", reset + "openConsoleSection('settings'); cats[1].items[5].action(); input('accept')"}
		  << Step{"friend-options", reset + "openConsoleSection('friends'); input('accept')", 3000}
		  << Step{"profile-card", reset + "profileCard.show(myPerson(), 'uscl'); profileCard.input('down')", 500}
		  << Step{"profile-card-steam", "profileCard.input('r1')", 500}
		  << Step{"trophy-list", reset + "openTrophies({ serial: 'TOUR', title: 'Tour Game', console: 'Wii' })"}
		  << Step{"trophy-toast", reset + "stageToast.show({ name: 'Tour Trophy', grade: 'gold', game: 'Tour Game' })", 700}
		  << Step{"notice", reset + "notice.show('A notice from the tour', ic('it_info'))", 500}
		  << Step{"guide", reset + "guide()"}
		  << Step{"confirm", reset + "dialog.confirm('Turn Off System', 'Quit USCL?', function() {})"}
		  << Step{"text-prompt", reset + "typeName()"}
		  << Step{"end", reset + "", 6000};   // the toast finishes before the next theme
		return s;
	}
}

void startTour(QObject* root, const QString& dir)
{
	QDir().mkpath(dir);
	qInstallMessageHandler(onMessage);
	auto* window = qobject_cast<QQuickWindow*>(root);
	static const QStringList themes = {"ps3", "ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"};
	auto plan = std::make_shared<QList<std::pair<QString, Step>>>();
	for (const QString& t : themes)
	{
		plan->append({t, Step{"theme", "prefs.theme = '" + t + "'", 600}});
		for (const Step& s : steps(t))
			plan->append({t, s});
	}
	auto index = std::make_shared<int>(0);
	auto run = std::make_shared<std::function<void()>>();
	*run = [=] {
		if (*index >= plan->size())
		{
			std::fprintf(stderr, "TOUR DONE %d warnings\n", g_warnings);
			QCoreApplication::exit(g_warnings ? 1 : 0);
			return;
		}
		const auto& [theme, step] = (*plan)[*index];
		g_step = theme + "/" + step.name;
		QQmlExpression expr(qmlContext(root), root, step.js);
		expr.evaluate();
		if (expr.hasError())
		{
			g_warnings++;
			std::fprintf(stderr, "TOUR ERROR [%s] %s\n", qPrintable(g_step), qPrintable(expr.error().toString()));
		}
		const int n = *index;
		QTimer::singleShot(step.wait, root, [=] {
			if (step.name != "theme" && step.name != "end")
				window->grabWindow().save(QString("%1/%2-%3-%4.png").arg(dir, theme).arg(n, 3, 10, QChar('0')).arg(step.name));
			++*index;
			(*run)();
		});
	};
	QTimer::singleShot(1200, root, [run] { (*run)(); });
}
