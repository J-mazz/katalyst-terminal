// QtShim.h — Qt/KDE/Vulkan/POSIX compatibility shim for C++23 modules.
//
// Include this header FIRST in every .cpp file, BEFORE `import std;`.
// Qt headers transitively pull in STL headers; by including them before
// the module import we avoid redefinition clashes with `import std;`.
#pragma once

// --- C++ standard library (included before import std; to avoid redefinition) ---
#include <deque>
#include <memory>

// --- Vulkan (must precede Qt Vulkan wrappers) ---
#include <vulkan/vulkan.h>

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
#include <QFocusEvent>
#include <QFontMetrics>
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
#include <QVulkanInstance>
#include <QWheelEvent>
#include <QWindow>

// --- Qt Widgets ---
#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
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
class VulkanRenderer;
class VulkanTerminalWindow;
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
	int cursorRow() const;
	int cursorColumn() const;

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
	int m_scrollbackLimit = 2000;
	int m_normalScreenStart = 0;
	int m_alternateScreenStart = 0;
	int m_scrollTop = 0;
	int m_scrollBottom = 23;
	bool m_useAlternateScreen = false;
	bool m_cursorVisible = true;

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
	QString renderer() const;

	QFont font() const;
	QColor backgroundColor() const;
	QColor foregroundColor() const;
	int scrollbackLines() const;

private:
	TerminalProfile m_defaultProfile;
	QString m_renderer;
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

signals:
	void screenUpdated();
	void titleChanged(const QString &title);

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

struct TerminalQuadVertex {
	float x;
	float y;
};

struct TerminalQuadInstance {
	float posX;
	float posY;
	float sizeX;
	float sizeY;
	float uvMinX;
	float uvMinY;
	float uvMaxX;
	float uvMaxY;
	float fgR;
	float fgG;
	float fgB;
	float fgA;
	float bgR;
	float bgG;
	float bgB;
	float bgA;
};

constexpr TerminalQuadVertex kTerminalQuadVertices[] = {
		TerminalQuadVertex{0.0f, 0.0f},
		TerminalQuadVertex{1.0f, 0.0f},
		TerminalQuadVertex{0.0f, 1.0f},
		TerminalQuadVertex{1.0f, 1.0f}};

class VulkanRenderer {
public:
	VulkanRenderer();
	~VulkanRenderer();

	struct Selection {
		bool active = false;
		int startRow = 0;
		int startCol = 0;
		int endRow = 0;
		int endCol = 0;
	};

	bool initialize(QVulkanInstance *instance, QWindow *window,
									const TerminalConfig::TerminalProfile &profile,
									const QFont &font);
	void cleanup();

	void resize(int width, int height);
	void updateFromBuffer(const TerminalBuffer *buffer, int scrollOffset,
												const Selection &selection);
	void render();

	bool isReady() const;

private:
	struct GlyphInfo {
		float uvMinX = 0.0f;
		float uvMinY = 0.0f;
		float uvMaxX = 0.0f;
		float uvMaxY = 0.0f;
	};

	bool selectPhysicalDevice();
	bool createDevice();
	bool createSwapchainImageViews();
	bool createSwapchain();
	void cleanupSwapchain();
	void cleanupAtlas();
	void cleanupBuffers();
	void cleanupDescriptors();
	void cleanupSyncObjects();
	bool createRenderPass();
	bool createDescriptorSetLayout();
	bool createPipelineLayout();
	bool createPipeline();
	bool createFramebuffers();
	bool createCommandPool();
	bool createCommandBuffers();
	bool createVertexBuffer();
	bool createInstanceBuffer();
	bool createBuffers();
	bool createAtlasImage(uint32_t width, uint32_t height);
	bool createAtlasView();
	bool createAtlasSampler();
	bool createAtlas();
	bool createDescriptorSet();
	bool createSyncObjects();

	void recordCommandBuffer(uint32_t imageIndex);
	void buildGlyphAtlas(const QFont &font);
	void insertGlyphAt(QPainter &painter, uint codepoint, int &x, int &y,
	                   int atlasWidth, int atlasHeight, const QFontMetrics &metrics);
	void preRasterizeGlyphRanges(QPainter &painter, int &x, int &y, int atlasWidth, int atlasHeight);
	void rasterizeBoldGlyph(QPainter &painter, uint cp,
	                        const QFontMetrics &metrics, int atlasWidth, int atlasHeight);
	void preRasterizeBoldGlyphs(QPainter &painter, const QFont &boldFont, int atlasWidth, int atlasHeight);
	void updateInstanceBuffer();
	bool growInstanceBuffer(size_t needed);
	bool rasterizeGlyph(uint codepoint, bool bold = false);
	struct BufferCopyRegion {
		uint32_t width;
		uint32_t height;
		uint32_t rowLength;
	};
	bool allocateStagingBuffer(VkDeviceSize imageSize, VkBuffer &stagingBuffer, VkDeviceMemory &stagingMemory);
	void uploadStagingToAtlas(VkBuffer stagingBuffer, const BufferCopyRegion &region);
	void reuploadAtlas();
	VkCommandBuffer beginSingleTimeCommands();
	void endSingleTimeCommands(VkCommandBuffer commandBuffer);
	void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
														 VkImageLayout oldLayout,
														 VkImageLayout newLayout);
	void copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer,
												 VkImage image, const BufferCopyRegion& copyRegion);

	// Helper functions for updateFromBuffer to reduce complexity
	void normalizeSelection(Selection &selection);
	uint getGlyphKey(uint codepoint, bool bold);
	bool isCellSelected(int row, int col, const Selection &sel) const;
	void addDecorationInstances(const TerminalBuffer::Cell &cell,
															const TerminalQuadInstance &baseInstance,
															const QColor &fg);

	uint32_t findMemoryType(uint32_t typeFilter,
													VkMemoryPropertyFlags properties) const;
	VkShaderModule createShaderModule(const QByteArray &code);

	QVulkanInstance *m_instance = nullptr;
	QWindow *m_window = nullptr;

	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueFamily = 0;

	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkFormat m_swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
	VkExtent2D m_swapchainExtent{};
	QVector<VkImage> m_swapchainImages;
	QVector<VkImageView> m_swapchainImageViews;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	QVector<VkFramebuffer> m_framebuffers;

	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	QVector<VkCommandBuffer> m_commandBuffers;

	VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
	VkSemaphore m_renderFinished = VK_NULL_HANDLE;
	VkFence m_inFlight = VK_NULL_HANDLE;

	VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
	VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;
	size_t m_instanceCapacity = 0;

	VkImage m_atlasImage = VK_NULL_HANDLE;
	VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE;
	VkImageView m_atlasView = VK_NULL_HANDLE;
	VkSampler m_atlasSampler = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

	TerminalConfig::TerminalProfile m_profile;
	QSize m_cellSize;
	QSize m_surfaceSize;
	QFont m_atlasFont;
	QHash<uint, GlyphInfo> m_glyphs;
	QVector<TerminalQuadInstance> m_instances;
	QVector<TerminalQuadInstance> m_cachedInstances;
	int m_dirtyFirst = 0;
	int m_dirtyLast = -1;
	QImage m_atlasImageCpu;
	int m_atlasCursorX = 0;
	int m_atlasCursorY = 0;
	bool m_atlasDirty = false;

	bool m_ready = false;
};

class VulkanTerminalWindow : public QWindow {
	Q_OBJECT

public:
	VulkanTerminalWindow();

	void setVulkanInstance(QVulkanInstance *instance);
	void setRenderer(VulkanRenderer *renderer);

signals:
	void keyInput(QKeyEvent *event);
	void mousePressed(QMouseEvent *event);
	void mouseMoved(QMouseEvent *event);
	void mouseReleased(QMouseEvent *event);
	void wheelScrolled(QWheelEvent *event);
	void windowFocused();

protected:
	void exposeEvent(QExposeEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;

private:
	QVulkanInstance *m_instance = nullptr;
	VulkanRenderer *m_renderer = nullptr;
};

class VulkanTerminalView : public TerminalViewCommon {
	Q_OBJECT

public:
	VulkanTerminalView(TerminalSession *session, TerminalConfig *config,
										 QWidget *parent = nullptr);

	void setSearchTerm(const QString &term) override;
	bool findNext(bool forward) override;
	void keyPressEvent(QKeyEvent *event) override;
	void inputMethodEvent(QInputMethodEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	void updateFrame();
	void connectVulkanSignals();
	CellPos cellFromPoint(const QPoint &pos) const;
	void updateSelection(const QPoint &pos);
	QString selectedRow(const QStringList &lines, int row,
							 const CellPos &start, const CellPos &end) const;
	QString selectedText() const;


	QVulkanInstance m_instance;
	VulkanTerminalWindow *m_window = nullptr;
	VulkanRenderer *m_renderer = nullptr;
	QWidget *m_container = nullptr;
};

class TerminalTab : public QWidget {
	Q_OBJECT

public:
	explicit TerminalTab(TerminalConfig *config, QWidget *parent = nullptr);

	TerminalViewBase *activeView() const;
	void split(Qt::Orientation orientation);
	void closeActiveSplit();
	void setSearchTerm(const QString &term);
	bool findNext(bool forward);
	QString tabTitle() const;

signals:
	void activeViewChanged(TerminalViewBase *view);
	void titleChanged();

private:
	TerminalViewBase *createView();
	void setActiveView(TerminalViewBase *view);
	QSplitter *splitterForView(TerminalViewBase *view) const;
	void cleanupSplitter(QSplitter *splitter);
	void insertNewSplitter(QSplitter *splitter, TerminalViewBase *newView, QSplitter *parentSplitter);
	void replaceSplitterWithChild(QSplitter *splitter, QWidget *remaining, QSplitter *parentSplitter, int parentIndex);
	TerminalViewBase *findFirstView(QWidget *root) const;

	TerminalConfig *m_config = nullptr;
	QWidget *m_root = nullptr;
	QList<TerminalViewBase *> m_views;
	TerminalViewBase *m_activeView = nullptr;
	QString m_title;
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
	void loadSavedShortcuts();
	void connectTabSignals(TerminalTab *tab);
	TerminalTab *currentTab() const;
	TerminalViewBase *activeView() const;

private slots:
	void newTab();
	void closeTab();
	void splitHorizontal();
	void splitVertical();
	void closeSplit();
	void updateTabTitle(int index);

private:
	std::unique_ptr<TerminalConfig> m_config;
	QTabWidget *m_tabs = nullptr;
	QPushButton *m_newTabButton = nullptr;
	QAction *m_newTabAction = nullptr;
	QAction *m_closeTabAction = nullptr;
	QAction *m_splitHorizontalAction = nullptr;
	QAction *m_splitVerticalAction = nullptr;
	QAction *m_closeSplitAction = nullptr;
	QAction *m_copyAction = nullptr;
	QAction *m_pasteAction = nullptr;
};
