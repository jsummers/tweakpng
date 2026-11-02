// viewer.cpp -- the image viewer window.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2011 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "viewer.h"
#include "metrics.h"
#include "settings.h"

#include "twpng.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QUrl>

using namespace twpng;

namespace {

const int kBorder = 4;

QPoint eventPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return event->position().toPoint();
#else
	return event->pos();
#endif
}

} // namespace

ViewerCanvas::ViewerCanvas(ViewerWindow *viewer)
	: QWidget(viewer), m_viewer(viewer)
{
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void ViewerCanvas::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	const AppSettings &settings = appSettings();

	if(m_viewer->image().isNull()) {
		painter.fillRect(rect(), palette().base());
		if(!m_viewer->errorMessage().isEmpty()) {
			painter.setPen(QColor(128, 0, 0));
			painter.drawText(rect().adjusted(kBorder, kBorder, -kBorder, -kBorder),
				Qt::TextWordWrap|Qt::AlignLeft|Qt::AlignTop, m_viewer->errorMessage());
		}
		return;
	}

	m_viewer->clampPosition();

	QColor bg = settings.customBg;
	if(settings.windowBg==WBG_SYSDEFAULT) {
		bg = palette().color(QPalette::Base);
	}
	else if(settings.windowBg==WBG_SAMEASIMAGE && m_viewer->hasImageBg) {
		bg = m_viewer->imageBg;
	}
	painter.fillRect(rect(), bg);

	const QRect target(m_viewer->imagePos, m_viewer->stretchedSize());
	painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
	painter.drawImage(target, m_viewer->image());
}

void ViewerCanvas::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if(appSettings().zoom==VS_FIT) m_viewer->calcStretchedSize();
}

void ViewerCanvas::mousePressEvent(QMouseEvent *event)
{
	if(event->button()!=Qt::LeftButton || m_dragging || m_viewer->image().isNull()) return;
	setCursor(Qt::ClosedHandCursor);
	m_dragStart = m_viewer->imagePos - eventPos(event);
	m_dragging = true;
}

void ViewerCanvas::mouseMoveEvent(QMouseEvent *event)
{
	if(m_dragging) {
		m_viewer->imagePos = m_dragStart + eventPos(event);
		update();
	}
}

void ViewerCanvas::mouseReleaseEvent(QMouseEvent *)
{
	m_dragging = false;
	unsetCursor();
}

void ViewerCanvas::mouseDoubleClickEvent(QMouseEvent *)
{
	AppSettings &settings = appSettings();
	if(settings.zoom==VS_1 || settings.zoom==VS_FIT) {
		settings.zoom = (settings.zoom==VS_1) ? VS_FIT : VS_1;
		m_viewer->calcStretchedSize();
		m_viewer->refreshTitle();
		update();
	}
}

void ViewerCanvas::keyPressEvent(QKeyEvent *event)
{
	const int step = 30;
	switch(event->key()) {
	case Qt::Key_Left:  m_viewer->imagePos.rx() -= step; break;
	case Qt::Key_Right: m_viewer->imagePos.rx() += step; break;
	case Qt::Key_Up:    m_viewer->imagePos.ry() -= step; break;
	case Qt::Key_Down:  m_viewer->imagePos.ry() += step; break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	update();
}

ViewerWindow::ViewerWindow(QWidget *parent)
	: QMainWindow(parent, Qt::Window), imagePos(kBorder, kBorder)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowIcon(QIcon(QStringLiteral(":/viewer.png")));
	setAcceptDrops(true);

	m_canvas = new ViewerCanvas(this);
	setCentralWidget(m_canvas);
	buildMenus();

	// Wide enough that the menu bar doesn't collapse, and for the title.
	setMinimumWidth(qMax(menuBar()->sizeHint().width(), titleWidth(this)));
	resize(textSize(this, 80, 24));
	if(!appSettings().viewerGeometry.isEmpty()) restoreGeometry(appSettings().viewerGeometry);
	refreshTitle();
}

void ViewerWindow::buildMenus()
{
	AppSettings &settings = appSettings();

	QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
	QAction *closeViewer = file->addAction(QStringLiteral("&Close Viewer"));
	connect(closeViewer, &QAction::triggered, this, [this]() {
		emit viewerClosedByUser();
		close();
	});

	QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
	m_copy = edit->addAction(QStringLiteral("&Copy Image"));
	m_copy->setEnabled(false);
	connect(m_copy, &QAction::triggered, this, [this]() {
		if(!m_image.isNull()) QApplication::clipboard()->setImage(m_image);
	});

	QMenu *options = menuBar()->addMenu(QStringLiteral("&Options"));

	m_gamma = options->addAction(QStringLiteral("&Gamma Correction"));
	m_gamma->setCheckable(true);
	connect(m_gamma, &QAction::triggered, this, [this, &settings](bool on) {
		settings.useGamma = on;
		refreshTitle();
		emit refreshRequested();
	});

	options->addSeparator();

	QActionGroup *bgGroup = new QActionGroup(this);
	m_bgCustom = options->addAction(QStringLiteral("Use &Custom Background Color"));
	m_bgImage = options->addAction(QStringLiteral("Use &Image Or Custom Background"));
	m_bgStrip = options->addAction(QStringLiteral("Strip Alpha Channel"));
	for(QAction *a : {m_bgCustom, m_bgImage, m_bgStrip}) {
		a->setCheckable(true);
		bgGroup->addAction(a);
	}
	connect(m_bgCustom, &QAction::triggered, this, [this]() { setBackgroundMode(false, true); });
	connect(m_bgImage, &QAction::triggered, this, [this]() { setBackgroundMode(true, true); });
	connect(m_bgStrip, &QAction::triggered, this, [this]() { setBackgroundMode(false, false); });

	options->addSeparator();

	QAction *setBg = options->addAction(QStringLiteral("Set Custom &Background Color..."));
	connect(setBg, &QAction::triggered, this, [this, &settings]() {
		const QColor c = QColorDialog::getColor(settings.customBg, this);
		if(c.isValid()) {
			settings.customBg = c;
			emit refreshRequested();
		}
	});

	QMenu *windowBg = options->addMenu(QStringLiteral("Window Background Color"));
	m_windowBgGroup = new QActionGroup(this);
	const auto addWindowBg = [this, windowBg, &settings](const QString &label, int value) {
		QAction *a = windowBg->addAction(label);
		a->setCheckable(true);
		a->setData(value);
		m_windowBgGroup->addAction(a);
		connect(a, &QAction::triggered, this, [this, value, &settings]() {
			settings.windowBg = value;
			m_canvas->update();
		});
	};
	addWindowBg(QStringLiteral("Same As Image"), WBG_SAMEASIMAGE);
	addWindowBg(QStringLiteral("Custom Background Color"), WBG_CUSTOM);
	addWindowBg(QStringLiteral("System Default"), WBG_SYSDEFAULT);

	options->addSeparator();

	m_nonSquare = options->addAction(QStringLiteral("Correct for Nonsquare Pixels"));
	m_nonSquare->setCheckable(true);
	connect(m_nonSquare, &QAction::triggered, this, [this, &settings](bool on) {
		settings.correctNonSquare = on;
		emit refreshRequested();
	});

	QMenu *zoom = menuBar()->addMenu(QStringLiteral("&Zoom"));
	m_zoomGroup = new QActionGroup(this);
	const auto addZoom = [this, zoom](const QString &label, int value) {
		QAction *a = zoom->addAction(label);
		a->setCheckable(true);
		a->setData(value);
		m_zoomGroup->addAction(a);
		connect(a, &QAction::triggered, this, [this, value]() { setZoom(value); });
	};
	addZoom(QStringLiteral("&Fit"), VS_FIT);
	addZoom(QStringLiteral("12.5%"), VS_1_8);
	addZoom(QStringLiteral("25%"), VS_1_4);
	addZoom(QStringLiteral("50%"), VS_1_2);
	addZoom(QStringLiteral("&100%"), VS_1);
	addZoom(QStringLiteral("&200%"), VS_2);
	addZoom(QStringLiteral("&400%"), VS_4);

	connect(options, &QMenu::aboutToShow, this, &ViewerWindow::syncMenuChecks);
	connect(zoom, &QMenu::aboutToShow, this, &ViewerWindow::syncMenuChecks);
	syncMenuChecks();
}

void ViewerWindow::syncMenuChecks()
{
	const AppSettings &settings = appSettings();

	m_gamma->setChecked(settings.useGamma);
	m_nonSquare->setChecked(settings.correctNonSquare);
	m_bgCustom->setChecked(settings.useCustomBg && !settings.useImageBg);
	m_bgImage->setChecked(settings.useCustomBg && settings.useImageBg);
	m_bgStrip->setChecked(!settings.useCustomBg && !settings.useImageBg);

	for(QAction *a : m_windowBgGroup->actions()) {
		a->setChecked(a->data().toInt()==settings.windowBg);
	}
	for(QAction *a : m_zoomGroup->actions()) {
		a->setChecked(a->data().toInt()==settings.zoom);
	}
}

void ViewerWindow::setBackgroundMode(bool imageBgMode, bool customBg)
{
	appSettings().useImageBg = imageBgMode;
	appSettings().useCustomBg = customBg;
	refreshTitle();
	emit refreshRequested();
}

void ViewerWindow::setZoom(int zoom)
{
	appSettings().zoom = zoom;
	calcStretchedSize();
	m_canvas->update();
}

void ViewerWindow::setCurrentFileName(const QString &fn)
{
	m_baseName = fn.isEmpty() ? QString() : QFileInfo(fn).fileName();
	refreshTitle();
}

void ViewerWindow::refreshTitle()
{
	const AppSettings &settings = appSettings();

	QString title;
	if(!m_baseName.isEmpty()) title = m_baseName + QStringLiteral(" - ");
	title += QStringLiteral("TweakPNG Image Viewer");
	if(!settings.useGamma) title += QStringLiteral(" [gamma off]");
	if(!settings.useCustomBg && !settings.useImageBg) title += QStringLiteral(" [background off]");
	setWindowTitle(title);
}

void ViewerWindow::updateImage(const Png *png)
{
	const AppSettings &settings = appSettings();

	m_image = QImage();
	m_error.clear();
	hasImageBg = false;

	if(png && png->imageType()==IMG_MNG) {
		m_error = QStringLiteral("Viewer doesn") + QChar(0x2019) + QStringLiteral("t support MNG files.");
	}
	else if(png && png->imageType()==IMG_JNG) {
		m_error = QStringLiteral("Viewer doesn") + QChar(0x2019) + QStringLiteral("t support JNG files.");
	}
	else if(png && png->count()>0) {
		DecodeOptions opts;
		opts.colorCorrection = settings.useGamma;
		opts.useFileBackground = settings.useImageBg;
		opts.useCustomBackground = settings.useCustomBg;
		opts.bgRed = (uchar)settings.customBg.red();
		opts.bgGreen = (uchar)settings.customBg.green();
		opts.bgBlue = (uchar)settings.customBg.blue();

		QApplication::setOverrideCursor(Qt::WaitCursor);
		const DecodeResult r = decodePng(png->toBytes(), opts);
		QApplication::restoreOverrideCursor();

		if(r.ok) {
			m_image = r.image;
			m_adjusted = m_image.size();

			if(settings.correctNonSquare && r.densityValid) {
				const int dx = r.densityX;
				const int dy = r.densityY;
				if(dx!=dy && dx>0 && dy>0 && 10LL*dx>dy && 10LL*dy>dx) {
					if(dx>dy) {
						m_adjusted.setHeight((int)(0.5+(double)m_adjusted.height()*((double)dx/(double)dy)));
					}
					else {
						m_adjusted.setWidth((int)(0.5+(double)m_adjusted.width()*((double)dy/(double)dx)));
					}
				}
			}

			calcStretchedSize();

			if(r.hasBackground) {
				hasImageBg = true;
				imageBg = QColor(r.bgRed, r.bgGreen, r.bgBlue);
			}
		}
		else {
			m_error = r.error;
		}
	}

	m_copy->setEnabled(!m_image.isNull());
	m_canvas->update();
}

void ViewerWindow::calcStretchedSize()
{
	if(m_image.isNull()) return;

	const int aw = m_adjusted.width();
	const int ah = m_adjusted.height();
	int w = aw;
	int h = ah;

	switch(appSettings().zoom) {
	case VS_FIT:
		{
			const int xavail = qMax(1, m_canvas->width()-kBorder*2);
			const int yavail = qMax(1, m_canvas->height()-kBorder*2);
			const double ratio1 = (double)yavail/(double)xavail;
			const double ratio2 = (double)ah/(double)aw;
			if(ratio1>ratio2) {
				w = xavail;
				h = (int)(0.5+((double)ah)*((double)xavail)/(double)aw);
			}
			else {
				w = (int)(0.5+((double)aw)*((double)yavail)/(double)ah);
				h = yavail;
			}
		}
		break;
	case VS_1_8: w = (int)(0.5+((double)aw)/8.0); h = (int)(0.5+((double)ah)/8.0); break;
	case VS_1_4: w = (int)(0.5+((double)aw)/4.0); h = (int)(0.5+((double)ah)/4.0); break;
	case VS_1_2: w = (1+aw)/2; h = (1+ah)/2; break;
	case VS_2: w = aw*2; h = ah*2; break;
	case VS_4: w = aw*4; h = ah*4; break;
	case VS_8: w = aw*8; h = ah*8; break;
	}

	m_stretched = QSize(qMax(1, w), qMax(1, h));
}

// Keeps the image in place: within the window if it fits, and otherwise so
// that no space is left beside it.
void ViewerWindow::clampPosition()
{
	const auto clamp = [](int pos, int imageSize, int windowSize) {
		const int far = windowSize-imageSize-kBorder;
		if(imageSize+2*kBorder<=windowSize) {
			if(pos<kBorder) pos = kBorder;
			if(pos>far) pos = far;
		}
		else {
			if(pos<far) pos = far;
			if(pos>kBorder) pos = kBorder;
		}
		return pos;
	};

	imagePos.setX(clamp(imagePos.x(), m_stretched.width(), m_canvas->width()));
	imagePos.setY(clamp(imagePos.y(), m_stretched.height(), m_canvas->height()));
}

void ViewerWindow::closeEvent(QCloseEvent *event)
{
	appSettings().viewerGeometry = saveGeometry();
	QMainWindow::closeEvent(event);
}

void ViewerWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if(event->mimeData()->hasUrls() && !QApplication::activeModalWidget()) {
		event->acceptProposedAction();
	}
}

// Files dropped here are a convenience; a modal dialog may be in the middle
// of editing the file, so nothing happens while one is open.
void ViewerWindow::dropEvent(QDropEvent *event)
{
	if(!QApplication::activeModalWidget()) {
		emit filesDropped(event->mimeData()->urls());
		event->acceptProposedAction();
	}
}
