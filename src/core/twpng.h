// twpng.h -- the Win32-free document model of TweakPNG: a PNG/MNG/JNG file
// represented as a list of chunks.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.
//
// The original program kept this logic inside its window procedures. It now
// lives here, depends only on QtCore, and reports problems through return
// values so that it can be exercised without a user interface.

#ifndef TWEAKPNG_CORE_TWPNG_H
#define TWEAKPNG_CORE_TWPNG_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

#define TWEAKPNG_VERSION_STRING "1.4.6+"
#define TWEAKPNG_HOMEPAGE "https://entropymine.com/jason/tweakpng/"

namespace twpng {

enum ImageType { IMG_UNKNOWN = 0, IMG_PNG = 1, IMG_MNG = 2, IMG_JNG = 3 };

enum ChunkId {
	CHUNK_UNKNOWN = 0,
	CHUNK_IHDR = 1, CHUNK_IEND, CHUNK_IDAT, CHUNK_PLTE, CHUNK_bKGD, CHUNK_cHRM,
	CHUNK_gAMA, CHUNK_hIST, CHUNK_pHYs, CHUNK_sBIT, CHUNK_tEXt, CHUNK_tIME,
	CHUNK_tRNS, CHUNK_zTXt, CHUNK_sRGB, CHUNK_iCCP, CHUNK_iTXt, CHUNK_sPLT,
	CHUNK_sTER, CHUNK_dSIG, CHUNK_eXIf,

	// MNG
	CHUNK_MHDR = 200, CHUNK_LOOP, CHUNK_ENDL, CHUNK_DEFI, CHUNK_JHDR, CHUNK_BASI,
	CHUNK_CLON, CHUNK_DHDR, CHUNK_PAST, CHUNK_DISC, CHUNK_BACK, CHUNK_FRAM,
	CHUNK_MOVE, CHUNK_CLIP, CHUNK_SHOW, CHUNK_TERM, CHUNK_SAVE, CHUNK_SEEK,
	CHUNK_MEND,
	CHUNK_eXPI = 230, CHUNK_fPRI, CHUNK_nEED, CHUNK_pHYg, CHUNK_JDAT, CHUNK_JSEP,
	CHUNK_PROM, CHUNK_IPNG, CHUNK_PPLT, CHUNK_IJNG, CHUNK_DROP, CHUNK_DBYK,
	CHUNK_ORDR,

	// APNG
	CHUNK_acTL = 300, CHUNK_fcTL, CHUNK_fdAT,

	// Other known private chunks
	CHUNK_CgBI = 310, CHUNK_vpAg, CHUNK_iDOT,

	// Registered PNG extensions
	CHUNK_oFFs = 501, CHUNK_pCAL, CHUNK_sCAL, CHUNK_gIFg, CHUNK_gIFx, CHUNK_gIFt,
	CHUNK_fRAc
};

quint32 readInt32(const uchar *x);
int readInt16(const uchar *x);
void writeInt32(uchar *buf, quint32 x);
void writeInt16(uchar *buf, int x);
quint32 crc32Of(const char *type, const QByteArray &data);

// Deflate/inflate helpers. compressData uses the level from
// setCompressionLevel(). uncompressData refuses output above kMaxInflatedSize,
// so a tiny malicious zTXt chunk cannot exhaust memory.
const qint64 kMaxInflatedSize = 512 * 1024 * 1024;
void setCompressionLevel(int level);
int compressionLevel();
bool compressData(const QByteArray &in, QByteArray *out);
bool uncompressData(const char *in, int inlen, QByteArray *out, QString *error);

class Png;

// Parsed contents of a tEXt, zTXt or iTXt chunk.
struct TextInfo {
	QString keyword;
	QString language;
	QString translatedKeyword;
	QString text;
	bool hasText = false;      // false if the text could not be decoded
	bool isCompressed = false;
};

struct SCalData {
	int units = 1;
	QString x;
	QString y;
};

struct KeywordInfo {
	int keywordLen = 0;  // length in the chunk; not limited to 79
	QString keyword;
};

class Chunk {
public:
	Chunk();

	QByteArray data;
	char type[5];     // always NUL-terminated
	int id = CHUNK_UNKNOWN;
	quint32 crc = 0;
	Png *parent = nullptr;
	bool flag = false; // scratch: used to remember a selection across moves

	quint32 length() const { return (quint32)data.size(); }
	const uchar *bytes() const { return (const uchar*)data.constData(); }
	uchar *mutableBytes() { return (uchar*)data.data(); }
	QString typeName() const { return QString::fromLatin1(type, 4); }

	void setType(const char *name);  // copies 4 bytes
	void afterInit();                // sets id; pushes IHDR fields to the parent
	void chunkModified();            // recalculates the CRC
	quint32 calcCrc() const;

	bool isCritical() const { return (type[0] & 0x20) == 0; }
	bool isPublic() const { return (type[1] & 0x20) == 0; }
	bool isSafeToCopy() const { return (type[3] & 0x20) != 0; }

	bool hasValidLength() const;
	bool canEdit() const;

	QString description();         // the "Contents" column
	QString attributes() const;    // the "Attributes" column

	// The complete on-disk form: length, type, data, CRC.
	QByteArray serialized() const;

	// Parses a serialized chunk. Returns the bytes consumed; 0 on error.
	int initFromMemory(const uchar *m, qint64 msize);

	// Text chunks. The parse is cached until setTextInfo() or invalidateTextInfo().
	bool textInfo(TextInfo *out);
	bool setTextInfo(const QString &keyword, const QString &language,
		const QString &translatedKeyword, const QString &text,
		bool compressed, bool international);
	void invalidateTextInfo();

	SCalData sCalData() const;
	void setSCalData(const SCalData &d);

	bool keywordInfo(KeywordInfo *kw) const;

	// iCCP: replaces the profile name, keeping the data after it.
	bool setIccProfileName(const QString &name);

private:
	QString describeIHDR() const;
	QString describeJHDR() const;
	QString describeMHDR() const;
	QString describebKGD() const;
	QString describehIST() const;
	QString describePLTE() const;
	QString describetRNS() const;
	QString describesRGB() const;
	QString describesTER() const;
	QString describegAMA() const;
	QString describesBIT() const;
	QString describecHRM() const;
	QString describepHYs() const;
	QString describeacTL() const;
	QString describefcTL() const;
	QString describefdAT() const;
	QString describeoFFs() const;
	QString describevpAg() const;
	QString describesCAL() const;
	QString describetIME() const;
	QString describeText();
	QString describeKeywordChunk(const QString &prefix) const;
	QString lengthError(const QString &name) const;

	int readTextField(int offset, QString *dst, int dstlen, bool floatingPoint) const;
	bool parseTextInfo();

	TextInfo m_text;
	bool m_textProcessed = false;
	bool m_textOk = false;
};

// Everything the palette editor works on: PLTE, plus tRNS and bKGD if present.
struct PaletteEntry { uchar red, green, blue, alpha; };

struct PaletteInfo {
	int numPlte = 0, minPlte = 1, maxPlte = 0;
	int numTrns = 0, minTrns = 0;   // minTrns is 1 if a tRNS chunk exists
	int numBkgd = 0;                // 1 if a usable bKGD chunk exists
	PaletteEntry plte[256];
	uchar bkgd = 0;
	uchar trnsColor = 0;            // used only for grayscale images
	bool canEditColors = true;      // false for grayscale
	bool bkgdButton = false;        // initial state of the Set-bkgd toggle
	bool alphaButton = false;       // initial state of the Edit-alpha toggle
	Chunk *chPlte = nullptr;
	Chunk *chTrns = nullptr;
	Chunk *chBkgd = nullptr;
};

struct ValidityResult {
	bool ok = true;
	QString message;
};

// Where a new chunk went, or why it could not be created.
struct NewChunkResult {
	int pos = -1;
	QString error;
	bool notice = false;  // the error is a warning, not a failure of the file
};

class Png {
public:
	Png();

	// Builds a document from a file image. On failure returns nullptr and sets
	// *error. Non-fatal problems (bad CRCs, trailing garbage) go in *warnings.
	static std::unique_ptr<Png> fromBytes(const QByteArray &bytes, QString *error,
		QStringList *warnings);
	static std::unique_ptr<Png> fromFile(const QString &loadName, const QString &saveName,
		QString *error, QStringList *warnings);

	int count() const { return (int)m_chunks.size(); }
	Chunk *chunk(int i) const { return m_chunks[(size_t)i].get(); }

	QByteArray toBytes() const;
	bool writeFile(const QString &fn, QString *error) const;
	quint32 fileSize() const;

	void setImageType(int type);
	int imageType() const { return m_imgType; }

	// Structural checks only. If reportNonPng is false, MNG and JNG files pass.
	ValidityResult checkValidity(bool reportNonPng = true) const;

	Chunk *findFirstChunk(int id, int *index = nullptr) const;
	void insertChunk(int pos, std::unique_ptr<Chunk> c);
	void deleteChunk(int n);
	void moveChunk(int n, int delta);
	NewChunkResult newChunk(int id);

	// Returns how many chunks a split would produce.
	int splitCount(int n, int size, bool repeat) const;
	bool splitIdat(int n, int size, bool repeat);

	void combineRange(int first, int last);
	// Returns an error message, or an empty string with *first set.
	QString combineSelected(const QVector<int> &selected, int *first);
	// Merges every run of IDAT/JDAT. Returns an error message, or an empty string.
	QString combineAll(int *numRanges, int *lastFirst);

	bool importChunkBytes(const QByteArray &bytes, int pos, QString *error);
	bool importIccProfile(const QByteArray &profile, int *pos, QString *error);
	QByteArray exportChunk(int n) const;

	// The clipboard format: a 4-byte total size, then serialized chunks.
	QByteArray chunksToClipboard(const QVector<int> &rows) const;
	int pasteClipboard(const QByteArray &clip, int pos);

	bool loadPaletteInfo(Chunk *ch, PaletteInfo *p, QString *error) const;
	void applyPaletteInfo(const PaletteInfo &p) const;

	// Fields read from IHDR.
	quint32 width = 1;
	quint32 height = 1;
	uchar bitDepth = 1;
	uchar colorType = 0;

	QString filename = QStringLiteral("untitled");
	bool named = false;
	bool dirty = false;

private:
	bool findIdatRange(int *first, int *last) const;

	std::vector<std::unique_ptr<Chunk>> m_chunks;
	int m_imgType = IMG_PNG;
	uchar m_signature[8];
};

bool canEditChunkType(int id);
QString chunkNameFromId(int id);

// Reads the 12-byte-per-tag ICC profile structure into readable text.
// Takes the whole iCCP chunk; handles the name and compression method itself.
QString dumpIccChunk(const Chunk &ch);

extern const char *const kKnownTextKeys[];
extern const char *const kMonthNames[];

} // namespace twpng

#endif // TWEAKPNG_CORE_TWPNG_H
