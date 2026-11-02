// palettedialog.cpp -- the palette, transparency and background editor.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "palettedialog.h"
#include "metrics.h"
#include "settings.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

using namespace twpng;

namespace {

const int kMargin = 3;
const QChar kEnDash(0x2013);

QPoint eventPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return event->position().toPoint();
#else
	return event->pos();
#endif
}

} // namespace

PaletteGrid::PaletteGrid(PaletteDialog *dialog)
	: QWidget(dialog), m_dialog(dialog)
{
	setObjectName(QStringLiteral("paletteGrid"));
	setMouseTracking(true);
	setMinimumSize(16*fontMetrics().height()+kMargin*2, 8*fontMetrics().height()+kMargin*2);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

int PaletteGrid::itemWidth() const
{
	return (width()-kMargin*2)/16;
}

int PaletteGrid::itemHeight() const
{
	if(m_dialog->info.maxPlte>16) return (height()-kMargin*2)/16;
	return height()-kMargin*2;
}

// Returns a palette entry, or -1 if the position is not over one.
int PaletteGrid::entryAt(int x, int y) const
{
	const int w = itemWidth();
	const int h = itemHeight();
	if(w<1 || h<1 || x<kMargin || y<kMargin) return -1;

	const int col = (x-kMargin)/w;
	const int row = (y-kMargin)/h;
	if(col>15 || row>15) return -1;

	const int v = row*16+col;
	return (v<m_dialog->info.numPlte) ? v : -1;
}

void PaletteGrid::paintEvent(QPaintEvent *)
{
	const PaletteInfo &p = m_dialog->info;
	const int w = itemWidth();
	const int h = itemHeight();

	QPainter painter(this);
	painter.fillRect(rect(), Qt::white);
	painter.setPen(Qt::gray);
	painter.drawRect(rect().adjusted(0, 0, -1, -1));

	// Indicate the background palette entry.
	if(p.numBkgd>0) {
		painter.fillRect((p.bkgd%16)*w+kMargin, (p.bkgd/16)*h+kMargin, w+1, h+1, QColor(0, 0, 255));
	}

	painter.setPen(Qt::black);
	for(int i=0; i<p.numPlte; i++) {
		const int xp = (i%16)*w+kMargin;
		const int yp = (i/16)*h+kMargin;
		painter.setBrush(QColor(p.plte[i].red, p.plte[i].green, p.plte[i].blue));
		painter.drawRect(xp+1, yp+2, w-3, h-5);
	}

	// A marker for each alpha value: its position shows the value.
	for(int i=0; i<p.numTrns; i++) {
		const int index = p.canEditColors ? i : p.trnsColor;
		const uchar alpha = p.canEditColors ? p.plte[i].alpha : 0;

		const int xp = (index%16)*w+kMargin;
		const int yp = (index/16)*h+kMargin;
		const int pos = (int)((double)(w-2.1)*((double)alpha/255.0));

		if(alpha==0) painter.setPen(QColor(255, 0, 0));
		else if(alpha==255) painter.setPen(QColor(0, 128, 128));
		else painter.setPen(QColor(128, 0, 0));

		painter.drawLine(xp+1+pos, yp+h-7, xp+1+pos, yp+h-1);
		painter.drawLine(xp+pos, yp+h-6, xp+pos, yp+h-2);
		painter.drawLine(xp+2+pos, yp+h-6, xp+2+pos, yp+h-2);
	}
}

void PaletteGrid::mouseMoveEvent(QMouseEvent *event)
{
	const QPoint pt = eventPos(event);
	m_dialog->entryHovered(entryAt(pt.x(), pt.y()));
	setCursor(m_dialog->crossCursor() ? Qt::CrossCursor : Qt::ArrowCursor);
}

void PaletteGrid::leaveEvent(QEvent *)
{
	m_dialog->entryHovered(-1);
}

void PaletteGrid::mousePressEvent(QMouseEvent *event)
{
	if(event->button()!=Qt::LeftButton) return;
	const QPoint pt = eventPos(event);
	const int c = entryAt(pt.x(), pt.y());
	if(c>=0) m_dialog->entryClicked(c);
}

PaletteDialog::PaletteDialog(const PaletteInfo &infoIn, QWidget *parent)
	: QDialog(parent), info(infoIn)
{
	setWindowTitle(QStringLiteral("Palette"));
	setSizeGripEnabled(true);

	m_numPlteLabel = new QLabel;
	m_numTrnsLabel = new QLabel;
	m_numPlte = new QLineEdit(QString::number(info.numPlte));
	m_numPlte->setObjectName(QStringLiteral("numPlte"));
	m_numTrns = new QLineEdit(QString::number(info.numTrns));
	m_numTrns->setObjectName(QStringLiteral("numTrns"));
	m_numPlte->setMaximumWidth(textFit(this, QStringLiteral("99999")));
	m_numTrns->setMaximumWidth(textFit(this, QStringLiteral("99999")));
	m_numPlte->setValidator(new QIntValidator(0, 999, m_numPlte));
	m_numTrns->setValidator(new QIntValidator(0, 999, m_numTrns));

	m_setBkgd = new QPushButton(QStringLiteral("Set bkgd"));
	m_setBkgd->setObjectName(QStringLiteral("setBkgd"));
	m_setBkgd->setCheckable(true);
	m_setBkgd->setAutoDefault(false);
	m_setBkgd->setChecked(info.bkgdButton);

	m_editAlpha = new QPushButton(info.canEditColors ? QStringLiteral("Edit alpha") : QStringLiteral("Set trns"));
	m_editAlpha->setObjectName(QStringLiteral("editAlpha"));
	m_editAlpha->setCheckable(true);
	m_editAlpha->setAutoDefault(false);
	m_editAlpha->setChecked(info.alphaButton);

	QPushButton *allAlpha = new QPushButton(QStringLiteral("*"));
	allAlpha->setObjectName(QStringLiteral("allAlpha"));
	allAlpha->setToolTip(QStringLiteral("Set alpha for all palette colors"));
	allAlpha->setAutoDefault(false);
	allAlpha->setMaximumWidth(textFit(allAlpha, QStringLiteral("**")));

	m_numPlte->setEnabled(info.canEditColors);
	m_numTrns->setEnabled(info.minTrns && info.canEditColors);
	m_setBkgd->setEnabled(info.numBkgd!=0);
	m_editAlpha->setEnabled(info.minTrns!=0);
	allAlpha->setEnabled(info.minTrns && info.canEditColors);

	QGridLayout *top = new QGridLayout;
	top->addWidget(m_numPlteLabel, 0, 0, Qt::AlignRight);
	top->addWidget(m_numPlte, 0, 1);
	top->addWidget(m_setBkgd, 0, 2, 1, 2);
	top->addWidget(m_numTrnsLabel, 1, 0, Qt::AlignRight);
	top->addWidget(m_numTrns, 1, 1);
	top->addWidget(m_editAlpha, 1, 2);
	top->addWidget(allAlpha, 1, 3);
	top->setColumnStretch(4, 1);

	m_i = new QLabel;
	m_r = new QLabel;
	m_g = new QLabel;
	m_b = new QLabel;
	m_a = new QLabel;
	QVBoxLayout *values = new QVBoxLayout;
	for(QLabel *l : {m_i, m_r, m_g, m_b, m_a}) {
		// Fixed, so that the grid stays still as the values come and go.
		l->setFixedWidth(textFit(l, QStringLiteral("A: (255)")));
		values->addWidget(l);
	}
	values->addStretch(1);

	grid = new PaletteGrid(this);

	QHBoxLayout *middle = new QHBoxLayout;
	middle->addLayout(values);
	middle->addWidget(grid, 1);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(top);
	layout->addLayout(middle, 1);
	layout->addWidget(buttons);

	connect(m_numPlte, &QLineEdit::textChanged, this, [this](const QString &text) {
		info.numPlte = qBound(info.minPlte, text.toInt(), info.maxPlte);
		updateLabels();
		grid->update();
	});
	connect(m_numTrns, &QLineEdit::textChanged, this, [this](const QString &text) {
		info.numTrns = qBound(info.minTrns, text.toInt(), info.maxPlte);
		updateLabels();
		grid->update();
	});

	// If colors can't be edited, the two buttons act like radio buttons.
	connect(m_setBkgd, &QPushButton::clicked, this, [this]() {
		if(!info.canEditColors) {
			m_setBkgd->setChecked(true);
			m_editAlpha->setChecked(false);
		}
	});
	connect(m_editAlpha, &QPushButton::clicked, this, [this]() {
		if(!info.canEditColors) {
			m_setBkgd->setChecked(false);
			m_editAlpha->setChecked(true);
		}
	});
	connect(allAlpha, &QPushButton::clicked, this, [this]() { setAllAlpha(); });

	updateLabels();
	entryHovered(-1, true);

	fitDialog(this);
	resize(textSize(this, 90, 30));
	if(!appSettings().paletteGeometry.isEmpty()) restoreGeometry(appSettings().paletteGeometry);
}

bool PaletteDialog::crossCursor() const
{
	return m_setBkgd->isChecked() || (m_editAlpha->isChecked() && !info.canEditColors);
}

void PaletteDialog::updateLabels()
{
	if(info.canEditColors) {
		m_numPlteLabel->setText(QStringLiteral("Colors in palette (%1").arg(info.minPlte) + kEnDash
			+ QStringLiteral("%1):").arg(info.maxPlte));
		m_numTrnsLabel->setText(QStringLiteral("Alpha values (%1").arg(info.minTrns) + kEnDash
			+ QStringLiteral("%1):").arg(info.minTrns ? info.numPlte : 0));
	}
	else {
		m_numPlteLabel->setText(QStringLiteral("Gray shades:"));
		m_numTrnsLabel->setText(QStringLiteral("Transparent colors:"));
	}
}

void PaletteDialog::entryHovered(int i, bool force)
{
	if(i==m_currentEntry && !force) return;
	m_currentEntry = i;

	if(i<0) {
		m_i->setText(QStringLiteral("I:"));
		m_r->setText(QStringLiteral("R:"));
		m_g->setText(QStringLiteral("G:"));
		m_b->setText(QStringLiteral("B:"));
		m_a->setText(QStringLiteral("A:"));
		return;
	}

	int alpha;
	bool implied;
	if(info.canEditColors) {
		alpha = info.plte[i].alpha;
		implied = (i>=info.numTrns);
	}
	else if(info.numTrns && i==info.trnsColor) {
		alpha = 0;
		implied = false;
	}
	else {
		alpha = 255;
		implied = true;
	}

	m_i->setText(QStringLiteral("I: %1").arg(i));
	m_r->setText(QStringLiteral("R: %1").arg(info.plte[i].red));
	m_g->setText(QStringLiteral("G: %1").arg(info.plte[i].green));
	m_b->setText(QStringLiteral("B: %1").arg(info.plte[i].blue));
	m_a->setText(implied ? QStringLiteral("A: (255)") : QStringLiteral("A: %1").arg(alpha));
}

void PaletteDialog::entryClicked(int c)
{
	if(m_setBkgd->isChecked()) {
		info.bkgd = (uchar)c;
		if(info.canEditColors) m_setBkgd->setChecked(false);
		grid->update();
	}
	else if(m_editAlpha->isChecked()) {
		if(info.canEditColors) {
			if(c>=info.numTrns) return;
			bool ok = false;
			const int v = QInputDialog::getInt(this, QStringLiteral("Alpha for palette index %1").arg(c),
				QStringLiteral("Enter alpha (0=transparent, 255=opaque)"), info.plte[c].alpha, 0, 255, 1, &ok);
			if(ok) {
				info.plte[c].alpha = (uchar)v;
				entryHovered(-1);
				grid->update();
			}
		}
		else {
			info.trnsColor = (uchar)c;
			entryHovered(c, true);
			grid->update();
		}
	}
	else if(info.canEditColors) {
		const PaletteEntry &e = info.plte[c];
		const QColor color = QColorDialog::getColor(QColor(e.red, e.green, e.blue), this);
		if(color.isValid()) {
			info.plte[c].red = (uchar)color.red();
			info.plte[c].green = (uchar)color.green();
			info.plte[c].blue = (uchar)color.blue();
			entryHovered(c, true);
			grid->update();
		}
	}
}

void PaletteDialog::setAllAlpha()
{
	bool ok = false;
	const int v = QInputDialog::getInt(this, QStringLiteral("Set alpha for all palette colors"),
		QStringLiteral("Set all alpha (0=transparent, 255=opaque)"), 255, 0, 255, 1, &ok);
	if(!ok) return;

	for(int i=0; i<info.maxPlte; i++) {
		info.plte[i].alpha = (uchar)v;
	}
	info.numTrns = (v==255) ? 1 : info.numPlte;
	m_numTrns->setText(QString::number(info.numTrns));
	grid->update();
}

void PaletteDialog::done(int result)
{
	appSettings().paletteGeometry = saveGeometry();
	QDialog::done(result);
}

bool runPaletteDialog(Png *png, Chunk *ch, QWidget *parent)
{
	PaletteInfo info;
	QString error;
	if(!png->loadPaletteInfo(ch, &info, &error)) {
		QMessageBox::warning(parent, QStringLiteral("Error"), error);
		return false;
	}

	PaletteDialog dialog(info, parent);
	if(dialog.exec()!=QDialog::Accepted) return false;

	png->applyPaletteInfo(dialog.info);
	return true;
}
