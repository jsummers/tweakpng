// mainwindow.cpp -- the main window, and the chunk list.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "mainwindow.h"
#include "chunkdialogs.h"
#include "metrics.h"
#include "settings.h"
#include "tools.h"
#include "viewer.h"

#include "pngimage.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <zlib.h>

#include <algorithm>

using namespace twpng;

namespace {

const int kUpdateDelayMs = 400;
const char *const kChunkMime = "application/x-tweakpng-chunks";
const QChar kRsquo(0x2019);
const QChar kEnDash(0x2013);
const QChar kLdquo(0x201c);
const QChar kRdquo(0x201d);
const QChar kMiddot(0x00b7);

const char *const kCompressionNames[] = { "Default compression", "0 = No compression",
	"1 = Fastest", "2", "3", "4", "5", "6", "7", "8", "9 = Best compression", nullptr };

} // namespace

ChunkModel::ChunkModel(QObject *parent)
	: QAbstractTableModel(parent)
{
}

void ChunkModel::setPng(Png *png)
{
	beginResetModel();
	m_png = png;
	endResetModel();
}

void ChunkModel::refresh()
{
	beginResetModel();
	endResetModel();
}

void ChunkModel::refreshRow(int row)
{
	emit dataChanged(index(row, 0), index(row, 4));
}

void ChunkModel::refreshAllRows()
{
	if(rowCount()>0) emit dataChanged(index(0, 0), index(rowCount()-1, 4));
}

int ChunkModel::rowCount(const QModelIndex &parent) const
{
	return (m_png && !parent.isValid()) ? m_png->count() : 0;
}

int ChunkModel::columnCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : 5;
}

QVariant ChunkModel::data(const QModelIndex &index, int role) const
{
	if(!m_png || !index.isValid() || index.row()>=m_png->count()) return QVariant();
	Chunk *c = m_png->chunk(index.row());

	if(role==Qt::TextAlignmentRole && index.column()==1) {
		return QVariant(Qt::AlignRight|Qt::AlignVCenter);
	}
	if(role!=Qt::DisplayRole) return QVariant();

	switch(index.column()) {
	case 0: return c->typeName();
	case 1: return QString::number(c->length());
	case 2: return QString::asprintf("%08x", c->crc);
	case 3: return c->attributes();
	case 4: return c->description();
	}
	return QVariant();
}

QVariant ChunkModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if(role!=Qt::DisplayRole || orientation!=Qt::Horizontal) return QVariant();
	static const char *const kHeaders[5] = {"Chunk", "Length", "CRC", "Attributes", "Contents"};
	return (section>=0 && section<5) ? QVariant(QLatin1String(kHeaders[section])) : QVariant();
}

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
{
	setWindowIcon(QIcon(QStringLiteral(":/tweakpng.png")));
	setAcceptDrops(true);

	m_model = new ChunkModel(this);

	m_table = new QTableView;
	m_table->setModel(m_model);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setShowGrid(false);
	m_table->setWordWrap(false);
	m_table->setSortingEnabled(false);
	m_table->verticalHeader()->hide();
	m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height()+6);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->horizontalHeader()->setHighlightSections(false);
	m_table->setColumnWidth(0, textFit(this, QStringLiteral("Chunk")));
	m_table->setColumnWidth(1, textFit(this, QStringLiteral("9999999999")));
	m_table->setColumnWidth(2, textFit(this, QStringLiteral("dddddddd")));
	m_table->setColumnWidth(3, textFit(this, QStringLiteral("ancillary, private, unsafe to copy")));
	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	setCentralWidget(m_table);

	if(!appSettings().headerState.isEmpty()) {
		m_table->horizontalHeader()->restoreState(appSettings().headerState);
	}

	m_status = new QLabel;
	statusBar()->addWidget(m_status, 1);

	m_viewerTimer = new QTimer(this);
	m_viewerTimer->setSingleShot(true);
	m_viewerTimer->setInterval(kUpdateDelayMs);
	connect(m_viewerTimer, &QTimer::timeout, this, &MainWindow::refreshViewer);

	buildMenus();

	connect(m_table, &QTableView::doubleClicked, this, [this]() { actionEditChunk(); });
	connect(m_table, &QTableView::customContextMenuRequested, this, &MainWindow::contextMenu);
	connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { updateActions(); });
	connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() { updateActions(); });

	// Wide enough for a typical Contents column, and tall enough for the
	// chunks of a typical file.
	setMinimumSize(textColumns(this, 70), textLines(this, 10));
	resize(textSize(this, 140, 26));
	if(!appSettings().mainGeometry.isEmpty()) restoreGeometry(appSettings().mainGeometry);

	setTitle();
	updateStatus();
	updateActions();
}

MainWindow::~MainWindow()
{
	if(m_viewer) delete m_viewer;

	// The table outlives m_png, and must not read a deleted document.
	m_model->setPng(nullptr);
}

QAction *MainWindow::addAction(QMenu *menu, const QString &name, const QString &text,
	const QKeySequence &key, void (MainWindow::*slot)())
{
	QAction *a = menu->addAction(text);
	a->setObjectName(name);
	if(!key.isEmpty()) a->setShortcut(key);
	connect(a, &QAction::triggered, this, slot);
	return a;
}

QAction *MainWindow::action(const QString &name) const
{
	return findChild<QAction*>(name);
}

void MainWindow::buildMenus()
{
	QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
	addAction(file, QStringLiteral("new"), QStringLiteral("&New"), QKeySequence::New, &MainWindow::actionNew);
	addAction(file, QStringLiteral("open"), QStringLiteral("&Open..."), QKeySequence::Open, &MainWindow::actionOpen);
	addAction(file, QStringLiteral("reopen"), QStringLiteral("Reopen"), QKeySequence(Qt::CTRL|Qt::Key_R), &MainWindow::actionReopen);
	addAction(file, QStringLiteral("save"), QStringLiteral("&Save"), QKeySequence::Save, &MainWindow::actionSave);
	QAction *saveAs = addAction(file, QStringLiteral("saveAs"), QStringLiteral("Save &As..."), QKeySequence(), &MainWindow::actionSaveAs);
	saveAs->setShortcuts({QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_S), QKeySequence(Qt::Key_F12)});
	addAction(file, QStringLiteral("close"), QStringLiteral("&Close"), QKeySequence(), &MainWindow::actionClose);
	file->addSeparator();
	addAction(file, QStringLiteral("check"), QStringLiteral("Check &Validity"), QKeySequence(Qt::Key_F5), &MainWindow::actionCheckValidity);
	addAction(file, QStringLiteral("signature"), QStringLiteral("&File Signature..."), QKeySequence(), &MainWindow::actionSignature);
	file->addSeparator();
	QAction *exitAction = file->addAction(QStringLiteral("E&xit"));
	exitAction->setObjectName(QStringLiteral("exit"));
	exitAction->setShortcut(QKeySequence::Quit);
	exitAction->setMenuRole(QAction::QuitRole);
	connect(exitAction, &QAction::triggered, this, &QWidget::close);

	QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
	QAction *editChunkAction = addAction(edit, QStringLiteral("editChunk"), QStringLiteral("&Edit Chunk..."), QKeySequence(), &MainWindow::actionEditChunk);
	editChunkAction->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
	QAction *del = addAction(edit, QStringLiteral("delete"), QStringLiteral("&Delete"), QKeySequence(), &MainWindow::deleteChunks);
#ifdef Q_OS_MACOS
	del->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
#else
	del->setShortcut(QKeySequence(Qt::Key_Delete));
#endif
	addAction(edit, QStringLiteral("moveUp"), QStringLiteral("Move &Up"), QKeySequence(Qt::ALT|Qt::Key_Up), &MainWindow::moveChunksUp);
	addAction(edit, QStringLiteral("moveDown"), QStringLiteral("Mo&ve Down"), QKeySequence(Qt::ALT|Qt::Key_Down), &MainWindow::moveChunksDown);
	edit->addSeparator();
	addAction(edit, QStringLiteral("cut"), QStringLiteral("Cu&t"), QKeySequence::Cut, &MainWindow::actionCut);
	QAction *copy = edit->addAction(QStringLiteral("&Copy"));
	copy->setObjectName(QStringLiteral("copy"));
	copy->setShortcut(QKeySequence::Copy);
	connect(copy, &QAction::triggered, this, [this]() { copyChunks(); });
	addAction(edit, QStringLiteral("paste"), QStringLiteral("&Paste"), QKeySequence::Paste, &MainWindow::pasteChunks);
	addAction(edit, QStringLiteral("selectAll"), QStringLiteral("Select &All"), QKeySequence::SelectAll, &MainWindow::actionSelectAll);
	edit->addSeparator();
	addAction(edit, QStringLiteral("combineSelected"), QStringLiteral("Combine Selected IDAT"), QKeySequence(), &MainWindow::combineSelectedIdat);
	addAction(edit, QStringLiteral("combineAll"), QStringLiteral("Combine All &IDAT"), QKeySequence(), &MainWindow::combineAllIdat);
	addAction(edit, QStringLiteral("split"), QStringLiteral("Split IDAT..."), QKeySequence(), &MainWindow::actionSplitIdat);
	edit->addSeparator();
	addAction(edit, QStringLiteral("importChunk"), QStringLiteral("Import Chunk..."), QKeySequence(), &MainWindow::actionImportChunk);
	addAction(edit, QStringLiteral("exportChunk"), QStringLiteral("Export Chunk..."), QKeySequence(), &MainWindow::actionExportChunk);
	addAction(edit, QStringLiteral("importIcc"), QStringLiteral("Import ICC Profile..."), QKeySequence(), &MainWindow::actionImportIcc);

	static const struct { const char *text; int id; } kInsert[] = {
		{"acTL (APNG animation control)", CHUNK_acTL},
		{"bKGD (Background color)", CHUNK_bKGD},
		{"cHRM (Chromaticities)", CHUNK_cHRM},
		{"fcTL (APNG frame control)", CHUNK_fcTL},
		{"gAMA (Gamma)", CHUNK_gAMA},
		{"IEND (End of image)", CHUNK_IEND},
		{"IHDR (Image header)", CHUNK_IHDR},
		{"oFFs (Image offset)", CHUNK_oFFs},
		{"pHYs (Pixel size, aspect ratio)", CHUNK_pHYs},
		{"PLTE (Palette)", CHUNK_PLTE},
		{"sBIT (Significant bits per sample)", CHUNK_sBIT},
		{"sCAL (Physical scale)", CHUNK_sCAL},
		{"sRGB (sRGB color space indicator)", CHUNK_sRGB},
		{"sTER (Stereo image indicator)", CHUNK_sTER},
		{"tEXt/zTXt/iTXt (Text)", CHUNK_tEXt},
		{"tIME (Time of last modification)", CHUNK_tIME},
		{"tRNS (Transparency)", CHUNK_tRNS},
		{"vpAg (Virtual page)", CHUNK_vpAg},
	};
	QMenu *insert = menuBar()->addMenu(QStringLiteral("&Insert"));
	for(const auto &item : kInsert) {
		QAction *a = insert->addAction(QLatin1String(item.text));
		a->setObjectName(QStringLiteral("insert") + chunkNameFromId(item.id));
		a->setProperty("requiresDocument", true);
		if(item.id==CHUNK_tEXt) a->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_T));
		const int id = item.id;
		connect(a, &QAction::triggered, this, [this, id]() { insertChunk(id); });
	}

	QMenu *options = menuBar()->addMenu(QStringLiteral("&Options"));
	QAction *prefs = addAction(options, QStringLiteral("preferences"), QStringLiteral("&Preferences..."), QKeySequence(), &MainWindow::actionPreferences);
	prefs->setMenuRole(QAction::PreferencesRole);
	addAction(options, QStringLiteral("configureTools"), QStringLiteral("Configure &Tools..."), QKeySequence(), &MainWindow::actionConfigureTools);

	m_toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
	m_viewerAction = addAction(m_toolsMenu, QStringLiteral("viewer"), QStringLiteral("&Image Viewer"), QKeySequence(Qt::Key_F7), &MainWindow::actionToggleViewer);
	m_viewerAction->setCheckable(true);
	m_toolsMenu->addSeparator();
	for(int i=0; i<kNumTools; i++) {
		QAction *a = m_toolsMenu->addAction(QString());
		a->setObjectName(QStringLiteral("tool%1").arg(i+1));
		a->setShortcut(QKeySequence(Qt::CTRL|static_cast<Qt::Key>(Qt::Key_1+i)));
		connect(a, &QAction::triggered, this, [this, i]() { runTool(i); });
		m_toolActions[(size_t)i] = a;
	}
	rebuildToolsMenu();

	QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
	addAction(help, QStringLiteral("help"), QStringLiteral("&Contents"), QKeySequence(Qt::Key_F1), &MainWindow::actionHelp);
	QAction *about = addAction(help, QStringLiteral("about"), QStringLiteral("&About..."), QKeySequence(), &MainWindow::actionAbout);
	about->setMenuRole(QAction::AboutRole);

	// Ctrl+Tab switches windows. Qt's "Ctrl" is the Command key on macOS,
	// where the system owns Command+Tab, so use the physical Control key.
	QAction *switchWindow = new QAction(this);
	switchWindow->setObjectName(QStringLiteral("switchWindow"));
#ifdef Q_OS_MACOS
	switchWindow->setShortcut(QKeySequence(Qt::META|Qt::Key_Tab));
#else
	switchWindow->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Tab));
#endif
	connect(switchWindow, &QAction::triggered, this, &MainWindow::actionSwitchWindow);
	QMainWindow::addAction(switchWindow);
}

void MainWindow::rebuildToolsMenu()
{
	for(int i=0; i<kNumTools; i++) {
		const QString &name = appSettings().tools[(size_t)i].name;
		QAction *a = m_toolActions[(size_t)i];
		a->setText(QStringLiteral("&%1  %2").arg(i+1).arg(name));
		a->setVisible(!name.isEmpty());
	}
}

void MainWindow::updateActions()
{
	const bool doc = (m_png != nullptr);
	const int sel = doc ? m_table->selectionModel()->selectedRows().size() : 0;

	static const char *const kNeedsDocument[] = { "save", "saveAs", "close", "combineAll",
		"importChunk", "importIcc", "signature", "check", "selectAll",
		"tool1", "tool2", "tool3", "tool4", "tool5", "tool6", nullptr };
	static const char *const kNeedsOne[] = { "editChunk", "split", "exportChunk", nullptr };
	static const char *const kNeedsSome[] = { "delete", "copy", "cut", "moveUp", "moveDown", nullptr };

	for(int i=0; kNeedsDocument[i]; i++) action(QLatin1String(kNeedsDocument[i]))->setEnabled(doc);
	for(int i=0; kNeedsOne[i]; i++) action(QLatin1String(kNeedsOne[i]))->setEnabled(sel==1);
	for(int i=0; kNeedsSome[i]; i++) action(QLatin1String(kNeedsSome[i]))->setEnabled(sel>=1);

	for(QAction *a : findChildren<QAction*>()) {
		if(a->property("requiresDocument").toBool()) a->setEnabled(doc);
	}

	action(QStringLiteral("reopen"))->setEnabled(doc && m_png->named);
	action(QStringLiteral("combineSelected"))->setEnabled(sel>1);

	const QMimeData *clip = QApplication::clipboard()->mimeData();
	action(QStringLiteral("paste"))->setEnabled(doc && clip && clip->hasFormat(QLatin1String(kChunkMime)));

	m_viewerAction->setChecked(!m_viewer.isNull());
}

void MainWindow::error(const QString &text) { QMessageBox::warning(this, QStringLiteral("Error"), text); }
void MainWindow::warning(const QString &text) { QMessageBox::warning(this, QStringLiteral("Warning"), text); }
void MainWindow::notice(const QString &text) { QMessageBox::information(this, QStringLiteral("Notice"), text); }

// One dialog per problem would be unusable on a damaged file, which is
// exactly the kind of file this program gets opened on.
void MainWindow::showLoadWarnings(const QStringList &warnings)
{
	if(warnings.isEmpty()) return;

	const int kMaxShown = 12;
	QString text = QStringList(warnings.mid(0, kMaxShown)).join(QLatin1Char('\n'));
	if(warnings.size()>kMaxShown) {
		text += QStringLiteral("\n\n...and %1 more").arg(warnings.size()-kMaxShown);
	}
	warning(text);
}

void MainWindow::setTitle()
{
	if(!m_png) {
		setWindowTitle(QStringLiteral("TweakPNG"));
		return;
	}

	const QString star = m_png->dirty ? QStringLiteral("*") : QString();
	if(m_png->named) {
		const QFileInfo info(m_png->filename);
		const QString dir = QDir::toNativeSeparators(info.absolutePath()+QLatin1Char('/'));
		setWindowTitle(info.fileName() + star + QStringLiteral(" (") + dir + QStringLiteral(") - TweakPNG"));
	}
	else {
		setWindowTitle(m_png->filename + star + QStringLiteral(" - TweakPNG"));
	}
}

QString MainWindow::statusText() const
{
	return m_status->text();
}

void MainWindow::updateStatus()
{
	if(m_png) {
		const char *type = "???";
		switch(m_png->imageType()) {
		case IMG_PNG: type = "PNG"; break;
		case IMG_MNG: type = "MNG"; break;
		case IMG_JNG: type = "JNG"; break;
		}
		m_status->setText(QString::asprintf("%s file size: %u bytes", type, m_png->fileSize()));
	}
	else {
		m_status->setText(QStringLiteral("No file loaded"));
	}

	if(m_viewer) m_viewerTimer->start();
}

void MainWindow::syncViewerFileName()
{
	if(m_viewer) m_viewer->setCurrentFileName((m_png && m_png->named) ? m_png->filename : QString());
}

void MainWindow::documentChanged()
{
	m_model->setPng(m_png.get());
	setTitle();
	syncViewerFileName();
	updateStatus();
	updateActions();
}

void MainWindow::modified()
{
	if(!m_png->dirty) {
		m_png->dirty = true;
		setTitle();
	}
	updateStatus();
	updateActions();
}

void MainWindow::refreshViewer()
{
	if(m_viewer) m_viewer->updateImage(m_png.get());
}

bool MainWindow::okToClose()
{
	if(!m_png || !m_png->dirty) return true;

	raise();
	activateWindow();
	const QMessageBox::StandardButton x = QMessageBox::warning(this, QStringLiteral("TweakPNG"),
		QStringLiteral("Save changes to %1?").arg(m_png->filename),
		QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel, QMessageBox::Cancel);

	if(x==QMessageBox::Yes) return savePng();
	return x==QMessageBox::No;
}

void MainWindow::newDocument()
{
	m_png.reset(new Png);
	documentChanged();
}

bool MainWindow::openFile(const QString &fn)
{
	QString err;
	QStringList warnings;

	// The model must not hold the old document while it is replaced.
	m_model->setPng(nullptr);
	m_png = Png::fromFile(fn, fn, &err, &warnings);

	documentChanged();
	if(!m_png) {
		error(err);
		return false;
	}
	showLoadWarnings(warnings);
	return true;
}

void MainWindow::openFileIfAllowed(const QString &fn)
{
	if(okToClose()) openFile(fn);
}

void MainWindow::actionNew()
{
	if(okToClose()) newDocument();
}

void MainWindow::actionOpen()
{
	if(!okToClose()) return;

	const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Open image file"), m_lastOpenDir,
		QStringLiteral("PNG, MNG, JNG (*.png *.mng *.jng);;PNG (*.png);;MNG (*.mng);;JNG (*.jng);;All files (*)"));
	if(fn.isEmpty()) return;

	m_lastOpenDir = QFileInfo(fn).absolutePath();
	openFile(fn);
}

void MainWindow::actionReopen()
{
	if(!m_png || !m_png->named) return;

	if(m_png->dirty) {
		const auto x = QMessageBox::warning(this, QStringLiteral("TweakPNG"), QStringLiteral("Reload and lose changes?"),
			QMessageBox::Ok|QMessageBox::Cancel, QMessageBox::Ok);
		if(x!=QMessageBox::Ok) return;
	}
	openFile(QString(m_png->filename));
}

void MainWindow::actionClose()
{
	if(!m_png || !okToClose()) return;
	m_model->setPng(nullptr);
	m_png.reset();
	documentChanged();
}

void MainWindow::actionCheckValidity()
{
	if(!m_png) return;
	const ValidityResult r = m_png->checkValidity(true);
	if(r.ok) QMessageBox::information(this, QStringLiteral("Validity check"), r.message);
	else QMessageBox::warning(this, QStringLiteral("Validity check"), r.message);
}

void MainWindow::actionSignature()
{
	if(!m_png) return;

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("Set file type signature"));

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QButtonGroup group(&dialog);
	static const struct { const char *label; int type; } kTypes[3] = {
		{"PNG", IMG_PNG}, {"MNG", IMG_MNG}, {"JNG", IMG_JNG} };
	for(const auto &t : kTypes) {
		QRadioButton *b = new QRadioButton(QLatin1String(t.label));
		b->setChecked(m_png->imageType()==t.type);
		group.addButton(b, t.type);
		layout->addWidget(b);
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	fitDialog(&dialog);
	if(dialog.exec()==QDialog::Accepted && group.checkedId()!=m_png->imageType()) {
		m_png->setImageType(group.checkedId());
		m_model->refreshAllRows();
		modified();
	}
}

bool MainWindow::savePng()
{
	if(!m_png->named) return savePngAs();

	const ValidityResult r = m_png->checkValidity(false);
	if(!r.ok) {
		const auto x = QMessageBox::warning(this, QStringLiteral("Validity check"),
			QStringLiteral("A problem was detected with the current file:\n\n%1\n\nDo you want to save it anyway?").arg(r.message),
			QMessageBox::Ok|QMessageBox::Cancel);
		if(x!=QMessageBox::Ok) return false;
	}

	QString err;
	if(!m_png->writeFile(m_png->filename, &err)) {
		error(err);
		return false;
	}
	m_png->dirty = false;
	setTitle();
	return true;
}

bool MainWindow::savePngAs()
{
	const ValidityResult r = m_png->checkValidity(false);
	if(!r.ok) {
		const auto x = QMessageBox::warning(this, QStringLiteral("Validity check"),
			QStringLiteral("A problem was detected with the current file:\n\n%1\n\nDo you want to save it anyway?").arg(r.message),
			QMessageBox::Ok|QMessageBox::Cancel);
		if(x!=QMessageBox::Ok) return false;
	}

	QString filter = QStringLiteral("PNG (*.png)");
	QString ext = QStringLiteral("png");
	if(m_png->imageType()==IMG_MNG) {
		filter = QStringLiteral("MNG (*.mng)");
		ext = QStringLiteral("mng");
	}
	else if(m_png->imageType()==IMG_JNG) {
		filter = QStringLiteral("JNG (*.jng)");
		ext = QStringLiteral("jng");
	}

	QFileDialog dialog(this, QStringLiteral("Save Image As..."), m_png->named ? m_png->filename : m_png->filename + QLatin1Char('.') + ext, filter);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	dialog.setDefaultSuffix(ext);
	if(dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
	const QString fn = dialog.selectedFiles().constFirst();

	QString err;
	if(!m_png->writeFile(fn, &err)) {
		error(err);
		return false;
	}

	m_png->filename = fn;
	m_png->named = true;
	m_png->dirty = false;
	setTitle();
	syncViewerFileName();
	updateActions();
	return true;
}

void MainWindow::actionSave()
{
	if(m_png) savePng();
}

void MainWindow::actionSaveAs()
{
	if(m_png) savePngAs();
}

QVector<int> MainWindow::selectedRows() const
{
	QVector<int> rows;
	const QModelIndexList list = m_table->selectionModel()->selectedRows();
	for(const QModelIndex &i : list) rows << i.row();
	std::sort(rows.begin(), rows.end());
	return rows;
}

int MainWindow::focusRow() const
{
	const QModelIndex i = m_table->currentIndex();
	return i.isValid() ? i.row() : 0;
}

// Selects a range of rows, and sets the focus to the first of them.
void MainWindow::selectRows(int pos, int num)
{
	QItemSelectionModel *sm = m_table->selectionModel();
	sm->clearSelection();
	if(num<1 || pos<0 || pos>=m_model->rowCount()) return;

	const int last = qMin(pos+num, m_model->rowCount())-1;
	const QItemSelection selection(m_model->index(pos, 0), m_model->index(last, 4));
	sm->select(selection, QItemSelectionModel::Select|QItemSelectionModel::Rows);
	sm->setCurrentIndex(m_model->index(pos, 0), QItemSelectionModel::NoUpdate);
	m_table->scrollTo(m_model->index(pos, 0));
}

void MainWindow::actionSelectAll()
{
	if(m_png) selectRows(0, m_png->count());
}

void MainWindow::actionEditChunk()
{
	const QVector<int> rows = selectedRows();
	if(!m_png || rows.size()!=1) return;

	const int n = rows.first();
	const EditResult r = editChunk(m_png.get(), n, this);
	if(r==EDIT_ROW) m_model->refreshRow(n);
	else if(r==EDIT_ALL) m_model->refreshAllRows();
	if(r!=EDIT_NONE) modified();
}

void MainWindow::deleteChunks()
{
	if(!m_png) return;
	const QVector<int> rows = selectedRows();
	if(rows.isEmpty()) return;

	for(int i=rows.size()-1; i>=0; i--) {
		m_png->deleteChunk(rows.at(i));
	}
	m_model->refresh();

	const int pos = qBound(0, rows.first(), qMax(0, m_png->count()-1));
	selectRows(pos, 1);
	modified();
}

void MainWindow::moveChunksUp()
{
	if(!m_png) return;
	const QVector<int> rows = selectedRows();
	if(rows.isEmpty()) return;

	for(int i=0; i<m_png->count(); i++) {
		m_png->chunk(i)->flag = rows.contains(i);
	}
	if(m_png->chunk(0)->flag) return;  // can't move up if the first is selected

	for(int i=1; i<m_png->count(); i++) {
		if(m_png->chunk(i)->flag) m_png->moveChunk(i, -1);
	}
	m_model->refresh();

	QItemSelection selection;
	for(int i=0; i<m_png->count(); i++) {
		if(m_png->chunk(i)->flag) {
			selection.select(m_model->index(i, 0), m_model->index(i, 4));
		}
	}
	m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
	modified();
}

void MainWindow::moveChunksDown()
{
	if(!m_png) return;
	const QVector<int> rows = selectedRows();
	if(rows.isEmpty()) return;

	for(int i=0; i<m_png->count(); i++) {
		m_png->chunk(i)->flag = rows.contains(i);
	}
	if(m_png->chunk(m_png->count()-1)->flag) return;  // or down if the last is

	for(int i=m_png->count()-1; i>=0; i--) {
		if(m_png->chunk(i)->flag) m_png->moveChunk(i, 1);
	}
	m_model->refresh();

	QItemSelection selection;
	for(int i=0; i<m_png->count(); i++) {
		if(m_png->chunk(i)->flag) {
			selection.select(m_model->index(i, 0), m_model->index(i, 4));
		}
	}
	m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
	modified();
}

bool MainWindow::copyChunks()
{
	if(!m_png) return false;
	const QByteArray clip = m_png->chunksToClipboard(selectedRows());
	if(clip.isEmpty()) return false;

	QMimeData *mime = new QMimeData;
	mime->setData(QLatin1String(kChunkMime), clip);
	QApplication::clipboard()->setMimeData(mime);
	return true;
}

void MainWindow::actionCut()
{
	if(copyChunks()) deleteChunks();
}

void MainWindow::pasteChunks()
{
	if(!m_png) return;
	const QMimeData *mime = QApplication::clipboard()->mimeData();
	if(!mime || !mime->hasFormat(QLatin1String(kChunkMime))) return;

	const int pos = qMin(focusRow(), m_png->count());
	const int added = m_png->pasteClipboard(mime->data(QLatin1String(kChunkMime)), pos);
	if(added<1) return;

	m_model->refresh();
	selectRows(pos, added);
	modified();
}

void MainWindow::combineSelectedIdat()
{
	if(!m_png) return;
	int first = 0;
	const QString err = m_png->combineSelected(selectedRows(), &first);
	if(!err.isEmpty()) {
		error(err);
		return;
	}
	m_model->refresh();
	selectRows(first, 1);
	modified();
}

void MainWindow::combineAllIdat()
{
	if(!m_png) return;
	int ranges = 0;
	int first = 0;
	const QString err = m_png->combineAll(&ranges, &first);
	if(!err.isEmpty()) {
		error(err);
		return;
	}

	if(ranges>0) {
		m_model->refresh();
		modified();
	}

	// Select all IDAT and JDAT chunks.
	QItemSelection selection;
	for(int i=0; i<m_png->count(); i++) {
		const int id = m_png->chunk(i)->id;
		if(id==CHUNK_IDAT || id==CHUNK_JDAT) {
			selection.select(m_model->index(i, 0), m_model->index(i, 4));
		}
	}
	m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
}

void MainWindow::actionSplitIdat()
{
	if(!m_png) return;
	const QVector<int> rows = selectedRows();
	if(rows.size()!=1) {
		notice(QStringLiteral("Select a single IDAT chunk first"));
		return;
	}

	const int n = rows.first();
	Chunk *ch = m_png->chunk(n);
	if(ch->id!=CHUNK_IDAT && ch->id!=CHUNK_JDAT) {
		notice(QStringLiteral("Select an IDAT chunk first"));
		return;
	}

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("Split IDAT chunk"));

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QStringLiteral("Size in bytes of first section (0") + kEnDash
		+ QStringLiteral("%1)").arg(ch->length())));
	QLineEdit *size = new QLineEdit;
	layout->addWidget(size);
	QCheckBox *repeat = new QCheckBox(QStringLiteral("Repeat as many times as possible"));
	layout->addWidget(repeat);
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	fitDialog(&dialog, 44);
	while(dialog.exec()==QDialog::Accepted) {
		const qint64 value = size->text().trimmed().toLongLong();
		if(value<0 || value>(qint64)ch->length()) {
			error(QStringLiteral("The size must be between 0 and %1.").arg(ch->length()));
			continue;
		}
		if(value==0 && repeat->isChecked()) {
			error(QStringLiteral("Can") + kRsquo + QStringLiteral("t divide repeatedly into chunks of zero size."));
			continue;
		}

		const int pieces = m_png->splitCount(n, (int)value, repeat->isChecked());
		if(pieces>100) {
			const auto x = QMessageBox::question(this, QStringLiteral("Warning"),
				QStringLiteral("This will create %1 new chunks. Are you sure you want to continue?").arg(pieces),
				QMessageBox::Yes|QMessageBox::No, QMessageBox::No);
			if(x!=QMessageBox::Yes) return;
		}

		if(m_png->splitIdat(n, (int)value, repeat->isChecked())) {
			m_model->refresh();
			selectRows(n, pieces);
			modified();
		}
		return;
	}
}

void MainWindow::insertChunk(int id)
{
	if(!m_png) return;
	const NewChunkResult r = m_png->newChunk(id);
	if(!r.error.isEmpty()) {
		if(r.notice) warning(r.error);
		else error(r.error);
		return;
	}
	if(r.pos<0) return;

	m_model->refresh();
	selectRows(r.pos, 1);
	modified();
}

void MainWindow::importChunkFile(const QString &fn, int pos)
{
	QFile f(fn);
	if(!f.open(QIODevice::ReadOnly)) {
		error(QStringLiteral("Can") + kRsquo + QStringLiteral("t open file (%1)").arg(fn));
		return;
	}

	QString err;
	const int p = qMin(pos, m_png->count());
	if(!m_png->importChunkBytes(f.readAll(), p, &err)) {
		error(err);
		return;
	}
	m_model->refresh();
	selectRows(p, 1);
	modified();
}

void MainWindow::actionImportChunk()
{
	if(!m_png) return;
	const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Import chunk..."), QString(),
		QStringLiteral("Chunk files (*.chunk);;All files (*)"));
	if(!fn.isEmpty()) importChunkFile(fn, focusRow());
}

void MainWindow::actionExportChunk()
{
	const QVector<int> rows = selectedRows();
	if(!m_png || rows.size()!=1) {
		notice(QStringLiteral("You must select a single chunk first"));
		return;
	}

	QFileDialog dialog(this, QStringLiteral("Export chunk to file..."), QString(),
		QStringLiteral("Chunk files (*.chunk);;All files (*)"));
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	dialog.setDefaultSuffix(QStringLiteral("chunk"));
	if(dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;

	QFile f(dialog.selectedFiles().constFirst());
	if(!f.open(QIODevice::WriteOnly)) {
		error(QStringLiteral("Can") + kRsquo + QStringLiteral("t create file"));
		return;
	}
	f.write(m_png->exportChunk(rows.first()));
}

void MainWindow::importIccFile(const QString &fn)
{
	QFile f(fn);
	if(!f.open(QIODevice::ReadOnly)) {
		error(QStringLiteral("Can") + kRsquo + QStringLiteral("t open %1").arg(fn));
		return;
	}

	QString err;
	int pos = 0;
	if(!m_png->importIccProfile(f.readAll(), &pos, &err)) {
		error(err);
		return;
	}
	m_model->refresh();
	selectRows(pos, 1);
	modified();
}

void MainWindow::actionImportIcc()
{
	if(!m_png) return;
	const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Import ICC profile..."), QString(),
		QStringLiteral("ICC profiles (*.icc *.icm);;All files (*)"));
	if(!fn.isEmpty()) importIccFile(fn);
}

void MainWindow::handleDroppedFile(const QString &fn)
{
	if(QFileInfo(fn).isDir()) return;

	// The extension decides what a dropped file means.
	const bool isChunk = fn.endsWith(QStringLiteral(".chunk"), Qt::CaseInsensitive) && fn.size()>=7;
	const bool isIcc = fn.endsWith(QStringLiteral(".icc"), Qt::CaseInsensitive);

	if((isChunk || isIcc) && !m_png) {
		error(QStringLiteral("Open a PNG file first."));
	}
	else if(isChunk) {
		importChunkFile(fn, focusRow());
	}
	else if(isIcc) {
		importIccFile(fn);
	}
	else {
		openFileIfAllowed(fn);
	}
}

void MainWindow::handleDroppedUrls(const QList<QUrl> &urls)
{
	if(urls.size()==1 && urls.first().isLocalFile()) {
		handleDroppedFile(urls.first().toLocalFile());
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if(event->mimeData()->hasUrls() && event->mimeData()->urls().size()==1) {
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent *event)
{
	event->acceptProposedAction();
	handleDroppedUrls(event->mimeData()->urls());
}

void MainWindow::contextMenu(const QPoint &pos)
{
	if(!m_png) return;
	const QVector<int> rows = selectedRows();
	if(rows.isEmpty()) return;

	bool canEdit = false;
	if(rows.size()==1) canEdit = m_png->chunk(rows.first())->canEdit();

	QMenu menu(this);
	menu.addAction(QStringLiteral("&Edit Chunk..."), this, &MainWindow::actionEditChunk)->setEnabled(canEdit);
	menu.addAction(QStringLiteral("&Delete"), this, &MainWindow::deleteChunks);
	menu.addAction(QStringLiteral("Move &Up"), this, &MainWindow::moveChunksUp);
	menu.addAction(QStringLiteral("Mo&ve Down"), this, &MainWindow::moveChunksDown);
	menu.addSeparator();
	menu.addAction(QStringLiteral("Cu&t"), this, &MainWindow::actionCut);
	menu.addAction(QStringLiteral("&Copy"), this, [this]() { copyChunks(); });
	menu.addAction(QStringLiteral("&Paste"), this, &MainWindow::pasteChunks)->setEnabled(action(QStringLiteral("paste"))->isEnabled());
	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::runViewerTool(int n, const QString &ext)
{
	const ToolConfig &tool = appSettings().tools[(size_t)n];

	// The viewer may still be reading this after we return, so the folder
	// stays until the next viewer tool runs or the program exits.
	m_viewerTemp.reset(new QTemporaryDir);
	if(!m_viewerTemp->isValid()) {
		error(QStringLiteral("Can") + kRsquo + QStringLiteral("t create a temporary folder"));
		return;
	}
	const QString tmp = m_viewerTemp->filePath(QStringLiteral("temp.") + ext);

	QString err;
	if(!m_png->writeFile(tmp, &err)) {
		error(err);
		return;
	}

	if(tool.cmd.isEmpty() && classifyToolParams(tool.params)==TPARAMS_EMPTY) {
		// With no application given, use the default PNG opener.
		if(!QDesktopServices::openUrl(QUrl::fromLocalFile(tmp))) {
			error(QStringLiteral("Cannot execute tool"));
		}
		return;
	}

	QString params = tool.params;
	if(classifyToolParams(params)!=TPARAMS_VIEWER_REPLACE) {
		params += QStringLiteral(" \"%1\"");
	}

	QString program;
	QStringList args;
	if(!buildToolCommand(tool.cmd, params, tmp, QString(), &program, &args)
		|| !QProcess::startDetached(program, args, QCoreApplication::applicationDirPath()))
	{
		error(QStringLiteral("Cannot execute tool"));
	}
}

void MainWindow::runFilterTool(int n, const QString &ext)
{
	const ToolConfig &tool = appSettings().tools[(size_t)n];

	// A private folder, because a predictable name in a shared /tmp could be
	// replaced by another user between our write and the tool's read.
	QTemporaryDir dir;
	if(!dir.isValid()) {
		error(QStringLiteral("Can") + kRsquo + QStringLiteral("t create a temporary folder"));
		return;
	}
	const QString inName = dir.filePath(QStringLiteral("twptemp1.") + ext);
	const QString outName = dir.filePath(QStringLiteral("twptemp2.") + ext);

	QString err;
	if(!m_png->writeFile(inName, &err)) {
		error(err);
		return;
	}

	QString program;
	QStringList args;
	if(!buildToolCommand(tool.cmd, tool.params, inName, outName, &program, &args)) {
		error(QStringLiteral("Cannot execute filter tool"));
		return;
	}

	QProcess process;
	process.setWorkingDirectory(QCoreApplication::applicationDirPath());

	QProgressDialog progress(QStringLiteral("Running %1...").arg(tool.name), QStringLiteral("Cancel"), 0, 0, this);
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(500);

	QEventLoop loop;
	bool failedToStart = false;
	connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
	connect(&process, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError e) {
		if(e==QProcess::FailedToStart) {
			failedToStart = true;
			loop.quit();
		}
	});
	connect(&progress, &QProgressDialog::canceled, &process, &QProcess::kill);

	process.start(program, args);
	if(!failedToStart) loop.exec();
	const bool cancelled = progress.wasCanceled();
	progress.reset();

	if(failedToStart) {
		error(QStringLiteral("Cannot execute filter tool"));
		return;
	}
	if(cancelled) return;

	if(!QFile::exists(outName)) {
		error(QStringLiteral("Filter failed to produce an output file"));
		return;
	}

	QStringList warnings;
	std::unique_ptr<Png> result = Png::fromFile(outName, m_png->filename, &err, &warnings);
	if(!result) {
		error(QStringLiteral("Filter produced an invalid output file"));
		return;
	}

	result->named = m_png->named;
	m_model->setPng(nullptr);
	m_png = std::move(result);
	documentChanged();
	modified();
	showLoadWarnings(warnings);
}

void MainWindow::runTool(int n)
{
	if(!m_png || n<0 || n>=kNumTools) return;

	const ToolConfig &tool = appSettings().tools[(size_t)n];
	if(tool.name.isEmpty()) {
		error(QStringLiteral("Tool %1 not defined").arg(n+1));
		return;
	}

	QString ext = QStringLiteral("png");
	if(m_png->imageType()==IMG_MNG) ext = QStringLiteral("mng");
	else if(m_png->imageType()==IMG_JNG) ext = QStringLiteral("jng");

	if(classifyToolParams(tool.params)==TPARAMS_FILTER) runFilterTool(n, ext);
	else runViewerTool(n, ext);
}

void MainWindow::actionToggleViewer()
{
	AppSettings &settings = appSettings();

	if(m_viewer) {
		settings.autoOpenViewer = false;
		m_viewer->close();
		updateActions();
		return;
	}

	ViewerWindow *v = new ViewerWindow(nullptr);
	m_viewer = v;
	settings.autoOpenViewer = true;

	connect(v, &ViewerWindow::refreshRequested, this, &MainWindow::refreshViewer);
	connect(v, &ViewerWindow::filesDropped, this, &MainWindow::handleDroppedUrls);
	connect(v, &QObject::destroyed, this, [this]() {
		// QPointer clears later than this signal, in the QObject destructor.
		m_viewer.clear();
		if(!m_quitting) {
			appSettings().autoOpenViewer = false;
			updateActions();
		}
	});

	syncViewerFileName();
	v->show();
	v->calcStretchedSize();
	refreshViewer();
	activateWindow();
	updateActions();
}

void MainWindow::startupViewer()
{
	if(appSettings().autoOpenViewer) actionToggleViewer();
}

void MainWindow::actionSwitchWindow()
{
	if(!m_viewer) return;
	if(m_viewer->isActiveWindow()) activateWindow();
	else m_viewer->activateWindow();
}

void MainWindow::actionPreferences()
{
	AppSettings &settings = appSettings();

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("Preferences"));

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QStringLiteral("Compression level for new zTXt chunks")));
	QComboBox *level = new QComboBox;
	for(int i=0; kCompressionNames[i]; i++) level->addItem(QLatin1String(kCompressionNames[i]));
	level->setCurrentIndex(settings.compressionLevel==Z_DEFAULT_COMPRESSION ? 0 : settings.compressionLevel+1);
	layout->addWidget(level);

	QCheckBox *shellMenu = nullptr;
	const bool wasSet = isShellMenuSet();
	if(shellMenuSupported()) {
		shellMenu = new QCheckBox(QStringLiteral("Add TweakPNG to Explorer context menu"));
		shellMenu->setChecked(wasSet);
		layout->addWidget(shellMenu);
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	fitDialog(&dialog);
	if(dialog.exec()!=QDialog::Accepted) return;

	if(shellMenu && shellMenu->isChecked()!=wasSet) setShellMenu(shellMenu->isChecked());

	const int i = level->currentIndex();
	settings.compressionLevel = (i==0) ? Z_DEFAULT_COMPRESSION : i-1;
	setCompressionLevel(settings.compressionLevel);
	settings.save();
}

void MainWindow::actionConfigureTools()
{
	AppSettings &settings = appSettings();

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("Configure tools"));

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *help = new QLabel(QStringLiteral("Configure the tools to list on the Tools menu. The Name field is any name you choose. "
		"In the Parameters field, for a viewer, ") + kLdquo + QStringLiteral("%1") + kRdquo + QStringLiteral(" will be replaced by the PNG filename. "
		"For a filter, ") + kLdquo + QStringLiteral("%1") + kRdquo + QStringLiteral(" is the input file and ") + kLdquo + QStringLiteral("%2") + kRdquo
		+ QStringLiteral(" is the output file."));
	help->setWordWrap(true);
	layout->addWidget(help);

	QGridLayout *grid = new QGridLayout;
	grid->addWidget(new QLabel(QStringLiteral("Name")), 0, 1);
	grid->addWidget(new QLabel(QStringLiteral("Program")), 0, 2);
	grid->addWidget(new QLabel(QStringLiteral("Parameters")), 0, 4);

	std::array<QLineEdit*, kNumTools> names, cmds, params;
	for(int i=0; i<kNumTools; i++) {
		const ToolConfig &tool = settings.tools[(size_t)i];
		names[(size_t)i] = new QLineEdit(tool.name);
		cmds[(size_t)i] = new QLineEdit(tool.cmd);
		params[(size_t)i] = new QLineEdit(tool.params);
		names[(size_t)i]->setMaxLength(kMaxToolName);
		cmds[(size_t)i]->setMaxLength(kMaxToolCmd);
		params[(size_t)i]->setMaxLength(kMaxToolParams);

		QPushButton *browse = new QPushButton(QStringLiteral("..."));
		browse->setAutoDefault(false);
		browse->setMaximumWidth(textFit(browse, QStringLiteral("...")));
		QLineEdit *cmd = cmds[(size_t)i];
		connect(browse, &QPushButton::clicked, &dialog, [&dialog, cmd]() {
			const QString fn = QFileDialog::getOpenFileName(&dialog, QStringLiteral("Browse for tool"));
			if(!fn.isEmpty()) cmd->setText(fn);
		});

		grid->addWidget(new QLabel(QString::number(i+1)), i+1, 0);
		grid->addWidget(names[(size_t)i], i+1, 1);
		grid->addWidget(cmds[(size_t)i], i+1, 2);
		grid->addWidget(browse, i+1, 3);
		grid->addWidget(params[(size_t)i], i+1, 4);
	}
	layout->addLayout(grid);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	fitDialog(&dialog, 100);
	dialog.resize(dialog.minimumWidth(), qMax(dialog.sizeHint().height(), dialog.heightForWidth(dialog.minimumWidth())));
	if(dialog.exec()!=QDialog::Accepted) return;

	for(int i=0; i<kNumTools; i++) {
		settings.tools[(size_t)i].name = names[(size_t)i]->text();
		settings.tools[(size_t)i].cmd = cmds[(size_t)i]->text();
		settings.tools[(size_t)i].params = params[(size_t)i]->text();
	}
	settings.save();
	rebuildToolsMenu();
}

void MainWindow::actionAbout()
{
	const QString details = QStringLiteral("Version " TWEAKPNG_VERSION_STRING " ") + kMiddot + QLatin1Char(' ')
		+ QStringLiteral(__DATE__ " ") + kMiddot + QStringLiteral(" %1-bit").arg((int)(sizeof(void*)*8));

	QString text = QStringLiteral("TweakPNG ") + kMiddot + QStringLiteral(" A PNG image file manipulation utility\n\n");
	text += details + QStringLiteral("\nCopyright ") + QChar(0x00a9) + QStringLiteral(" 1999") + kEnDash
		+ QStringLiteral("2023 by Jason Summers\nWebsite: " TWEAKPNG_HOMEPAGE "\n");
	text += QStringLiteral("\nThis program is distributed under the terms of the GNU General Public License, "
		"version 3 or higher. Please read the file COPYING.txt for more information.\n");
	text += QStringLiteral("\nUses zlib, version %1.").arg(QLatin1String(zlibVersion()));
	text += QStringLiteral("\nUses libpng, version %1.").arg(libpngVersion());
	text += QStringLiteral("\nUses Qt, version %1.").arg(QLatin1String(qVersion()));

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("About TweakPNG"));

	QLabel *icon = new QLabel;
	icon->setPixmap(windowIcon().pixmap(48, 48));
	icon->setAlignment(Qt::AlignTop);

	QLabel *body = new QLabel(text);
	body->setWordWrap(true);
	body->setTextInteractionFlags(Qt::TextSelectableByMouse);
	body->setMinimumWidth(textColumns(body, 70));

	QHBoxLayout *row = new QHBoxLayout;
	row->addWidget(icon);
	row->addWidget(body, 1);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->addLayout(row);
	layout->addWidget(buttons);
	fitDialog(&dialog);
	dialog.exec();
}

void MainWindow::actionHelp()
{
	QFile f(QStringLiteral(":/tweakpng.txt"));
	if(!f.open(QIODevice::ReadOnly)) {
		error(QStringLiteral("Cannot display help file"));
		return;
	}

	QDialog *dialog = new QDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(QStringLiteral("TweakPNG Help"));
	dialog->resize(textSize(dialog, 95, 34));

	QPlainTextEdit *text = new QPlainTextEdit;
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	text->setPlainText(QString::fromLatin1(f.readAll()));

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

	QVBoxLayout *layout = new QVBoxLayout(dialog);
	layout->addWidget(text);
	layout->addWidget(buttons);
	dialog->show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if(!okToClose()) {
		event->ignore();
		return;
	}

	AppSettings &settings = appSettings();

	// Quitting leaves the setting alone, so an open viewer reopens next time.
	m_quitting = true;
	if(m_viewer) m_viewer->close();

	settings.mainGeometry = saveGeometry();
	settings.headerState = m_table->horizontalHeader()->saveState();
	settings.save();
	event->accept();
}
