// settings.h -- persistent user settings.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#ifndef TWEAKPNG_UI_SETTINGS_H
#define TWEAKPNG_UI_SETTINGS_H

#include <QByteArray>
#include <QColor>
#include <QString>

#include <array>

const int kNumTools = 6;
const int kMaxToolName = 79;
const int kMaxToolCmd = 127;
const int kMaxToolParams = 79;

struct ToolConfig {
	QString name;
	QString cmd;
	QString params;
};

enum ViewerZoom { VS_FIT = 1, VS_1_8, VS_1_4, VS_1_2, VS_1, VS_2, VS_4, VS_8 };
enum WindowBackground { WBG_SAMEASIMAGE = 0, WBG_SYSDEFAULT = 1, WBG_CUSTOM = 2 };

struct AppSettings {
	int compressionLevel = -1;  // Z_DEFAULT_COMPRESSION
	bool autoOpenViewer = false;
	bool useGamma = true;
	bool useCustomBg = true;
	bool useImageBg = true;
	bool correctNonSquare = true;
	QColor customBg = QColor(128, 128, 128);
	int windowBg = WBG_SAMEASIMAGE;
	int zoom = VS_FIT;
	std::array<ToolConfig, kNumTools> tools;

	QByteArray mainGeometry;
	QByteArray headerState;
	QByteArray textGeometry;
	QByteArray paletteGeometry;
	QByteArray viewerGeometry;

	void load();
	void save() const;
};

AppSettings &appSettings();

// True on Windows, where TweakPNG can add itself to the Explorer context menu.
bool shellMenuSupported();
bool isShellMenuSet();
void setShellMenu(bool enable);

#endif // TWEAKPNG_UI_SETTINGS_H
