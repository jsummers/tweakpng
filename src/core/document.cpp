// document.cpp -- a PNG/MNG/JNG file as a list of chunks.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "twpng.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>

#include <climits>
#include <cstring>

namespace twpng {

namespace {

const uchar kSigPng[8] = {137,80,78,71,13,10,26,10};
const uchar kSigMng[8] = {138,77,78,71,13,10,26,10};
const uchar kSigJng[8] = {139,74,78,71,13,10,26,10};
const int kMaxChunks = 1024000;
const QChar kRsquo(0x2019);

} // namespace

Png::Png()
{
	memcpy(m_signature, kSigPng, 8);
}

void Png::setImageType(int type)
{
	m_imgType = type;
	switch(type) {
	case IMG_PNG: memcpy(m_signature, kSigPng, 8); break;
	case IMG_MNG: memcpy(m_signature, kSigMng, 8); break;
	case IMG_JNG: memcpy(m_signature, kSigJng, 8); break;
	}
}

std::unique_ptr<Png> Png::fromBytes(const QByteArray &bytes, QString *error, QStringList *warnings)
{
	std::unique_ptr<Png> png(new Png);
	png->colorType = 255;  // invalid until an IHDR is read

	const qint64 fileSize = bytes.size();
	const uchar *f = (const uchar*)bytes.constData();

	int type = IMG_UNKNOWN;
	if(fileSize>=8) {
		if(!memcmp(f, kSigPng, 8)) type = IMG_PNG;
		else if(!memcmp(f, kSigMng, 8)) type = IMG_MNG;
		else if(!memcmp(f, kSigJng, 8)) type = IMG_JNG;
	}
	if(type==IMG_UNKNOWN) {
		if(error) *error = QStringLiteral("Unrecognized file format\n\nThis is not a valid PNG file.");
		return nullptr;
	}
	png->setImageType(type);

	qint64 pos = 8;
	while(pos < fileSize) {
		if(fileSize-pos < 8) {
			if(warnings) warnings->append(QStringLiteral("Garbage found at end of file"));
			break;
		}

		const quint32 length = readInt32(f+pos);
		char name[5];
		memcpy(name, f+pos+4, 4);
		name[4] = '\0';

		bool validType = true;
		for(int i=0; i<4; i++) {
			const char c = name[i];
			if(!((c>='a' && c<='z') || (c>='A' && c<='Z'))) validType = false;
		}
		if(!validType) {
			if(warnings) {
				warnings->append(QStringLiteral("Invalid chunk type found at file position %1. "
					"This may indicate garbage at the end of the file.").arg(pos));
			}
			break;
		}

		if(length>0 && (qint64)length > fileSize-pos-12) {
			if(warnings) {
				warnings->append(QStringLiteral("Bogus chunk length found. This may indicate "
					"garbage at the end of the file."));
			}
			break;
		}
		if((qint64)length + 12 > fileSize-pos) {
			if(warnings) warnings->append(QStringLiteral("Garbage found at end of file"));
			break;
		}

		if(png->count() >= kMaxChunks) {
			if(warnings) warnings->append(QStringLiteral("Too many chunks; the remainder of the file was ignored"));
			break;
		}

		std::unique_ptr<Chunk> c(new Chunk);
		c->parent = png.get();
		c->setType(name);
		c->data = QByteArray((const char*)f+pos+8, (int)length);
		c->crc = readInt32(f+pos+8+length);

		const quint32 ccrc = c->calcCrc();
		if(c->crc != ccrc) {
			if(warnings) {
				warnings->append(QString::asprintf("Incorrect crc for %s chunk (is %08x, should be %08x)",
					name, c->crc, ccrc));
			}
			c->crc = ccrc;  // correct it
		}

		c->afterInit();
		png->m_chunks.push_back(std::move(c));
		pos += (qint64)length + 12;
	}

	return png;
}

std::unique_ptr<Png> Png::fromFile(const QString &loadName, const QString &saveName,
	QString *error, QStringList *warnings)
{
	QFile f(loadName);
	if(!f.open(QIODevice::ReadOnly)) {
		if(error) *error = QStringLiteral("Can") + kRsquo + QStringLiteral("t open file (%1)").arg(loadName);
		return nullptr;
	}
	if(f.size() > INT_MAX) {
		if(error) *error = QStringLiteral("File is too large (limit is 2 GB)");
		return nullptr;
	}

	std::unique_ptr<Png> png = fromBytes(f.readAll(), error, warnings);
	if(png) {
		png->filename = saveName;
		png->named = true;
		png->dirty = false;
	}
	return png;
}

quint32 Png::fileSize() const
{
	quint32 s = 8;
	for(const auto &c : m_chunks) {
		s += 12 + c->length();
	}
	return s;
}

QByteArray Png::toBytes() const
{
	QByteArray out;
	out.reserve((int)fileSize());
	out.append((const char*)m_signature, 8);
	for(const auto &c : m_chunks) {
		out.append(c->serialized());
	}
	return out;
}

// QSaveFile writes to a temporary file and renames it, so a failed save (a
// full disk, say) cannot destroy the file being edited.
bool Png::writeFile(const QString &fn, QString *error) const
{
	QSaveFile f(fn);
	if(f.open(QIODevice::WriteOnly)) {
		const QByteArray bytes = toBytes();
		if(f.write(bytes)==bytes.size() && f.commit()) return true;
	}
	if(error) *error = QStringLiteral("Can") + kRsquo + QStringLiteral("t write file (%1)").arg(fn);
	return false;
}

Chunk *Png::findFirstChunk(int id, int *index) const
{
	for(int i=0; i<count(); i++) {
		if(chunk(i)->id==id) {
			if(index) *index = i;
			return chunk(i);
		}
	}
	if(index) *index = -1;
	return nullptr;
}

void Png::insertChunk(int pos, std::unique_ptr<Chunk> c)
{
	c->parent = this;
	m_chunks.insert(m_chunks.begin()+pos, std::move(c));
}

void Png::deleteChunk(int n)
{
	if(n<0 || n>=count()) return;
	m_chunks.erase(m_chunks.begin()+n);
}

void Png::moveChunk(int n, int delta)
{
	if(n<0 || n>=count()) return;
	const int moveTo = qBound(0, n+delta, count()-1);
	if(moveTo==n) return;

	std::unique_ptr<Chunk> c = std::move(m_chunks[(size_t)n]);
	m_chunks.erase(m_chunks.begin()+n);
	m_chunks.insert(m_chunks.begin()+moveTo, std::move(c));
}

NewChunkResult Png::newChunk(int newid)
{
	NewChunkResult r;
	std::unique_ptr<Chunk> c(new Chunk);
	c->parent = this;

	const QString name = chunkNameFromId(newid);
	if(name==QLatin1String("????")) {
		r.error = QStringLiteral("internal: chunk name not found for %1").arg(newid);
		return r;
	}
	c->setType(name.toLatin1().constData());

	int pltePos = -1;  // stays -1 if no PLTE chunk exists
	findFirstChunk(CHUNK_PLTE, &pltePos);

	int pos = 1;
	bool ok = true;
	QByteArray &d = c->data;

	const auto sized = [&d](int n) {
		d = QByteArray(n, '\0');
		return (uchar*)d.data();
	};

	switch(newid) {
	case CHUNK_tEXt:
		pos = count()-1;
		d = QByteArray("Comment", 8);  // includes the NUL
		break;

	case CHUNK_gAMA:
		writeInt32(sized(4), 45455);
		break;

	case CHUNK_oFFs:
		sized(9);
		break;

	case CHUNK_pHYs:
		{
			uchar *m = sized(9);
			writeInt32(m, 1);
			writeInt32(m+4, 1);
		}
		break;

	case CHUNK_sRGB:
	case CHUNK_sTER:
		sized(1);
		break;

	case CHUNK_cHRM:
		{
			uchar *m = sized(32);
			writeInt32(m+ 0, 31270);
			writeInt32(m+ 4, 32900);
			writeInt32(m+ 8, 64000);
			writeInt32(m+12, 33000);
			writeInt32(m+16, 30000);
			writeInt32(m+20, 60000);
			writeInt32(m+24, 15000);
			writeInt32(m+28,  6000);
		}
		break;

	case CHUNK_bKGD:
		if(pltePos>=0) pos = pltePos+1;
		switch(colorType) {
		case 3: sized(1); break;
		case 0: case 4: sized(2); break;
		case 2: case 6: sized(6); break;
		default: return r;
		}
		break;

	case CHUNK_sBIT:
		switch(colorType) {
		case 0: sized(1); break;
		case 2: case 3: sized(3); break;
		case 4: sized(2); break;
		case 6: sized(4); break;
		default: return r;
		}
		d.fill((char)((colorType==3) ? 8 : bitDepth));
		break;

	case CHUNK_PLTE:
		if(colorType!=2 && colorType!=3 && colorType!=6) {
			r.error = QStringLiteral("Palette is not allowed for grayscale images");
			return r;
		}
		sized(3);
		break;

	case CHUNK_tRNS:
		if(pltePos>=0) pos = pltePos+1;
		switch(colorType) {
		case 3: sized(1)[0] = 255; break;  // default to opaque
		case 0: sized(2); break;
		case 2: sized(6); break;
		case 4: case 6:
			r.error = QStringLiteral("Transparency chunk is not allowed for this image, "
				"since it already has a full alpha channel");
			return r;
		default: return r;
		}
		break;

	case CHUNK_IHDR:
		{
			pos = 0;
			uchar *m = sized(13);
			writeInt32(m, 1);    // width
			writeInt32(m+4, 1);  // height
			m[8] = 1;            // bit depth
		}
		break;

	case CHUNK_IEND:
		pos = count();
		break;

	case CHUNK_tIME:
		{
			pos = count()-1;
			const QDateTime now = QDateTime::currentDateTimeUtc();
			uchar *m = sized(7);
			writeInt16(m, now.date().year());
			m[2] = (uchar)now.date().month();
			m[3] = (uchar)now.date().day();
			m[4] = (uchar)now.time().hour();
			m[5] = (uchar)now.time().minute();
			m[6] = (uchar)now.time().second();
		}
		break;

	case CHUNK_sCAL:
		d = QByteArray("\x01" "1.0\0" "1.0", 8);
		break;

	case CHUNK_acTL:
		{
			uchar *m = sized(8);
			writeInt32(m, 1);
			writeInt32(m+4, 0);
		}
		break;

	case CHUNK_fcTL:
		{
			uchar *m = sized(26);
			writeInt32(m+4, width);
			writeInt32(m+8, height);
			writeInt16(m+20, 100);  // delay numerator
			writeInt16(m+22, 100);  // delay denominator
		}
		break;

	case CHUNK_vpAg:
		{
			uchar *m = sized(9);
			writeInt32(m, width);
			writeInt32(m+4, height);
		}
		break;

	default:
		ok = false;
	}

	if(!ok) {
		r.error = QStringLiteral("Not implemented");
		r.notice = true;
		return r;
	}

	pos = qBound(0, pos, count());
	c->afterInit();
	c->chunkModified();
	insertChunk(pos, std::move(c));
	r.pos = pos;
	return r;
}

int Png::splitCount(int n, int size, bool repeat) const
{
	if(!repeat) return 2;
	if(size<=0) return 0;
	const qint64 len = chunk(n)->length();
	return qMax(1, (int)((len + size - 1) / size));
}

bool Png::splitIdat(int n, int size, bool repeat)
{
	if(n<0 || n>=count()) return false;

	const int pieces = splitCount(n, size, repeat);
	if(pieces<1 || size<0 || (quint32)size > chunk(n)->length()) return false;

	std::unique_ptr<Chunk> old = std::move(m_chunks[(size_t)n]);
	m_chunks.erase(m_chunks.begin()+n);

	int used = 0;
	for(int i=0; i<pieces; i++) {
		const int thisSize = (i<pieces-1) ? size : (int)old->length()-used;

		std::unique_ptr<Chunk> c(new Chunk);
		c->setType(old->type);
		c->data = old->data.mid(used, thisSize);
		used += thisSize;
		insertChunk(n+i, std::move(c));
		chunk(n+i)->afterInit();
		chunk(n+i)->chunkModified();
	}
	return true;
}

void Png::combineRange(int first, int last)
{
	std::unique_ptr<Chunk> c(new Chunk);
	c->setType(chunk(first)->type);
	for(int i=first; i<=last; i++) {
		c->data.append(chunk(i)->data);
	}

	m_chunks.erase(m_chunks.begin()+first, m_chunks.begin()+last+1);
	insertChunk(first, std::move(c));
	chunk(first)->afterInit();
	chunk(first)->chunkModified();
}

QString Png::combineSelected(const QVector<int> &selected, int *firstOut)
{
	int first = -1;
	int last = -1;
	int idatCount = 0;
	int jdatCount = 0;

	for(int i=0; i<count(); i++) {
		if(selected.contains(i)) {
			const int t = chunk(i)->id;
			if(t==CHUNK_IDAT) idatCount++;
			else if(t==CHUNK_JDAT) jdatCount++;
			else return QStringLiteral("Only IDAT chunks may be selected");

			if(first==-1) first = i;
			else if(last!=-1) return QStringLiteral("Selected chunks must be consecutive");
		}
		else if(first!=-1 && last==-1) {
			last = i-1;
		}
	}

	if(first==-1) return QStringLiteral("Must select IDAT chunks first");
	if(last==-1) last = count()-1;
	if(idatCount>0 && jdatCount>0) return QStringLiteral("Selected chunks must be the same type");
	if(last<=first) return QStringLiteral("Must select more than one IDAT chunk");

	combineRange(first, last);
	if(firstOut) *firstOut = first;
	return QString();
}

// Locates the first run of 2 or more IDAT or JDAT chunks.
bool Png::findIdatRange(int *first, int *last) const
{
	int prev = CHUNK_UNKNOWN;
	bool inRun = false;

	for(int i=0; i<count(); i++) {
		const int t = chunk(i)->id;
		if(inRun) {
			if(t==prev) {
				*last = i;
			}
			else {
				*last = i-1;
				return true;
			}
		}
		else if(t==prev && (t==CHUNK_IDAT || t==CHUNK_JDAT)) {
			inRun = true;
			*first = i-1;
			*last = i;
		}
		prev = t;
	}
	return inRun;
}

QString Png::combineAll(int *numRanges, int *lastFirst)
{
	bool any = false;
	for(int i=0; i<count(); i++) {
		if(chunk(i)->id==CHUNK_IDAT || chunk(i)->id==CHUNK_JDAT) any = true;
	}
	if(!any) return QStringLiteral("No IDAT chunks present");

	int ranges = 0;
	int first = -1;
	int last = -1;
	while(findIdatRange(&first, &last)) {
		combineRange(first, last);
		ranges++;
		if(lastFirst) *lastFirst = first;
	}
	if(numRanges) *numRanges = ranges;
	return QString();
}

bool Png::importChunkBytes(const QByteArray &bytes, int pos, QString *error)
{
	if(bytes.size()<4) {
		if(error) *error = QStringLiteral("Not a valid chunk file: it must begin with a 4-byte chunk type");
		return false;
	}

	std::unique_ptr<Chunk> c(new Chunk);
	c->setType(bytes.constData());
	c->data = bytes.mid(4);
	c->parent = this;
	c->afterInit();
	c->chunkModified();
	insertChunk(qBound(0, pos, count()), std::move(c));
	return true;
}

QByteArray Png::exportChunk(int n) const
{
	const Chunk *c = chunk(n);
	return QByteArray(c->type, 4) + c->data;
}

// The original read file size minus 4 here, a copy-paste from the chunk
// importer, so every imported profile lost its last four bytes.
bool Png::importIccProfile(const QByteArray &profile, int *pos, QString *error)
{
	if(profile.size()<128 || profile.size()>100000000) {
		if(error) *error = QStringLiteral("This does not look like an ICC profile (bad size)");
		return false;
	}

	QByteArray cmpr;
	if(!compressData(profile, &cmpr)) {
		if(error) *error = QStringLiteral("error compressing data");
		return false;
	}

	std::unique_ptr<Chunk> c(new Chunk);
	c->setType("iCCP");
	c->data = QByteArray("ICC profile", 12);  // name and NUL separator
	c->data.append('\0');                     // compression method
	c->data.append(cmpr);
	c->parent = this;
	c->afterInit();
	c->chunkModified();

	// Make the new iCCP chunk the second chunk in the file if possible.
	const int p = (count()>0) ? 1 : 0;
	insertChunk(p, std::move(c));
	if(pos) *pos = p;
	return true;
}

QByteArray Png::chunksToClipboard(const QVector<int> &rows) const
{
	QByteArray body;
	for(int i : rows) {
		body.append(chunk(i)->serialized());
	}
	if(body.isEmpty()) return QByteArray();

	QByteArray out(4, '\0');
	writeInt32((uchar*)out.data(), (quint32)body.size()+4);
	out.append(body);
	return out;
}

int Png::pasteClipboard(const QByteArray &clip, int pos)
{
	if(clip.size()<4) return 0;
	const uchar *m = (const uchar*)clip.constData();
	const qint64 msize = readInt32(m);
	if(msize>clip.size()) return 0;

	int added = 0;
	qint64 p = 4;
	while(p<msize) {
		std::unique_ptr<Chunk> c(new Chunk);
		c->parent = this;
		const int r = c->initFromMemory(m+p, msize-p);
		if(!r) break;
		insertChunk(pos+added, std::move(c));
		added++;
		p += r;
	}
	return added;
}

// Structural checks only: chunk number and ordering, not parameter values.
ValidityResult Png::checkValidity(bool reportNonPng) const
{
	ValidityResult res;
	QString m = QStringLiteral("No problems found.");
	int e = 0;

	if(m_imgType!=IMG_PNG) {
		if(reportNonPng) {
			res.ok = false;
			res.message = QStringLiteral("Can only check PNG files");
		}
		else {
			res.message = m;
		}
		return res;
	}

	int idatSeen=0, plteSeen=0, chrmSeen=0, gamaSeen=0;
	int iccpSeen=0, sbitSeen=0, srgbSeen=0, bkgdSeen=0;
	int histSeen=0, trnsSeen=0, physSeen=0, timeSeen=0;
	int sterSeen=0, offsSeen=0, pcalSeen=0, scalSeen=0;
	int dsigPending=0;
	int dsigNesting=0;
	int prev = CHUNK_UNKNOWN;

	const auto fail = [&e, &m](const char *msg) { e++; m = QLatin1String(msg); };

	for(int i=0; i<count() && e==0; i++) {
		const int t = chunk(i)->id;

		if(chunk(i)->isCritical() && t!=CHUNK_IHDR && t!=CHUNK_PLTE && t!=CHUNK_IDAT && t!=CHUNK_IEND) {
			fail("Unrecognized critical chunk");
		}

		if(dsigPending && t!=CHUNK_dSIG) {
			if(t!=CHUNK_IEND) fail("Misplaced dSIG chunk");
			dsigPending = 0;
		}

		if(i==0 && t!=CHUNK_IHDR) {
			fail("First chunk must be IHDR");
		}
		else if(i==count()-1 && t!=CHUNK_IEND) {
			fail("Last chunk must be IEND");
		}
		else {
			switch(t) {
			case CHUNK_IHDR:
				if(i!=0) fail("Misplaced or extra IHDR");
				break;
			case CHUNK_IEND:
				if(i!=count()-1) fail("Misplaced or extra IEND");
				break;
			case CHUNK_IDAT:
				if(idatSeen && prev!=CHUNK_IDAT) fail("IDAT chunks must be consecutive");
				idatSeen++;
				break;
			case CHUNK_PLTE:
				if(plteSeen) fail("Multiple PLTE chunks not allowed");
				else if(colorType==0 || colorType==4) fail("PLTE chunk not allowed in grayscale image");
				else if(idatSeen) fail("PLTE must appear before IDAT");
				else if(bkgdSeen) fail("bKGD must appear after PLTE");
				else if(histSeen) fail("hIST must appear after PLTE");
				else if(trnsSeen) fail("tRNS must appear after PLTE");
				plteSeen++;
				break;
			case CHUNK_tIME:
				if(timeSeen) fail("Multiple tIME chunks not allowed");
				timeSeen++;
				break;
			case CHUNK_cHRM:
				if(chrmSeen) fail("Multiple cHRM chunks not allowed");
				else if(plteSeen) fail("cHRM must appear before PLTE");
				else if(idatSeen) fail("cHRM must appear before IDAT");
				chrmSeen++;
				break;
			case CHUNK_gAMA:
				if(gamaSeen) fail("Multiple gAMA chunks not allowed");
				else if(plteSeen) fail("gAMA must appear before PLTE");
				else if(idatSeen) fail("gAMA must appear before IDAT");
				gamaSeen++;
				break;
			case CHUNK_iCCP:
				if(iccpSeen) fail("Multiple iCCP chunks not allowed");
				else if(plteSeen) fail("iCCP must appear before PLTE");
				else if(idatSeen) fail("iCCP must appear before IDAT");
				iccpSeen++;
				break;
			case CHUNK_sBIT:
				if(sbitSeen) fail("Multiple sBIT chunks not allowed");
				else if(plteSeen) fail("sBIT must appear before PLTE");
				else if(idatSeen) fail("sBIT must appear before IDAT");
				sbitSeen++;
				break;
			case CHUNK_sRGB:
				if(srgbSeen) fail("Multiple sRGB chunks not allowed");
				else if(plteSeen) fail("sRGB must appear before PLTE");
				else if(idatSeen) fail("sRGB must appear before IDAT");
				srgbSeen++;
				break;
			case CHUNK_sTER:
				if(sterSeen) fail("Multiple sTER chunks not allowed");
				else if(idatSeen) fail("sTER must appear before IDAT");
				sterSeen++;
				break;
			case CHUNK_bKGD:
				if(bkgdSeen) fail("Multiple bKGD chunks not allowed");
				else if(idatSeen) fail("bKGD must appear before IDAT");
				bkgdSeen++;
				break;
			case CHUNK_hIST:
				if(histSeen) fail("Multiple hIST chunks not allowed");
				else if(idatSeen) fail("hIST must appear before IDAT");
				histSeen++;
				break;
			case CHUNK_tRNS:
				if(trnsSeen) fail("Multiple tRNS chunks not allowed");
				else if(idatSeen) fail("tRNS must appear before IDAT");
				trnsSeen++;
				break;
			case CHUNK_pHYs:
				if(physSeen) fail("Multiple pHYs chunks not allowed");
				else if(idatSeen) fail("pHYs must appear before IDAT");
				physSeen++;
				break;
			case CHUNK_sPLT:
				if(idatSeen) fail("sPLT must appear before IDAT");
				break;
			case CHUNK_dSIG:
				if(prev!=CHUNK_IHDR && prev!=CHUNK_dSIG) {
					dsigPending = 1;  // the next non-dSIG chunk must be IEND
				}
				if(idatSeen) dsigNesting--;
				else dsigNesting++;
				break;
			case CHUNK_oFFs:
				if(offsSeen) fail("Multiple oFFs chunks not allowed");
				else if(idatSeen) fail("oFFs must appear before IDAT");
				offsSeen++;
				break;
			case CHUNK_pCAL:
				if(pcalSeen) fail("Multiple pCAL chunks not allowed");
				else if(idatSeen) fail("pCAL must appear before IDAT");
				pcalSeen++;
				break;
			case CHUNK_sCAL:
				if(scalSeen) fail("Multiple sCAL chunks not allowed");
				else if(idatSeen) fail("sCAL must appear before IDAT");
				scalSeen++;
				break;
			}
		}
		prev = t;
	}

	if(e==0) {
		if(count()<1) fail("No chunks. Not valid.");
		else if(!idatSeen) fail("Required IDAT chunk not found");
		else if(colorType==3 && !plteSeen) fail("Required PLTE chunk not found");
		else if(dsigNesting!=0) fail("Mismatched dSIG chunks");
	}

	res.ok = (e==0);
	res.message = m;
	return res;
}

bool Png::loadPaletteInfo(Chunk *ch, PaletteInfo *p, QString *error) const
{
	*p = PaletteInfo();

	const bool grayscale = (colorType==0 || colorType==4);

	for(int i=0; i<256; i++) {
		p->plte[i] = PaletteEntry{0, 0, 0, 255};
	}

	// The palette arrays hold 256 entries, so the size is limited here even
	// for a 16-bit image or an illegal bit depth. The original was not, and
	// the PLTE of a 16-bit grayscale image overflowed its array.
	int maxpal = 256;
	if(bitDepth>=1 && bitDepth<8) maxpal = 1<<bitDepth;

	Chunk *chPlte = nullptr;
	Chunk *chTrns = nullptr;
	Chunk *chBkgd = nullptr;

	switch(ch->id) {
	case CHUNK_PLTE:
		chPlte = ch;
		break;
	case CHUNK_tRNS:
		chTrns = ch;
		p->alphaButton = true;
		break;
	case CHUNK_bKGD:
		chBkgd = ch;
		p->bkgdButton = true;
		break;
	}

	if(!chPlte && !grayscale) chPlte = findFirstChunk(CHUNK_PLTE);
	if(!chTrns && colorType==3) chTrns = findFirstChunk(CHUNK_tRNS);
	if(!chBkgd && colorType==3) chBkgd = findFirstChunk(CHUNK_bKGD);

	if(!chPlte && !grayscale) {
		if(error) *error = QStringLiteral("Need a PLTE chunk to edit this");
		return false;
	}

	if(chTrns) {
		p->minTrns = 1;
		if(grayscale) {
			p->numTrns = 1;
			if(chTrns->length()==2) p->trnsColor = (uchar)readInt16(chTrns->bytes());
		}
		else {
			p->numTrns = qMin((qint64)chTrns->length(), (qint64)maxpal);
			for(int i=0; i<p->numTrns; i++) {
				p->plte[i].alpha = chTrns->bytes()[i];
			}
		}
	}

	if(chBkgd) {
		if(grayscale) {
			if(chBkgd->length()==2) {
				p->numBkgd = 1;
				p->bkgd = (uchar)readInt16(chBkgd->bytes());
			}
		}
		else if(chBkgd->length()==1) {
			p->numBkgd = 1;
			p->bkgd = chBkgd->bytes()[0];
		}
	}

	p->maxPlte = maxpal;
	if(grayscale) {
		p->numPlte = maxpal;
		const int step = (maxpal>1) ? 255/(maxpal-1) : 255;
		for(int i=0; i<maxpal; i++) {
			const uchar v = (uchar)(i*step);
			p->plte[i].red = p->plte[i].green = p->plte[i].blue = v;
		}
		p->canEditColors = false;
	}
	else {
		p->numPlte = qMin((qint64)(chPlte->length()/3), (qint64)maxpal);
		for(int i=0; i<p->numPlte; i++) {
			p->plte[i].red   = chPlte->bytes()[3*i];
			p->plte[i].green = chPlte->bytes()[3*i+1];
			p->plte[i].blue  = chPlte->bytes()[3*i+2];
		}
	}

	p->chPlte = chPlte;
	p->chTrns = chTrns;
	p->chBkgd = chBkgd;
	return true;
}

void Png::applyPaletteInfo(const PaletteInfo &p) const
{
	const bool grayscale = !p.canEditColors;

	// Writing is limited to a chunk we were able to read. The original also
	// wrote to a bKGD of the wrong length, including one with no data at all.
	if(p.chBkgd && p.numBkgd) {
		if(grayscale) writeInt16(p.chBkgd->mutableBytes(), p.bkgd);
		else p.chBkgd->mutableBytes()[0] = p.bkgd;
		p.chBkgd->chunkModified();
	}

	if(p.chTrns) {
		if(grayscale) {
			if(p.chTrns->length()==2) writeInt16(p.chTrns->mutableBytes(), p.trnsColor);
		}
		else {
			p.chTrns->data.resize(p.numTrns);
			for(int i=0; i<p.numTrns; i++) {
				p.chTrns->mutableBytes()[i] = p.plte[i].alpha;
			}
		}
		p.chTrns->chunkModified();
	}

	if(p.chPlte) {
		p.chPlte->data.resize(3*p.numPlte);
		for(int i=0; i<p.numPlte; i++) {
			p.chPlte->mutableBytes()[3*i]   = p.plte[i].red;
			p.chPlte->mutableBytes()[3*i+1] = p.plte[i].green;
			p.chPlte->mutableBytes()[3*i+2] = p.plte[i].blue;
		}
		p.chPlte->chunkModified();
	}
}

} // namespace twpng
