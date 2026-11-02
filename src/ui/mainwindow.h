// mainwindow.h -- the main window, and the chunk list.
//
// This file is part of TweakPNG, a low-level PNG file manipulation utility.
// Copyright (C) 1999-2023 Jason Summers. Licensed under the GNU GPL, version 3
// or later. See COPYING.txt.

#ifndef TWEAKPNG_UI_MAINWINDOW_H
#define TWEAKPNG_UI_MAINWINDOW_H

#include "twpng.h"
#include "viewer.h"

#include <QAbstractTableModel>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include <array>
#include <memory>

class QAction;
class QLabel;
class QMenu;
class QTableView;
class QTemporaryDir;
class QTimer;


class ChunkModel : public QAbstractTableModel {
public:
	explicit ChunkModel(QObject *parent = nullptr);

	void setPng(twpng::Png *png);
	void refresh();
	void refreshRow(int row);
	void refreshAllRows();

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	twpng::Png *m_png = nullptr;
};

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;

	bool openFile(const QString &fn);
	void openFileIfAllowed(const QString &fn);
	void newDocument();
	void startupViewer();

	twpng::Png *png() const { return m_png.get(); }
	QTableView *table() const { return m_table; }
	ChunkModel *model() const { return m_model; }
	ViewerWindow *viewer() const { return m_viewer; }
	QAction *action(const QString &name) const;
	QString statusText() const;

	// The pieces of the Edit menu, public for the tests.
	void deleteChunks();
	void moveChunksUp();
	void moveChunksDown();
	bool copyChunks();
	void pasteChunks();
	void combineSelectedIdat();
	void combineAllIdat();
	void selectRows(int pos, int num);
	QVector<int> selectedRows() const;

protected:
	void closeEvent(QCloseEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void buildMenus();
	QAction *addAction(QMenu *menu, const QString &name, const QString &text,
		const QKeySequence &key, void (MainWindow::*slot)());
	void rebuildToolsMenu();
	void updateActions();

	void error(const QString &text);
	void warning(const QString &text);
	void notice(const QString &text);
	void showLoadWarnings(const QStringList &warnings);

	void setTitle();
	void updateStatus();
	void documentChanged();
	void modified();
	void refreshViewer();
	void syncViewerFileName();

	bool okToClose();
	bool savePng();
	bool savePngAs();

	void actionNew();
	void actionOpen();
	void actionReopen();
	void actionClose();
	void actionCheckValidity();
	void actionSignature();
	void actionSave();
	void actionSaveAs();
	void actionEditChunk();
	void actionCut();
	void actionSplitIdat();
	void actionImportChunk();
	void actionExportChunk();
	void actionImportIcc();
	void actionPreferences();
	void actionConfigureTools();
	void actionAbout();
	void actionHelp();
	void actionToggleViewer();
	void actionSwitchWindow();
	void actionSelectAll();

	void insertChunk(int id);
	void importChunkFile(const QString &fn, int pos);
	void importIccFile(const QString &fn);
	void handleDroppedFile(const QString &fn);
	void handleDroppedUrls(const QList<QUrl> &urls);
	void contextMenu(const QPoint &pos);
	void runTool(int n);
	void runFilterTool(int n, const QString &ext);
	void runViewerTool(int n, const QString &ext);

	int focusRow() const;

	std::unique_ptr<twpng::Png> m_png;
	ChunkModel *m_model;
	QTableView *m_table;
	QLabel *m_status;
	QTimer *m_viewerTimer;
	QPointer<ViewerWindow> m_viewer;
	QMenu *m_toolsMenu;
	QAction *m_viewerAction;
	std::array<QAction*, 6> m_toolActions;
	std::unique_ptr<QTemporaryDir> m_viewerTemp;
	QString m_lastOpenDir;
	bool m_quitting = false;
};

#endif // TWEAKPNG_UI_MAINWINDOW_H
