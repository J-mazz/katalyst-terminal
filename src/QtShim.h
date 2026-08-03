// QtShim.h — Qt/KDE/POSIX compatibility shim for C++23 modules.
//
// Include this header FIRST in every .cpp file, BEFORE `import std;`.
// Qt headers transitively pull in STL headers; by including them before
// the module import we avoid redefinition clashes with `import std;`.
#pragma once

// --- C++ standard library (included before import std; to avoid redefinition) ---
#include <deque>
#include <memory>

// --- Qt Core ---
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QLatin1Char>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QString>
#include <QStringDecoder>
#include <QStringList>
#include <QStandardPaths>
#include <QVector>
#include <QVersionNumber>
#include <QtGlobal>

// --- Qt GUI ---
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QInputMethodEvent>
#include <QExposeEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSize>
#include <QWheelEvent>
#include <QWindow>

// --- Qt Widgets ---
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

// --- KDE Frameworks ---
#include <KConfig>
#include <KConfigGroup>

// --- POSIX (C headers, not part of import std;) ---
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <pty.h>
#else
#include <util.h>
#endif

class QSplitter;
class QSocketNotifier;

class TerminalBuffer;
class TerminalConfig;
class TerminalSession;
class TerminalViewBase;
class TerminalViewCommon;
class TerminalView;
class TerminalTab;
struct VtParserCore;

class TerminalDBus : public QObject {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.katalyst.Terminal")

public:
	explicit TerminalDBus(QObject *parent = nullptr);

public slots:
	void NewWindow();
	void OpenTab();

signals:
	void newWindowRequested();
	void newTabRequested();
};

class TerminalBuffer {
public:
	struct Match {
		int line = -1;
		int column = -1;
	};

	struct Cell {
		QChar ch = QLatin1Char(' ');
		QColor fg = QColor(220, 220, 220);
		QColor bg = QColor(20, 22, 26);
		bool bold = false;
		bool italic = false;
		bool underline = false;
		bool strikethrough = false;
	};

	TerminalBuffer();

	void resize(int columns, int rows);
	void clear();
	void clearToEnd();
	void clearLine();
	void clearLineToEnd();
	void clearLineFromStart();

	void setScrollbackLimit(int lines);
	void setDefaultColors(const QColor &foreground, const QColor &background);

	void setForeground(const QColor &foreground);
	void setBackground(const QColor &background);
	void setBold(bool bold);
	void setItalic(bool italic);
	void setUnderline(bool underline);
	void setStrikethrough(bool strikethrough);
	void setInverse(bool inverse);
	void resetAttributes();

	QColor defaultForeground() const;
	QColor defaultBackground() const;

	void putChar(QChar ch);
	void newline();
	void carriageReturn();
	void backspace();
	void tab();

	void setCursorPosition(int row, int column);
	void cursorUp(int n);
	void cursorDown(int n);
	void cursorForward(int n);
	void cursorBack(int n);
	void cursorToColumn(int col);
	void setScrollRegion(int top, int bottom);
	void resetScrollRegion();
	void scrollUp();
	void scrollDown();
	void enterAlternateScreen();
	void exitAlternateScreen();
	void setCursorVisible(bool visible);
	bool cursorVisible() const;
	bool bracketedPasteMode() const;
	void setBracketedPasteMode(bool enabled);
	int cursorRow() const;
	int cursorColumn() const;

	void clearFromTop();
	void insertChars(int n);
	void deleteChars(int n);
	void eraseChars(int n);
	void insertLines(int n);
	void deleteLines(int n);
	void reverseIndex();
	void saveCursor();
	void restoreCursor();

	int rows() const;
	int columns() const;
	int totalLines() const;

	QString lineAt(int index) const;
	Cell cellAt(int index, int column) const;
	Cell cellAtVisible(int row, int column, int scrollOffset) const;
	bool findNext(const QString &term, int startLine, int startColumn,
								bool forward, Match *match) const;

	QStringList snapshot(int scrollOffset) const;

private:
	bool findForward(const QString &term, int startLine, int startColumn, int total, Match *match) const;
	bool findBackward(const QString &term, int startLine, int startColumn, int total, Match *match) const;
	QVector<Cell> blankRow(const QColor &fg, const QColor &bg) const;
	void ensureScreenSize();
	void clampCursor();
	void pushScrollback(const QVector<Cell> &line);
	QString lineToString(const QVector<Cell> &line) const;
	void scrollRegionUp(int top, int bottom);
	void scrollRegionDown(int top, int bottom);
	QVector<QVector<Cell>> &activeScreen();
	const QVector<QVector<Cell>> &activeScreen() const;
	int &activeScreenStart();
	int activeScreenStart() const;

	QVector<Cell>& screenRow(int row) {
		auto &screen = activeScreen();
		return screen[(activeScreenStart() + row) % m_rows];
	}
	const QVector<Cell>& screenRow(int row) const {
		const auto &screen = activeScreen();
		return screen[(activeScreenStart() + row) % m_rows];
	}

	int m_columns = 80;
	int m_rows = 24;
	int m_cursorRow = 0;
	int m_cursorColumn = 0;
	int m_savedCursorRow = 0;
	int m_savedCursorColumn = 0;
	int m_altSavedCursorRow = 0;
	int m_altSavedCursorColumn = 0;
	int m_scrollbackLimit = 2000;
	int m_normalScreenStart = 0;
	int m_alternateScreenStart = 0;
	int m_scrollTop = 0;
	int m_scrollBottom = 23;
	bool m_useAlternateScreen = false;
	bool m_cursorVisible = true;
	bool m_bracketedPaste = false;

	QColor m_savedFg = QColor(220, 220, 220);
	QColor m_savedBg = QColor(20, 22, 26);
	bool m_savedBold = false;
	bool m_savedItalic = false;
	bool m_savedUnderline = false;
	bool m_savedStrikethrough = false;
	bool m_savedInverse = false;

	QColor m_defaultFg = QColor(220, 220, 220);
	QColor m_defaultBg = QColor(20, 22, 26);
	QColor m_currentFg = m_defaultFg;
	QColor m_currentBg = m_defaultBg;
	bool m_currentBold = false;
	bool m_currentItalic = false;
	bool m_currentUnderline = false;
	bool m_currentStrikethrough = false;
	bool m_currentInverse = false;
	bool m_pendingWrap = false;

	std::deque<QVector<Cell>> m_scrollback;
	QVector<QVector<Cell>> m_normalScreen;
	QVector<QVector<Cell>> m_alternateScreen;
};

class TerminalConfig {
public:
	struct TerminalProfile {
		QString name;
		QString program;
		QStringList arguments;
		QStringList env;
		QString term;
		QFont font;
		QColor background;
		QColor foreground;
		QColor selection;
		QColor searchHighlight;
		QColor cursor;
		int scrollbackLines = 2000;
	};

	TerminalConfig();

	TerminalProfile defaultProfile() const;
	TerminalProfile profile(const QString &name) const;
	QList<TerminalProfile> profiles() const;
	QString defaultProfileName() const;
	void saveProfiles(const QList<TerminalProfile> &profiles,
							 const QString &defaultProfileName);

	QFont font() const;
	QColor backgroundColor() const;
	QColor foregroundColor() const;
	int scrollbackLines() const;

private:
	QList<TerminalProfile> m_profiles;
	QString m_defaultProfileName;
	TerminalProfile m_defaultProfile;
};

class PtyProcess : public QObject {
	Q_OBJECT

public:
	explicit PtyProcess(QObject *parent = nullptr);
	~PtyProcess() override;

	bool start(const QString &program, const QStringList &args,
						 const QStringList &env);
	void stop();

	void send(const QByteArray &data);
	void setWindowSize(int columns, int rows);

signals:
	void dataReady(const QByteArray &data);
	void exited();

private slots:
	void handleReadyRead();

private:
	void closeMaster();

	int m_masterFd = -1;
	int m_childPid = -1;
	QSocketNotifier *m_notifier = nullptr;
};

class VtParser : public QObject {
	Q_OBJECT

public:
	explicit VtParser(TerminalBuffer *buffer, QObject *parent = nullptr);
	~VtParser() override;

	void reset();
	void feed(const QByteArray &data);

signals:
	void titleChanged(const QString &title);

private:
	TerminalBuffer *m_buffer = nullptr;
	VtParserCore *m_core = nullptr;
};

class TerminalSession : public QObject {
	Q_OBJECT

public:
	TerminalSession(const TerminalConfig::TerminalProfile &profile,
									QObject *parent = nullptr);

	void startShell();
	void sendInput(const QByteArray &data);
	void resize(int columns, int rows);

	TerminalBuffer *buffer() const;
	const TerminalConfig::TerminalProfile &profile() const;

signals:
	void screenUpdated();
	void titleChanged(const QString &title);
	void sessionEnded();

private:
	void handlePtyData(const QByteArray &data);

	PtyProcess *m_pty = nullptr;
	std::unique_ptr<TerminalBuffer> m_buffer;
	VtParser *m_parser = nullptr;
	TerminalConfig::TerminalProfile m_profile;
};

class TerminalViewBase : public QWidget {
	Q_OBJECT

public:
	explicit TerminalViewBase(QWidget *parent = nullptr) : QWidget(parent) {}
	~TerminalViewBase() override = default;

	virtual void setSearchTerm(const QString &term) = 0;
	virtual bool findNext(bool forward) = 0;
	virtual void copySelection() = 0;
	virtual void pasteClipboard() = 0;
	virtual TerminalSession *session() const = 0;

signals:
	void focused(TerminalViewBase *view);
};

class TerminalViewCommon : public TerminalViewBase {
    Q_OBJECT

public:
    TerminalViewCommon(TerminalSession *session, TerminalConfig *config, QWidget *parent = nullptr);
    virtual ~TerminalViewCommon() = default;

    TerminalSession *session() const { return m_session; }

    void copySelection();
    void pasteClipboard();
    bool hasSelection() const;
    QString selectedText() const;
    void clearSelection();

    virtual void setSearchTerm(const QString &term) {}
    virtual bool findNext(bool forward) { return false; }

    struct CellPos {
        int row = -1;
        int column = -1;
    };

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    CellPos cellFromPoint(const QPoint &pos) const;
    void updateSelection(const QPoint &pos);
    bool isSelectionReversed(const CellPos &start, const CellPos &end) const;

    QByteArray keyToSequence(QKeyEvent *event) const;
    virtual void requestRepaint();

    TerminalSession *m_session;
    TerminalConfig *m_config;
    CellPos m_selectStart;
    CellPos m_selectEnd;
    bool m_selecting = false;
    int m_scrollOffset = 0;
    bool m_userScroll = false;
    int m_cellWidth = 1;
    int m_cellHeight = 1;
};

class TerminalView : public TerminalViewCommon {
	Q_OBJECT

public:
	explicit TerminalView(TerminalSession *session, TerminalConfig *config,
												QWidget *parent = nullptr);
	void setSearchTerm(const QString &term);
	bool findNext(bool forward);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	QFont m_font;
	QColor m_background;
	QColor m_foreground;
	QColor m_selection;
	QColor m_searchHighlight;
	QColor m_cursorColor;
	QString m_searchTerm;
	TerminalBuffer::Match m_searchMatch;

	void updateMetrics();
	void scrollToLine(int line);
	void drawSearchHighlights(QPainter &painter, const QString &line, int row);
	void drawCursor(QPainter &painter, int startLine);
	void paintRowBackgrounds(QPainter &painter, const TerminalBuffer *buffer, int row, int cols);
	void paintRowSelection(QPainter &painter, int row, int cols);
	void paintRowText(QPainter &painter, const TerminalBuffer *buffer, int row, int cols, int y);
	void setCellFont(QPainter &painter, const TerminalBuffer::Cell &cell) const;
	void drawCellGlyph(QPainter &painter, const TerminalBuffer::Cell &cell, int x, int y) const;
	void drawCellDecorations(QPainter &painter, const TerminalBuffer::Cell &cell, int x, int row) const;
	static bool isCellVisuallyEmpty(const TerminalBuffer::Cell &cell);
};

class TerminalTab : public QWidget {
	Q_OBJECT

public:
	explicit TerminalTab(TerminalConfig *config, QWidget *parent = nullptr);
	TerminalTab(TerminalConfig *config,
					const TerminalConfig::TerminalProfile &profile,
					QWidget *parent = nullptr);

	TerminalViewBase *activeView() const;
	void split(Qt::Orientation orientation);
	void closeActiveSplit();
	void setSearchTerm(const QString &term);
	bool findNext(bool forward);
	QString tabTitle() const;
	QString profileName() const;
	QString customTitle() const;
	void setCustomTitle(const QString &title);
	int viewCount() const;

signals:
	void activeViewChanged(TerminalViewBase *view);
	void titleChanged();
	void sessionClosed();

private:
	TerminalViewBase *createView();
	void setActiveView(TerminalViewBase *view);
	QSplitter *splitterForView(TerminalViewBase *view) const;
	void cleanupSplitter(QSplitter *splitter);
	void insertNewSplitter(QSplitter *splitter, TerminalViewBase *newView, QSplitter *parentSplitter);
	void replaceSplitterWithChild(QSplitter *splitter, QWidget *remaining, QSplitter *parentSplitter, int parentIndex);
	TerminalViewBase *findFirstView(QWidget *root) const;

	TerminalConfig *m_config = nullptr;
	TerminalConfig::TerminalProfile m_profile;
	QWidget *m_root = nullptr;
	QList<TerminalViewBase *> m_views;
	TerminalViewBase *m_activeView = nullptr;
	QString m_title;
	QString m_customTitle;
};

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;
	void openTab();

private:
	void setupUi();
	void setupActions();
	void configureShortcuts();
	void configureProfiles();
	void loadSavedShortcuts();
	void connectTabSignals(TerminalTab *tab);
	void refreshProfileMenus();
	void newTabWithProfile(const QString &profileName);
	void closeTabAt(int index);
	void duplicateTabAt(int index);
	void renameTabAt(int index);
	void showTabContextMenu(const QPoint &position);
	void updateStatus();
	TerminalTab *currentTab() const;
	TerminalViewBase *activeView() const;

private slots:
	void newTab();
	void closeTab();
	void splitHorizontal();
	void splitVertical();
	void closeSplit();
	void duplicateTab();
	void renameTab();
	void nextTab();
	void previousTab();
	void showSearch();
	void hideSearch();
	void findNextMatch();
	void findPreviousMatch();
	void updateTabTitle(int index);

private:
	std::unique_ptr<TerminalConfig> m_config;
	QTabWidget *m_tabs = nullptr;
	QToolButton *m_newTabButton = nullptr;
	QMenu *m_profilesMenu = nullptr;
	QMenu *m_newTabMenu = nullptr;
	QFrame *m_searchBar = nullptr;
	QLineEdit *m_searchEdit = nullptr;
	QLabel *m_searchStatus = nullptr;
	QLabel *m_profileStatus = nullptr;
	QAction *m_newTabAction = nullptr;
	QAction *m_closeTabAction = nullptr;
	QAction *m_duplicateTabAction = nullptr;
	QAction *m_renameTabAction = nullptr;
	QAction *m_nextTabAction = nullptr;
	QAction *m_previousTabAction = nullptr;
	QAction *m_splitHorizontalAction = nullptr;
	QAction *m_splitVerticalAction = nullptr;
	QAction *m_closeSplitAction = nullptr;
	QAction *m_copyAction = nullptr;
	QAction *m_pasteAction = nullptr;
	QAction *m_findAction = nullptr;
	QAction *m_findNextAction = nullptr;
	QAction *m_findPreviousAction = nullptr;
};
