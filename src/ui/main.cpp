// main.cpp -- part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "mainwindow.h"
#include "settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QFileOpenEvent>

namespace {

// macOS passes a double-clicked document as an event, not as an argument.
class TweakPngApplication : public QApplication {
public:
	TweakPngApplication(int &argc, char **argv) : QApplication(argc, argv) {}

	MainWindow *window = nullptr;
	QString pendingFile;

protected:
	bool event(QEvent *e) override
	{
		if(e->type()==QEvent::FileOpen) {
			const QString fn = static_cast<QFileOpenEvent*>(e)->file();
			if(window) window->openFileIfAllowed(fn);
			else pendingFile = fn;
			return true;
		}
		return QApplication::event(e);
	}
};

} // namespace

int main(int argc, char **argv)
{
	TweakPngApplication app(argc, argv);

	// On Windows, this is the registry key the original program used, which
	// carries over its configured tools and preferences.
#ifdef Q_OS_WIN
	QCoreApplication::setOrganizationName(QStringLiteral("Generic"));
#else
	QCoreApplication::setOrganizationName(QStringLiteral("TweakPNG"));
#endif
	QCoreApplication::setApplicationName(QStringLiteral("TweakPNG"));
	QCoreApplication::setApplicationVersion(QStringLiteral(TWEAKPNG_VERSION_STRING));
	QApplication::setWindowIcon(QIcon(QStringLiteral(":/tweakpng.png")));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("A low-level PNG image file manipulation utility"));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A PNG, MNG or JNG file to open."));
	parser.process(app);

	appSettings().load();
	twpng::setCompressionLevel(appSettings().compressionLevel);

	MainWindow window;
	app.window = &window;
	window.show();

	const QStringList files = parser.positionalArguments();
	if(!files.isEmpty()) window.openFile(QFileInfo(files.first()).absoluteFilePath());
	else if(!app.pendingFile.isEmpty()) window.openFile(app.pendingFile);

	window.startupViewer();

	return app.exec();
}
