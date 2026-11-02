// palettedialog.h -- the palette, transparency and background editor.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#ifndef TWEAKPNG_UI_PALETTEDIALOG_H
#define TWEAKPNG_UI_PALETTEDIALOG_H

#include "twpng.h"

#include <QDialog>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class PaletteDialog;

// The 16-by-16 grid of colors.
class PaletteGrid : public QWidget {
public:
	explicit PaletteGrid(PaletteDialog *dialog);

	int entryAt(int x, int y) const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	int itemWidth() const;
	int itemHeight() const;

	PaletteDialog *m_dialog;
};

class PaletteDialog : public QDialog {
public:
	PaletteDialog(const twpng::PaletteInfo &info, QWidget *parent);

	twpng::PaletteInfo info;
	PaletteGrid *grid;

	void entryHovered(int i, bool force = false);
	void entryClicked(int i);
	bool crossCursor() const;

protected:
	void done(int result) override;

private:
	void updateLabels();
	void setAllAlpha();

	QLabel *m_numPlteLabel;
	QLabel *m_numTrnsLabel;
	QLabel *m_i;
	QLabel *m_r;
	QLabel *m_g;
	QLabel *m_b;
	QLabel *m_a;
	QLineEdit *m_numPlte;
	QLineEdit *m_numTrns;
	QPushButton *m_setBkgd;
	QPushButton *m_editAlpha;
	int m_currentEntry = -1;
};

// Returns true if the user changed the palette and accepted.
bool runPaletteDialog(twpng::Png *png, twpng::Chunk *ch, QWidget *parent);

#endif // TWEAKPNG_UI_PALETTEDIALOG_H
