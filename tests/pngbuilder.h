// pngbuilder.h -- builds small PNG files for the tests, one chunk at a time.

#ifndef TWEAKPNG_TESTS_PNGBUILDER_H
#define TWEAKPNG_TESTS_PNGBUILDER_H

#include "twpng.h"

#include <QByteArray>

#include <zlib.h>

inline QByteArray makeChunk(const char *type, const QByteArray &data)
{
	twpng::Chunk c;
	c.setType(type);
	c.data = data;
	c.chunkModified();
	return c.serialized();
}

inline QByteArray be32(quint32 v)
{
	QByteArray b(4, '\0');
	twpng::writeInt32((uchar*)b.data(), v);
	return b;
}

inline QByteArray makeIhdr(quint32 w, quint32 h, int depth, int colorType, int interlace = 0)
{
	QByteArray d = be32(w) + be32(h);
	d.append((char)depth);
	d.append((char)colorType);
	d.append('\0');
	d.append('\0');
	d.append((char)interlace);
	return makeChunk("IHDR", d);
}

inline QByteArray pngSignature()
{
	return QByteArray::fromHex("89504e470d0a1a0a");
}

inline QByteArray deflated(const QByteArray &raw)
{
	QByteArray out;
	twpng::compressData(raw, &out);
	return out;
}

inline QByteArray makeIdat(const QByteArray &raw)
{
	return makeChunk("IDAT", deflated(raw));
}

// A minimal valid 1x1 grayscale image, with extra chunks before the IDAT.
inline QByteArray makePng(const QByteArray &beforeIdat = QByteArray(), int depth = 8, int colorType = 0)
{
	const int channels = (colorType==2) ? 3 : (colorType==4) ? 2 : (colorType==6) ? 4 : 1;
	const int bytes = qMax(1, channels*depth/8);
	return pngSignature() + makeIhdr(1, 1, depth, colorType) + beforeIdat
		+ makeIdat(QByteArray(1+bytes, '\0')) + makeChunk("IEND", QByteArray());
}

#endif // TWEAKPNG_TESTS_PNGBUILDER_H
