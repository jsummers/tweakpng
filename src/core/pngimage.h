// pngimage.h -- decodes a PNG for the viewer, using the pngtodib decoder.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 2011 Jason Summers. Licensed under the GNU GPL, version 3 or
// later. See COPYING.txt.

#ifndef TWEAKPNG_CORE_PNGIMAGE_H
#define TWEAKPNG_CORE_PNGIMAGE_H

#include <QByteArray>
#include <QImage>
#include <QString>

namespace twpng {

struct DecodeOptions {
	bool colorCorrection = true;
	bool useFileBackground = true;
	bool useCustomBackground = true;
	uchar bgRed = 128;
	uchar bgGreen = 128;
	uchar bgBlue = 128;
};

struct DecodeResult {
	bool ok = false;
	QString error;
	QImage image;
	bool densityValid = false;
	int densityX = 1;
	int densityY = 1;
	bool hasBackground = false;
	uchar bgRed = 0;
	uchar bgGreen = 0;
	uchar bgBlue = 0;
};

DecodeResult decodePng(const QByteArray &fileBytes, const DecodeOptions &options);
QString libpngVersion();

} // namespace twpng

#endif // TWEAKPNG_CORE_PNGIMAGE_H
