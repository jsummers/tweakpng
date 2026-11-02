// pngimage.cpp -- runs the pngtodib decoder and converts its DIB to a QImage.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 2011 Jason Summers. Licensed under the GNU GPL, version 3 or
// later. See COPYING.txt.

#include "pngimage.h"
#include "pngtodib.h"

#include <cstring>

namespace twpng {

namespace {

struct ReadContext {
	const QByteArray *bytes;
	int pos;
};

int readBytes(void *userdata, void *buf, int nbytes)
{
	ReadContext *ctx = (ReadContext*)userdata;
	const int avail = (int)qMin((qint64)nbytes, (qint64)ctx->bytes->size()-ctx->pos);
	if(avail>0) {
		memcpy(buf, ctx->bytes->constData()+ctx->pos, (size_t)avail);
		ctx->pos += avail;
	}
	return avail;
}

// Palette indices come straight from the file, so a pixel can refer to an
// entry past the end of a short palette. Those are shown as black.
QImage dibToImage(const P2D_BITMAPINFOHEADER *dib, const uchar *bits)
{
	const int w = dib->biWidth;
	const int h = dib->biHeight;
	const int bpp = dib->biBitCount;
	const int entries = (int)dib->biClrUsed;
	const size_t rowBytes = ((((size_t)w * (size_t)bpp)+31)/32)*4;
	const P2D_RGBQUAD *pal = (const P2D_RGBQUAD*)(dib+1);

	QImage img(w, h, QImage::Format_RGB32);
	if(img.isNull()) return img;

	QRgb table[256];
	for(int i=0; i<256; i++) {
		table[i] = (i<entries) ? qRgb(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue) : qRgb(0,0,0);
	}

	for(int y=0; y<h; y++) {
		const uchar *src = bits + (size_t)(h-1-y)*rowBytes;  // DIBs are bottom-up
		QRgb *dst = (QRgb*)img.scanLine(y);

		switch(bpp) {
		case 24:
			for(int x=0; x<w; x++) {
				dst[x] = qRgb(src[x*3+2], src[x*3+1], src[x*3]);
			}
			break;
		case 8:
			for(int x=0; x<w; x++) dst[x] = table[src[x]];
			break;
		case 4:
			for(int x=0; x<w; x++) dst[x] = table[(src[x/2] >> (4*(1-x%2))) & 0x0f];
			break;
		case 1:
			for(int x=0; x<w; x++) dst[x] = table[(src[x/8] >> (7-x%8)) & 0x01];
			break;
		}
	}
	return img;
}

} // namespace

DecodeResult decodePng(const QByteArray &fileBytes, const DecodeOptions &options)
{
	DecodeResult result;
	ReadContext ctx{&fileBytes, 0};

	p2d_globals_struct *globals = p2d_create_globals();
	P2D *p2d = p2d_init(globals);
	if(!p2d) {
		p2d_destroy_globals(globals);
		result.error = QStringLiteral("Unable to allocate memory");
		return result;
	}

	p2d_set_png_read_fn(p2d, readBytes);
	p2d_set_userdata(p2d, &ctx);
	p2d_enable_color_correction(p2d, options.colorCorrection ? 1 : 0);
	p2d_set_use_file_bg(p2d, options.useFileBackground ? 1 : 0);
	if(options.useCustomBackground) {
		p2d_set_custom_bg(p2d, options.bgRed, options.bgGreen, options.bgBlue);
	}

	if(p2d_run(p2d) != PNGD_E_SUCCESS) {
		result.error = QString::fromLatin1(p2d_get_error_msg(p2d));
	}
	else {
		P2D_BITMAPINFOHEADER *dib = nullptr;
		void *bits = nullptr;
		p2d_get_dib(p2d, &dib);
		p2d_get_dibbits(p2d, &bits);

		result.image = dibToImage(dib, (const uchar*)bits);
		result.ok = !result.image.isNull();
		if(!result.ok) result.error = QStringLiteral("Unable to allocate memory");

		int units = 0;
		result.densityValid = p2d_get_density(p2d, &result.densityX, &result.densityY, &units) != 0;
		result.hasBackground = p2d_get_bgcolor(p2d, &result.bgRed, &result.bgGreen, &result.bgBlue) != 0;

		p2d_free_dib(dib);
	}

	p2d_done(p2d);
	p2d_destroy_globals(globals);
	return result;
}

QString libpngVersion()
{
	char buf[64];
	p2d_get_libpng_version(buf, (int)sizeof(buf));
	return QString::fromLatin1(buf);
}

} // namespace twpng
