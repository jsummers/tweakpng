// chunkdialogs.h -- the editors for individual chunks.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#ifndef TWEAKPNG_UI_CHUNKDIALOGS_H
#define TWEAKPNG_UI_CHUNKDIALOGS_H

#include <QByteArray>
#include <QDialog>
#include <QSize>

#include <functional>

class QFormLayout;
class QLineEdit;
class QVBoxLayout;

namespace twpng { class Chunk; class Png; }

// A dialog for one chunk. Accepting it does not change the chunk; the caller
// runs commit() afterwards. This keeps "fill in the fields" and "write the
// bytes" apart, which is what lets the tests drive every editor.
class ChunkEditor : public QDialog {
public:
	explicit ChunkEditor(const QString &title, QWidget *parent);

	std::function<void()> commit;

	// For resizable dialogs: a starting size in text columns and lines, and
	// the saved geometry that replaces it. finish() applies them.
	QSize defaultSize;
	const QByteArray *geometrySetting = nullptr;

	QFormLayout *form;
	QVBoxLayout *column;

	void finish();  // adds the OK and Cancel buttons

	QLineEdit *addNumber(const QString &label, quint32 value);
	QLineEdit *addSigned(const QString &label, int value);
	QLineEdit *addDecimal(const QString &label, double value);
	QLineEdit *addText(const QString &label, const QString &value);
};

enum EditResult {
	EDIT_NONE = 0,      // nothing changed
	EDIT_ROW = 1,       // this chunk changed
	EDIT_ALL = 2        // other rows may have changed
};

// Builds the editor for a chunk, or returns nullptr if it has none of this
// kind (palette and color-picker editing are handled by editChunk).
ChunkEditor *createChunkEditor(twpng::Chunk *ch, QWidget *parent);

// True if the chunk is edited through the palette editor.
bool usesPaletteEditor(const twpng::Chunk *ch);

// Runs the right editor for a chunk.
EditResult editChunk(twpng::Png *png, int n, QWidget *parent);

#endif // TWEAKPNG_UI_CHUNKDIALOGS_H
