// chunkdialogs.cpp -- the editors for individual chunks.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "chunkdialogs.h"
#include "metrics.h"
#include "palettedialog.h"
#include "settings.h"

#include "twpng.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <array>
#include <memory>

using namespace twpng;

namespace {

const QChar kEnDash(0x2013);

quint32 uintField(const QLineEdit *e)
{
	bool ok = false;
	const qulonglong v = e->text().trimmed().toULongLong(&ok);
	return (ok && v<=0xffffffffULL) ? (quint32)v : 0;
}

quint32 signedField(const QLineEdit *e)
{
	bool ok = false;
	const int v = e->text().trimmed().toInt(&ok);
	return ok ? (quint32)v : 0;
}

// Fixed-point values such as gamma: the value times 100000.
quint32 decimalField(const QLineEdit *e)
{
	const double v = e->text().trimmed().toDouble()*100000.0 + 0.000001;
	if(v<=0.0) return 0;
	if(v>=4294967295.0) return 0xffffffffU;
	return (quint32)v;
}

quint32 decimalFromText(const QString &text)
{
	QLineEdit tmp;
	tmp.setText(text);
	return decimalField(&tmp);
}

QString fixed5(double v)
{
	return QString::asprintf("%0.5f", v);
}

QRadioButton *addRadio(QVBoxLayout *layout, QButtonGroup *group, const QString &label, int id, bool checked)
{
	QRadioButton *b = new QRadioButton(label);
	b->setChecked(checked);
	group->addButton(b, id);
	layout->addWidget(b);
	return b;
}

ChunkEditor *ihdrEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("PNG Header"), parent);
	const uchar *d = ch->bytes();

	QLabel *warning = new QLabel(QStringLiteral("Warning: Modifying the fields below will probably make the image unreadable."));
	warning->setWordWrap(true);
	e->column->insertWidget(0, warning);

	QLineEdit *w = e->addNumber(QStringLiteral("Width"), readInt32(d));
	QLineEdit *h = e->addNumber(QStringLiteral("Height"), readInt32(d+4));
	QLineEdit *depth = e->addNumber(QStringLiteral("Bit depth"), d[8]);

	static const int kTypes[5] = {0, 2, 3, 4, 6};
	QComboBox *type = new QComboBox;
	type->setObjectName(QStringLiteral("colorType"));
	type->addItems({QStringLiteral("0 = grayscale"), QStringLiteral("2 = RGB"), QStringLiteral("3 = paletted"),
		QStringLiteral("4 = grayscale+alpha"), QStringLiteral("6 = RGB+alpha")});
	int typeIndex = 0;
	for(int i=0; i<5; i++) {
		if(kTypes[i]==d[9]) typeIndex = i;
	}
	type->setCurrentIndex(typeIndex);
	e->form->addRow(QStringLiteral("Color type"), type);

	QCheckBox *interlaced = new QCheckBox(QStringLiteral("Interlaced"));
	interlaced->setChecked(d[12]>0);
	e->form->addRow(QString(), interlaced);

	e->commit = [=]() {
		uchar *m = ch->mutableBytes();
		writeInt32(m, uintField(w));
		writeInt32(m+4, uintField(h));
		m[8] = (uchar)uintField(depth);
		m[12] = interlaced->isChecked() ? 1 : 0;
		m[9] = (uchar)kTypes[type->currentIndex()];
	};
	return e;
}

ChunkEditor *gammaEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("File gamma"), parent);

	QComboBox *gamma = new QComboBox;
	gamma->setObjectName(QStringLiteral("gamma"));
	gamma->setEditable(true);
	gamma->addItems({QStringLiteral("0.40000"), QStringLiteral("0.45000"), QStringLiteral("0.45455"),
		QStringLiteral("0.50000"), QStringLiteral("0.55000"), QStringLiteral("0.60000"),
		QStringLiteral("0.66667"), QStringLiteral("0.70000"), QStringLiteral("1.00000")});
	gamma->setEditText(fixed5(((double)readInt32(ch->bytes()))/100000.0));
	e->form->addRow(QStringLiteral("Gamma"), gamma);

	e->commit = [=]() {
		writeInt32(ch->mutableBytes(), decimalFromText(gamma->currentText()));
	};
	return e;
}

ChunkEditor *chrmEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Chromaticities"), parent);
	static const char *const kRows[4] = {"White point", "Red", "Green", "Blue"};

	QGridLayout *grid = new QGridLayout;
	grid->addWidget(new QLabel(QStringLiteral("X")), 0, 1);
	grid->addWidget(new QLabel(QStringLiteral("Y")), 0, 2);

	std::array<QLineEdit*, 8> fields;
	for(int r=0; r<4; r++) {
		grid->addWidget(new QLabel(QLatin1String(kRows[r])), r+1, 0);
		for(int c=0; c<2; c++) {
			QLineEdit *f = new QLineEdit(fixed5(((double)readInt32(ch->bytes()+(r*2+c)*4))/100000.0));
			f->setObjectName(QStringLiteral("value%1").arg(r*2+c));
			fields[(size_t)(r*2+c)] = f;
			grid->addWidget(f, r+1, c+1);
		}
	}
	e->column->insertLayout(0, grid);

	e->commit = [=]() {
		for(int i=0; i<8; i++) {
			writeInt32(ch->mutableBytes()+i*4, decimalField(fields[(size_t)i]));
		}
	};
	return e;
}

ChunkEditor *radioEditor(Chunk *ch, const QString &title, const QStringList &labels, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(title, parent);
	QButtonGroup *group = new QButtonGroup(e);
	QVBoxLayout *box = new QVBoxLayout;
	for(int i=0; i<labels.size(); i++) {
		addRadio(box, group, labels.at(i), i, ch->bytes()[0]==i);
	}
	e->column->insertLayout(0, box);

	e->commit = [=]() {
		if(group->checkedId()>=0) ch->mutableBytes()[0] = (uchar)group->checkedId();
	};
	return e;
}

ChunkEditor *physEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Physical pixel size"), parent);
	const uchar *d = ch->bytes();

	QLineEdit *x = e->addNumber(QStringLiteral("X"), readInt32(d));
	QLineEdit *y = e->addNumber(QStringLiteral("Y"), readInt32(d+4));

	QButtonGroup *group = new QButtonGroup(e);
	QVBoxLayout *units = new QVBoxLayout;
	QRadioButton *unspecified = addRadio(units, group, QStringLiteral("unspecified units"), 0, d[8]==0);
	QRadioButton *meter = addRadio(units, group, QStringLiteral("meter"), 1, d[8]==1);
	e->form->addRow(QStringLiteral("Pixels per"), units);

	QLabel *inches = new QLabel;
	inches->setObjectName(QStringLiteral("inches"));
	e->column->insertWidget(1, inches);

	const auto update = [=]() {
		if(!meter->isChecked()) {
			inches->clear();
			return;
		}
		inches->setText(QString::asprintf("= %.2f ", ((double)uintField(x))*0.0254) + QChar(0x00d7)
			+ QString::asprintf(" %.2f pixels/inch", ((double)uintField(y))*0.0254));
	};
	QObject::connect(x, &QLineEdit::textChanged, e, update);
	QObject::connect(y, &QLineEdit::textChanged, e, update);
	QObject::connect(meter, &QRadioButton::toggled, e, update);
	update();

	e->commit = [=]() {
		if(unspecified->isChecked()) ch->mutableBytes()[8] = 0;
		if(meter->isChecked()) ch->mutableBytes()[8] = 1;
		writeInt32(ch->mutableBytes(), uintField(x));
		writeInt32(ch->mutableBytes()+4, uintField(y));
	};
	return e;
}

// DLG_NUMBER1, DLG_COLOR2, DLG_COLOR3, DLG_COLOR4: a label and some numbers.
struct NumberFields {
	QLineEdit *f[4] = {nullptr, nullptr, nullptr, nullptr};
};

NumberFields addChannels(ChunkEditor *e, const QString &label, const QStringList &names, const QVector<quint32> &values)
{
	QLabel *l = new QLabel(label);
	l->setObjectName(QStringLiteral("label"));
	e->column->insertWidget(0, l);

	NumberFields n;
	for(int i=0; i<names.size(); i++) {
		n.f[i] = e->addNumber(names.at(i), values.at(i));
	}
	return n;
}

const QStringList &channelNames(int count)
{
	static const QStringList names1 = {QStringLiteral("Value")};
	static const QStringList names2 = {QStringLiteral("Gray"), QStringLiteral("Alpha")};
	static const QStringList names3 = {QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")};
	static const QStringList names4 = {QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue"), QStringLiteral("Alpha")};
	switch(count) {
	case 1: return names1;
	case 2: return names2;
	case 3: return names3;
	}
	return names4;
}

ChunkEditor *bkgdEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Background color"), parent);
	const int max = (1<<qMin((int)ch->parent->bitDepth, 16))-1;
	const uchar *d = ch->bytes();

	switch(ch->parent->colorType) {
	case 0: case 4:
		{
			const NumberFields n = addChannels(e, QStringLiteral("Enter background gray shade (0") + kEnDash + QStringLiteral("%1)").arg(max),
				channelNames(1), {(quint32)readInt16(d)});
			e->commit = [=]() { writeInt16(ch->mutableBytes(), (int)uintField(n.f[0])); };
		}
		break;
	default:
		{
			const NumberFields n = addChannels(e, QStringLiteral("Enter background RGB values (0") + kEnDash + QStringLiteral("%1)").arg(max),
				channelNames(3), {(quint32)readInt16(d), (quint32)readInt16(d+2), (quint32)readInt16(d+4)});
			e->commit = [=]() {
				for(int i=0; i<3; i++) writeInt16(ch->mutableBytes()+i*2, (int)uintField(n.f[i]));
			};
		}
	}
	return e;
}

ChunkEditor *sbitEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Significant bits"), parent);
	const int max = (ch->parent->colorType==3) ? 8 : (int)ch->parent->bitDepth;
	const int count = (int)ch->length();

	QVector<quint32> values;
	for(int i=0; i<count; i++) values << ch->bytes()[i];

	const NumberFields n = addChannels(e, QStringLiteral("Enter significant bits (1") + kEnDash + QStringLiteral("%1)").arg(max),
		channelNames(count), values);

	e->commit = [=]() {
		for(int i=0; i<count; i++) ch->mutableBytes()[i] = (uchar)uintField(n.f[i]);
	};
	return e;
}

ChunkEditor *trnsEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Transparent color"), parent);
	const int max = (1<<qMin((int)ch->parent->bitDepth, 16))-1;
	const uchar *d = ch->bytes();

	if(ch->parent->colorType==0) {
		const NumberFields n = addChannels(e, QStringLiteral("Enter transparent gray shade (0") + kEnDash + QStringLiteral("%1)").arg(max),
			channelNames(1), {(quint32)readInt16(d)});
		e->commit = [=]() { writeInt16(ch->mutableBytes(), (int)uintField(n.f[0])); };
	}
	else {
		const NumberFields n = addChannels(e, QStringLiteral("Enter transparent RGB color (0") + kEnDash + QStringLiteral("%1)").arg(max),
			channelNames(3), {(quint32)readInt16(d), (quint32)readInt16(d+2), (quint32)readInt16(d+4)});
		e->commit = [=]() {
			for(int i=0; i<3; i++) writeInt16(ch->mutableBytes()+i*2, (int)uintField(n.f[i]));
		};
	}
	return e;
}

ChunkEditor *timeEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Last modification time"), parent);
	const uchar *d = ch->bytes();

	e->column->insertWidget(0, new QLabel(QStringLiteral("Enter time in universal time (UTC or GMT)")));

	QLineEdit *year = e->addNumber(QStringLiteral("Year"), (quint32)readInt16(d));
	QComboBox *month = new QComboBox;
	month->setObjectName(QStringLiteral("month"));
	for(int i=1; i<=12; i++) month->addItem(QLatin1String(kMonthNames[i]));
	month->setCurrentIndex((d[2]>=1 && d[2]<=12) ? d[2]-1 : -1);
	e->form->addRow(QStringLiteral("Month"), month);
	QLineEdit *day = e->addNumber(QStringLiteral("Day"), d[3]);

	const auto twoDigit = [e](const QString &label, int v) {
		QLineEdit *f = e->addNumber(label, 0);
		f->setText(QString::asprintf("%02d", v));
		return f;
	};
	QLineEdit *hour = twoDigit(QStringLiteral("Hour"), d[4]);
	QLineEdit *minute = twoDigit(QStringLiteral("Minute"), d[5]);
	QLineEdit *second = twoDigit(QStringLiteral("Second"), d[6]);

	QPushButton *now = new QPushButton(QStringLiteral("Set to current time"));
	now->setObjectName(QStringLiteral("setCurrentTime"));
	now->setAutoDefault(false);
	e->form->addRow(QString(), now);
	QObject::connect(now, &QPushButton::clicked, e, [=]() {
		const QDateTime t = QDateTime::currentDateTimeUtc();
		year->setText(QString::number(t.date().year()));
		month->setCurrentIndex(t.date().month()-1);
		day->setText(QString::number(t.date().day()));
		hour->setText(QString::asprintf("%02d", t.time().hour()));
		minute->setText(QString::asprintf("%02d", t.time().minute()));
		second->setText(QString::asprintf("%02d", t.time().second()));
	});

	e->commit = [=]() {
		uchar *m = ch->mutableBytes();
		writeInt16(m, (int)uintField(year));
		m[2] = (uchar)(month->currentIndex()+1);
		m[3] = (uchar)uintField(day);
		m[4] = (uchar)uintField(hour);
		m[5] = (uchar)uintField(minute);
		m[6] = (uchar)uintField(second);
	};
	return e;
}

ChunkEditor *actlEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("APNG animation control"), parent);
	const quint32 plays = readInt32(ch->bytes()+4);

	QLineEdit *frames = e->addNumber(QStringLiteral("Number of frames"), readInt32(ch->bytes()));
	QLineEdit *numPlays = e->addNumber(QStringLiteral("Number of plays"), plays);
	QCheckBox *playForever = new QCheckBox(QStringLiteral("Play indefinitely"));
	playForever->setObjectName(QStringLiteral("forever"));
	playForever->setChecked(plays==0);
	numPlays->setEnabled(plays!=0);
	e->form->addRow(QString(), playForever);
	QObject::connect(playForever, &QCheckBox::toggled, numPlays, &QLineEdit::setDisabled);

	e->commit = [=]() {
		writeInt32(ch->mutableBytes(), uintField(frames));
		writeInt32(ch->mutableBytes()+4, playForever->isChecked() ? 0 : uintField(numPlays));
	};
	return e;
}

QComboBox *addCombo(ChunkEditor *e, const QString &label, const QStringList &items, int value)
{
	QComboBox *c = new QComboBox;
	c->addItems(items);
	c->setCurrentIndex((value>=0 && value<items.size()) ? value : 0);
	e->form->addRow(label, c);
	return c;
}

ChunkEditor *fctlEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("APNG frame control"), parent);
	const uchar *d = ch->bytes();

	QLineEdit *seq = e->addNumber(QStringLiteral("Sequence number"), readInt32(d));
	QLineEdit *w = e->addNumber(QStringLiteral("Width"), readInt32(d+4));
	QLineEdit *h = e->addNumber(QStringLiteral("Height"), readInt32(d+8));
	QLineEdit *x = e->addNumber(QStringLiteral("X offset"), readInt32(d+12));
	QLineEdit *y = e->addNumber(QStringLiteral("Y offset"), readInt32(d+16));
	QLineEdit *num = e->addNumber(QStringLiteral("Delay numerator"), (quint32)readInt16(d+20));
	QLineEdit *den = e->addNumber(QStringLiteral("Delay denominator (seconds)"), (quint32)readInt16(d+22));
	QComboBox *dispose = addCombo(e, QStringLiteral("Dispose"),
		{QStringLiteral("0 = None"), QStringLiteral("1 = Background"), QStringLiteral("2 = Previous")}, d[24]);
	QComboBox *blend = addCombo(e, QStringLiteral("Blend"),
		{QStringLiteral("0 = Source"), QStringLiteral("1 = Over")}, d[25]);

	e->commit = [=]() {
		uchar *m = ch->mutableBytes();
		writeInt32(m, uintField(seq));
		writeInt32(m+4, uintField(w));
		writeInt32(m+8, uintField(h));
		writeInt32(m+12, uintField(x));
		writeInt32(m+16, uintField(y));
		writeInt16(m+20, (int)uintField(num));
		writeInt16(m+22, (int)uintField(den));
		m[24] = (uchar)dispose->currentIndex();
		m[25] = (uchar)blend->currentIndex();
	};
	return e;
}

ChunkEditor *fdatEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("APNG frame data"), parent);
	QLineEdit *seq = e->addNumber(QStringLiteral("Sequence number"), readInt32(ch->bytes()));
	e->commit = [=]() { writeInt32(ch->mutableBytes(), uintField(seq)); };
	return e;
}

ChunkEditor *offsEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Image offset"), parent);
	const uchar *d = ch->bytes();

	QLineEdit *x = e->addSigned(QStringLiteral("X"), (int)readInt32(d));
	QLineEdit *y = e->addSigned(QStringLiteral("Y"), (int)readInt32(d+4));

	QButtonGroup *group = new QButtonGroup(e);
	QVBoxLayout *units = new QVBoxLayout;
	QRadioButton *pixels = addRadio(units, group, QStringLiteral("pixels"), 0, d[8]==0);
	QRadioButton *micro = addRadio(units, group, QStringLiteral("micrometers"), 1, d[8]==1);
	e->form->addRow(QStringLiteral("Units"), units);

	e->commit = [=]() {
		if(pixels->isChecked()) ch->mutableBytes()[8] = 0;
		if(micro->isChecked()) ch->mutableBytes()[8] = 1;
		writeInt32(ch->mutableBytes(), signedField(x));
		writeInt32(ch->mutableBytes()+4, signedField(y));
	};
	return e;
}

ChunkEditor *vpagEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Virtual page"), parent);
	const uchar *d = ch->bytes();

	QLineEdit *w = e->addSigned(QStringLiteral("Width"), (int)readInt32(d));
	QLineEdit *h = e->addSigned(QStringLiteral("Height"), (int)readInt32(d+4));
	QLineEdit *units = e->addNumber(QStringLiteral("Units"), d[8]);

	e->commit = [=]() {
		writeInt32(ch->mutableBytes(), signedField(w));
		writeInt32(ch->mutableBytes()+4, signedField(h));
		ch->mutableBytes()[8] = (uchar)uintField(units);
	};
	return e;
}

ChunkEditor *scalEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Physical scale of image subject"), parent);
	const SCalData d = ch->sCalData();

	QLineEdit *x = e->addText(QStringLiteral("X"), d.x);
	QLineEdit *y = e->addText(QStringLiteral("Y"), d.y);

	QButtonGroup *group = new QButtonGroup(e);
	QVBoxLayout *units = new QVBoxLayout;
	QRadioButton *meter = addRadio(units, group, QStringLiteral("meter"), 1, d.units==1);
	QRadioButton *radian = addRadio(units, group, QStringLiteral("radian"), 2, d.units==2);
	e->form->addRow(QStringLiteral("Pixels per"), units);

	e->commit = [=]() {
		SCalData out;
		out.units = meter->isChecked() ? 1 : (radian->isChecked() ? 2 : 0);
		out.x = x->text();
		out.y = y->text();
		ch->setSCalData(out);
	};
	return e;
}

ChunkEditor *iccEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("ICC Profile"), parent);
	e->setSizeGripEnabled(true);

	KeywordInfo kw;
	ch->keywordInfo(&kw);

	QLineEdit *name = e->addText(QStringLiteral("Name"), kw.keyword);
	name->setMaxLength(79);

	QPlainTextEdit *contents = new QPlainTextEdit;
	contents->setObjectName(QStringLiteral("contents"));
	contents->setReadOnly(true);
	contents->setLineWrapMode(QPlainTextEdit::NoWrap);
	contents->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	contents->setPlainText(dumpIccChunk(*ch));

	e->column->addWidget(new QLabel(QStringLiteral("Contents")));
	e->column->addWidget(contents, 1);

	e->commit = [=]() { ch->setIccProfileName(name->text()); };
	e->defaultSize = QSize(100, 36);
	return e;
}

ChunkEditor *textEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = new ChunkEditor(QStringLiteral("Text"), parent);
	e->setSizeGripEnabled(true);

	TextInfo ti;
	const bool ok = ch->textInfo(&ti);

	QComboBox *keyword = new QComboBox;
	keyword->setObjectName(QStringLiteral("keyword"));
	keyword->setEditable(true);
	keyword->setInsertPolicy(QComboBox::NoInsert);
	QStringList keys;
	for(int i=0; kKnownTextKeys[i]; i++) keys << QLatin1String(kKnownTextKeys[i]);
	keys.sort();
	keyword->addItems(keys);
	keyword->lineEdit()->setMaxLength(79);
	keyword->setEditText(ok ? ti.keyword : QString());
	e->form->addRow(QStringLiteral("Keyword"), keyword);

	QCheckBox *compressed = new QCheckBox(QStringLiteral("Compressed"));
	compressed->setObjectName(QStringLiteral("compressed"));
	compressed->setChecked(ti.isCompressed);
	QCheckBox *international = new QCheckBox(QStringLiteral("International text"));
	international->setObjectName(QStringLiteral("international"));
	international->setChecked(ch->id==CHUNK_iTXt);
	QHBoxLayout *flags = new QHBoxLayout;
	flags->addWidget(compressed);
	flags->addWidget(international);
	flags->addStretch(1);
	e->form->addRow(QString(), flags);

	QLabel *langLabel = new QLabel(QStringLiteral("Language tag"));
	QLineEdit *language = new QLineEdit(ti.language);
	language->setObjectName(QStringLiteral("language"));
	language->setMaxLength(79);
	QLabel *tkwLabel = new QLabel(QStringLiteral("Translated keyword"));
	QLineEdit *translated = new QLineEdit(ti.translatedKeyword);
	translated->setObjectName(QStringLiteral("translatedKeyword"));
	translated->setMaxLength(79);
	e->form->addRow(langLabel, language);
	e->form->addRow(tkwLabel, translated);

	const auto showInternational = [=](bool on) {
		langLabel->setVisible(on);
		language->setVisible(on);
		tkwLabel->setVisible(on);
		translated->setVisible(on);
	};
	showInternational(international->isChecked());
	QObject::connect(international, &QCheckBox::toggled, e, showInternational);

	QPlainTextEdit *text = new QPlainTextEdit(ti.text);
	text->setObjectName(QStringLiteral("text"));
	text->setTabChangesFocus(true);
	e->column->addWidget(text, 1);

	e->defaultSize = QSize(90, 28);
	e->geometrySetting = &appSettings().textGeometry;
	QObject::connect(e, &QDialog::finished, e, [e]() { appSettings().textGeometry = e->saveGeometry(); });

	e->commit = [=]() {
		const bool intl = international->isChecked();
		ch->setTextInfo(keyword->currentText(), intl ? language->text() : QString(),
			intl ? translated->text() : QString(), text->toPlainText(), compressed->isChecked(), intl);
	};
	return e;
}

} // namespace

ChunkEditor::ChunkEditor(const QString &title, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(title);
	column = new QVBoxLayout(this);
	form = new QFormLayout;
	column->addLayout(form);
}

void ChunkEditor::finish()
{
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	column->addWidget(buttons);

	fitDialog(this, 44);
	if(defaultSize.isValid()) resize(textSize(this, defaultSize.width(), defaultSize.height()));
	if(geometrySetting && !geometrySetting->isEmpty()) restoreGeometry(*geometrySetting);
}

QLineEdit *ChunkEditor::addNumber(const QString &label, quint32 value)
{
	QLineEdit *e = new QLineEdit(QString::number(value));
	e->setObjectName(label);
	e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,10}")), e));
	form->addRow(label, e);
	return e;
}

QLineEdit *ChunkEditor::addSigned(const QString &label, int value)
{
	QLineEdit *e = new QLineEdit(QString::number(value));
	e->setObjectName(label);
	e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("-?[0-9]{0,10}")), e));
	form->addRow(label, e);
	return e;
}

QLineEdit *ChunkEditor::addDecimal(const QString &label, double value)
{
	QLineEdit *e = new QLineEdit(fixed5(value));
	e->setObjectName(label);
	form->addRow(label, e);
	return e;
}

QLineEdit *ChunkEditor::addText(const QString &label, const QString &value)
{
	QLineEdit *e = new QLineEdit(value);
	e->setObjectName(label);
	form->addRow(label, e);
	return e;
}

bool usesPaletteEditor(const Chunk *ch)
{
	const Png *png = ch->parent;
	switch(ch->id) {
	case CHUNK_PLTE:
		return true;
	case CHUNK_bKGD:
		return png->colorType==3 || ((png->colorType==0 || png->colorType==4) && png->bitDepth<=8);
	case CHUNK_tRNS:
		return png->colorType==3 || (png->colorType==0 && png->bitDepth<=8);
	}
	return false;
}

ChunkEditor *createChunkEditor(Chunk *ch, QWidget *parent)
{
	ChunkEditor *e = nullptr;

	switch(ch->id) {
	case CHUNK_tEXt: case CHUNK_zTXt: case CHUNK_iTXt: e = textEditor(ch, parent); break;
	case CHUNK_IHDR: e = ihdrEditor(ch, parent); break;
	case CHUNK_gAMA: e = gammaEditor(ch, parent); break;
	case CHUNK_cHRM: e = chrmEditor(ch, parent); break;
	case CHUNK_sRGB:
		e = radioEditor(ch, QStringLiteral("sRGB"), {QStringLiteral("Perceptual"), QStringLiteral("Relative Colorimetric"),
			QStringLiteral("Saturation"), QStringLiteral("Absolute Colorimetric")}, parent);
		break;
	case CHUNK_sTER:
		e = radioEditor(ch, QStringLiteral("sTER"), {QStringLiteral("Cross-fuse layout"), QStringLiteral("Diverging-fuse layout")}, parent);
		break;
	case CHUNK_pHYs: e = physEditor(ch, parent); break;
	case CHUNK_bKGD: if(ch->length()==2 || ch->length()==6) e = bkgdEditor(ch, parent); break;
	case CHUNK_sBIT: if(ch->length()>=1 && ch->length()<=4) e = sbitEditor(ch, parent); break;
	case CHUNK_tRNS: if(ch->length()==2 || ch->length()==6) e = trnsEditor(ch, parent); break;
	case CHUNK_tIME: e = timeEditor(ch, parent); break;
	case CHUNK_acTL: e = actlEditor(ch, parent); break;
	case CHUNK_fcTL: e = fctlEditor(ch, parent); break;
	case CHUNK_fdAT: e = fdatEditor(ch, parent); break;
	case CHUNK_oFFs: e = offsEditor(ch, parent); break;
	case CHUNK_sCAL: e = scalEditor(ch, parent); break;
	case CHUNK_iCCP: e = iccEditor(ch, parent); break;
	case CHUNK_vpAg: e = vpagEditor(ch, parent); break;
	}

	if(e) e->finish();
	return e;
}

EditResult editChunk(Png *png, int n, QWidget *parent)
{
	if(n<0 || n>=png->count()) return EDIT_NONE;
	Chunk *ch = png->chunk(n);

	// The editors for most chunks can't handle invalid chunks.
	if(!ch->hasValidLength()) {
		QMessageBox::warning(parent, QStringLiteral("TweakPNG"),
			QStringLiteral("This chunk can't be edited, because its length is incorrect."));
		return EDIT_NONE;
	}

	if(usesPaletteEditor(ch)) {
		return runPaletteDialog(png, ch, parent) ? EDIT_ALL : EDIT_NONE;
	}

	// An 8-bit RGB background goes straight to the color picker.
	if(ch->id==CHUNK_bKGD && ch->length()==6 && png->bitDepth==8) {
		const uchar *d = ch->bytes();
		const QColor c = QColorDialog::getColor(QColor(d[1], d[3], d[5]), parent);
		if(!c.isValid()) return EDIT_NONE;
		uchar *m = ch->mutableBytes();
		m[0] = 0; m[1] = (uchar)c.red();
		m[2] = 0; m[3] = (uchar)c.green();
		m[4] = 0; m[5] = (uchar)c.blue();
		ch->chunkModified();
		return EDIT_ROW;
	}

	std::unique_ptr<ChunkEditor> e(createChunkEditor(ch, parent));
	if(!e || e->exec()!=QDialog::Accepted) return EDIT_NONE;

	e->commit();
	ch->invalidateTextInfo();

	if(ch->id==CHUNK_IHDR) {
		ch->afterInit();
		ch->chunkModified();
		return EDIT_ALL;  // other chunks are described using the header
	}
	ch->chunkModified();
	return EDIT_ROW;
}
