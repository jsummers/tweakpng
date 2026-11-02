// metrics.h -- window sizes in terms of the font in use.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.
//
// Sizes here are in text columns and lines, never pixels, so that windows stay
// usable with large fonts or a high-density display.

#ifndef TWEAKPNG_UI_METRICS_H
#define TWEAKPNG_UI_METRICS_H

#include <QSize>

class QDialog;
class QString;
class QWidget;

int textColumns(const QWidget *w, int columns);
int textLines(const QWidget *w, int lines);

// The width of a string in the widget's font, with room for margins.
int textFit(const QWidget *w, const QString &text);

// A size of the given columns and lines, limited to what fits on the screen.
QSize textSize(const QWidget *w, int columns, int lines);

// The width a window needs to show its title in full.
int titleWidth(const QWidget *w);

// Sets the minimum width of a dialog whose layout is complete, so that its
// title isn't cut off. The height is left to the layout. Don't set a minimum
// size on a dialog in any other way: it would override the layout's minimum,
// and allow the contents to be squashed and clipped.
void fitDialog(QDialog *dialog, int minColumns = 0);

#endif // TWEAKPNG_UI_METRICS_H
