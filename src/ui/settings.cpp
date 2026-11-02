// settings.cpp -- persistent user settings.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "settings.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStringList>

namespace {

// Colors are stored as 0x00BBGGRR, the Windows COLORREF layout, because on
// Windows these values share a registry key with the original program.
quint32 toColorRef(const QColor &c)
{
	return (quint32)c.red() | ((quint32)c.green()<<8) | ((quint32)c.blue()<<16);
}

QColor fromColorRef(quint32 v)
{
	return QColor((int)(v&0xff), (int)((v>>8)&0xff), (int)((v>>16)&0xff));
}

} // namespace

AppSettings &appSettings()
{
	static AppSettings s;
	return s;
}

void AppSettings::load()
{
	QSettings s;

	compressionLevel = s.value(QStringLiteral("deflate_lvl"), -1).toInt();
	autoOpenViewer = s.value(QStringLiteral("show_viewer"), 0).toInt() != 0;
	useGamma = s.value(QStringLiteral("use_gamma"), 1).toInt() != 0;
	useCustomBg = s.value(QStringLiteral("use_custombg"), 1).toInt() != 0;
	useImageBg = s.value(QStringLiteral("use_imagebg"), 1).toInt() != 0;
	windowBg = s.value(QStringLiteral("windowbg"), (int)WBG_SAMEASIMAGE).toInt();
	zoom = s.value(QStringLiteral("zoom"), (int)VS_FIT).toInt();
	customBg = fromColorRef(s.value(QStringLiteral("bgcolor"), 0x808080).toUInt());

	if(zoom<VS_FIT || zoom>VS_8) zoom = VS_FIT;
	if(windowBg<WBG_SAMEASIMAGE || windowBg>WBG_CUSTOM) windowBg = WBG_SAMEASIMAGE;

	for(int i=0; i<kNumTools; i++) {
		tools[(size_t)i].name = s.value(QStringLiteral("tool%1n").arg(i), i==0 ? QStringLiteral("Default viewer") : QString()).toString();
		tools[(size_t)i].cmd = s.value(QStringLiteral("tool%1c").arg(i)).toString();
		tools[(size_t)i].params = s.value(QStringLiteral("tool%1p").arg(i)).toString();
	}

	mainGeometry = s.value(QStringLiteral("qt/main_geometry")).toByteArray();
	headerState = s.value(QStringLiteral("qt/header_state")).toByteArray();
	textGeometry = s.value(QStringLiteral("qt/text_geometry")).toByteArray();
	paletteGeometry = s.value(QStringLiteral("qt/palette_geometry")).toByteArray();
	viewerGeometry = s.value(QStringLiteral("qt/viewer_geometry")).toByteArray();

	const QStringList custom = s.value(QStringLiteral("qt/custom_colors")).toStringList();
	for(int i=0; i<custom.size() && i<QColorDialog::customCount(); i++) {
		QColorDialog::setCustomColor(i, QColor(custom.at(i)));
	}
}

void AppSettings::save() const
{
	QSettings s;

	s.setValue(QStringLiteral("deflate_lvl"), compressionLevel);
	s.setValue(QStringLiteral("show_viewer"), autoOpenViewer ? 1 : 0);
	s.setValue(QStringLiteral("use_gamma"), useGamma ? 1 : 0);
	s.setValue(QStringLiteral("use_custombg"), useCustomBg ? 1 : 0);
	s.setValue(QStringLiteral("use_imagebg"), useImageBg ? 1 : 0);
	s.setValue(QStringLiteral("windowbg"), windowBg);
	s.setValue(QStringLiteral("zoom"), zoom);
	s.setValue(QStringLiteral("bgcolor"), toColorRef(customBg));

	for(int i=0; i<kNumTools; i++) {
		s.setValue(QStringLiteral("tool%1n").arg(i), tools[(size_t)i].name);
		s.setValue(QStringLiteral("tool%1c").arg(i), tools[(size_t)i].cmd);
		s.setValue(QStringLiteral("tool%1p").arg(i), tools[(size_t)i].params);
	}

	s.setValue(QStringLiteral("qt/main_geometry"), mainGeometry);
	s.setValue(QStringLiteral("qt/header_state"), headerState);
	s.setValue(QStringLiteral("qt/text_geometry"), textGeometry);
	s.setValue(QStringLiteral("qt/palette_geometry"), paletteGeometry);
	s.setValue(QStringLiteral("qt/viewer_geometry"), viewerGeometry);

	QStringList custom;
	for(int i=0; i<QColorDialog::customCount(); i++) {
		custom << QColorDialog::customColor(i).name();
	}
	s.setValue(QStringLiteral("qt/custom_colors"), custom);
}

#ifdef Q_OS_WIN

namespace {

const char *const kExts[3] = { ".png", ".mng", ".jng" };
const char *const kDefaultNames[3] = { "png_auto_file", "mng_auto_file", "jng_auto_file" };

QString classesPath(const QString &sub)
{
	return QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\") + sub;
}

// Returns the logical file type name for an extension, or an empty string.
// A UserChoice key takes priority over the Classes key.
QString regNameForExt(const QString &ext)
{
	QSettings choice(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\")
		+ ext + QStringLiteral("\\UserChoice"), QSettings::NativeFormat);
	const QString progid = choice.value(QStringLiteral("Progid")).toString();
	if(!progid.isEmpty()) return progid;

	QSettings classes(classesPath(ext), QSettings::NativeFormat);
	return classes.value(QStringLiteral("Default")).toString();
}

} // namespace

bool shellMenuSupported() { return true; }

bool isShellMenuSet()
{
	const QString name = regNameForExt(QStringLiteral(".png"));
	if(name.isEmpty()) return false;
	QSettings s(classesPath(name + QStringLiteral("\\shell\\tweakpng")), QSettings::NativeFormat);
	return s.contains(QStringLiteral("Default"));
}

void setShellMenu(bool enable)
{
	const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

	for(int i=0; i<3; i++) {
		const QString ext = QLatin1String(kExts[i]);
		QString name = regNameForExt(ext);

		if(enable) {
			if(name.isEmpty()) {
				name = QLatin1String(kDefaultNames[i]);
				QSettings extKey(classesPath(ext), QSettings::NativeFormat);
				extKey.setValue(QStringLiteral("Default"), name);
			}
			QSettings verb(classesPath(name + QStringLiteral("\\shell\\tweakpng")), QSettings::NativeFormat);
			verb.setValue(QStringLiteral("Default"), QStringLiteral("TweakPNG"));
			QSettings command(classesPath(name + QStringLiteral("\\shell\\tweakpng\\command")), QSettings::NativeFormat);
			command.setValue(QStringLiteral("Default"), QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1")));
		}
		else if(!name.isEmpty()) {
			QSettings shell(classesPath(name + QStringLiteral("\\shell")), QSettings::NativeFormat);
			shell.remove(QStringLiteral("tweakpng"));
		}
	}
}

#else

bool shellMenuSupported() { return false; }
bool isShellMenuSet() { return false; }
void setShellMenu(bool) {}

#endif
