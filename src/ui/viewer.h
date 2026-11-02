// viewer.h -- the image viewer window.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2011 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#ifndef TWEAKPNG_UI_VIEWER_H
#define TWEAKPNG_UI_VIEWER_H

#include "pngimage.h"

#include <QImage>
#include <QMainWindow>
#include <QUrl>
#include <QWidget>

class QAction;
class QActionGroup;

namespace twpng { class Png; }

class ViewerWindow;

class ViewerCanvas : public QWidget {
public:
	explicit ViewerCanvas(ViewerWindow *viewer);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	ViewerWindow *m_viewer;
	bool m_dragging = false;
	QPoint m_dragStart;
};

class ViewerWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit ViewerWindow(QWidget *parent = nullptr);

	// Pass nullptr to clear the viewer.
	void updateImage(const twpng::Png *png);
	void setCurrentFileName(const QString &fn);

	const QImage &image() const { return m_image; }
	QString errorMessage() const { return m_error; }
	QSize stretchedSize() const { return m_stretched; }

	void calcStretchedSize();
	void clampPosition();
	void refreshTitle();

	QPoint imagePos;

	bool hasImageBg = false;
	QColor imageBg;

signals:
	void refreshRequested();
	void viewerClosedByUser();
	void filesDropped(const QList<QUrl> &urls);

protected:
	void closeEvent(QCloseEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void buildMenus();
	void syncMenuChecks();
	void setBackgroundMode(bool imageBg, bool customBg);
	void setZoom(int zoom);

	ViewerCanvas *m_canvas;
	QImage m_image;
	QString m_error;
	QString m_baseName;
	QSize m_adjusted;
	QSize m_stretched;

	QAction *m_copy;
	QAction *m_gamma;
	QAction *m_nonSquare;
	QAction *m_bgCustom;
	QAction *m_bgImage;
	QAction *m_bgStrip;
	QActionGroup *m_windowBgGroup;
	QActionGroup *m_zoomGroup;
};

#endif // TWEAKPNG_UI_VIEWER_H
