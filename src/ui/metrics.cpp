// metrics.cpp -- window sizes in terms of the font in use.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "metrics.h"

#include <QDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QString>
#include <QWidget>

int textColumns(const QWidget *w, int columns)
{
	return columns*w->fontMetrics().averageCharWidth();
}

int textLines(const QWidget *w, int lines)
{
	return lines*w->fontMetrics().lineSpacing();
}

int textFit(const QWidget *w, const QString &text)
{
	return w->fontMetrics().horizontalAdvance(text) + w->fontMetrics().height();
}

QSize textSize(const QWidget *w, int columns, int lines)
{
	QSize size(textColumns(w, columns), textLines(w, lines));

	const QScreen *screen = w->screen();
	if(!screen) screen = QGuiApplication::primaryScreen();
	if(screen) size = size.boundedTo(screen->availableGeometry().size()*9/10);
	return size;
}

int titleWidth(const QWidget *w)
{
	// Window managers draw titles in their own font, which is often bold, and
	// put buttons beside them. This allows for both.
	const QFontMetrics fm = w->fontMetrics();
	return fm.horizontalAdvance(w->windowTitle())*5/4 + fm.height()*6;
}

void fitDialog(QDialog *dialog, int minColumns)
{
	dialog->ensurePolished();

	int width = qMax(titleWidth(dialog), textColumns(dialog, minColumns));
	if(dialog->layout()) {
		dialog->layout()->activate();
		width = qMax(width, dialog->layout()->minimumSize().width());
	}
	dialog->setMinimumWidth(width);
}
