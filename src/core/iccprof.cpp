// iccprof.cpp -- displays the contents of an ICC profile.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 2012 Jason Summers. Licensed under the GNU GPL, version 3 or
// later. See COPYING.txt.
//
// Profiles come from untrusted files. Offsets are computed in 64 bits and
// checked before each read; the original's 32-bit additions could wrap, and
// its 'desc' handler read past a tag with an empty payload.

#include "twpng.h"

namespace twpng {

namespace {

const QChar kHorzbar(0x2015);
const QChar kLdquo(0x201c);
const QChar kRdquo(0x201d);

QString quoted(const QString &s)
{
	return kLdquo + s + kRdquo;
}

// Printable ASCII, with NULs dropped and other bytes shown as underscores.
QString bytesToString(const uchar *d, qint64 len, int maxLen = 90)
{
	QString s;
	for(qint64 i=0; i<len && s.size()<maxLen; i++) {
		if(d[i]>=32 && d[i]<=126) s += QLatin1Char((char)d[i]);
		else if(d[i]!=0) s += QLatin1Char('_');
	}
	return s;
}

QString bytesToHex(const uchar *d, int len)
{
	return QString::fromLatin1(QByteArray((const char*)d, len).toHex());
}

double readS15Fixed16(const uchar *d)
{
	return ((double)(int)readInt32(d))/65536.0;
}

QString xyzData(const uchar *d, qint64 size, const QString &prefix)
{
	if(size!=12) return QString();
	return prefix + QString::asprintf("X=%.5f, Y=%.5f, Z=%.5f\n",
		readS15Fixed16(d), readS15Fixed16(d+4), readS15Fixed16(d+8));
}

QString renderingIntent(quint32 u)
{
	switch(u) {
	case 0: return QStringLiteral("Perceptual");
	case 1: return QStringLiteral("Media-Relative Colorimetric");
	case 2: return QStringLiteral("Saturation");
	case 3: return QStringLiteral("ICC-Absolute Colorimetric");
	}
	return QStringLiteral("Unknown");
}

QString dumpHeader(const uchar *d, qint64 len)
{
	if(len<128) return QString();

	const auto sig = [d](int offset) { return quoted(bytesToString(d+offset, 4)); };

	QString s = kHorzbar + QStringLiteral(" Header ") + kHorzbar + QLatin1Char('\n');

	quint32 u = readInt32(d);
	s += QString::asprintf("Profile size: %u\n", u);
	s += QStringLiteral("Preferred CMM type: ") + sig(4) + QLatin1Char('\n');

	u = readInt32(d+8);
	s += QString::asprintf("Profile version: %u.%u.%u\n",
		(u&0xff000000)>>24, (u&0x00f00000)>>20, (u&0x000f0000)>>16);

	s += QStringLiteral("Profile/device class: ") + sig(12) + QLatin1Char('\n');
	s += QStringLiteral("Data color space: ") + sig(16) + QLatin1Char('\n');
	s += QStringLiteral("Profile connection space: ") + sig(20) + QLatin1Char('\n');

	s += QString::asprintf("Profile creation time: %d-%02d-%02d %02d:%02d:%02d UTC\n",
		readInt16(d+24), readInt16(d+26), readInt16(d+28),
		readInt16(d+30), readInt16(d+32), readInt16(d+34));

	s += QStringLiteral("Profile file signature: ") + sig(36) + QLatin1Char('\n');
	s += QStringLiteral("Primary platform: ") + sig(40) + QLatin1Char('\n');

	u = readInt32(d+44);
	s += QString::asprintf("Embedded profile flag: %u\n", u&0x1);
	s += QString::asprintf("Dependent profile flag: %u\n", (u&0x2)>>1);

	s += QStringLiteral("Device manufacturer: ") + sig(48) + QLatin1Char('\n');
	s += QStringLiteral("Device model: ") + sig(52) + QLatin1Char('\n');

	u = readInt32(d+60);
	s += QString::asprintf("Attributes: %s, %s, %s, %s\n",
		(u&0x01) ? "Transparency" : "Reflective",
		(u&0x02) ? "Matte" : "Glossy",
		(u&0x04) ? "Negative" : "Positive",
		(u&0x08) ? "B&W" : "Color");

	u = readInt32(d+64) & 0xffff;
	s += QString::asprintf("Rendering intent: %u (", u) + renderingIntent(u) + QStringLiteral(")\n");

	s += xyzData(d+68, 12, QStringLiteral("Illuminant: "));
	s += QStringLiteral("Profile creator: ") + sig(80) + QLatin1Char('\n');
	s += QStringLiteral("Profile ID: ") + bytesToHex(d+84, 16) + QLatin1Char('\n');
	return s;
}

QString labeledEnum(const char *label, quint32 u, const char *const *names, quint32 maxIndex)
{
	return QString::asprintf("\t%s: %u (%s)\n", label, u, u<=maxIndex ? names[u] : "unrecognized");
}

QString illuminantType(const uchar *d)
{
	static const char *const names[9] = { "unknown", "D50", "D65", "D93",
		"F2", "D55", "A", "Equi-Power (E)", "F8" };
	return labeledEnum("illuminant type", readInt32(d), names, 8);
}

QString viewingConditions(const uchar *d, qint64 size)
{
	if(size!=28) return QString();
	return xyzData(d, 12, QStringLiteral("\tilluminant: "))
		+ xyzData(d+12, 12, QStringLiteral("\tsurround: "))
		+ illuminantType(d+24);
}

QString measurementType(const uchar *d, qint64 size)
{
	if(size!=28) return QString();

	static const char *const observers[3] = { "unknown", "1931 2 degree observer", "1964 10 degree observer" };
	static const char *const geometries[3] = { "unknown", "0/45 or 45/0", "0/d or d/0" };

	QString s = labeledEnum("standard observer", readInt32(d), observers, 2);
	s += xyzData(d+4, 12, QStringLiteral("\tmeasurement backing: "));
	s += labeledEnum("measurement geometry", readInt32(d+16), geometries, 2);

	const quint32 flare = readInt32(d+20);
	const char *flareName = "unrecognized";
	if(flare==0) flareName = "0 (0%)";
	else if(flare==0x10000) flareName = "1.0 (or 100%)";
	s += QString::asprintf("\tmeasurement flare: %u (%s)\n", flare, flareName);

	return s + illuminantType(d+24);
}

QString dumpTag(const uchar *d, qint64 len, qint64 offset, qint64 size)
{
	if(size>100000000 || size<8 || offset>1000000000 || offset+size>len) return QString();

	const uchar *body = d+offset+8;
	const qint64 bodySize = size-8;

	switch(readInt32(d+offset)) {
	case 0x74657874: // 'text'
		return QLatin1Char('\t') + quoted(bytesToString(body, bodySize, 490)) + QLatin1Char('\n');
	case 0x64657363: // 'desc'
		if(bodySize>=4) {
			const qint64 asciiCount = readInt32(body);
			if(asciiCount>0 && asciiCount+4<=bodySize) {
				return QLatin1Char('\t') + quoted(bytesToString(body+4, asciiCount-1, 490)) + QLatin1Char('\n');
			}
		}
		break;
	case 0x58595a20: // 'XYZ '
		return xyzData(body, bodySize, QStringLiteral("\t"));
	case 0x76696577: // 'view'
		return viewingConditions(body, bodySize);
	case 0x6d656173: // 'meas'
		return measurementType(body, bodySize);
	case 0x73696720: // 'sig '
		if(bodySize==4) return QLatin1Char('\t') + bytesToHex(body, 4) + QLatin1Char('\n');
		break;
	}
	return QString();
}

QString dumpTags(const uchar *d, qint64 len)
{
	if(len<132) return QString();
	const uchar *tags = d+128;

	QString s = QLatin1Char('\n') + kHorzbar + QStringLiteral(" Tags ") + kHorzbar + QLatin1Char('\n');

	const quint32 ntags = readInt32(tags);
	s += QString::asprintf("Number of tags: %u\n", ntags);

	if(ntags>10000) return s;
	if(len < 132 + 12*(qint64)ntags) return s;

	for(quint32 i=0; i<ntags; i++) {
		const QString name = bytesToString(tags+4+12*i, 4);
		const qint64 offset = readInt32(tags+4+12*i+4);
		const qint64 size = readInt32(tags+4+12*i+8);

		QString tagType;
		if(offset+4<=len) tagType = bytesToString(d+offset, 4);

		s += QString::asprintf("Tag #%u signature=", i+1) + quoted(name)
			+ QStringLiteral(" offset=%1 size=%2 type=").arg(offset).arg(size)
			+ quoted(tagType) + QLatin1Char('\n');
		s += dumpTag(d, len, offset, size);
	}
	return s;
}

} // namespace

QString dumpIccChunk(const Chunk &ch)
{
	KeywordInfo kw;
	ch.keywordInfo(&kw);

	// +2: one for the NUL terminator, and one for the compression method.
	if(ch.data.size() < kw.keywordLen+2) return QString();

	if(ch.bytes()[kw.keywordLen+1] != 0) {
		return QStringLiteral("Unsupported compression method\n");
	}

	const int cmprLen = ch.data.size() - kw.keywordLen - 2;
	QByteArray profile;
	QString error;
	const bool ok = uncompressData(ch.data.constData()+kw.keywordLen+2, cmprLen, &profile, &error);

	QString s = QStringLiteral("Compressed size: %1\n").arg(cmprLen);
	if(!ok) {
		return s + QStringLiteral("Decompression failed: ") + error + QLatin1Char('\n');
	}
	s += QStringLiteral("Uncompressed size: %1\n\n").arg(profile.size());

	const uchar *d = (const uchar*)profile.constData();
	s += dumpHeader(d, profile.size());
	s += dumpTags(d, profile.size());
	return s;
}

} // namespace twpng
