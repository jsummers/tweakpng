// tools.cpp -- the user-configured external tools on the Tools menu.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#include "tools.h"

ToolParamsType classifyToolParams(const QString &params)
{
	if(params.trimmed().isEmpty()) return TPARAMS_EMPTY;
	if(params.contains(QStringLiteral("%2"))) return TPARAMS_FILTER;
	if(params.contains(QStringLiteral("%1"))) return TPARAMS_VIEWER_REPLACE;
	return TPARAMS_VIEWER_NOREPLACE;
}

QStringList splitToolArgs(const QString &params)
{
	QStringList args;
	QString cur;
	bool inQuotes = false;
	bool haveArg = false;

	for(const QChar c : params) {
		if(c==QLatin1Char('"')) {
			inQuotes = !inQuotes;
			haveArg = true;
		}
		else if(c.isSpace() && !inQuotes) {
			if(haveArg) {
				args << cur;
				cur.clear();
				haveArg = false;
			}
		}
		else {
			cur += c;
			haveArg = true;
		}
	}
	if(haveArg) args << cur;
	return args;
}

QStringList substituteToolArgs(const QStringList &args, const QString &p1, const QString &p2)
{
	QStringList out;
	for(const QString &arg : args) {
		QString s;
		for(int i=0; i<arg.size(); i++) {
			if(arg.at(i)==QLatin1Char('%') && i+1<arg.size() && arg.at(i+1)==QLatin1Char('1')) {
				s += p1;
				i++;
			}
			else if(arg.at(i)==QLatin1Char('%') && i+1<arg.size() && arg.at(i+1)==QLatin1Char('2')) {
				s += p2;
				i++;
			}
			else {
				s += arg.at(i);
			}
		}
		out << s;
	}
	return out;
}

bool buildToolCommand(const QString &cmd, const QString &params, const QString &p1,
	const QString &p2, QString *program, QStringList *args)
{
	QStringList words = substituteToolArgs(splitToolArgs(params), p1, p2);

	if(!cmd.isEmpty()) {
		*program = cmd;
		*args = words;
		return true;
	}
	if(words.isEmpty()) return false;

	*program = words.takeFirst();
	*args = words;
	return true;
}
