// test_ui.cpp -- tests of the windows and dialogs, run on the offscreen
// platform. Set TWEAKPNG_SCREENSHOT_DIR to save a picture of each dialog.

#include "chunkdialogs.h"
#include "mainwindow.h"
#include "metrics.h"
#include "palettedialog.h"
#include "pngbuilder.h"
#include "settings.h"
#include "tools.h"
#include "viewer.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace twpng;

namespace {

void screenshot(QWidget *w, const QString &name)
{
	const QByteArray dir = qgetenv("TWEAKPNG_SCREENSHOT_DIR");
	if(dir.isEmpty()) return;
	QDir().mkpath(QString::fromLocal8Bit(dir));
	w->show();
	QApplication::processEvents();
	w->grab().save(QString::fromLocal8Bit(dir)+QLatin1Char('/')+name+QStringLiteral(".png"));
}

std::unique_ptr<Png> load(const QByteArray &bytes)
{
	QString error;
	return Png::fromBytes(bytes, &error, nullptr);
}

QByteArray u32s(std::initializer_list<quint32> values)
{
	QByteArray out;
	for(quint32 v : values) out += be32(v);
	return out;
}

// Every chunk that has a field-based editor, in a file where its length is valid.
QByteArray fullFile(int colorType, int depth = 8)
{
	QByteArray extra;
	extra += makeChunk("gAMA", be32(45455));
	extra += makeChunk("cHRM", u32s({31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000}));
	extra += makeChunk("sRGB", QByteArray(1, '\2'));
	extra += makeChunk("sTER", QByteArray(1, '\1'));
	extra += makeChunk("pHYs", u32s({2835, 2835})+QByteArray(1, '\1'));
	extra += makeChunk("tIME", QByteArray::fromHex("07e60c1f173b3c"));
	extra += makeChunk("acTL", u32s({3, 2}));
	extra += makeChunk("fcTL", u32s({0, 10, 20, 1, 2})+QByteArray::fromHex("000100020101"));
	extra += makeChunk("fdAT", be32(5)+QByteArray("xyz"));
	extra += makeChunk("oFFs", u32s({0xffffffffU, 2})+QByteArray(1, '\1'));
	extra += makeChunk("sCAL", QByteArray("\x02" "1.5\0" "2e3", 8));
	extra += makeChunk("vpAg", u32s({640, 480})+QByteArray(1, '\0'));
	extra += makeChunk("tEXt", QByteArray("Title\0hello", 11));
	extra += makeChunk("zTXt", QByteArray("Comment\0\0", 9)+deflated("compressed text"));
	extra += makeChunk("iTXt", QByteArray("Title\0\0\0de\0Titel\0caf\xc3\xa9", 22));
	extra += makeChunk("iCCP", QByteArray("prof\0\0", 6)+deflated(QByteArray(200, '\0')));
	if(colorType==2) {
		extra += makeChunk("sBIT", QByteArray::fromHex("050607"));
		extra += makeChunk("bKGD", QByteArray::fromHex("000100020003"));
		extra += makeChunk("tRNS", QByteArray::fromHex("000400050006"));
	}
	else if(colorType==6) {
		extra += makeChunk("sBIT", QByteArray::fromHex("05060708"));
	}
	else if(colorType==4) {
		extra += makeChunk("sBIT", QByteArray::fromHex("0506"));
	}
	return makePng(extra, depth, colorType);
}

QByteArray palettedFile()
{
	return pngSignature()+makeIhdr(1, 1, 8, 3)+makeChunk("PLTE", QByteArray::fromHex("ff000000ff000000ff"))
		+makeChunk("tRNS", QByteArray::fromHex("80"))+makeChunk("bKGD", QByteArray::fromHex("01"))
		+makeIdat(QByteArray(2, '\0'))+makeChunk("IEND", QByteArray());
}

QString writeTemp(QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
	const QString fn = dir.filePath(name);
	QFile f(fn);
	f.open(QIODevice::WriteOnly);
	f.write(bytes);
	return fn;
}

} // namespace

class TestUi : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void init();

	void everyEditorRoundTrips_data();
	void everyEditorRoundTrips();
	void editorsWriteTheirFields();
	void textEditor();
	void paletteEditorRouting();
	void paletteDialog();
	void paletteDialogGray();
	void mainWindowLoadsFile();
	void editActionsFollowSelection();
	void deleteMoveCopyPaste();
	void combineFromSelection();
	void insertMenu();
	void dirtyTitleAndSave();
	void toolParsing();
	void filterTool();
	void viewerShowsImage();
	void viewerErrors();
	void viewerZoom();
	void settingsRoundTrip();
	void resourcesArePresent();
	void windowsFitTheirContents_data();
	void windowsFitTheirContents();
};

void TestUi::initTestCase()
{
	QStandardPaths::setTestModeEnabled(true);
	QCoreApplication::setOrganizationName(QStringLiteral("TweakPNG-test"));
	QCoreApplication::setApplicationName(QStringLiteral("TweakPNG-test"));
}

void TestUi::init()
{
	appSettings() = AppSettings();
}

void TestUi::everyEditorRoundTrips_data()
{
	QTest::addColumn<int>("colorType");
	QTest::newRow("rgb") << 2;
	QTest::newRow("rgba") << 6;
	QTest::newRow("graya") << 4;
}

// Opening each editor and pressing OK without touching it must not change
// a single byte of the file.
void TestUi::everyEditorRoundTrips()
{
	QFETCH(int, colorType);

	const QByteArray bytes = fullFile(colorType);
	const auto png = load(bytes);
	QVERIFY(png);

	int editors = 0;
	for(int i=0; i<png->count(); i++) {
		Chunk *ch = png->chunk(i);
		QVERIFY2(ch->hasValidLength(), ch->type);
		if(usesPaletteEditor(ch)) continue;

		std::unique_ptr<ChunkEditor> e(createChunkEditor(ch, nullptr));
		if(!e) {
			QVERIFY2(!ch->canEdit() || ch->id==CHUNK_bKGD, ch->type);
			continue;
		}
		editors++;
		screenshot(e.get(), QStringLiteral("editor-%1-%2").arg(colorType).arg(QLatin1String(ch->type)));

		e->commit();
		ch->invalidateTextInfo();
		ch->chunkModified();
	}
	QVERIFY(editors>=16);

	// iCCP and text chunks are rebuilt and may recompress; compare chunk by chunk.
	const auto original = load(bytes);
	for(int i=0; i<png->count(); i++) {
		Chunk *a = png->chunk(i);
		Chunk *b = original->chunk(i);
		QCOMPARE(a->typeName(), b->typeName());
		if(a->id==CHUNK_zTXt || a->id==CHUNK_iTXt) {
			TextInfo ta, tb;
			QVERIFY(a->textInfo(&ta));
			QVERIFY(b->textInfo(&tb));
			QCOMPARE(ta.text, tb.text);
			QCOMPARE(ta.keyword, tb.keyword);
			QCOMPARE(ta.language, tb.language);
			QCOMPARE(ta.translatedKeyword, tb.translatedKeyword);
		}
		else {
			QVERIFY2(a->data==b->data, a->type);
		}
	}
}

void TestUi::editorsWriteTheirFields()
{
	const auto png = load(fullFile(2));

	Chunk *gama = png->findFirstChunk(CHUNK_gAMA);
	{
		std::unique_ptr<ChunkEditor> e(createChunkEditor(gama, nullptr));
		e->findChild<QComboBox*>(QStringLiteral("gamma"))->setEditText(QStringLiteral("0.66667"));
		e->commit();
		QCOMPARE(readInt32(gama->bytes()), 66667U);
	}

	Chunk *phys = png->findFirstChunk(CHUNK_pHYs);
	{
		std::unique_ptr<ChunkEditor> e(createChunkEditor(phys, nullptr));
		QCOMPARE(e->findChild<QLabel*>(QStringLiteral("inches"))->text().left(7), QStringLiteral("= 72.01"));
		e->findChild<QLineEdit*>(QStringLiteral("X"))->setText(QStringLiteral("100"));
		e->commit();
		QCOMPARE(readInt32(phys->bytes()), 100U);
		QCOMPARE(readInt32(phys->bytes()+4), 2835U);
	}

	Chunk *offs = png->findFirstChunk(CHUNK_oFFs);
	{
		std::unique_ptr<ChunkEditor> e(createChunkEditor(offs, nullptr));
		QCOMPARE(e->findChild<QLineEdit*>(QStringLiteral("X"))->text(), QStringLiteral("-1"));
		e->findChild<QLineEdit*>(QStringLiteral("Y"))->setText(QStringLiteral("-5"));
		e->commit();
		QCOMPARE((int)readInt32(offs->bytes()+4), -5);
	}

	Chunk *actl = png->findFirstChunk(CHUNK_acTL);
	{
		std::unique_ptr<ChunkEditor> e(createChunkEditor(actl, nullptr));
		QCheckBox *playForever = e->findChild<QCheckBox*>(QStringLiteral("forever"));
		QLineEdit *plays = e->findChild<QLineEdit*>(QStringLiteral("Number of plays"));
		QVERIFY(plays->isEnabled());
		playForever->setChecked(true);
		QVERIFY(!plays->isEnabled());
		e->commit();
		QCOMPARE(readInt32(actl->bytes()+4), 0U);
	}

	Chunk *time = png->findFirstChunk(CHUNK_tIME);
	{
		std::unique_ptr<ChunkEditor> e(createChunkEditor(time, nullptr));
		e->findChild<QPushButton*>(QStringLiteral("setCurrentTime"))->click();
		e->commit();
		QVERIFY(readInt16(time->bytes())>=2024);
	}

	Chunk *scal = png->findFirstChunk(CHUNK_sCAL);
	{
		QCOMPARE(scal->sCalData().units, 2);
		std::unique_ptr<ChunkEditor> e(createChunkEditor(scal, nullptr));
		e->findChild<QLineEdit*>(QStringLiteral("X"))->setText(QStringLiteral("9.5"));
		e->commit();
		QCOMPARE(scal->sCalData().x, QStringLiteral("9.5"));
		QCOMPARE(scal->sCalData().units, 2);
	}
}

void TestUi::textEditor()
{
	const auto png = load(fullFile(2));
	Chunk *ch = png->findFirstChunk(CHUNK_tEXt);

	std::unique_ptr<ChunkEditor> e(createChunkEditor(ch, nullptr));
	e->show();
	QLineEdit *language = e->findChild<QLineEdit*>(QStringLiteral("language"));
	QVERIFY(!language->isVisible());

	QCheckBox *intl = e->findChild<QCheckBox*>(QStringLiteral("international"));
	intl->setChecked(true);
	QVERIFY(language->isVisible());
	language->setText(QStringLiteral("ja"));
	screenshot(e.get(), QStringLiteral("editor-text"));

	const QString unicode = QStringLiteral("line 1\nline ") + QChar(0x65e5);
	e->findChild<QPlainTextEdit*>(QStringLiteral("text"))->setPlainText(unicode);
	e->findChild<QCheckBox*>(QStringLiteral("compressed"))->setChecked(true);
	e->commit();

	QCOMPARE(QByteArray(ch->type), QByteArray("iTXt"));
	TextInfo ti;
	QVERIFY(ch->textInfo(&ti));
	QCOMPARE(ti.text, unicode);
	QCOMPARE(ti.language, QStringLiteral("ja"));
	QVERIFY(ti.isCompressed);
}

void TestUi::paletteEditorRouting()
{
	const auto pal = load(palettedFile());
	QVERIFY(usesPaletteEditor(pal->findFirstChunk(CHUNK_PLTE)));
	QVERIFY(usesPaletteEditor(pal->findFirstChunk(CHUNK_tRNS)));
	QVERIFY(usesPaletteEditor(pal->findFirstChunk(CHUNK_bKGD)));

	const auto rgb = load(fullFile(2));
	QVERIFY(!usesPaletteEditor(rgb->findFirstChunk(CHUNK_tRNS)));
	QVERIFY(!usesPaletteEditor(rgb->findFirstChunk(CHUNK_bKGD)));

	const auto gray8 = load(makePng(makeChunk("tRNS", QByteArray::fromHex("0007")), 8, 0));
	QVERIFY(usesPaletteEditor(gray8->findFirstChunk(CHUNK_tRNS)));
	const auto gray16 = load(makePng(makeChunk("tRNS", QByteArray::fromHex("0007")), 16, 0));
	QVERIFY(!usesPaletteEditor(gray16->findFirstChunk(CHUNK_tRNS)));
}

void TestUi::paletteDialog()
{
	const auto png = load(palettedFile());
	PaletteInfo info;
	QString error;
	QVERIFY(png->loadPaletteInfo(png->findFirstChunk(CHUNK_bKGD), &info, &error));

	PaletteDialog d(info, nullptr);
	d.resize(490, 440);
	d.show();
	QApplication::processEvents();
	screenshot(&d, QStringLiteral("palette"));

	QLineEdit *num = d.findChild<QLineEdit*>(QStringLiteral("numPlte"));
	num->setText(QStringLiteral("2"));
	QCOMPARE(d.info.numPlte, 2);
	num->setText(QStringLiteral("9999"));  // clamped
	QCOMPARE(d.info.numPlte, 256);
	num->setText(QStringLiteral("3"));

	// bKGD opened the dialog, so the Set-bkgd button starts down: a click
	// on the grid picks the background entry, and releases the button.
	QPushButton *setBkgd = d.findChild<QPushButton*>(QStringLiteral("setBkgd"));
	QVERIFY(setBkgd->isChecked());
	QVERIFY(d.crossCursor());
	const int cell = d.grid->width()/16;
	QTest::mouseClick(d.grid, Qt::LeftButton, Qt::NoModifier, QPoint(cell*2+cell/2, 10));
	QCOMPARE((int)d.info.bkgd, 2);
	QVERIFY(!setBkgd->isChecked());

	QCOMPARE(d.grid->entryAt(1, 1), -1);
	QCOMPARE(d.grid->entryAt(cell+6, 10), 1);
	QCOMPARE(d.grid->entryAt(cell*15+6, 10), -1);  // past the end of the palette

	png->applyPaletteInfo(d.info);
	QCOMPARE(png->findFirstChunk(CHUNK_bKGD)->data, QByteArray::fromHex("02"));
}

void TestUi::paletteDialogGray()
{
	const auto png = load(makePng(makeChunk("tRNS", QByteArray::fromHex("0002")), 2, 0));
	PaletteInfo info;
	QString error;
	Chunk *trns = png->findFirstChunk(CHUNK_tRNS);
	QVERIFY(png->loadPaletteInfo(trns, &info, &error));
	QCOMPARE(info.numPlte, 4);
	QCOMPARE((int)info.plte[1].red, 85);

	PaletteDialog d(info, nullptr);
	d.show();
	QApplication::processEvents();
	screenshot(&d, QStringLiteral("palette-gray"));

	QVERIFY(!d.findChild<QLineEdit*>(QStringLiteral("numPlte"))->isEnabled());
	QVERIFY(d.findChild<QPushButton*>(QStringLiteral("editAlpha"))->isChecked());
	QCOMPARE(d.findChild<QPushButton*>(QStringLiteral("editAlpha"))->text(), QStringLiteral("Set trns"));

	const int cell = d.grid->width()/16;
	QTest::mouseClick(d.grid, Qt::LeftButton, Qt::NoModifier, QPoint(cell*3+cell/2, 10));
	QCOMPARE((int)d.info.trnsColor, 3);

	png->applyPaletteInfo(d.info);
	QCOMPARE(trns->data, QByteArray::fromHex("0003"));
}

void TestUi::mainWindowLoadsFile()
{
	MainWindow w;
	w.show();
	QCOMPARE(w.statusText(), QStringLiteral("No file loaded"));
	QCOMPARE(w.windowTitle(), QStringLiteral("TweakPNG"));
	QVERIFY(!w.action(QStringLiteral("save"))->isEnabled());

	const QString sample = QStringLiteral(TWEAKPNG_SOURCE_DIR "/samples/sample.png");
	QVERIFY(w.openFile(sample));
	QCOMPARE(w.model()->rowCount(), w.png()->count());
	QVERIFY(w.statusText().startsWith(QStringLiteral("PNG file size: ")));
	QVERIFY(w.windowTitle().startsWith(QStringLiteral("sample.png (")));
	QVERIFY(w.action(QStringLiteral("save"))->isEnabled());
	QCOMPARE(w.model()->index(0, 0).data().toString(), QStringLiteral("IHDR"));
	QVERIFY(w.model()->index(0, 4).data().toString().startsWith(QStringLiteral("PNG image header")));
	screenshot(&w, QStringLiteral("main"));
}

void TestUi::editActionsFollowSelection()
{
	QTemporaryDir dir;
	MainWindow w;
	QVERIFY(w.openFile(writeTemp(dir, QStringLiteral("a.png"), fullFile(2))));

	QVERIFY(!w.action(QStringLiteral("delete"))->isEnabled());
	QVERIFY(!w.action(QStringLiteral("editChunk"))->isEnabled());

	w.selectRows(1, 1);
	QVERIFY(w.action(QStringLiteral("delete"))->isEnabled());
	QVERIFY(w.action(QStringLiteral("editChunk"))->isEnabled());
	QVERIFY(!w.action(QStringLiteral("combineSelected"))->isEnabled());

	w.selectRows(1, 3);
	QVERIFY(w.action(QStringLiteral("delete"))->isEnabled());
	QVERIFY(!w.action(QStringLiteral("editChunk"))->isEnabled());
	QVERIFY(w.action(QStringLiteral("combineSelected"))->isEnabled());
}

void TestUi::deleteMoveCopyPaste()
{
	QTemporaryDir dir;
	MainWindow w;
	const QByteArray file = makePng(makeChunk("gAMA", be32(1))+makeChunk("sRGB", QByteArray(1, '\0')));
	QVERIFY(w.openFile(writeTemp(dir, QStringLiteral("a.png"), file)));
	Png *png = w.png();

	w.selectRows(1, 1);
	w.moveChunksDown();
	QCOMPARE(png->chunk(2)->id, (int)CHUNK_gAMA);
	QCOMPARE(w.selectedRows(), QVector<int>({2}));
	QVERIFY(png->dirty);
	QVERIFY(w.windowTitle().contains(QLatin1Char('*')));

	w.moveChunksUp();
	QCOMPARE(png->chunk(1)->id, (int)CHUNK_gAMA);

	// Multiple rows move together; a block at the top doesn't move.
	w.selectRows(0, 2);
	w.moveChunksUp();
	QCOMPARE(png->chunk(0)->id, (int)CHUNK_IHDR);

	w.selectRows(1, 2);
	QVERIFY(w.copyChunks());
	QVERIFY(w.action(QStringLiteral("paste"))->isEnabled());
	w.selectRows(3, 1);
	w.pasteChunks();
	QCOMPARE(png->count(), 7);
	QCOMPARE(w.selectedRows(), QVector<int>({3, 4}));
	QCOMPARE(png->chunk(3)->id, (int)CHUNK_gAMA);

	w.deleteChunks();
	QCOMPARE(png->count(), 5);
	QCOMPARE(w.selectedRows(), QVector<int>({3}));

	// Delete everything: the document stays open and empty.
	w.selectRows(0, png->count());
	w.deleteChunks();
	QCOMPARE(png->count(), 0);
	QVERIFY(w.selectedRows().isEmpty());
	QApplication::clipboard()->clear();
}

void TestUi::combineFromSelection()
{
	QTemporaryDir dir;
	MainWindow w;
	const QByteArray idat = makeChunk("IDAT", QByteArray(4, 'x'));
	QVERIFY(w.openFile(writeTemp(dir, QStringLiteral("a.png"),
		pngSignature()+makeIhdr(1, 1, 8, 0)+idat+idat+idat+makeChunk("IEND", QByteArray()))));

	w.selectRows(1, 2);
	w.combineSelectedIdat();
	QCOMPARE(w.png()->count(), 4);
	QCOMPARE((int)w.png()->chunk(1)->length(), 8);
	QCOMPARE(w.selectedRows(), QVector<int>({1}));

	w.combineAllIdat();
	QCOMPARE(w.png()->count(), 3);
	QCOMPARE((int)w.png()->chunk(1)->length(), 12);
}

void TestUi::insertMenu()
{
	MainWindow w;
	w.newDocument();
	QVERIFY(w.png());
	QCOMPARE(w.statusText(), QStringLiteral("PNG file size: 8 bytes"));
	QVERIFY(w.windowTitle().startsWith(QStringLiteral("untitled")));

	w.action(QStringLiteral("insertIHDR"))->trigger();
	w.action(QStringLiteral("insertIEND"))->trigger();
	QCOMPARE(w.png()->count(), 2);
	QCOMPARE(w.png()->chunk(0)->id, (int)CHUNK_IHDR);

	w.action(QStringLiteral("insertgAMA"))->trigger();
	QCOMPARE(w.png()->chunk(1)->id, (int)CHUNK_gAMA);
	QCOMPARE(w.selectedRows(), QVector<int>({1}));

	w.action(QStringLiteral("inserttEXt"))->trigger();
	QCOMPARE(w.png()->chunk(2)->id, (int)CHUNK_tEXt);
}

void TestUi::dirtyTitleAndSave()
{
	QTemporaryDir dir;
	const QString fn = writeTemp(dir, QStringLiteral("a.png"), makePng());

	MainWindow w;
	QVERIFY(w.openFile(fn));
	w.action(QStringLiteral("insertgAMA"))->trigger();
	QVERIFY(w.windowTitle().contains(QLatin1Char('*')));

	w.action(QStringLiteral("save"))->trigger();
	QVERIFY(!w.png()->dirty);
	QVERIFY(!w.windowTitle().contains(QLatin1Char('*')));

	QFile f(fn);
	QVERIFY(f.open(QIODevice::ReadOnly));
	QCOMPARE(f.readAll(), w.png()->toBytes());
}

void TestUi::toolParsing()
{
	QCOMPARE(classifyToolParams(QStringLiteral("   ")), TPARAMS_EMPTY);
	QCOMPARE(classifyToolParams(QStringLiteral("-v")), TPARAMS_VIEWER_NOREPLACE);
	QCOMPARE(classifyToolParams(QStringLiteral("-v %1")), TPARAMS_VIEWER_REPLACE);
	QCOMPARE(classifyToolParams(QStringLiteral("%1 %2")), TPARAMS_FILTER);

	QCOMPARE(splitToolArgs(QStringLiteral("a  \"b c\" d\"e f\"")),
		QStringList({QStringLiteral("a"), QStringLiteral("b c"), QStringLiteral("de f")}));
	QCOMPARE(splitToolArgs(QStringLiteral("\"\"")), QStringList({QString()}));

	// A filename with spaces and quotes stays one argument.
	const QString spaced = QStringLiteral("/tmp/my \"file\".png");
	QCOMPARE(substituteToolArgs({QStringLiteral("-i"), QStringLiteral("--in=%1"), QStringLiteral("%2")}, spaced, QStringLiteral("out")),
		QStringList({QStringLiteral("-i"), QStringLiteral("--in=")+spaced, QStringLiteral("out")}));

	QString program;
	QStringList args;
	QVERIFY(buildToolCommand(QStringLiteral("/bin/x"), QStringLiteral("-a %1"), QStringLiteral("f"), QString(), &program, &args));
	QCOMPARE(program, QStringLiteral("/bin/x"));
	QCOMPARE(args, QStringList({QStringLiteral("-a"), QStringLiteral("f")}));

	QVERIFY(buildToolCommand(QString(), QStringLiteral("prog %1 %2"), QStringLiteral("a"), QStringLiteral("b"), &program, &args));
	QCOMPARE(program, QStringLiteral("prog"));
	QCOMPARE(args, QStringList({QStringLiteral("a"), QStringLiteral("b")}));

	QVERIFY(!buildToolCommand(QString(), QString(), QString(), QString(), &program, &args));
}

void TestUi::filterTool()
{
#ifdef Q_OS_WIN
	QSKIP("uses a shell script");
#else
	QTemporaryDir dir;
	const QString script = writeTemp(dir, QStringLiteral("filter.sh"),
		QByteArray("#!/bin/sh\ncp \"$1\" \"$2\"\n"));
	QFile::setPermissions(script, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);

	appSettings().tools[0] = ToolConfig{QStringLiteral("Copy"), script, QStringLiteral("%1 %2")};

	MainWindow w;
	w.show();
	QVERIFY(w.openFile(writeTemp(dir, QStringLiteral("a.png"), makePng())));
	const QByteArray before = w.png()->toBytes();

	w.action(QStringLiteral("tool1"))->trigger();
	QCOMPARE(w.png()->toBytes(), before);
	QVERIFY(w.png()->dirty);
	QVERIFY(w.png()->named);
#endif
}

void TestUi::viewerShowsImage()
{
	QTemporaryDir dir;
	MainWindow w;
	w.show();
	QVERIFY(w.openFile(QStringLiteral(TWEAKPNG_SOURCE_DIR "/samples/sample.png")));

	w.action(QStringLiteral("viewer"))->trigger();
	QVERIFY(w.viewer());
	QVERIFY(w.action(QStringLiteral("viewer"))->isChecked());
	QVERIFY(appSettings().autoOpenViewer);
	QVERIFY(!w.viewer()->image().isNull());
	QCOMPARE(w.viewer()->windowTitle(), QStringLiteral("sample.png - TweakPNG Image Viewer"));
	screenshot(w.viewer(), QStringLiteral("viewer"));

	// An edit reaches the viewer after the update delay.
	w.selectRows(1, 1);
	w.action(QStringLiteral("insertgAMA"))->trigger();
	QTest::qWait(600);
	QVERIFY(!w.viewer()->image().isNull());

	w.action(QStringLiteral("viewer"))->trigger();
	QTRY_VERIFY(!w.viewer());
	QVERIFY(!appSettings().autoOpenViewer);
	QVERIFY(!w.action(QStringLiteral("viewer"))->isChecked());
}

void TestUi::viewerErrors()
{
	ViewerWindow *v = new ViewerWindow(nullptr);
	v->show();

	const auto mng = load(makePng());
	mng->setImageType(IMG_MNG);
	v->updateImage(mng.get());
	QVERIFY(v->errorMessage().contains(QStringLiteral("support MNG")));
	QVERIFY(v->image().isNull());
	screenshot(v, QStringLiteral("viewer-error"));

	const auto broken = load(pngSignature()+makeIhdr(1, 1, 8, 3)+makeChunk("IEND", QByteArray()));
	v->updateImage(broken.get());
	QVERIFY(!v->errorMessage().isEmpty());

	v->updateImage(nullptr);
	QVERIFY(v->errorMessage().isEmpty());
	v->close();
}

void TestUi::viewerZoom()
{
	const auto png = load(pngSignature()+makeIhdr(16, 8, 8, 0)+makeIdat(QByteArray(17*8, '\0'))+makeChunk("IEND", QByteArray()));

	ViewerWindow *v = new ViewerWindow(nullptr);
	v->resize(400, 300);
	v->show();
	v->updateImage(png.get());
	QVERIFY(!v->image().isNull());

	const struct { int zoom; QSize size; } kCases[] = {
		{VS_1, QSize(16, 8)}, {VS_2, QSize(32, 16)}, {VS_4, QSize(64, 32)},
		{VS_1_2, QSize(8, 4)}, {VS_1_4, QSize(4, 2)}, {VS_1_8, QSize(2, 1)},
	};
	for(const auto &c : kCases) {
		appSettings().zoom = c.zoom;
		v->calcStretchedSize();
		QCOMPARE(v->stretchedSize(), c.size);
	}

	// Fit keeps the aspect ratio.
	appSettings().zoom = VS_FIT;
	v->calcStretchedSize();
	QCOMPARE(v->stretchedSize().width(), v->stretchedSize().height()*2);

	// Non-square pixels: twice the density in X doubles the height.
	appSettings().zoom = VS_1;
	const auto wide = load(pngSignature()+makeIhdr(16, 8, 8, 0)+makeChunk("pHYs", u32s({200, 100})+QByteArray(1, '\0'))
		+makeIdat(QByteArray(17*8, '\0'))+makeChunk("IEND", QByteArray()));
	v->updateImage(wide.get());
	QCOMPARE(v->stretchedSize(), QSize(16, 16));

	v->close();
}

void TestUi::settingsRoundTrip()
{
	AppSettings &s = appSettings();
	s.compressionLevel = 7;
	s.useGamma = false;
	s.customBg = QColor(1, 2, 3);
	s.zoom = VS_2;
	s.tools[2] = ToolConfig{QStringLiteral("n"), QStringLiteral("c"), QStringLiteral("p")};
	s.save();

	AppSettings fresh;
	fresh.load();
	QCOMPARE(fresh.compressionLevel, 7);
	QCOMPARE(fresh.useGamma, false);
	QCOMPARE(fresh.customBg, QColor(1, 2, 3));
	QCOMPARE(fresh.zoom, (int)VS_2);
	QCOMPARE(fresh.tools[2].cmd, QStringLiteral("c"));
}

void TestUi::resourcesArePresent()
{
	QVERIFY(QFile::exists(QStringLiteral(":/tweakpng.txt")));
	QVERIFY(!QIcon(QStringLiteral(":/tweakpng.png")).pixmap(32, 32).isNull());
	QVERIFY(!QIcon(QStringLiteral(":/viewer.png")).pixmap(32, 32).isNull());
}

void TestUi::windowsFitTheirContents_data()
{
	QTest::addColumn<int>("pointSize");
	QTest::newRow("normal font") << 9;
	QTest::newRow("large font") << 20;
}

// Nothing may be clipped or squashed, whatever the size of the font.
void TestUi::windowsFitTheirContents()
{
	QFETCH(int, pointSize);

	struct RestoreFont {
		QFont saved = QApplication::font();
		~RestoreFont() { QApplication::setFont(saved); }
	} restoreFont;

	QFont big = QApplication::font();
	big.setPointSize(pointSize);
	QApplication::setFont(big);

	const auto check = [](QWidget *w, const QString &name) {
		w->show();
		QApplication::processEvents();
		screenshot(w, name);

		QVERIFY2(w->width()>=titleWidth(w) || w->width()*10>=w->screen()->availableGeometry().width()*9, qPrintable(name));

		for(const QLabel *l : w->findChildren<QLabel*>()) {
			if(l->isVisible() && !l->wordWrap()) {
				QVERIFY2(l->width()>=l->fontMetrics().horizontalAdvance(l->text()),
					qPrintable(name+QStringLiteral(": ")+l->text()));
			}
		}
		for(const QWidget *f : w->findChildren<QLineEdit*>()) {
			if(f->isVisible()) QVERIFY2(f->height()>=f->fontMetrics().height(), qPrintable(name));
		}
		for(const QWidget *f : w->findChildren<QComboBox*>()) {
			if(f->isVisible()) QVERIFY2(f->height()>=f->fontMetrics().height(), qPrintable(name));
		}
	};

	const QString suffix = QStringLiteral("-%1pt").arg(pointSize);

	const auto png = load(fullFile(2));
	for(int i=0; i<png->count(); i++) {
		Chunk *ch = png->chunk(i);
		if(usesPaletteEditor(ch)) continue;
		std::unique_ptr<ChunkEditor> e(createChunkEditor(ch, nullptr));
		if(e) check(e.get(), QStringLiteral("fit-")+QLatin1String(ch->type)+suffix);
		if(QTest::currentTestFailed()) return;
	}

	const auto pal = load(palettedFile());
	PaletteInfo info;
	QString error;
	QVERIFY(pal->loadPaletteInfo(pal->findFirstChunk(CHUNK_PLTE), &info, &error));
	PaletteDialog palette(info, nullptr);
	check(&palette, QStringLiteral("fit-palette")+suffix);
	if(QTest::currentTestFailed()) return;

	// Showing an entry's values must not move the grid.
	const QRect gridBefore = palette.grid->geometry();
	for(int i : {0, 1, 2, -1}) {
		palette.entryHovered(i);
		QApplication::processEvents();
		QCOMPARE(palette.grid->geometry(), gridBefore);
	}

	MainWindow w;
	QVERIFY(w.openFile(QStringLiteral(TWEAKPNG_SOURCE_DIR "/samples/sample.png")));
	w.show();
	QApplication::processEvents();
	screenshot(&w, QStringLiteral("fit-main")+suffix);
	QVERIFY(w.width()>=textColumns(&w, 70) || w.width()*10>=w.screen()->availableGeometry().width()*9);
	QVERIFY(w.table()->columnWidth(3)>=w.fontMetrics().horizontalAdvance(w.model()->index(1, 3).data().toString()));

	w.action(QStringLiteral("viewer"))->trigger();
	QVERIFY(w.viewer());
	QVERIFY(w.viewer()->width()>=w.viewer()->menuBar()->sizeHint().width());
	screenshot(w.viewer(), QStringLiteral("fit-viewer")+suffix);
}

QTEST_MAIN(TestUi)
#include "test_ui.moc"
