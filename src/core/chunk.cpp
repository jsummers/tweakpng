// chunk.cpp -- a single PNG chunk: parsing, text handling, descriptions.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "twpng.h"

#include <zlib.h>

#include <cstring>

namespace twpng {

const char *const kKnownTextKeys[] = { "Title", "Author", "Description",
	"Copyright", "Creation Time", "Software", "Disclaimer",
	"Warning", "Source", "Comment", nullptr };

const char *const kMonthNames[] = { "INVALID-MONTH", "Jan", "Feb", "Mar", "Apr",
	"May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

namespace {

const QChar kTimes(0x00d7);
const QChar kLdquo(0x201c);
const QChar kRdquo(0x201d);
const QChar kRsquo(0x2019);
const int kMaxTextDisplay = 150;  // how much text to show in the chunk list

struct ChunkName { int id; const char *name; };

const ChunkName kChunkNames[] = {
	{CHUNK_IHDR,"IHDR"}, {CHUNK_IEND,"IEND"}, {CHUNK_IDAT,"IDAT"}, {CHUNK_PLTE,"PLTE"},
	{CHUNK_bKGD,"bKGD"}, {CHUNK_cHRM,"cHRM"}, {CHUNK_gAMA,"gAMA"}, {CHUNK_hIST,"hIST"},
	{CHUNK_pHYs,"pHYs"}, {CHUNK_sBIT,"sBIT"}, {CHUNK_tEXt,"tEXt"}, {CHUNK_tIME,"tIME"},
	{CHUNK_tRNS,"tRNS"}, {CHUNK_zTXt,"zTXt"}, {CHUNK_sRGB,"sRGB"}, {CHUNK_iCCP,"iCCP"},
	{CHUNK_iTXt,"iTXt"}, {CHUNK_sPLT,"sPLT"},
	{CHUNK_oFFs,"oFFs"}, {CHUNK_pCAL,"pCAL"}, {CHUNK_sCAL,"sCAL"}, {CHUNK_gIFg,"gIFg"},
	{CHUNK_gIFx,"gIFx"}, {CHUNK_gIFt,"gIFt"}, {CHUNK_fRAc,"fRAc"},
	{CHUNK_sTER,"sTER"}, {CHUNK_dSIG,"dSIG"}, {CHUNK_eXIf,"eXIf"}, {CHUNK_acTL,"acTL"},
	{CHUNK_fcTL,"fcTL"}, {CHUNK_fdAT,"fdAT"}, {CHUNK_CgBI,"CgBI"}, {CHUNK_vpAg,"vpAg"},
	{CHUNK_iDOT,"iDOT"},
	{CHUNK_MHDR,"MHDR"}, {CHUNK_MEND,"MEND"}, {CHUNK_LOOP,"LOOP"}, {CHUNK_ENDL,"ENDL"},
	{CHUNK_DEFI,"DEFI"}, {CHUNK_JHDR,"JHDR"}, {CHUNK_BASI,"BASI"}, {CHUNK_CLON,"CLON"},
	{CHUNK_DHDR,"DHDR"}, {CHUNK_PAST,"PAST"}, {CHUNK_DISC,"DISC"}, {CHUNK_BACK,"BACK"},
	{CHUNK_FRAM,"FRAM"}, {CHUNK_MOVE,"MOVE"}, {CHUNK_CLIP,"CLIP"}, {CHUNK_SHOW,"SHOW"},
	{CHUNK_TERM,"TERM"}, {CHUNK_SAVE,"SAVE"}, {CHUNK_SEEK,"SEEK"}, {CHUNK_eXPI,"eXPI"},
	{CHUNK_fPRI,"fPRI"}, {CHUNK_nEED,"nEED"}, {CHUNK_pHYg,"pHYg"}, {CHUNK_JDAT,"JDAT"},
	{CHUNK_JSEP,"JSEP"}, {CHUNK_PROM,"PROM"}, {CHUNK_IPNG,"IPNG"}, {CHUNK_PPLT,"PPLT"},
	{CHUNK_IJNG,"IJNG"}, {CHUNK_DROP,"DROP"}, {CHUNK_DBYK,"DBYK"}, {CHUNK_ORDR,"ORDR"},
	{0,nullptr}
};

quint32 crcTableEntry(int n)
{
	quint32 c = (quint32)n;
	for(int k=0; k<8; k++) {
		c = (c & 1) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
	}
	return c;
}

int findNul(const QByteArray &s, int startpos)
{
	const int i = s.indexOf('\0', startpos);
	return (i < 0 ? s.size() : i) - startpos;
}

QString clobberControls(QString s)
{
	for(int i=0; i<s.size(); i++) {
		const ushort c = s.at(i).unicode();
		if(c<32 || (c>126 && c<161)) s[i] = QLatin1Char('|');
	}
	return s;
}

int gCompressionLevel = Z_DEFAULT_COMPRESSION;

} // namespace

quint32 readInt32(const uchar *x)
{
	return (((quint32)x[0])<<24) | (((quint32)x[1])<<16) | (((quint32)x[2])<<8) | ((quint32)x[3]);
}

int readInt16(const uchar *x)
{
	return x[0]*256 + x[1];
}

void writeInt32(uchar *buf, quint32 x)
{
	buf[0] = (uchar)((x>>24) & 0xff);
	buf[1] = (uchar)((x>>16) & 0xff);
	buf[2] = (uchar)((x>> 8) & 0xff);
	buf[3] = (uchar)( x      & 0xff);
}

void writeInt16(uchar *buf, int x)
{
	buf[0] = (uchar)((x>>8) & 0xff);
	buf[1] = (uchar)( x     & 0xff);
}

quint32 crc32Of(const char *type, const QByteArray &data)
{
	static quint32 table[256];
	static const bool ready = []() {
		for(int n=0; n<256; n++) table[n] = crcTableEntry(n);
		return true;
	}();
	(void)ready;

	quint32 c = 0xffffffffU;
	for(int i=0; i<4; i++) {
		c = table[(c ^ (uchar)type[i]) & 0xff] ^ (c >> 8);
	}
	const uchar *p = (const uchar*)data.constData();
	for(int i=0; i<data.size(); i++) {
		c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	}
	return c ^ 0xffffffffU;
}

void setCompressionLevel(int level) { gCompressionLevel = level; }
int compressionLevel() { return gCompressionLevel; }

bool compressData(const QByteArray &in, QByteArray *out)
{
	uLongf outlen = compressBound((uLong)in.size());
	out->resize((int)outlen);
	const int ret = compress2((Bytef*)out->data(), &outlen, (const Bytef*)in.constData(),
		(uLong)in.size(), gCompressionLevel);
	if(ret != Z_OK) {
		out->clear();
		return false;
	}
	out->resize((int)outlen);
	return true;
}

bool uncompressData(const char *in, int inlen, QByteArray *out, QString *error)
{
	z_stream z;
	memset(&z, 0, sizeof(z));
	out->clear();

	if(inflateInit(&z) != Z_OK) {
		if(error) *error = QStringLiteral("decompression error");
		return false;
	}

	z.next_in = (Bytef*)in;
	z.avail_in = (uInt)inlen;

	const int chunkSize = 16384;
	char buf[chunkSize];
	bool ok = false;

	for(;;) {
		z.next_out = (Bytef*)buf;
		z.avail_out = chunkSize;
		const int err = inflate(&z, Z_NO_FLUSH);
		const int produced = chunkSize - (int)z.avail_out;

		if((qint64)out->size() + produced > kMaxInflatedSize) {
			if(error) *error = QStringLiteral("decompressed data is too large");
			break;
		}
		out->append(buf, produced);

		if(err == Z_STREAM_END) {
			ok = true;
			break;
		}
		if(err != Z_OK) {
			if(error) *error = z.msg ? QString::fromLatin1(z.msg) : QStringLiteral("decompression error");
			break;
		}
		if(z.avail_in == 0 && produced == 0) {
			if(error) *error = QStringLiteral("unexpected end of compressed data");
			break;
		}
	}
	inflateEnd(&z);
	if(!ok) out->clear();
	return ok;
}

QString chunkNameFromId(int id)
{
	for(int i=0; kChunkNames[i].id; i++) {
		if(kChunkNames[i].id == id) return QString::fromLatin1(kChunkNames[i].name);
	}
	return QStringLiteral("????");
}

bool canEditChunkType(int id)
{
	switch(id) {
	case CHUNK_IHDR: case CHUNK_PLTE: case CHUNK_bKGD:
	case CHUNK_gAMA: case CHUNK_cHRM: case CHUNK_tRNS:
	case CHUNK_sBIT: case CHUNK_sRGB: case CHUNK_tEXt:
	case CHUNK_pHYs: case CHUNK_tIME: case CHUNK_sTER:
	case CHUNK_acTL: case CHUNK_fcTL: case CHUNK_fdAT:
	case CHUNK_oFFs: case CHUNK_sCAL: case CHUNK_iCCP:
	case CHUNK_vpAg: case CHUNK_zTXt: case CHUNK_iTXt:
		return true;
	}
	return false;
}

Chunk::Chunk()
{
	memset(type, 0, sizeof(type));
}

void Chunk::setType(const char *name)
{
	memcpy(type, name, 4);
	type[4] = '\0';
}

void Chunk::afterInit()
{
	id = CHUNK_UNKNOWN;
	for(int i=0; kChunkNames[i].id; i++) {
		if(!strcmp(type, kChunkNames[i].name)) {
			id = kChunkNames[i].id;
			break;
		}
	}

	if(id==CHUNK_IHDR && parent && data.size()>=13) {
		parent->width = readInt32(bytes());
		parent->height = readInt32(bytes()+4);
		parent->bitDepth = bytes()[8];
		parent->colorType = bytes()[9];
	}
}

void Chunk::chunkModified()
{
	crc = calcCrc();
}

quint32 Chunk::calcCrc() const
{
	return crc32Of(type, data);
}

bool Chunk::canEdit() const
{
	return canEditChunkType(id);
}

QByteArray Chunk::serialized() const
{
	QByteArray out;
	out.resize(data.size()+12);
	uchar *m = (uchar*)out.data();
	writeInt32(m, length());
	memcpy(m+4, type, 4);
	if(!data.isEmpty()) memcpy(m+8, data.constData(), (size_t)data.size());
	writeInt32(m+8+data.size(), crc);
	return out;
}

int Chunk::initFromMemory(const uchar *m, qint64 msize)
{
	if(msize<12) return 0;
	const qint64 len = readInt32(m);
	if(len+12 > msize) return 0;

	char name[5];
	memcpy(name, m+4, 4);
	name[4] = '\0';
	setType(name);
	data = QByteArray((const char*)m+8, (int)len);
	// A CRC follows, but we recalculate it.
	invalidateTextInfo();
	afterInit();
	chunkModified();
	return (int)(len+12);
}

// Returns false if the chunk is known to be invalid because of its length.
// This is only useful for simple chunks with predictable sizes.
bool Chunk::hasValidLength() const
{
	const int ct = parent ? parent->colorType : 0;
	const quint32 len = length();

	switch(id) {
	case CHUNK_IHDR: return len==13;
	case CHUNK_IEND: return len==0;
	case CHUNK_bKGD:
		if(ct==3) return len==1;
		if(ct==0 || ct==4) return len==2;
		if(ct==2 || ct==6) return len==6;
		break;
	case CHUNK_tRNS:
		if(ct==0) return len==2;
		if(ct==2) return len==6;
		break;
	case CHUNK_sRGB: case CHUNK_sTER: return len==1;
	case CHUNK_gAMA: return len==4;
	case CHUNK_sBIT:
		if(ct==0) return len==1;
		if(ct==2 || ct==3) return len==3;
		if(ct==4) return len==2;
		if(ct==6) return len==4;
		break;
	case CHUNK_cHRM: return len==32;
	case CHUNK_pHYs: return len==9;
	case CHUNK_tIME: return len==7;
	case CHUNK_JHDR: return len==16;
	case CHUNK_MHDR: return len==28;
	case CHUNK_oFFs: return len==9;
	case CHUNK_sCAL: return len>=4;
	case CHUNK_acTL: return len==8;
	case CHUNK_fcTL: return len==26;
	case CHUNK_fdAT: return len>=4;
	case CHUNK_vpAg: return len==9;
	}
	return true;
}

QString Chunk::lengthError(const QString &name) const
{
	return name + QStringLiteral("; incorrect chunk length");
}

QString Chunk::describeIHDR() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("PNG image header"));
	const uchar *d = bytes();

	QString s = QStringLiteral("PNG image header: ") + QString::number(readInt32(d)) + kTimes
		+ QString::number(readInt32(d+4));
	s += QString::asprintf(", %u bit%s/%s", (unsigned)d[8], d[8]==1 ? "" : "s", d[9]==3 ? "pixel" : "sample");

	switch(d[9]) {
	case 0: s += QStringLiteral(", grayscale"); break;
	case 2: s += QStringLiteral(", truecolor"); break;
	case 3: s += QStringLiteral(", paletted"); break;
	case 4: s += QStringLiteral(", grayscale+alpha"); break;
	case 6: s += QStringLiteral(", truecolor+alpha"); break;
	default: s += QString::asprintf(", illegal color type (%u)", (unsigned)d[9]);
	}

	switch(d[12]) {
	case 0: s += QStringLiteral(", noninterlaced"); break;
	case 1: s += QStringLiteral(", interlaced"); break;
	default: s += QString::asprintf(", unrecognized interlace method (%u)", (unsigned)d[12]);
	}
	return s;
}

QString Chunk::describeJHDR() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("JNG file header"));
	const uchar *d = bytes();

	QString s = QStringLiteral("JNG file header: ") + QString::number(readInt32(d)) + kTimes
		+ QString::number(readInt32(d+4));

	const int ct = d[8];
	switch(ct) {
	case 8: s += QStringLiteral(", grayscale"); break;
	case 10: s += QStringLiteral(", color"); break;
	case 12: s += QStringLiteral(", grayscale+alpha"); break;
	case 14: s += QStringLiteral(", color+alpha"); break;
	default: s += QString::asprintf(", color type=%d", ct);
	}

	if(d[9]==20) s += QStringLiteral(", 8 and 12-bit");
	else s += QString::asprintf(", %d-bit", (int)d[9]);

	if(d[11]==8) s += QStringLiteral(", progressive");

	if(ct==12 || ct==14) {
		s += QString::asprintf(", alpha: %d-bit", (int)d[12]);
		if(d[13]==0) s += QStringLiteral(", alpha cmpr.: PNG");
		else if(d[13]==8) s += QStringLiteral(", alpha cmpr.: JDAA");
	}
	return s;
}

QString Chunk::describeMHDR() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("MNG file header"));
	const uchar *d = bytes();

	QString s = QStringLiteral("MNG file header: ") + QString::number(readInt32(d)) + kTimes
		+ QString::number(readInt32(d+4));
	s += QString::asprintf(", %u ticks/sec.", readInt32(d+8));
	quint32 u = readInt32(d+12);
	if(u) s += QString::asprintf(", %u layers", u);
	u = readInt32(d+16);
	if(u) s += QString::asprintf(", %u frames", u);
	u = readInt32(d+20);
	if(u) s += QString::asprintf(", %u ticks", u);
	s += QString::asprintf(", profile: 0x%08x", readInt32(d+24));
	return s;
}

QString Chunk::describebKGD() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("background color"));
	if(parent->imageType() != IMG_PNG) return QStringLiteral("background color");

	const uchar *d = bytes();
	const int ct = parent->colorType;
	if(ct==3) {
		if(length()==1) return QString::asprintf("background color = palette entry %u", (unsigned)d[0]);
	}
	else if(ct==0 || ct==4) {
		if(length()==2) return QString::asprintf("background color = (%u)", (unsigned)readInt16(d));
	}
	else if(ct==2 || ct==6) {
		if(length()==6) {
			return QString::asprintf("background color = (%u,%u,%u)",
				(unsigned)readInt16(d), (unsigned)readInt16(d+2), (unsigned)readInt16(d+4));
		}
	}
	return QStringLiteral("invalid background color chunk");
}

QString Chunk::describehIST() const
{
	const quint32 len = length();
	if(len<2 || len>512 || (len%2)) return QStringLiteral("invalid histogram chunk");
	return QString::asprintf("histogram for palette colors, %u entr%s", len/2, len==2 ? "y" : "ies");
}

QString Chunk::describePLTE() const
{
	QString s;
	switch(parent->colorType) {
	case 2: case 6: s = QStringLiteral("palette (suggested)"); break;
	case 0: case 4: s = QStringLiteral("illegal palette chunk"); break;
	default: s = QStringLiteral("palette");
	}

	const quint32 len = length();
	if(len<3 || len>768 || (len%3 != 0)) return s + QStringLiteral(", incorrect palette size");
	return s + QString::asprintf(", %u entr%s", len/3, len==3 ? "y" : "ies");
}

QString Chunk::describetRNS() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("transparency chunk"));

	const uchar *d = bytes();
	const quint32 len = length();
	switch(parent->colorType) {
	case 3:
		if(len<=256) {
			return QString::asprintf("alpha values for palette colors, %u entr%s", len, len==1 ? "y" : "ies");
		}
		// fall through
	case 0:
		if(len==2) return QString::asprintf("transparent color = (%u)", (unsigned)readInt16(d));
		break;
	case 2:
		if(len==6) {
			return QString::asprintf("transparent color = (%u,%u,%u)",
				(unsigned)readInt16(d), (unsigned)readInt16(d+2), (unsigned)readInt16(d+4));
		}
		break;
	default:
		return QStringLiteral("illegal transparency chunk, alpha channel is present");
	}
	return QStringLiteral("invalid transparency chunk");
}

QString Chunk::describesRGB() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("sRGB color space"));
	QString s;
	switch(bytes()[0]) {
	case 0: s = QStringLiteral("Perceptual"); break;
	case 1: s = QStringLiteral("Relative colorimetric"); break;
	case 2: s = QStringLiteral("Saturation"); break;
	case 3: s = QStringLiteral("Absolute colorimetric"); break;
	default: s = QString::asprintf("[unknown: %d]", (int)bytes()[0]);
	}
	return QStringLiteral("sRGB color space, rendering intent: ") + s;
}

QString Chunk::describesTER() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("stereo image"));
	QString s;
	switch(bytes()[0]) {
	case 0: s = QStringLiteral("Cross-fuse layout"); break;
	case 1: s = QStringLiteral("Diverging-fuse layout"); break;
	default: s = QString::asprintf("[unknown: %d]", (int)bytes()[0]);
	}
	return QStringLiteral("stereo image, mode: ") + s;
}

QString Chunk::describegAMA() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("file gamma"));
	return QString::asprintf("file gamma = %.5f", ((double)readInt32(bytes()))/100000.0);
}

QString Chunk::describesBIT() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("significant bits/sample"));

	const uchar *d = bytes();
	const quint32 len = length();
	switch(parent->colorType) {
	case 0:
		if(len==1) return QString::asprintf("significant bits/sample: %u", (unsigned)d[0]);
		break;
	case 2: case 3:
		if(len==3) {
			return QString::asprintf("significant bits/sample: R:%u,G:%u,B:%u",
				(unsigned)d[0], (unsigned)d[1], (unsigned)d[2]);
		}
		break;
	case 4:
		if(len==2) return QString::asprintf("significant bits/sample: %u,A:%u", (unsigned)d[0], (unsigned)d[1]);
		break;
	case 6:
		if(len==4) {
			return QString::asprintf("significant bits/sample: R:%u,G:%u,B:%u,A:%u",
				(unsigned)d[0], (unsigned)d[1], (unsigned)d[2], (unsigned)d[3]);
		}
	}
	return QStringLiteral("invalid significant bits/sample chunk");
}

QString Chunk::describecHRM() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("chromaticities"));
	const uchar *d = bytes();
	return QString::asprintf("chromaticities: WP(%.5f,%.5f),R(%.5f,%.5f),G(%.5f,%.5f),B(%.5f,%.5f)",
		((double)readInt32(d))/100000.0,    ((double)readInt32(d+4))/100000.0,
		((double)readInt32(d+8))/100000.0,  ((double)readInt32(d+12))/100000.0,
		((double)readInt32(d+16))/100000.0, ((double)readInt32(d+20))/100000.0,
		((double)readInt32(d+24))/100000.0, ((double)readInt32(d+28))/100000.0);
}

QString Chunk::describepHYs() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("pixel size"));
	const uchar *d = bytes();
	const quint32 x = readInt32(d);
	const quint32 y = readInt32(d+4);

	QString s = QStringLiteral("pixel size = ") + QString::number(x) + kTimes + QString::number(y)
		+ QStringLiteral(" pixels");

	switch(d[8]) {
	case 0: s += QStringLiteral(" (per unspecified unit)"); break;
	case 1:
		s += QString::asprintf(" per meter (%.1f", (double)x * 0.0254) + kTimes
			+ QString::asprintf("%.1f dpi)", (double)y * 0.0254);
		break;
	default: s += QStringLiteral(" per (unrecognized unit)");
	}
	return s;
}

QString Chunk::describeacTL() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("APNG animation control"));
	const int nframes = (int)readInt32(bytes());
	const int nplays = (int)readInt32(bytes()+4);

	QString s = QString::asprintf("APNG animation control: %d frame%s, play ", nframes, nframes==1 ? "" : "s");
	if(nplays==0) s += QStringLiteral("indefinitely");
	else s += QString::asprintf("%d time%s", nplays, nplays==1 ? "" : "s");
	return s;
}

QString Chunk::describefcTL() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("APNG frame control"));
	const uchar *d = bytes();

	const int dNum = readInt16(d+20);
	const int dDen = readInt16(d+22);
	const double delay = (dDen==0) ? ((double)dNum)/100.0 : ((double)dNum)/(double)dDen;

	const char *dispose = "?";
	switch(d[24]) {
	case 0: dispose = "none"; break;
	case 1: dispose = "bkgd"; break;
	case 2: dispose = "prev"; break;
	}

	const char *blend = "?";
	switch(d[25]) {
	case 0: blend = "source"; break;
	case 1: blend = "over"; break;
	}

	return QString::asprintf("APNG frame control, seq#=%d, %d", (int)readInt32(d), (int)readInt32(d+4)) + kTimes
		+ QString::asprintf("%d+%d+%d, delay=%.3fs, dispose=%s, blend=%s",
			(int)readInt32(d+8), (int)readInt32(d+12), (int)readInt32(d+16), delay, dispose, blend);
}

QString Chunk::describefdAT() const
{
	if(length()<4) return lengthError(QStringLiteral("APNG frame data"));
	return QString::asprintf("APNG frame data, seq#=%d", (int)readInt32(bytes()));
}

QString Chunk::describeoFFs() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("image offset"));
	const uchar *d = bytes();

	QString s = QString::asprintf("image offset = (%d,%d) ", (int)readInt32(d), (int)readInt32(d+4));
	switch(d[8]) {
	case 0: s += QStringLiteral("pixels"); break;
	case 1: s += QChar(0x03bc) + QStringLiteral("m"); break;
	default: s += QStringLiteral("(invalid units)");
	}
	return s;
}

QString Chunk::describevpAg() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("virtual page"));
	const uchar *d = bytes();

	QString s = QStringLiteral("virtual page = ") + QString::number((int)readInt32(d)) + kTimes
		+ QString::number((int)readInt32(d+4)) + QLatin1Char(' ');
	// There is no documentation of unit codes other than 0, though it is
	// probably the same as the oFFs chunk.
	s += (d[8]==0) ? QStringLiteral("pixels") : QStringLiteral("(unknown units)");
	return s;
}

// Reads a text field beginning at the given offset, and ending at a NUL byte
// or the end of the data. Returns the number of bytes read.
int Chunk::readTextField(int offset, QString *dst, int dstlen, bool floatingPoint) const
{
	dst->clear();
	const int srclen = data.size()-offset;
	int srcpos;

	for(srcpos=0; srcpos<srclen; srcpos++) {
		if(dst->size() >= dstlen-1) break;
		char c = data.at(offset+srcpos);
		if(!c) break;
		if(floatingPoint) {
			if(!((c>='0' && c<='9') || c=='.' || c=='+' || c=='-' || c=='e' || c=='E')) c = '_';
		}
		dst->append(QChar((ushort)(uchar)c));
	}
	return srcpos;
}

SCalData Chunk::sCalData() const
{
	SCalData d;
	if(!hasValidLength()) return d;

	d.units = bytes()[0];
	const int ret = readTextField(1, &d.x, 100, true);
	readTextField(1+ret+1, &d.y, 100, true);
	return d;
}

void Chunk::setSCalData(const SCalData &d)
{
	const QByteArray x = d.x.toLatin1();
	const QByteArray y = d.y.toLatin1();
	QByteArray out;
	out.append((char)d.units);
	out.append(x);
	out.append('\0');
	out.append(y);
	data = out;
}

QString Chunk::describesCAL() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("physical scale of image subject"));
	const SCalData d = sCalData();

	const char *units = "unknown units";
	if(d.units==1) units = "meters";
	else if(d.units==2) units = "radians";

	return QStringLiteral("physical scale of image subject: ") + d.x + kTimes + d.y + QLatin1Char(' ')
		+ QLatin1String(units);
}

void Chunk::invalidateTextInfo()
{
	m_text = TextInfo();
	m_textProcessed = false;
	m_textOk = false;
}

// The original cached only "processed" and reported success for a repeat
// call on a malformed chunk, which then crashed on the missing keyword. The
// result is now cached too.
bool Chunk::textInfo(TextInfo *out)
{
	if(!m_textProcessed) {
		m_textProcessed = true;
		m_textOk = parseTextInfo();
	}
	if(out) *out = m_text;
	return m_textOk;
}

bool Chunk::parseTextInfo()
{
	m_text = TextInfo();
	const int len0 = data.size();
	int p = 0;
	int len = findNul(data, p);

	if(len<1 || len>79) return false;

	m_text.keyword = QString::fromLatin1(data.constData(), len);
	p += len+1;

	uchar cmprMethod = 0;
	bool isUtf8 = false;

	if(id==CHUNK_zTXt) {
		if(p > len0-1) return false;
		cmprMethod = bytes()[p++];
		m_text.isCompressed = true;
	}
	else if(id==CHUNK_iTXt) {
		isUtf8 = true;
		if(p > len0-4) return false;
		if(bytes()[p++] != 0) m_text.isCompressed = true;
		cmprMethod = bytes()[p++];

		len = findNul(data, p);
		m_text.language = QString::fromLatin1(data.constData()+p, len);
		p += len+1;

		if(p > len0-1) return false;
		len = findNul(data, p);
		m_text.translatedKeyword = QString::fromUtf8(data.constData()+p, len);
		p += len+1;
	}

	if(p > len0) return false;

	if(m_text.isCompressed) {
		if(cmprMethod != 0) return false;  // unknown compression method

		QByteArray unc;
		if(!uncompressData(data.constData()+p, len0-p, &unc, nullptr)) {
			return true;  // keyword is known; hasText remains false
		}
		m_text.text = isUtf8 ? QString::fromUtf8(unc) : QString::fromLatin1(unc);
	}
	else if(isUtf8) {
		m_text.text = QString::fromUtf8(data.constData()+p, len0-p);
	}
	else {
		m_text.text = QString::fromLatin1(data.constData()+p, len0-p);
	}
	m_text.hasText = true;
	return true;
}

bool Chunk::setTextInfo(const QString &keyword, const QString &language,
	const QString &translatedKeyword, const QString &text, bool compressed, bool international)
{
	const QByteArray kw = keyword.toLatin1();
	const QByteArray body = international ? text.toUtf8() : text.toLatin1();

	QByteArray payload = body;
	if(compressed && !compressData(body, &payload)) {
		return false;
	}

	QByteArray out;
	out.append(kw);
	out.append('\0');

	if(international) {
		out.append(compressed ? '\1' : '\0');
		out.append('\0');  // compression method
		out.append(language.toLatin1());
		out.append('\0');
		out.append(translatedKeyword.toUtf8());
		out.append('\0');
		setType("iTXt");
		id = CHUNK_iTXt;
	}
	else if(compressed) {
		out.append('\0');  // compression method
		setType("zTXt");
		id = CHUNK_zTXt;
	}
	else {
		setType("tEXt");
		id = CHUNK_tEXt;
	}
	out.append(payload);
	data = out;
	invalidateTextInfo();
	return true;
}

QString Chunk::describeText()
{
	TextInfo ti;
	if(!textInfo(&ti)) {
		return QStringLiteral("can") + kRsquo + QStringLiteral("t read text chunk");
	}

	QString text;
	if(!ti.hasText) {
		text = ti.isCompressed ? QStringLiteral(" --decompression failed!--")
			: QStringLiteral(" --can") + kRsquo + QStringLiteral("t read text--");
	}
	else {
		text = ti.text;
		if(text.size() > kMaxTextDisplay) {
			text = text.left(kMaxTextDisplay) + QStringLiteral("...");
		}
		const int nul = text.indexOf(QChar());
		if(nul>=0) text.truncate(nul);
	}

	const QString keyword = clobberControls(ti.keyword);
	text = clobberControls(text);

	bool standard = false;
	for(int i=0; kKnownTextKeys[i]; i++) {
		if(keyword == QLatin1String(kKnownTextKeys[i])) standard = true;
	}

	QString s = QStringLiteral("text");
	if(ti.isCompressed && id==CHUNK_iTXt) s += QStringLiteral(" (international, compressed)");
	else if(ti.isCompressed) s += QStringLiteral(" (compressed)");
	else if(id==CHUNK_iTXt) s += QStringLiteral(" (international)");

	s += QStringLiteral(", key=") + kLdquo + keyword + kRdquo;
	if(!standard) s += QStringLiteral(" (nonstandard)");
	if(id==CHUNK_iTXt && !ti.language.isEmpty()) {
		s += QStringLiteral(", lang=") + ti.language;
	}
	s += QStringLiteral(": ") + kLdquo + text + kRdquo;
	return s;
}

QString Chunk::describetIME() const
{
	if(!hasValidLength()) return lengthError(QStringLiteral("time of last modification"));
	const uchar *d = bytes();
	int month = d[2];
	if(month<1 || month>12) month = 0;
	return QString::asprintf("time of last modification = %d %s %d, %02d:%02d:%02d UTC",
		(int)d[3], kMonthNames[month], readInt16(d), (int)d[4], (int)d[5], (int)d[6]);
}

bool Chunk::keywordInfo(KeywordInfo *kw) const
{
	kw->keyword.clear();
	kw->keywordLen = 0;

	const int nul = data.indexOf('\0');
	if(nul<0) return false;  // no NUL separator

	kw->keywordLen = nul;
	kw->keyword = QString::fromLatin1(data.constData(), qMin(nul, 79));
	return true;
}

// Several chunk types start with a NUL-terminated keyword. In lieu of fully
// handling them, we at least display that keyword.
QString Chunk::describeKeywordChunk(const QString &prefix) const
{
	KeywordInfo kw;
	keywordInfo(&kw);

	if(kw.keywordLen>=1 && kw.keywordLen<=79) {
		return prefix + QStringLiteral(": [") + kw.keyword + QLatin1Char(']');
	}
	return prefix + QStringLiteral(", invalid name length");
}

bool Chunk::setIccProfileName(const QString &name)
{
	KeywordInfo kw;
	if(!keywordInfo(&kw)) return false;

	// mid(keywordLen) keeps the NUL separator and everything after it.
	data = name.toLatin1() + data.mid(kw.keywordLen);
	return true;
}

QString Chunk::description()
{
	switch(id) {
	case CHUNK_IHDR: return describeIHDR();
	case CHUNK_IEND:
		return hasValidLength() ? QStringLiteral("end-of-image marker")
			: lengthError(QStringLiteral("end-of-image marker"));
	case CHUNK_bKGD: return describebKGD();
	case CHUNK_PLTE: return describePLTE();
	case CHUNK_tRNS: return describetRNS();
	case CHUNK_sBIT: return describesBIT();
	case CHUNK_gAMA: return describegAMA();
	case CHUNK_cHRM: return describecHRM();
	case CHUNK_pHYs: return describepHYs();
	case CHUNK_tIME: return describetIME();
	case CHUNK_hIST: return describehIST();
	case CHUNK_tEXt: case CHUNK_zTXt: case CHUNK_iTXt: return describeText();
	case CHUNK_IDAT: return QStringLiteral("PNG image data");
	case CHUNK_sRGB: return describesRGB();
	case CHUNK_iCCP: return describeKeywordChunk(QStringLiteral("embedded ICC profile"));
	case CHUNK_sPLT: return describeKeywordChunk(QStringLiteral("suggested palette"));
	case CHUNK_sTER: return describesTER();
	case CHUNK_oFFs: return describeoFFs();
	case CHUNK_pCAL: return describeKeywordChunk(QStringLiteral("calibration of pixel values"));
	case CHUNK_sCAL: return describesCAL();
	case CHUNK_gIFg: return QStringLiteral("GIF graphic control extension");
	case CHUNK_gIFx: return QStringLiteral("GIF application extension");
	case CHUNK_gIFt: return QStringLiteral("GIF plain text extension [DEPRECATED]");
	case CHUNK_fRAc: return QStringLiteral("parameters for fractal image");
	case CHUNK_dSIG: return QStringLiteral("digital signature data");
	case CHUNK_eXIf: return QStringLiteral("Exif metadata");
	case CHUNK_MHDR: return describeMHDR();
	case CHUNK_MEND: return QStringLiteral("end-of-file marker");
	case CHUNK_LOOP: return QStringLiteral("start of loop");
	case CHUNK_ENDL: return QStringLiteral("end of loop");
	case CHUNK_DEFI: return QStringLiteral("define an object");
	case CHUNK_JHDR: return describeJHDR();
	case CHUNK_BASI: return QStringLiteral("PNG chunks");
	case CHUNK_CLON: return QStringLiteral("clone an object");
	case CHUNK_DHDR: return QStringLiteral("Delta-PNG datastream header");
	case CHUNK_PAST: return QStringLiteral("paste an image into another");
	case CHUNK_DISC: return QStringLiteral("discard objects");
	case CHUNK_BACK: return QStringLiteral("background");
	case CHUNK_FRAM: return QStringLiteral("frame definition");
	case CHUNK_MOVE: return QStringLiteral("new image location");
	case CHUNK_CLIP: return QStringLiteral("object clipping boundaries");
	case CHUNK_SHOW: return QStringLiteral("show images");
	case CHUNK_TERM: return QStringLiteral("termination action");
	case CHUNK_SAVE: return QStringLiteral("save information");
	case CHUNK_SEEK: return QStringLiteral("seek point");
	case CHUNK_eXPI: return QStringLiteral("export image");
	case CHUNK_fPRI: return QStringLiteral("frame priority");
	case CHUNK_nEED: return QStringLiteral("resources needed");
	case CHUNK_pHYg: return QStringLiteral("physical pixel size (global)");
	case CHUNK_JDAT: return QStringLiteral("JNG image data");
	case CHUNK_JSEP: return QStringLiteral("8-bit/12-bit image separator");
	case CHUNK_PROM: return QStringLiteral("promotion of parent object");
	case CHUNK_IPNG: return QStringLiteral("incomplete PNG");
	case CHUNK_PPLT: return QStringLiteral("partial palette");
	case CHUNK_IJNG: return QStringLiteral("incomplete JNG");
	case CHUNK_DROP: return QStringLiteral("drop chunks");
	case CHUNK_DBYK: return QStringLiteral("drop chunks by keyword");
	case CHUNK_ORDR: return QStringLiteral("ordering restrictions");
	case CHUNK_acTL: return describeacTL();
	case CHUNK_fcTL: return describefcTL();
	case CHUNK_fdAT: return describefdAT();
	case CHUNK_CgBI: return QStringLiteral("iPhone PNG-like file header");
	case CHUNK_vpAg: return describevpAg();
	case CHUNK_iDOT: return QStringLiteral("Apple private data");
	}
	return isPublic() ? QStringLiteral("unrecognized chunk type") : QStringLiteral("private chunk");
}

QString Chunk::attributes() const
{
	QString s = isCritical() ? QStringLiteral("critical") : QStringLiteral("ancillary");
	if(!isPublic()) s += QStringLiteral(", private");
	if(!isCritical()) {  // safe-to-copy is not used for critical chunks
		s += isSafeToCopy() ? QStringLiteral(", safe to copy") : QStringLiteral(", unsafe to copy");
	}
	return s;
}

} // namespace twpng
