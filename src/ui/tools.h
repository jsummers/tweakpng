// tools.h -- the user-configured external tools on the Tools menu.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#ifndef TWEAKPNG_UI_TOOLS_H
#define TWEAKPNG_UI_TOOLS_H

#include <QString>
#include <QStringList>

enum ToolParamsType {
	TPARAMS_EMPTY,             // params is blank
	TPARAMS_VIEWER_NOREPLACE,  // no %1 or %2: the filename is appended
	TPARAMS_VIEWER_REPLACE,    // contains %1
	TPARAMS_FILTER             // contains %2
};

ToolParamsType classifyToolParams(const QString &params);

// Splits a parameter string into arguments, honoring double quotes.
QStringList splitToolArgs(const QString &params);

// Replaces %1 and %2 in each argument.
// Because this runs after splitting, a filename containing spaces stays a
// single argument.
QStringList substituteToolArgs(const QStringList &args, const QString &p1, const QString &p2);

// Works out the program and arguments for a tool. With no program configured,
// the first word of the parameters is the program. Returns false if there is
// nothing to run.
bool buildToolCommand(const QString &cmd, const QString &params, const QString &p1,
	const QString &p2, QString *program, QStringList *args);

#endif // TWEAKPNG_UI_TOOLS_H
