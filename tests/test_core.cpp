// test_core.cpp -- tests of the file model, with no user interface involved.

#include "pngbuilder.h"
#include "pngimage.h"
#include "twpng.h"

#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

using namespace twpng;

namespace {

const QChar kTimes(0x00d7);

std::unique_ptr<Png> load(const QByteArray &bytes, QStringList *warnings = nullptr)
{
	QString error;
	std::unique_ptr<Png> png = Png::fromBytes(bytes, &error, warnings);
	Q_ASSERT_X(png, "load", qPrintable(error));
	return png;
}

QByteArray sampleBytes()
{
	QFile f(QStringLiteral(TWEAKPNG_SOURCE_DIR "/samples/sample.png"));
	if(!f.open(QIODevice::ReadOnly)) return QByteArray();
	return f.readAll();
}

// The description of the given chunk placed in a 1x1 image.
QString describe(const char *type, const QByteArray &data, int colorType = 2, int depth = 8)
{
	const auto png = load(makePng(makeChunk(type, data), depth, colorType));
	return png->chunk(1)->description();
}

QByteArray u32s(std::initializer_list<quint32> values)
{
	QByteArray out;
	for(quint32 v : values) out += be32(v);
	return out;
}

} // namespace

class TestCore : public QObject {
	Q_OBJECT

private slots:
	void crcKnownValue();
	void sampleRoundTripsByteForByte();
	void rejectsNonPng();
	void repairsBadCrc();
	void stopsAtGarbage_data();
	void stopsAtGarbage();
	void signatures();
	void describesChunks_data();
	void describesChunks();
	void attributes();
	void validity_data();
	void validity();
	void nonPngSkipsValidityWhenSaving();
	void newChunks_data();
	void newChunks();
	void newChunkRefusals();
	void splitAndCombineKeepData();
	void splitRejectsBadInput();
	void combineSelectedErrors();
	void moveAndDelete();
	void textRoundTrip_data();
	void textRoundTrip();
	void malformedTextIsStable();
	void corruptCompressedText();
	void textDisplayIsLimited();
	void scal();
	void iccImportKeepsWholeProfile();
	void iccRename();
	void iccHostileOffsets();
	void iccEmptyDescTag();
	void clipboardRoundTrip();
	void clipboardRejectsTruncation();
	void chunkImportRejectsShortFile();
	void chunkExportImport();
	void paletteOf16BitGrayDoesNotOverflow();
	void paletteIllegalDepths();
	void paletteIgnoresEmptyBkgd();
	void paletteEdit();
	void saveIsAtomicAndExact();
	void decodeMatchesExpected_data();
	void decodeMatchesExpected();
	void decodeBlendsBackground();
	void decodeSampleMatchesQt();
	void decodeRejectsGarbage();
	void damagedFilesAreHandled();
};

void TestCore::crcKnownValue()
{
	// The CRC of a standard empty IEND chunk.
	QCOMPARE(crc32Of("IEND", QByteArray()), 0xae426082U);
}

void TestCore::sampleRoundTripsByteForByte()
{
	const QByteArray bytes = sampleBytes();
	QVERIFY(!bytes.isEmpty());

	QStringList warnings;
	const auto png = load(bytes, &warnings);
	QVERIFY(warnings.isEmpty());
	QVERIFY(png->count()>=3);
	QCOMPARE(png->toBytes(), bytes);
	QCOMPARE((int)png->fileSize(), bytes.size());
	QVERIFY(png->checkValidity().ok);
}

void TestCore::rejectsNonPng()
{
	QString error;
	QVERIFY(!Png::fromBytes(QByteArray("this is not a png"), &error, nullptr));
	QVERIFY(error.contains(QStringLiteral("not a valid PNG")));
	QVERIFY(!Png::fromBytes(QByteArray(), &error, nullptr));
}

void TestCore::repairsBadCrc()
{
	QByteArray bytes = makePng();
	bytes[8+4+4+13] = (char)(bytes[8+4+4+13]^0xff);  // corrupt IHDR's CRC

	QStringList warnings;
	const auto png = load(bytes, &warnings);
	QCOMPARE(warnings.size(), 1);
	QVERIFY(warnings.first().startsWith(QStringLiteral("Incorrect crc for IHDR chunk")));
	QCOMPARE(png->chunk(0)->crc, png->chunk(0)->calcCrc());
}

void TestCore::stopsAtGarbage_data()
{
	QTest::addColumn<QByteArray>("tail");
	QTest::addColumn<QString>("warning");

	QTest::newRow("short header") << QByteArray("abc") << QStringLiteral("Garbage found");
	QTest::newRow("invalid type") << (be32(0)+QByteArray("1234"))+QByteArray(4, '\0') << QStringLiteral("Invalid chunk type");
	QTest::newRow("huge length") << (be32(0xfffffff0U)+QByteArray("abcd")) << QStringLiteral("Bogus chunk length");
	QTest::newRow("empty chunk, no crc") << (be32(0)+QByteArray("abcd")) << QStringLiteral("Garbage found");
}

void TestCore::stopsAtGarbage()
{
	QFETCH(QByteArray, tail);
	QFETCH(QString, warning);

	const QByteArray good = makePng();
	QStringList warnings;
	const auto png = load(good+tail, &warnings);
	QCOMPARE(warnings.size(), 1);
	QVERIFY2(warnings.first().contains(warning), qPrintable(warnings.first()));
	QCOMPARE(png->count(), 3);
	QCOMPARE(png->toBytes(), good);
}

void TestCore::signatures()
{
	const auto png = load(makePng());
	QCOMPARE(png->imageType(), (int)IMG_PNG);

	png->setImageType(IMG_MNG);
	const auto mng = load(png->toBytes());
	QCOMPARE(mng->imageType(), (int)IMG_MNG);

	png->setImageType(IMG_JNG);
	const auto jng = load(png->toBytes());
	QCOMPARE(jng->imageType(), (int)IMG_JNG);
}

void TestCore::describesChunks_data()
{
	QTest::addColumn<QByteArray>("type");
	QTest::addColumn<QByteArray>("data");
	QTest::addColumn<int>("colorType");
	QTest::addColumn<QString>("expected");

	const QString lq(QChar(0x201c));
	const QString rq(QChar(0x201d));

	QTest::newRow("gAMA") << QByteArray("gAMA") << be32(45455) << 2 << QStringLiteral("file gamma = 0.45455");
	QTest::newRow("gAMA bad") << QByteArray("gAMA") << QByteArray(3, '\0') << 2 << QStringLiteral("file gamma; incorrect chunk length");
	QTest::newRow("sRGB") << QByteArray("sRGB") << QByteArray(1, '\1') << 2 << QStringLiteral("sRGB color space, rendering intent: Relative colorimetric");
	QTest::newRow("sRGB unknown") << QByteArray("sRGB") << QByteArray(1, '\7') << 2 << QStringLiteral("sRGB color space, rendering intent: [unknown: 7]");
	QTest::newRow("sTER") << QByteArray("sTER") << QByteArray(1, '\1') << 2 << QStringLiteral("stereo image, mode: Diverging-fuse layout");
	QTest::newRow("pHYs meters") << QByteArray("pHYs") << (u32s({2835, 2835})+QByteArray(1, '\1')) << 2
		<< QStringLiteral("pixel size = 2835") + kTimes + QStringLiteral("2835 pixels per meter (72.0") + kTimes + QStringLiteral("72.0 dpi)");
	QTest::newRow("pHYs unitless") << QByteArray("pHYs") << (u32s({1, 2})+QByteArray(1, '\0')) << 2
		<< QStringLiteral("pixel size = 1") + kTimes + QStringLiteral("2 pixels (per unspecified unit)");
	QTest::newRow("cHRM") << QByteArray("cHRM") << u32s({31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000}) << 2
		<< QStringLiteral("chromaticities: WP(0.31270,0.32900),R(0.64000,0.33000),G(0.30000,0.60000),B(0.15000,0.06000)");
	QTest::newRow("tIME") << QByteArray("tIME") << QByteArray::fromHex("07e60c1f173b3c") << 2
		<< QStringLiteral("time of last modification = 31 Dec 2022, 23:59:60 UTC");
	QTest::newRow("tIME bad month") << QByteArray("tIME") << QByteArray::fromHex("07e60d01000000") << 2
		<< QStringLiteral("time of last modification = 1 INVALID-MONTH 2022, 00:00:00 UTC");
	QTest::newRow("bKGD rgb") << QByteArray("bKGD") << QByteArray::fromHex("000100020003") << 2 << QStringLiteral("background color = (1,2,3)");
	QTest::newRow("bKGD gray") << QByteArray("bKGD") << QByteArray::fromHex("0005") << 0 << QStringLiteral("background color = (5)");
	QTest::newRow("tRNS gray") << QByteArray("tRNS") << QByteArray::fromHex("0005") << 0 << QStringLiteral("transparent color = (5)");
	QTest::newRow("tRNS alpha") << QByteArray("tRNS") << QByteArray(2, '\0') << 6 << QStringLiteral("illegal transparency chunk, alpha channel is present");
	QTest::newRow("sBIT rgba") << QByteArray("sBIT") << QByteArray::fromHex("05060708") << 6 << QStringLiteral("significant bits/sample: R:5,G:6,B:7,A:8");
	QTest::newRow("PLTE") << QByteArray("PLTE") << QByteArray(6, '\0') << 2 << QStringLiteral("palette (suggested), 2 entries");
	QTest::newRow("PLTE one") << QByteArray("PLTE") << QByteArray(3, '\0') << 2 << QStringLiteral("palette (suggested), 1 entry");
	QTest::newRow("PLTE bad") << QByteArray("PLTE") << QByteArray(4, '\0') << 2 << QStringLiteral("palette (suggested), incorrect palette size");
	QTest::newRow("PLTE gray") << QByteArray("PLTE") << QByteArray(3, '\0') << 0 << QStringLiteral("illegal palette chunk, 1 entry");
	QTest::newRow("hIST") << QByteArray("hIST") << QByteArray(4, '\0') << 2 << QStringLiteral("histogram for palette colors, 2 entries");
	QTest::newRow("hIST bad") << QByteArray("hIST") << QByteArray(3, '\0') << 2 << QStringLiteral("invalid histogram chunk");
	QTest::newRow("acTL") << QByteArray("acTL") << u32s({3, 0}) << 2 << QStringLiteral("APNG animation control: 3 frames, play indefinitely");
	QTest::newRow("acTL once") << QByteArray("acTL") << u32s({1, 1}) << 2 << QStringLiteral("APNG animation control: 1 frame, play 1 time");
	QTest::newRow("fcTL") << QByteArray("fcTL") << (u32s({0, 10, 20, 1, 2})+QByteArray::fromHex("00010002")+QByteArray::fromHex("0101")) << 2
		<< QStringLiteral("APNG frame control, seq#=0, 10") + kTimes + QStringLiteral("20+1+2, delay=0.500s, dispose=bkgd, blend=over");
	QTest::newRow("fdAT") << QByteArray("fdAT") << be32(7) << 2 << QStringLiteral("APNG frame data, seq#=7");
	QTest::newRow("oFFs") << QByteArray("oFFs") << (u32s({0xffffffffU, 2})+QByteArray(1, '\0')) << 2 << QStringLiteral("image offset = (-1,2) pixels");
	QTest::newRow("oFFs micro") << QByteArray("oFFs") << (u32s({1, 2})+QByteArray(1, '\1')) << 2 << QStringLiteral("image offset = (1,2) ") + QChar(0x03bc) + QStringLiteral("m");
	QTest::newRow("vpAg") << QByteArray("vpAg") << (u32s({640, 480})+QByteArray(1, '\0')) << 2 << QStringLiteral("virtual page = 640") + kTimes + QStringLiteral("480 pixels");
	QTest::newRow("sCAL") << QByteArray("sCAL") << QByteArray("\x01" "1.5\0" "2e3", 8) << 2 << QStringLiteral("physical scale of image subject: 1.5") + kTimes + QStringLiteral("2e3 meters");
	QTest::newRow("sPLT") << QByteArray("sPLT") << QByteArray("name\0\x08", 6) << 2 << QStringLiteral("suggested palette: [name]");
	QTest::newRow("iCCP noname") << QByteArray("iCCP") << QByteArray("x", 1) << 2 << QStringLiteral("embedded ICC profile, invalid name length");
	QTest::newRow("eXIf") << QByteArray("eXIf") << QByteArray(1, '\0') << 2 << QStringLiteral("Exif metadata");
	QTest::newRow("private") << QByteArray("abCd") << QByteArray() << 2 << QStringLiteral("unrecognized chunk type");
	QTest::newRow("public") << QByteArray("aBCd") << QByteArray() << 2 << QStringLiteral("unrecognized chunk type");
	QTest::newRow("text") << QByteArray("tEXt") << QByteArray("Title\0hello", 11) << 2 << QStringLiteral("text, key=") + lq + QStringLiteral("Title") + rq + QStringLiteral(": ") + lq + QStringLiteral("hello") + rq;
	QTest::newRow("text nonstandard") << QByteArray("tEXt") << QByteArray("Foo\0a", 5) << 2
		<< QStringLiteral("text, key=") + lq + QStringLiteral("Foo") + rq + QStringLiteral(" (nonstandard): ") + lq + QStringLiteral("a") + rq;
	QTest::newRow("text controls") << QByteArray("tEXt") << QByteArray("Title\0a\nb", 9) << 2
		<< QStringLiteral("text, key=") + lq + QStringLiteral("Title") + rq + QStringLiteral(": ") + lq + QStringLiteral("a|b") + rq;
}

void TestCore::describesChunks()
{
	QFETCH(QByteArray, type);
	QFETCH(QByteArray, data);
	QFETCH(int, colorType);
	QFETCH(QString, expected);

	if(type=="abCd") {
		// Bit 5 of the second letter marks a private chunk.
		QCOMPARE(describe("abcd", data, colorType), QStringLiteral("private chunk"));
		return;
	}
	QCOMPARE(describe(type.constData(), data, colorType), expected);
}

void TestCore::attributes()
{
	const auto png = load(makePng(makeChunk("gAMA", be32(1))+makeChunk("abcd", QByteArray())+makeChunk("abcD", QByteArray())));
	QCOMPARE(png->chunk(0)->attributes(), QStringLiteral("critical"));
	QCOMPARE(png->chunk(1)->attributes(), QStringLiteral("ancillary, unsafe to copy"));
	QCOMPARE(png->chunk(2)->attributes(), QStringLiteral("ancillary, private, safe to copy"));
	QCOMPARE(png->chunk(3)->attributes(), QStringLiteral("ancillary, private, unsafe to copy"));
}

void TestCore::validity_data()
{
	QTest::addColumn<QByteArray>("file");
	QTest::addColumn<QString>("message");

	const QByteArray sig = pngSignature();
	const QByteArray ihdr = makeIhdr(1, 1, 8, 0);
	const QByteArray ihdrPal = makeIhdr(1, 1, 8, 3);
	const QByteArray idat = makeIdat(QByteArray(2, '\0'));
	const QByteArray iend = makeChunk("IEND", QByteArray());
	const QByteArray gama = makeChunk("gAMA", be32(45455));
	const QByteArray plte = makeChunk("PLTE", QByteArray(3, '\0'));
	const QByteArray trns = makeChunk("tRNS", QByteArray(1, '\0'));

	QTest::newRow("good") << sig+ihdr+idat+iend << QStringLiteral("No problems found.");
	QTest::newRow("empty") << sig << QStringLiteral("No chunks. Not valid.");
	QTest::newRow("no IHDR") << sig+idat+iend << QStringLiteral("First chunk must be IHDR");
	QTest::newRow("no IEND") << sig+ihdr+idat << QStringLiteral("Last chunk must be IEND");
	QTest::newRow("no IDAT") << sig+ihdr+iend << QStringLiteral("Required IDAT chunk not found");
	QTest::newRow("no PLTE") << sig+ihdrPal+idat+iend << QStringLiteral("Required PLTE chunk not found");
	QTest::newRow("split IDAT") << sig+ihdr+idat+gama+idat+iend << QStringLiteral("gAMA must appear before IDAT");
	QTest::newRow("late gAMA") << sig+ihdr+idat+gama+iend << QStringLiteral("gAMA must appear before IDAT");
	QTest::newRow("double gAMA") << sig+ihdr+gama+gama+idat+iend << QStringLiteral("Multiple gAMA chunks not allowed");
	QTest::newRow("gAMA after PLTE") << sig+ihdrPal+plte+gama+idat+iend << QStringLiteral("gAMA must appear before PLTE");
	QTest::newRow("gray PLTE") << sig+ihdr+plte+idat+iend << QStringLiteral("PLTE chunk not allowed in grayscale image");
	QTest::newRow("tRNS before PLTE") << sig+ihdrPal+trns+plte+idat+iend << QStringLiteral("tRNS must appear after PLTE");
	QTest::newRow("extra IHDR") << sig+ihdr+ihdr+idat+iend << QStringLiteral("Misplaced or extra IHDR");
	QTest::newRow("extra IEND") << sig+ihdr+idat+iend+iend << QStringLiteral("Misplaced or extra IEND");
	QTest::newRow("gap") << sig+ihdr+idat+makeChunk("tEXt", QByteArray("a\0", 2))+idat+iend << QStringLiteral("IDAT chunks must be consecutive");
	QTest::newRow("unknown critical") << sig+ihdr+makeChunk("ABCD", QByteArray())+idat+iend << QStringLiteral("Unrecognized critical chunk");
	QTest::newRow("dSIG unmatched") << sig+ihdr+makeChunk("dSIG", QByteArray())+idat+iend << QStringLiteral("Mismatched dSIG chunks");
	QTest::newRow("dSIG matched") << sig+ihdr+makeChunk("dSIG", QByteArray())+idat+makeChunk("dSIG", QByteArray())+iend << QStringLiteral("No problems found.");
}

void TestCore::validity()
{
	QFETCH(QByteArray, file);
	QFETCH(QString, message);

	const auto png = load(file);
	const ValidityResult r = png->checkValidity();
	QCOMPARE(r.message, message);
	QCOMPARE(r.ok, message==QLatin1String("No problems found."));
}

void TestCore::nonPngSkipsValidityWhenSaving()
{
	const auto png = load(makePng());
	png->setImageType(IMG_MNG);
	QVERIFY(!png->checkValidity(true).ok);
	QVERIFY(png->checkValidity(false).ok);
}

void TestCore::newChunks_data()
{
	QTest::addColumn<int>("id");
	QTest::addColumn<int>("expectedPos");
	QTest::addColumn<int>("expectedLen");

	QTest::newRow("tEXt") << (int)CHUNK_tEXt << 2 << 8;
	QTest::newRow("gAMA") << (int)CHUNK_gAMA << 1 << 4;
	QTest::newRow("oFFs") << (int)CHUNK_oFFs << 1 << 9;
	QTest::newRow("pHYs") << (int)CHUNK_pHYs << 1 << 9;
	QTest::newRow("sRGB") << (int)CHUNK_sRGB << 1 << 1;
	QTest::newRow("sTER") << (int)CHUNK_sTER << 1 << 1;
	QTest::newRow("cHRM") << (int)CHUNK_cHRM << 1 << 32;
	QTest::newRow("bKGD") << (int)CHUNK_bKGD << 1 << 6;
	QTest::newRow("sBIT") << (int)CHUNK_sBIT << 1 << 3;
	QTest::newRow("PLTE") << (int)CHUNK_PLTE << 1 << 3;
	QTest::newRow("tRNS") << (int)CHUNK_tRNS << 1 << 6;
	QTest::newRow("IHDR") << (int)CHUNK_IHDR << 0 << 13;
	QTest::newRow("IEND") << (int)CHUNK_IEND << 3 << 0;
	QTest::newRow("tIME") << (int)CHUNK_tIME << 2 << 7;
	QTest::newRow("sCAL") << (int)CHUNK_sCAL << 1 << 8;
	QTest::newRow("acTL") << (int)CHUNK_acTL << 1 << 8;
	QTest::newRow("fcTL") << (int)CHUNK_fcTL << 1 << 26;
	QTest::newRow("vpAg") << (int)CHUNK_vpAg << 1 << 9;
}

void TestCore::newChunks()
{
	QFETCH(int, id);
	QFETCH(int, expectedPos);
	QFETCH(int, expectedLen);

	const auto png = load(makePng(QByteArray(), 8, 2));
	const NewChunkResult r = png->newChunk(id);
	QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
	QCOMPARE(r.pos, expectedPos);

	const Chunk *c = png->chunk(r.pos);
	QCOMPARE(c->id, id);
	QCOMPARE((int)c->length(), expectedLen);
	QCOMPARE(c->crc, c->calcCrc());
	QVERIFY(c->hasValidLength());
	QVERIFY(!const_cast<Chunk*>(c)->description().isEmpty());
}

void TestCore::newChunkRefusals()
{
	const auto gray = load(makePng());
	QVERIFY(gray->newChunk(CHUNK_PLTE).error.contains(QStringLiteral("not allowed for grayscale")));

	const auto rgba = load(makePng(QByteArray(), 8, 6));
	QVERIFY(rgba->newChunk(CHUNK_tRNS).error.contains(QStringLiteral("full alpha channel")));

	const auto png = load(makePng());
	const NewChunkResult r = png->newChunk(CHUNK_hIST);
	QVERIFY(r.notice);
	QCOMPARE(r.error, QStringLiteral("Not implemented"));

	// bKGD goes directly after PLTE.
	const auto pal = load(pngSignature()+makeIhdr(1, 1, 8, 3)+makeChunk("gAMA", be32(1))+makeChunk("PLTE", QByteArray(3, '\0'))
		+makeIdat(QByteArray(2, '\0'))+makeChunk("IEND", QByteArray()));
	QCOMPARE(pal->newChunk(CHUNK_bKGD).pos, 3);
}

void TestCore::splitAndCombineKeepData()
{
	QByteArray payload;
	for(int i=0; i<1000; i++) payload.append((char)(i*7));
	const auto png = load(pngSignature()+makeIhdr(1, 1, 8, 0)+makeChunk("IDAT", payload)+makeChunk("IEND", QByteArray()));

	QCOMPARE(png->splitCount(1, 300, true), 4);
	QVERIFY(png->splitIdat(1, 300, true));
	QCOMPARE(png->count(), 6);
	QCOMPARE((int)png->chunk(1)->length(), 300);
	QCOMPARE((int)png->chunk(4)->length(), 100);
	QCOMPARE(png->chunk(2)->crc, png->chunk(2)->calcCrc());

	int ranges = 0;
	int first = 0;
	QVERIFY(png->combineAll(&ranges, &first).isEmpty());
	QCOMPARE(ranges, 1);
	QCOMPARE(first, 1);
	QCOMPARE(png->count(), 3);
	QCOMPARE(png->chunk(1)->data, payload);

	QVERIFY(png->splitIdat(1, 0, false));  // a zero-length first section is legal
	QCOMPARE(png->count(), 4);
	QCOMPARE((int)png->chunk(1)->length(), 0);
	QCOMPARE(png->chunk(2)->data, payload);

	QVERIFY(png->combineSelected({1, 2}, &first).isEmpty());
	QCOMPARE(png->chunk(1)->data, payload);
}

void TestCore::splitRejectsBadInput()
{
	const auto png = load(pngSignature()+makeIhdr(1, 1, 8, 0)+makeChunk("IDAT", QByteArray())+makeChunk("IDAT", QByteArray(10, 'x'))+makeChunk("IEND", QByteArray()));
	QVERIFY(!png->splitIdat(2, 0, true));    // repeated zero-size pieces
	QVERIFY(!png->splitIdat(2, 11, false));  // past the end
	QVERIFY(!png->splitIdat(99, 1, false));
	QCOMPARE(png->count(), 4);
	QCOMPARE(png->splitCount(1, 5, true), 1);  // an empty chunk stays one chunk
}

void TestCore::combineSelectedErrors()
{
	const QByteArray idat = makeChunk("IDAT", QByteArray(1, 'x'));
	const auto png = load(pngSignature()+makeIhdr(1, 1, 8, 0)+idat+makeChunk("gAMA", be32(1))+idat+idat+makeChunk("IEND", QByteArray()));

	int first = 0;
	QCOMPARE(png->combineSelected({}, &first), QStringLiteral("Must select IDAT chunks first"));
	QCOMPARE(png->combineSelected({0}, &first), QStringLiteral("Only IDAT chunks may be selected"));
	QCOMPARE(png->combineSelected({1, 3}, &first), QStringLiteral("Selected chunks must be consecutive"));
	QCOMPARE(png->combineSelected({3}, &first), QStringLiteral("Must select more than one IDAT chunk"));
	QVERIFY(png->combineSelected({3, 4}, &first).isEmpty());
	QCOMPARE(first, 3);
	QCOMPARE(png->count(), 5);

	const auto noIdat = load(pngSignature());
	int r = 0;
	QCOMPARE(noIdat->combineAll(&r, &first), QStringLiteral("No IDAT chunks present"));
}

void TestCore::moveAndDelete()
{
	const auto png = load(makePng(makeChunk("gAMA", be32(1))+makeChunk("sRGB", QByteArray(1, '\0'))));
	QCOMPARE(png->chunk(1)->id, (int)CHUNK_gAMA);

	png->moveChunk(1, 1);
	QCOMPARE(png->chunk(1)->id, (int)CHUNK_sRGB);
	QCOMPARE(png->chunk(2)->id, (int)CHUNK_gAMA);

	png->moveChunk(2, -100);
	QCOMPARE(png->chunk(0)->id, (int)CHUNK_gAMA);

	png->moveChunk(0, 100);
	QCOMPARE(png->chunk(png->count()-1)->id, (int)CHUNK_gAMA);

	const int before = png->count();
	png->deleteChunk(99);
	png->deleteChunk(-1);
	QCOMPARE(png->count(), before);
	png->deleteChunk(0);
	QCOMPARE(png->count(), before-1);
}

void TestCore::textRoundTrip_data()
{
	QTest::addColumn<QString>("text");
	QTest::addColumn<bool>("compressed");
	QTest::addColumn<bool>("international");
	QTest::addColumn<QByteArray>("type");

	const QString latin1 = QStringLiteral("caf") + QChar(0x00e9);
	const QString unicode = QStringLiteral("nihon ") + QChar(0x65e5) + QChar(0x672c) + QChar(0xd83d) + QChar(0xde00);

	QTest::newRow("tEXt") << latin1 << false << false << QByteArray("tEXt");
	QTest::newRow("zTXt") << latin1 << true << false << QByteArray("zTXt");
	QTest::newRow("iTXt") << unicode << false << true << QByteArray("iTXt");
	QTest::newRow("iTXt compressed") << unicode << true << true << QByteArray("iTXt");
	QTest::newRow("empty") << QString() << true << true << QByteArray("iTXt");
}

void TestCore::textRoundTrip()
{
	QFETCH(QString, text);
	QFETCH(bool, compressed);
	QFETCH(bool, international);
	QFETCH(QByteArray, type);

	const auto png = load(makePng());
	const NewChunkResult r = png->newChunk(CHUNK_tEXt);
	Chunk *c = png->chunk(r.pos);

	const QString translated = QStringLiteral("Titel ") + QChar(0x00fc);
	QVERIFY(c->setTextInfo(QStringLiteral("Title"), QStringLiteral("de"), translated, text, compressed, international));
	c->chunkModified();
	QCOMPARE(QByteArray(c->type), type);

	// Through the file format and back.
	const auto again = load(png->toBytes());
	TextInfo ti;
	QVERIFY(again->chunk(r.pos)->textInfo(&ti));
	QCOMPARE(ti.keyword, QStringLiteral("Title"));
	QCOMPARE(ti.text, text);
	QCOMPARE(ti.isCompressed, compressed);
	QVERIFY(ti.hasText);
	if(international) {
		QCOMPARE(ti.language, QStringLiteral("de"));
		QCOMPARE(ti.translatedKeyword, translated);
	}
}

// The original crashed the second time it described one of these.
void TestCore::malformedTextIsStable()
{
	static const char *const kCases[][2] = {
		{"tEXt", ""}, {"zTXt", ""}, {"iTXt", ""},
	};
	const QString cant = QStringLiteral("can") + QChar(0x2019) + QStringLiteral("t read text chunk");

	for(const auto &c : kCases) {
		const auto png = load(makePng(makeChunk(c[0], QByteArray())));
		Chunk *ch = png->chunk(1);
		QCOMPARE(ch->description(), cant);
		QCOMPARE(ch->description(), cant);
		TextInfo ti;
		QVERIFY(!ch->textInfo(&ti));
	}

	// A keyword with nothing after it, in each type.
	const auto png = load(makePng(makeChunk("zTXt", QByteArray("k", 1))+makeChunk("iTXt", QByteArray("k\0", 2))));
	QCOMPARE(png->chunk(1)->description(), cant);
	QCOMPARE(png->chunk(2)->description(), cant);
}

void TestCore::corruptCompressedText()
{
	const auto png = load(makePng(makeChunk("zTXt", QByteArray("Title\0\0notzlib", 13))));
	const QString d = png->chunk(1)->description();
	QVERIFY2(d.contains(QStringLiteral("--decompression failed!--")), qPrintable(d));

	QByteArray out;
	QString error;
	const QByteArray good = deflated(QByteArray(1000, 'a'));
	QVERIFY(uncompressData(good.constData(), good.size(), &out, &error));
	QCOMPARE(out.size(), 1000);
	QVERIFY(!uncompressData(good.constData(), good.size()/2, &out, &error));
	QVERIFY(!error.isEmpty());
}

void TestCore::textDisplayIsLimited()
{
	const auto png = load(makePng(makeChunk("tEXt", QByteArray("Title\0", 6)+QByteArray(1000, 'a'))));
	const QString d = png->chunk(1)->description();
	QVERIFY(d.contains(QString(150, QLatin1Char('a'))+QStringLiteral("...")));
	QVERIFY(!d.contains(QString(151, QLatin1Char('a'))));
}

void TestCore::scal()
{
	const auto png = load(makePng());
	const NewChunkResult r = png->newChunk(CHUNK_sCAL);
	Chunk *c = png->chunk(r.pos);

	SCalData d = c->sCalData();
	QCOMPARE(d.units, 1);
	QCOMPARE(d.x, QStringLiteral("1.0"));
	QCOMPARE(d.y, QStringLiteral("1.0"));

	d.units = 2;
	d.x = QStringLiteral("2.5e3");
	d.y = QStringLiteral("x1");
	c->setSCalData(d);
	const SCalData d2 = c->sCalData();
	QCOMPARE(d2.units, 2);
	QCOMPARE(d2.x, QStringLiteral("2.5e3"));
	QCOMPARE(d2.y, QStringLiteral("_1"));  // bad characters are shown as underscores
}

void TestCore::iccImportKeepsWholeProfile()
{
	QByteArray profile(500, '\0');
	for(int i=0; i<profile.size(); i++) profile[i] = (char)(i*3);

	const auto png = load(makePng());
	int pos = -1;
	QString error;
	QVERIFY(png->importIccProfile(profile, &pos, &error));
	QCOMPARE(pos, 1);

	const Chunk *c = png->chunk(pos);
	QCOMPARE(c->id, (int)CHUNK_iCCP);
	QVERIFY(const_cast<Chunk*>(c)->description().contains(QStringLiteral("[ICC profile]")));

	QByteArray out;
	const int skip = 12+1;  // the name, its NUL, and the compression method
	QVERIFY(uncompressData(c->data.constData()+skip, c->data.size()-skip, &out, &error));
	QCOMPARE(out, profile);

	QVERIFY(!png->importIccProfile(QByteArray(10, 'x'), &pos, &error));
}

void TestCore::iccRename()
{
	const auto png = load(makePng());
	int pos = 0;
	QString error;
	QVERIFY(png->importIccProfile(QByteArray(200, 'p'), &pos, &error));
	Chunk *c = png->chunk(pos);
	const QByteArray tail = c->data.mid(11);

	QVERIFY(c->setIccProfileName(QStringLiteral("sRGB")));
	QCOMPARE(c->data, QByteArray("sRGB")+tail);
}

// Builds an iCCP chunk from a profile.
static QByteArray iccChunk(const QByteArray &profile)
{
	return makeChunk("iCCP", QByteArray("p\0\0", 3)+deflated(profile));
}

void TestCore::iccHostileOffsets()
{
	QByteArray profile(128+4+12*3, '\0');
	uchar *m = (uchar*)profile.data();
	writeInt32(m+128, 3);  // three tags

	// A tag whose offset wraps around in 32-bit arithmetic.
	writeInt32(m+132+4, 0xfffffffeU);
	writeInt32(m+132+8, 8);
	// A tag past the end of the profile.
	writeInt32(m+144+4, 100000);
	writeInt32(m+144+8, 100);
	// A tag with a huge size.
	writeInt32(m+156+4, 8);
	writeInt32(m+156+8, 0xffffffffU);

	const auto png = load(makePng(iccChunk(profile)));
	const QString s = dumpIccChunk(*png->chunk(1));
	QVERIFY(s.contains(QStringLiteral("Number of tags: 3")));
	QVERIFY(s.contains(QStringLiteral("offset=4294967294")));

	// A tag count too big for the data stops the listing.
	writeInt32(m+128, 1000);
	const auto png2 = load(makePng(iccChunk(profile)));
	QVERIFY(dumpIccChunk(*png2->chunk(1)).contains(QStringLiteral("Number of tags: 1000")));

	// Short or broken data.
	const auto png3 = load(makePng(iccChunk(QByteArray(20, 'x'))));
	QVERIFY(dumpIccChunk(*png3->chunk(1)).contains(QStringLiteral("Uncompressed size: 20")));
	const auto png4 = load(makePng(makeChunk("iCCP", QByteArray("p\0\0garbage", 10))));
	QVERIFY(dumpIccChunk(*png4->chunk(1)).contains(QStringLiteral("Decompression failed")));
	const auto png5 = load(makePng(makeChunk("iCCP", QByteArray("p\0\1", 3))));
	QVERIFY(dumpIccChunk(*png5->chunk(1)).contains(QStringLiteral("Unsupported compression method")));
	const auto png6 = load(makePng(makeChunk("iCCP", QByteArray("p", 1))));
	QVERIFY(dumpIccChunk(*png6->chunk(1)).isEmpty());
}

// In the original this read past the end of the profile's buffer.
void TestCore::iccEmptyDescTag()
{
	QByteArray profile(128+4+12+8, '\0');
	uchar *m = (uchar*)profile.data();
	writeInt32(m+128, 1);
	memcpy(m+132, "desc", 4);
	writeInt32(m+132+4, 144);  // the type signature is the final 8 bytes
	writeInt32(m+132+8, 8);    // a tag with an empty body
	memcpy(m+144, "desc", 4);

	const auto png = load(makePng(iccChunk(profile)));
	QVERIFY(dumpIccChunk(*png->chunk(1)).contains(QStringLiteral("Tag #1 signature=")));
}

void TestCore::clipboardRoundTrip()
{
	const auto src = load(makePng(makeChunk("gAMA", be32(45455))+makeChunk("tEXt", QByteArray("a\0b", 3))));
	const QByteArray clip = src->chunksToClipboard({1, 2});
	QVERIFY(!clip.isEmpty());
	QVERIFY(src->chunksToClipboard({}).isEmpty());

	const auto dst = load(makePng());
	QCOMPARE(dst->pasteClipboard(clip, 1), 2);
	QCOMPARE(dst->count(), 5);
	QCOMPARE(dst->chunk(1)->id, (int)CHUNK_gAMA);
	QCOMPARE(dst->chunk(2)->id, (int)CHUNK_tEXt);
	QCOMPARE(dst->chunk(1)->crc, src->chunk(1)->crc);
}

void TestCore::clipboardRejectsTruncation()
{
	const auto png = load(makePng());
	const QByteArray clip = png->chunksToClipboard({0, 1});

	QCOMPARE(png->pasteClipboard(clip.left(3), 0), 0);
	QCOMPARE(png->pasteClipboard(clip.left(clip.size()-1), 0), 0);  // size field too big

	// A chunk claiming more data than exists; the original's 32-bit check
	// could wrap around here.
	QByteArray bad = be32(4+12) + be32(0xffffffffU) + QByteArray("abcd") + be32(0);
	QCOMPARE(png->pasteClipboard(bad, 0), 0);
	QCOMPARE(png->count(), 3);
}

void TestCore::chunkImportRejectsShortFile()
{
	const auto png = load(makePng());
	QString error;
	QVERIFY(!png->importChunkBytes(QByteArray("abc"), 1, &error));
	QVERIFY(!error.isEmpty());
	QCOMPARE(png->count(), 3);
}

void TestCore::chunkExportImport()
{
	const auto src = load(makePng(makeChunk("gAMA", be32(45455))));
	const QByteArray exported = src->exportChunk(1);
	QCOMPARE(exported, QByteArray("gAMA")+be32(45455));

	const auto dst = load(makePng());
	QString error;
	QVERIFY(dst->importChunkBytes(exported, 1, &error));
	QCOMPARE(dst->chunk(1)->id, (int)CHUNK_gAMA);
	QCOMPARE(dst->chunk(1)->crc, src->chunk(1)->crc);
}

// In the original, an array of 256 entries took 65536 writes here.
void TestCore::paletteOf16BitGrayDoesNotOverflow()
{
	const auto png = load(makePng(makeChunk("PLTE", QByteArray(3, '\0')), 16, 0));
	PaletteInfo p;
	QString error;
	QVERIFY(png->loadPaletteInfo(png->chunk(1), &p, &error));
	QCOMPARE(p.maxPlte, 256);
	QCOMPARE(p.numPlte, 256);
	QVERIFY(!p.canEditColors);
}

void TestCore::paletteIllegalDepths()
{
	for(int depth : {0, 3, 9, 200, 255}) {
		const auto png = load(makePng(makeChunk("PLTE", QByteArray(3, '\0')), depth, 0));
		PaletteInfo p;
		QString error;
		QVERIFY(png->loadPaletteInfo(png->chunk(1), &p, &error));
		QVERIFY(p.maxPlte>=1 && p.maxPlte<=256);
	}
}

// In the original this wrote to a chunk with no data.
void TestCore::paletteIgnoresEmptyBkgd()
{
	const auto png = load(pngSignature()+makeIhdr(1, 1, 8, 3)+makeChunk("PLTE", QByteArray(3, '\0'))
		+makeChunk("bKGD", QByteArray())+makeIdat(QByteArray(2, '\0'))+makeChunk("IEND", QByteArray()));

	PaletteInfo p;
	QString error;
	QVERIFY(png->loadPaletteInfo(png->chunk(1), &p, &error));
	QCOMPARE(p.numBkgd, 0);
	png->applyPaletteInfo(p);
	QCOMPARE((int)png->chunk(2)->length(), 0);
}

void TestCore::paletteEdit()
{
	const auto png = load(pngSignature()+makeIhdr(1, 1, 8, 3)+makeChunk("PLTE", QByteArray::fromHex("010203040506"))
		+makeChunk("tRNS", QByteArray::fromHex("80"))+makeChunk("bKGD", QByteArray::fromHex("01"))
		+makeIdat(QByteArray(2, '\0'))+makeChunk("IEND", QByteArray()));

	PaletteInfo p;
	QString error;
	QVERIFY(png->loadPaletteInfo(png->chunk(1), &p, &error));
	QCOMPARE(p.numPlte, 2);
	QCOMPARE(p.numTrns, 1);
	QCOMPARE(p.bkgd, (uchar)1);
	QCOMPARE(p.plte[1].green, (uchar)5);
	QCOMPARE(p.plte[0].alpha, (uchar)0x80);

	p.numPlte = 3;
	p.plte[2] = PaletteEntry{7, 8, 9, 10};
	p.numTrns = 3;
	p.bkgd = 2;
	png->applyPaletteInfo(p);

	QCOMPARE(png->chunk(1)->data, QByteArray::fromHex("010203040506070809"));
	QCOMPARE(png->chunk(2)->data, QByteArray::fromHex("80ff0a"));
	QCOMPARE(png->chunk(3)->data, QByteArray::fromHex("02"));
	QCOMPARE(png->chunk(1)->crc, png->chunk(1)->calcCrc());
	QCOMPARE(png->chunk(2)->crc, png->chunk(2)->calcCrc());

	// A paletted image with no PLTE has nothing to edit.
	const auto noPal = load(pngSignature()+makeIhdr(1, 1, 8, 3)+makeChunk("tRNS", QByteArray(1, '\0'))+makeChunk("IEND", QByteArray()));
	QVERIFY(!noPal->loadPaletteInfo(noPal->chunk(1), &p, &error));
	QCOMPARE(error, QStringLiteral("Need a PLTE chunk to edit this"));
}

void TestCore::saveIsAtomicAndExact()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString fn = dir.filePath(QStringLiteral("out.png"));

	const QByteArray bytes = makePng();
	const auto png = load(bytes);
	QString error;
	QVERIFY(png->writeFile(fn, &error));

	QFile f(fn);
	QVERIFY(f.open(QIODevice::ReadOnly));
	QCOMPARE(f.readAll(), bytes);

	QVERIFY(!png->writeFile(dir.filePath(QStringLiteral("no/such/folder/out.png")), &error));
	QVERIFY(!error.isEmpty());

	QStringList warnings;
	const auto reloaded = Png::fromFile(fn, fn, &error, &warnings);
	QVERIFY(reloaded);
	QVERIFY(reloaded->named);
	QVERIFY(!reloaded->dirty);

	QVERIFY(!Png::fromFile(dir.filePath(QStringLiteral("missing.png")), QString(), &error, &warnings));
}

void TestCore::decodeMatchesExpected_data()
{
	QTest::addColumn<QByteArray>("file");
	QTest::addColumn<QRgb>("first");
	QTest::addColumn<QRgb>("second");

	const QByteArray iend = makeChunk("IEND", QByteArray());
	const QByteArray srgb = makeChunk("sRGB", QByteArray(1, '\0'));

	const auto image = [&](int depth, int type, const QByteArray &extra, const QByteArray &row) {
		return pngSignature()+makeIhdr(2, 1, depth, type)+srgb+extra+makeIdat(QByteArray(1, '\0')+row)+iend;
	};

	QTest::newRow("gray1") << image(1, 0, QByteArray(), QByteArray::fromHex("40")) << qRgb(0, 0, 0) << qRgb(255, 255, 255);
	QTest::newRow("gray2") << image(2, 0, QByteArray(), QByteArray::fromHex("60")) << qRgb(85, 85, 85) << qRgb(170, 170, 170);
	QTest::newRow("gray4") << image(4, 0, QByteArray(), QByteArray::fromHex("1f")) << qRgb(17, 17, 17) << qRgb(255, 255, 255);
	QTest::newRow("gray8") << image(8, 0, QByteArray(), QByteArray::fromHex("107f")) << qRgb(16, 16, 16) << qRgb(127, 127, 127);
	QTest::newRow("gray16") << image(16, 0, QByteArray(), QByteArray::fromHex("10ff7f00")) << qRgb(16, 16, 16) << qRgb(127, 127, 127);
	QTest::newRow("rgb8") << image(8, 2, QByteArray(), QByteArray::fromHex("0102030a0b0c")) << qRgb(1, 2, 3) << qRgb(10, 11, 12);
	QTest::newRow("rgb16") << image(16, 2, QByteArray(), QByteArray::fromHex("0100020003000a000b000c00")) << qRgb(1, 2, 3) << qRgb(10, 11, 12);

	const QByteArray plte = makeChunk("PLTE", QByteArray::fromHex("ff0000""00ff00""0000ff""ffffff"));
	QTest::newRow("pal8") << image(8, 3, plte, QByteArray::fromHex("0102")) << qRgb(0, 255, 0) << qRgb(0, 0, 255);
	QTest::newRow("pal2") << image(2, 3, plte, QByteArray::fromHex("b0")) << qRgb(0, 0, 255) << qRgb(255, 255, 255);
	QTest::newRow("pal4") << image(4, 3, plte, QByteArray::fromHex("03")) << qRgb(255, 0, 0) << qRgb(255, 255, 255);
	QTest::newRow("pal1") << image(1, 3, plte, QByteArray::fromHex("40")) << qRgb(255, 0, 0) << qRgb(0, 255, 0);

	// An index past the end of the palette must not read out of bounds.
	QTest::newRow("pal short") << image(8, 3, makeChunk("PLTE", QByteArray::fromHex("112233")), QByteArray::fromHex("00c8"))
		<< qRgb(0x11, 0x22, 0x33) << qRgb(0, 0, 0);
}

void TestCore::decodeMatchesExpected()
{
	QFETCH(QByteArray, file);
	QFETCH(QRgb, first);
	QFETCH(QRgb, second);

	DecodeOptions opts;
	opts.useCustomBackground = false;
	opts.useFileBackground = false;
	const DecodeResult r = decodePng(file, opts);

	if(!r.ok && QByteArray(QTest::currentDataTag())=="pal short") {
		// libpng may reject the out-of-range index outright, which is also safe.
		return;
	}
	QVERIFY2(r.ok, qPrintable(r.error));
	QCOMPARE(r.image.size(), QSize(2, 1));
	QCOMPARE(r.image.pixel(0, 0), first);
	QCOMPARE(r.image.pixel(1, 0), second);
}

void TestCore::decodeBlendsBackground()
{
	const QByteArray iend = makeChunk("IEND", QByteArray());
	const QByteArray srgb = makeChunk("sRGB", QByteArray(1, '\0'));

	// One opaque pixel, then one transparent pixel.
	const QByteArray rgba = pngSignature()+makeIhdr(2, 1, 8, 6)+srgb
		+makeIdat(QByteArray(1, '\0')+QByteArray::fromHex("ff0000ff00ff0000"))+iend;

	DecodeOptions opts;
	opts.useFileBackground = false;
	opts.bgRed = 10; opts.bgGreen = 20; opts.bgBlue = 30;
	DecodeResult r = decodePng(rgba, opts);
	QVERIFY2(r.ok, qPrintable(r.error));
	QCOMPARE(r.image.pixel(0, 0), qRgb(255, 0, 0));
	QCOMPARE(r.image.pixel(1, 0), qRgb(10, 20, 30));
	QVERIFY(r.hasBackground);

	// With no background at all, alpha is stripped.
	opts.useCustomBackground = false;
	r = decodePng(rgba, opts);
	QVERIFY2(r.ok, qPrintable(r.error));
	QCOMPARE(r.image.pixel(1, 0), qRgb(0, 255, 0));
	QVERIFY(!r.hasBackground);

	// The file's own background takes priority when enabled.
	const QByteArray withBkgd = pngSignature()+makeIhdr(2, 1, 8, 6)+srgb+makeChunk("bKGD", QByteArray::fromHex("006400c80032"))
		+makeIdat(QByteArray(1, '\0')+QByteArray::fromHex("ff0000ff00ff0000"))+iend;
	opts.useCustomBackground = true;
	opts.useFileBackground = true;
	r = decodePng(withBkgd, opts);
	QVERIFY2(r.ok, qPrintable(r.error));
	QCOMPARE(r.image.pixel(1, 0), qRgb(100, 200, 50));

	// Density is passed through for non-square correction.
	const QByteArray phys = pngSignature()+makeIhdr(1, 1, 8, 0)+makeChunk("pHYs", u32s({2, 4})+QByteArray(1, '\0'))
		+makeIdat(QByteArray(2, '\0'))+iend;
	r = decodePng(phys, DecodeOptions());
	QVERIFY(r.ok);
	QVERIFY(r.densityValid);
	QCOMPARE(r.densityX, 2);
	QCOMPARE(r.densityY, 4);
}

void TestCore::decodeSampleMatchesQt()
{
	const QByteArray bytes = sampleBytes();
	QImage reference;
	QVERIFY(reference.loadFromData(bytes, "PNG"));

	DecodeOptions opts;
	opts.colorCorrection = false;
	opts.useCustomBackground = false;
	opts.useFileBackground = false;
	const DecodeResult r = decodePng(bytes, opts);
	QVERIFY2(r.ok, qPrintable(r.error));
	QCOMPARE(r.image.size(), reference.size());

	const QImage a = r.image.convertToFormat(QImage::Format_RGB32);
	const QImage b = reference.convertToFormat(QImage::Format_RGB32);
	for(int y=0; y<a.height(); y+=7) {
		for(int x=0; x<a.width(); x+=7) {
			if(qAlpha(reference.pixel(x, y))==255) QCOMPARE(a.pixel(x, y), b.pixel(x, y));
		}
	}
}

void TestCore::decodeRejectsGarbage()
{
	QVERIFY(!decodePng(QByteArray("nope"), DecodeOptions()).ok);
	QVERIFY(!decodePng(pngSignature()+makeIhdr(1, 1, 8, 0), DecodeOptions()).ok);

	const DecodeResult r = decodePng(pngSignature()+makeIhdr(1, 1, 8, 3)+makeIdat(QByteArray(2, '\0'))+makeChunk("IEND", QByteArray()), DecodeOptions());
	QVERIFY(!r.ok);
	QVERIFY(!r.error.isEmpty());
}

// Damages valid files at random, with a fixed seed so that a failure can be
// reproduced, and exercises everything that reads chunk contents. This does
// its real work under the sanitizers in CI.
void TestCore::damagedFilesAreHandled()
{
	QByteArray iccProfile(400, '\0');
	writeInt32((uchar*)iccProfile.data()+128, 2);
	memcpy(iccProfile.data()+132, "desc", 4);
	writeInt32((uchar*)iccProfile.data()+136, 156);
	writeInt32((uchar*)iccProfile.data()+140, 40);
	memcpy(iccProfile.data()+156, "desc", 4);

	QByteArray rich;
	rich += makeChunk("tEXt", QByteArray("Title\0hello", 11));
	rich += makeChunk("zTXt", QByteArray("Comment\0\0", 9)+deflated("compressed"));
	rich += makeChunk("iTXt", QByteArray("Title\0\0\0de\0Titel\0abc", 20));
	rich += makeChunk("iCCP", QByteArray("p\0\0", 3)+deflated(iccProfile));
	rich += makeChunk("sCAL", QByteArray("\x01" "1\0" "2", 4));
	rich += makeChunk("sPLT", QByteArray("n\0\x08", 3));
	rich += makeChunk("tIME", QByteArray(7, '\1'));
	rich += makeChunk("fcTL", QByteArray(26, '\1'));

	const QVector<QByteArray> seeds = {sampleBytes(), makePng(rich), makePng(rich, 8, 3), makePng(rich, 16, 6)};

	QRandomGenerator rng(20240229);
	DecodeOptions opts;
	int decoded = 0;

	for(int round=0; round<3000; round++) {
		QByteArray bytes = seeds.at(round%seeds.size());
		const int edits = 1+rng.bounded(8);
		for(int e=0; e<edits; e++) {
			const int pos = rng.bounded(bytes.size());
			switch(rng.bounded(4)) {
			case 0: bytes[pos] = (char)rng.bounded(256); break;
			case 1: bytes[pos] = (char)(bytes[pos]^(1<<rng.bounded(8))); break;
			case 2: bytes[pos] = (char)0xff; break;
			case 3: if(round%5==0) bytes.truncate(qMax(9, pos)); break;
			}
		}

		QString error;
		QStringList warnings;
		const auto png = Png::fromBytes(bytes, &error, &warnings);
		if(!png) continue;

		for(int i=0; i<png->count(); i++) {
			Chunk *c = png->chunk(i);
			(void)c->description();
			(void)c->attributes();
			if(c->id==CHUNK_iCCP) (void)dumpIccChunk(*c);

			PaletteInfo p;
			if(png->loadPaletteInfo(c, &p, &error)) png->applyPaletteInfo(p);
		}
		(void)png->checkValidity();
		if(decodePng(bytes, opts).ok) decoded++;
	}
	QVERIFY(decoded>0);
}

QTEST_MAIN(TestCore)
#include "test_core.moc"
