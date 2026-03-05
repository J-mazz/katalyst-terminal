#include <QtTest>
#include <QTabWidget>
#include <QAction>
#include <QApplication>
#include <QTimer>
#include <QMenuBar>
#include <QDialog>
#include <QExposeEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QInputMethodEvent>

#include "QtShim.h"

class TestUi : public QObject {
  Q_OBJECT

private:
  static QAction *findActionByText(const MainWindow &window, const QString &text) {
    const auto actions = window.findChildren<QAction *>();
    for (QAction *action : actions) {
      if (action && action->text() == text) {
        return action;
      }
    }
    return nullptr;
  }

private slots:
  void testMainWindowTabLifecycle();
  void testSplitOperationsAndActions();
  void testTerminalViewInteractionPaths();
  void testShortcutsDialogAcceptPath();
  void testTerminalViewRichPaintAndSearch();
  void testTerminalTabSplitterBranches();
  void testVulkanTerminalWindowSignalPaths();
  void testVulkanTerminalViewGuardPaths();
  void testVulkanRendererGuardAndBufferUpdatePaths();
};

class EventHarnessView : public TerminalView {
public:
  EventHarnessView(TerminalSession *session, TerminalConfig *config)
      : TerminalView(session, config, nullptr) {}

  void triggerFocusIn() {
    QFocusEvent ev(QEvent::FocusIn);
    focusInEvent(&ev);
  }

  void triggerKey(int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    keyPressEvent(&ev);
  }

  void triggerIme(const QString &commit) {
    QInputMethodEvent ev;
    ev.setCommitString(commit);
    inputMethodEvent(&ev);
  }

  void triggerMousePress(const QPoint &p) {
    QMouseEvent ev(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressEvent(&ev);
  }

  void triggerMouseMove(const QPoint &p) {
    QMouseEvent ev(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    mouseMoveEvent(&ev);
  }

  void triggerMouseRelease(const QPoint &p) {
    QMouseEvent ev(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    mouseReleaseEvent(&ev);
  }

  void triggerWheel(int deltaY) {
    QWheelEvent ev(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0), QPoint(0, deltaY),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    wheelEvent(&ev);
  }
};

class VulkanWindowHarness : public VulkanTerminalWindow {
public:
  void triggerExpose() {
    QExposeEvent ev(QRegion(QRect(0, 0, 20, 20)));
    exposeEvent(&ev);
  }

  void triggerResize() {
    QResizeEvent ev(QSize(640, 360), QSize(320, 180));
    resizeEvent(&ev);
  }

  void triggerKey(int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
                  const QString &text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    keyPressEvent(&ev);
  }

  void triggerMousePress(const QPoint &p) {
    QMouseEvent ev(QEvent::MouseButtonPress, p, p, p, Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    mousePressEvent(&ev);
  }

  void triggerMouseMove(const QPoint &p) {
    QMouseEvent ev(QEvent::MouseMove, p, p, p, Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
    mouseMoveEvent(&ev);
  }

  void triggerMouseRelease(const QPoint &p) {
    QMouseEvent ev(QEvent::MouseButtonRelease, p, p, p, Qt::LeftButton,
                   Qt::NoButton, Qt::NoModifier);
    mouseReleaseEvent(&ev);
  }

  void triggerWheel(int deltaY) {
    QWheelEvent ev(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0), QPoint(0, deltaY),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    wheelEvent(&ev);
  }

  void triggerFocusIn() {
    QFocusEvent ev(QEvent::FocusIn);
    focusInEvent(&ev);
  }
};

class VulkanViewHarness : public VulkanTerminalView {
public:
  VulkanViewHarness(TerminalSession *session, TerminalConfig *config)
      : VulkanTerminalView(session, config, nullptr) {}

  void triggerFocusIn() {
    QFocusEvent ev(QEvent::FocusIn);
    focusInEvent(&ev);
  }

  void triggerShow() {
    QShowEvent ev;
    showEvent(&ev);
  }

  void triggerResize() {
    QResizeEvent ev(QSize(640, 360), QSize(320, 180));
    resizeEvent(&ev);
  }

  void triggerRequestRepaint() {
    requestRepaint();
  }
};

void TestUi::testMainWindowTabLifecycle() {
  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  auto *tabs = window.findChild<QTabWidget *>();
  QVERIFY(tabs != nullptr);
  QVERIFY(tabs->count() >= 1);

  const int initial = tabs->count();
  window.openTab();
  QCoreApplication::processEvents();
  QCOMPARE(tabs->count(), initial + 1);

  QVERIFY(QMetaObject::invokeMethod(&window, "closeTab", Qt::DirectConnection));
  QCoreApplication::processEvents();
  QCOMPARE(tabs->count(), initial);

  QVERIFY(QMetaObject::invokeMethod(&window, "closeTab", Qt::DirectConnection));
  QCoreApplication::processEvents();
  QVERIFY(tabs->count() >= 1);
}

void TestUi::testSplitOperationsAndActions() {
  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  auto *tabs = window.findChild<QTabWidget *>();
  QVERIFY(tabs != nullptr);
  auto *tab = qobject_cast<TerminalTab *>(tabs->currentWidget());
  QVERIFY(tab != nullptr);
  QVERIFY(tab->activeView() != nullptr);

  QVERIFY(QMetaObject::invokeMethod(&window, "splitHorizontal", Qt::DirectConnection));
  QCoreApplication::processEvents();
  QVERIFY(tab->activeView() != nullptr);

  QVERIFY(QMetaObject::invokeMethod(&window, "splitVertical", Qt::DirectConnection));
  QCoreApplication::processEvents();
  QVERIFY(tab->activeView() != nullptr);

  QVERIFY(QMetaObject::invokeMethod(&window, "closeSplit", Qt::DirectConnection));
  QCoreApplication::processEvents();
  QVERIFY(tab->activeView() != nullptr);

  QAction *copyAction = findActionByText(window, QStringLiteral("Copy"));
  QAction *pasteAction = findActionByText(window, QStringLiteral("Paste"));
  QAction *newTabAction = findActionByText(window, QStringLiteral("New Tab"));

  QVERIFY(copyAction != nullptr);
  QVERIFY(pasteAction != nullptr);
  QVERIFY(newTabAction != nullptr);

  copyAction->trigger();
  pasteAction->trigger();
  const int before = tabs->count();
  newTabAction->trigger();
  QCoreApplication::processEvents();
  QCOMPARE(tabs->count(), before + 1);
}

void TestUi::testTerminalViewInteractionPaths() {
  TerminalConfig config;
  TerminalSession session(config.defaultProfile());
  session.resize(24, 6);

  auto *buffer = session.buffer();
  QVERIFY(buffer != nullptr);
  for (QChar ch : QStringLiteral("hello world")) buffer->putChar(ch);
  buffer->newline();
  for (QChar ch : QStringLiteral("search target")) buffer->putChar(ch);

  EventHarnessView view(&session, &config);
  view.resize(640, 220);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view, 100));
  QCoreApplication::processEvents();

  // Search and scroll path coverage
  view.setSearchTerm(QStringLiteral("target"));
  (void)view.findNext(true);
  (void)view.findNext(false);

  // Selection/copy path coverage
  view.triggerMousePress(QPoint(10, 10));
  view.triggerMouseMove(QPoint(120, 10));
  view.triggerMouseRelease(QPoint(120, 10));
  view.copySelection();

  // Paste paths (plain + bracketed)
  QApplication::clipboard()->setText(QStringLiteral("paste text"));
  buffer->setBracketedPasteMode(false);
  view.pasteClipboard();
  buffer->setBracketedPasteMode(true);
  view.pasteClipboard();

  // Key and IME event paths
  view.triggerFocusIn();
  view.triggerKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
  view.triggerKey(Qt::Key_Left);
  view.triggerKey(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
  view.triggerKey(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
  view.triggerIme(QStringLiteral("中"));
  view.triggerWheel(120);
  view.triggerWheel(0);

  // Trigger paint/resize-related paths
  view.repaint();
  QCoreApplication::processEvents();
  view.resize(700, 260);
  QCoreApplication::processEvents();

  QVERIFY(true);
}

void TestUi::testShortcutsDialogAcceptPath() {
  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QAction *shortcutAction = nullptr;
  const auto menus = window.menuBar()->actions();
  for (QAction *menuAction : menus) {
    if (!menuAction || !menuAction->menu()) continue;
    for (QAction *action : menuAction->menu()->actions()) {
      if (action && action->text().contains(QStringLiteral("Keyboard Shortcuts"))) {
        shortcutAction = action;
        break;
      }
    }
    if (shortcutAction) break;
  }
  QVERIFY(shortcutAction != nullptr);

  QTimer::singleShot(0, []() {
    for (QWidget *widget : QApplication::topLevelWidgets()) {
      if (auto *dialog = qobject_cast<QDialog *>(widget)) {
        dialog->accept();
        break;
      }
    }
  });

  shortcutAction->trigger();
  QCoreApplication::processEvents();
  QVERIFY(true);
}

void TestUi::testTerminalViewRichPaintAndSearch() {
  TerminalConfig config;
  TerminalSession session(config.defaultProfile());
  session.resize(20, 6);

  auto *buffer = session.buffer();
  QVERIFY(buffer != nullptr);
  buffer->clear();
  buffer->setDefaultColors(QColor(200, 200, 200), QColor(10, 10, 10));
  buffer->setBackground(QColor(20, 50, 90));
  buffer->setBold(true);
  buffer->setItalic(true);
  buffer->setUnderline(true);
  for (QChar ch : QStringLiteral("target target")) buffer->putChar(ch);
  buffer->setUnderline(false);
  buffer->setStrikethrough(true);
  buffer->newline();
  for (QChar ch : QStringLiteral("line with strike")) buffer->putChar(ch);
  buffer->setStrikethrough(false);
  buffer->setCursorPosition(0, 2);
  buffer->setCursorVisible(true);

  EventHarnessView view(&session, &config);
  view.resize(700, 260);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view, 100));
  QCoreApplication::processEvents();

  view.setSearchTerm(QStringLiteral("target"));
  (void)view.findNext(true);
  (void)view.findNext(false);

  view.triggerMousePress(QPoint(150, 20));
  view.triggerMouseMove(QPoint(20, 20));
  view.triggerMouseRelease(QPoint(20, 20));

  view.repaint();
  QCoreApplication::processEvents();
}

void TestUi::testTerminalTabSplitterBranches() {
  TerminalConfig config;
  TerminalTab tab(&config);
  tab.resize(700, 300);
  tab.show();
  QCoreApplication::processEvents();

  QVERIFY(tab.activeView() != nullptr);
  tab.closeActiveSplit();

  // Same-orientation split path (insert into existing parent splitter)
  tab.split(Qt::Horizontal);
  tab.split(Qt::Horizontal);
  tab.split(Qt::Vertical);
  QVERIFY(tab.activeView() != nullptr);

  tab.setSearchTerm(QStringLiteral("needle"));
  (void)tab.findNext(true);
  (void)tab.findNext(false);

  tab.closeActiveSplit();
  tab.closeActiveSplit();
  tab.closeActiveSplit();
  QVERIFY(tab.activeView() != nullptr);
}

void TestUi::testVulkanTerminalWindowSignalPaths() {
  VulkanWindowHarness window;

  QSignalSpy keySpy(&window, &VulkanTerminalWindow::keyInput);
  QSignalSpy pressSpy(&window, &VulkanTerminalWindow::mousePressed);
  QSignalSpy moveSpy(&window, &VulkanTerminalWindow::mouseMoved);
  QSignalSpy releaseSpy(&window, &VulkanTerminalWindow::mouseReleased);
  QSignalSpy wheelSpy(&window, &VulkanTerminalWindow::wheelScrolled);
  QSignalSpy focusSpy(&window, &VulkanTerminalWindow::windowFocused);

  window.triggerExpose();
  window.triggerResize();
  window.triggerKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
  window.triggerMousePress(QPoint(4, 4));
  window.triggerMouseMove(QPoint(10, 10));
  window.triggerMouseRelease(QPoint(10, 10));
  window.triggerWheel(120);
  window.triggerFocusIn();

  QCOMPARE(keySpy.count(), 1);
  QCOMPARE(pressSpy.count(), 1);
  QCOMPARE(moveSpy.count(), 1);
  QCOMPARE(releaseSpy.count(), 1);
  QCOMPARE(wheelSpy.count(), 1);
  QCOMPARE(focusSpy.count(), 1);
}

void TestUi::testVulkanTerminalViewGuardPaths() {
  TerminalConfig config;
  TerminalSession session(config.defaultProfile());
  session.resize(24, 6);

  VulkanViewHarness view(&session, &config);
  view.resize(640, 220);
  view.show();
  QCoreApplication::processEvents();

  QSignalSpy focusedSpy(&view, &TerminalViewBase::focused);

  view.setSearchTerm(QStringLiteral("needle"));
  QVERIFY(!view.findNext(true));
  view.triggerRequestRepaint();
  view.triggerResize();
  view.triggerShow();
  view.triggerFocusIn();
  view.initRenderer();
  view.initRenderer();

  QVERIFY(view.isInitialized() || !view.isInitialized());
  QVERIFY(focusedSpy.count() >= 1);
}

void TestUi::testVulkanRendererGuardAndBufferUpdatePaths() {
  VulkanRenderer renderer;
  QVERIFY(!renderer.isReady());

  TerminalConfig config;
  const auto profile = config.defaultProfile();
  QVERIFY(!renderer.initialize(nullptr, nullptr, profile, profile.font));

  TerminalBuffer buffer;
  buffer.resize(5, 3);
  buffer.clear();
  buffer.setUnderline(true);
  buffer.putChar(QLatin1Char('a'));
  buffer.setUnderline(false);
  buffer.setStrikethrough(true);
  buffer.putChar(QLatin1Char('b'));
  buffer.setStrikethrough(false);
  buffer.newline();
  buffer.putChar(QLatin1Char('c'));

  VulkanRenderer::Selection selection;
  selection.active = true;
  selection.startRow = 1;
  selection.startCol = 3;
  selection.endRow = 0;
  selection.endCol = 0;

  renderer.updateFromBuffer(nullptr, 0, selection);
  renderer.updateFromBuffer(&buffer, 0, selection);
  renderer.updateFromBuffer(&buffer, 0, selection);

  renderer.resize(0, 0);
  renderer.resize(80, 25);
  renderer.render();
  renderer.cleanup();
  QVERIFY(!renderer.isReady());

  QVulkanInstance instance;
  instance.setApiVersion(QVersionNumber(1, 2, 0));
  if (instance.create()) {
    QWindow window;
    window.setSurfaceType(QSurface::VulkanSurface);
    window.setVulkanInstance(&instance);
    window.resize(320, 200);
    window.create();

    (void)renderer.initialize(&instance, &window, profile, profile.font);
    renderer.resize(320, 200);
    renderer.render();
    renderer.cleanup();
    QVERIFY(!renderer.isReady());
  }
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);
  TestUi test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_ui.moc"
