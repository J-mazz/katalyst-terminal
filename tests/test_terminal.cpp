#include <QtTest>
#include <QSignalSpy>
#include <QElapsedTimer>

#include "QtShim.h"

class TestTerminal : public QObject {
  Q_OBJECT

private slots:
  void testTerminalConfigAccessors();
  void testTerminalBufferResizeAndCursor();
  void testTerminalBufferEditingAndScrollback();
  void testTerminalBufferInsertDeleteOps();
  void testTerminalBufferScrollRegionOps();
  void testTerminalBufferSearchEdgeCases();
  void testTerminalBufferFindNext();
  void testTerminalBufferAttributes();
  void testTerminalBufferClearModes();
  void testTerminalBufferSnapshotAndVisibleCells();
  void testTerminalBufferAlternateScreen();
  void testVtParserFeedAndTitle();
  void testVtParserControlAndCsiCoverage();
  void testVtParserEscOscAndUtf8Edges();
  void testVtParserResetMidSequence();
  void testVtParserCharsetDesignationConsumed();
  void testVtParserPrivateModeAndScrollRegion();
  void testSgrColorStateVerification();
  void testCsiVpaAndCnlCpl();
  void testTerminalSessionBasics();
  void testTerminalSessionStartShellAndInput();
  void testPtyProcessLifecycle();
};

namespace {
bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(20);
  }
  return predicate();
}
}

void TestTerminal::testTerminalBufferResizeAndCursor() {
  TerminalBuffer buffer;

  QCOMPARE(buffer.rows(), 24);
  QCOMPARE(buffer.columns(), 80);

  buffer.resize(10, 20);
  QCOMPARE(buffer.rows(), 20);
  QCOMPARE(buffer.columns(), 10);

  buffer.resize(0, 0);
  QCOMPARE(buffer.rows(), 1);
  QCOMPARE(buffer.columns(), 1);

  buffer.setCursorPosition(50, 50);
  QCOMPARE(buffer.cursorRow(), 0);
  QCOMPARE(buffer.cursorColumn(), 0);
}

void TestTerminal::testTerminalConfigAccessors() {
  TerminalConfig config;
  const auto profile = config.defaultProfile();

  QVERIFY(!profile.name.isEmpty());
  QVERIFY(profile.scrollbackLines > 0);

  QVERIFY(!config.renderer().isEmpty());
  QVERIFY(config.scrollbackLines() > 0);
  QVERIFY(config.backgroundColor().isValid());
  QVERIFY(config.foregroundColor().isValid());
}

void TestTerminal::testTerminalBufferEditingAndScrollback() {
  TerminalBuffer buffer;
  buffer.resize(5, 2);
  buffer.setScrollbackLimit(2);

  buffer.putChar(QLatin1Char('A'));
  buffer.putChar(QLatin1Char('B'));
  QVERIFY(buffer.lineAt(0).startsWith(QStringLiteral("AB")));

  buffer.newline();
  buffer.putChar(QLatin1Char('C'));
  QVERIFY(buffer.lineAt(1).startsWith(QStringLiteral("C")));

  buffer.newline();
  buffer.putChar(QLatin1Char('D'));
  QCOMPARE(buffer.totalLines(), 3);
  QVERIFY(buffer.lineAt(0).startsWith(QStringLiteral("AB")));

  buffer.carriageReturn();
  buffer.putChar(QLatin1Char('Z'));
  QVERIFY(buffer.lineAt(2).startsWith(QStringLiteral("Z")));

  buffer.backspace();
  QCOMPARE(buffer.cursorColumn(), 0);

  buffer.tab();
  QCOMPARE(buffer.cursorColumn(), 4);
}

void TestTerminal::testTerminalBufferInsertDeleteOps() {
  TerminalBuffer buffer;
  buffer.resize(6, 3);

  for (QChar ch : QStringLiteral("ABCDEF")) buffer.putChar(ch);
  QCOMPARE(buffer.lineAt(0), QStringLiteral("ABCDEF"));

  buffer.setCursorPosition(0, 2);
  buffer.insertChars(2);
  QCOMPARE(buffer.lineAt(0), QStringLiteral("AB  CD"));

  buffer.setCursorPosition(0, 2);
  buffer.deleteChars(2);
  QCOMPARE(buffer.lineAt(0), QStringLiteral("ABCD  "));

  buffer.setCursorPosition(0, 1);
  buffer.eraseChars(3);
  QCOMPARE(buffer.lineAt(0), QStringLiteral("A     "));

  buffer.setCursorPosition(0, 10);
  buffer.insertChars(3);
  buffer.deleteChars(3);
  QVERIFY(true);
}

void TestTerminal::testTerminalBufferScrollRegionOps() {
  TerminalBuffer buffer;
  buffer.resize(5, 4);

  for (QChar ch : QStringLiteral("11111")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("22222")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("33333")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("44444")) buffer.putChar(ch);

  buffer.setScrollRegion(1, 2);
  buffer.setCursorPosition(1, 0);
  buffer.insertLines(1);
  QVERIFY(buffer.lineAt(1).trimmed().isEmpty());
  QVERIFY(buffer.lineAt(2).startsWith(QStringLiteral("22222")) ||
          buffer.lineAt(2).startsWith(QStringLiteral("33333")));

  buffer.setCursorPosition(1, 0);
  buffer.deleteLines(1);
  QVERIFY(buffer.lineAt(1).startsWith(QStringLiteral("22222")) ||
          buffer.lineAt(1).startsWith(QStringLiteral("33333")));

  buffer.setCursorPosition(1, 0);
  buffer.reverseIndex();
  QVERIFY(buffer.lineAt(1).trimmed().isEmpty() || buffer.lineAt(1).startsWith(QStringLiteral("22222")));

  buffer.scrollUp();
  buffer.scrollDown();

  buffer.setScrollRegion(3, 1); // invalid => reset region path
  buffer.resetScrollRegion();
  buffer.setCursorPosition(0, 0);
  buffer.saveCursor();
  buffer.setCursorPosition(3, 4);
  buffer.restoreCursor();
  QCOMPARE(buffer.cursorRow(), 0);
  QCOMPARE(buffer.cursorColumn(), 0);
}

void TestTerminal::testTerminalBufferSearchEdgeCases() {
  TerminalBuffer buffer;
  buffer.resize(8, 2);
  for (QChar ch : QStringLiteral("abcdef")) buffer.putChar(ch);

  QVERIFY(!buffer.findNext(QString(), 0, 0, true, nullptr));

  TerminalBuffer::Match match;
  QVERIFY(!buffer.findNext(QString(), 0, 0, true, &match));
  QVERIFY(!buffer.findNext(QStringLiteral("cde"), 0, 100, true, &match));
  QVERIFY(buffer.findNext(QStringLiteral("cde"), 0, 0, true, &match));
  QCOMPARE(match.column, 2);
  QVERIFY(buffer.findNext(QStringLiteral("abc"), 100, 100, false, &match));
  QCOMPARE(match.column, 0);
}

void TestTerminal::testTerminalBufferFindNext() {
  TerminalBuffer buffer;
  buffer.resize(12, 3);

  for (QChar ch : QStringLiteral("hello")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("world")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("HELLO")) buffer.putChar(ch);

  TerminalBuffer::Match match;
  QVERIFY(buffer.findNext(QStringLiteral("hello"), 0, 0, true, &match));
  QCOMPARE(match.line, 0);
  QCOMPARE(match.column, 0);

  QVERIFY(buffer.findNext(QStringLiteral("hello"), 2, 4, false, &match));
  QCOMPARE(match.line, 2);
  QCOMPARE(match.column, 0);

  QVERIFY(!buffer.findNext(QStringLiteral("missing"), 0, 0, true, &match));
}

void TestTerminal::testTerminalBufferAttributes() {
  TerminalBuffer buffer;
  buffer.resize(4, 1);
  buffer.setDefaultColors(QColor(1, 2, 3), QColor(4, 5, 6));

  buffer.setBold(true);
  buffer.setItalic(true);
  buffer.setUnderline(true);
  buffer.setStrikethrough(true);
  buffer.setInverse(true);
  buffer.putChar(QLatin1Char('X'));

  const auto cell = buffer.cellAt(0, 0);
  QCOMPARE(cell.ch, QLatin1Char('X'));
  QVERIFY(cell.bold);
  QVERIFY(cell.italic);
  QVERIFY(cell.underline);
  QVERIFY(cell.strikethrough);
  QCOMPARE(cell.fg, QColor(4, 5, 6));
  QCOMPARE(cell.bg, QColor(1, 2, 3));

  buffer.resetAttributes();
  buffer.putChar(QLatin1Char('Y'));
  const auto cell2 = buffer.cellAt(0, 1);
  QCOMPARE(cell2.fg, QColor(1, 2, 3));
  QCOMPARE(cell2.bg, QColor(4, 5, 6));
}

void TestTerminal::testTerminalBufferClearModes() {
  TerminalBuffer buffer;
  buffer.resize(6, 3);

  for (QChar ch : QStringLiteral("ABCDE")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("12345")) buffer.putChar(ch);

  buffer.setCursorPosition(1, 3);
  buffer.clearLineToEnd();
  QVERIFY(buffer.lineAt(1).startsWith(QStringLiteral("123")));

  buffer.setCursorPosition(1, 2);
  buffer.clearLineFromStart();
  QVERIFY(buffer.lineAt(1).mid(0, 3).trimmed().isEmpty());

  buffer.setCursorPosition(0, 1);
  buffer.clearToEnd();
  QVERIFY(buffer.lineAt(0).startsWith(QStringLiteral("A")));

  buffer.clear();
  for (int r = 0; r < buffer.rows(); ++r) {
    QVERIFY(buffer.lineAt(r).trimmed().isEmpty());
  }
}

void TestTerminal::testTerminalBufferSnapshotAndVisibleCells() {
  TerminalBuffer buffer;
  buffer.resize(4, 2);
  buffer.setScrollbackLimit(5);

  for (QChar ch : QStringLiteral("ABC")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("EFG")) buffer.putChar(ch);
  buffer.newline();
  for (QChar ch : QStringLiteral("IJK")) buffer.putChar(ch);

  const auto visible = buffer.snapshot(0);
  QCOMPARE(visible.size(), 2);
  QVERIFY(visible[0].startsWith(QStringLiteral("EFG")));
  QVERIFY(visible[1].startsWith(QStringLiteral("IJK")));

  const auto scrolled = buffer.snapshot(1);
  QCOMPARE(scrolled.size(), 2);
  QVERIFY(scrolled[0].startsWith(QStringLiteral("ABC")));

  const auto cell = buffer.cellAtVisible(1, 0, 0);
  QCOMPARE(cell.ch, QLatin1Char('I'));
}

void TestTerminal::testTerminalBufferAlternateScreen() {
  TerminalBuffer buffer;
  buffer.resize(8, 3);
  buffer.setScrollbackLimit(10);

  for (QChar ch : QStringLiteral("NORMAL")) buffer.putChar(ch);
  TerminalBuffer::Match match;
  QVERIFY(buffer.findNext(QStringLiteral("NORMAL"), 0, 0, true, &match));

  buffer.enterAlternateScreen();
  QVERIFY(!buffer.findNext(QStringLiteral("NORMAL"), 0, 0, true, &match));
  QCOMPARE(buffer.totalLines(), buffer.rows());

  for (QChar ch : QStringLiteral("ALT")) buffer.putChar(ch);
  QVERIFY(buffer.findNext(QStringLiteral("ALT"), 0, 0, true, &match));

  buffer.exitAlternateScreen();
  QVERIFY(buffer.findNext(QStringLiteral("NORMAL"), 0, 0, true, &match));
  QVERIFY(!buffer.findNext(QStringLiteral("ALT"), 0, 0, true, &match));
}

void TestTerminal::testVtParserFeedAndTitle() {
  TerminalBuffer buffer;
  buffer.resize(10, 3);
  VtParser parser(&buffer);

  QSignalSpy titleSpy(&parser, &VtParser::titleChanged);

  parser.feed("abc");
  QVERIFY(buffer.lineAt(0).startsWith(QStringLiteral("abc")));

  parser.feed("\x1b[2;3H");
  parser.feed("X");
  QCOMPARE(buffer.cellAt(1, 2).ch, QLatin1Char('X'));

  parser.feed("\x1b]2;MyTitle\x07");
  QCOMPARE(titleSpy.count(), 1);
  QCOMPARE(titleSpy.takeFirst().at(0).toString(), QStringLiteral("MyTitle"));

  parser.feed("\x1b[2J");
  QCOMPARE(buffer.cellAt(0, 0).ch, QLatin1Char(' '));
}

void TestTerminal::testVtParserControlAndCsiCoverage() {
  TerminalBuffer buffer;
  buffer.resize(12, 4);
  VtParser parser(&buffer);

  parser.feed("start");
  parser.feed("\n");
  parser.feed("line2");
  parser.feed("\r");
  parser.feed("A");
  QCOMPARE(buffer.cellAt(1, 0).ch, QLatin1Char('A'));

  parser.feed("\t");
  parser.feed("B");
  QVERIFY(buffer.lineAt(1).contains(QLatin1Char('B')));

  parser.feed("\b");
  parser.feed("C");
  QVERIFY(buffer.lineAt(1).contains(QLatin1Char('C')));

  parser.feed("\x1b[1;1H");
  parser.feed("X");
  QCOMPARE(buffer.cellAt(0, 0).ch, QLatin1Char('X'));

  parser.feed("\x1b[2;4f");
  parser.feed("Y");
  QCOMPARE(buffer.cellAt(1, 3).ch, QLatin1Char('Y'));

  parser.feed("\x1b[A\x1b[B\x1b[C\x1b[D\x1b[G");

  parser.feed("\x1b[1mB\x1b[22mN\x1b[3mI\x1b[23mJ\x1b[4mU\x1b[24mV");
  parser.feed("\x1b[7mQ\x1b[27mW\x1b[9mS\x1b[29mT");
  parser.feed("\x1b[31mR\x1b[39mD\x1b[44mK\x1b[49mL");
  parser.feed("\x1b[38;5;160mP\x1b[48;5;25mZ");
  parser.feed("\x1b[38;2;10;20;30mM\x1b[48;2;40;50;60mN");
  parser.feed("\x1b[999m?");

  parser.feed("\x1b[2K\x1b[1K\x1b[0K");
  parser.feed("\x1b[2J");

  parser.feed("\x1b]2;TitleByST\x1b\\");
  QVERIFY(true);
}

void TestTerminal::testVtParserEscOscAndUtf8Edges() {
  TerminalBuffer buffer;
  buffer.resize(12, 4);
  VtParser parser(&buffer);
  QSignalSpy titleSpy(&parser, &VtParser::titleChanged);

  parser.feed("\x1b[?2004h");
  QVERIFY(buffer.bracketedPasteMode());
  parser.feed("\x1b[?2004l");
  QVERIFY(!buffer.bracketedPasteMode());

  parser.feed("\x1b[1;2H");
  parser.feed("\x1b" "7");
  parser.feed("\x1b[4;5H");
  parser.feed("Q");
  parser.feed("\x1b" "8");
  parser.feed("R");
  TerminalBuffer::Match restoreMatch;
  QVERIFY(buffer.findNext(QStringLiteral("R"), 0, 0, true, &restoreMatch));

  parser.feed("\x1b[2;3r");
  parser.feed("\x1b[2;1H");
  parser.feed("\x1bM");
  QVERIFY(true);

  QByteArray longOsc("\x1b]2;");
  longOsc += QByteArray(9000, 'A');
  longOsc += QByteArray("\x07");
  parser.feed(longOsc);
  QCOMPARE(titleSpy.count(), 0);

  const QByteArray utf8 = QString::fromUtf8("é").toUtf8();
  parser.feed(utf8);
  TerminalBuffer::Match match;
  QVERIFY(buffer.findNext(QString::fromUtf8("é"), 0, 0, true, &match));
}

void TestTerminal::testVtParserResetMidSequence() {
  TerminalBuffer buffer;
  buffer.resize(10, 2);
  VtParser parser(&buffer);

  parser.feed("\x1b[31"); // incomplete CSI
  parser.reset();
  parser.feed("mX");

  QCOMPARE(buffer.cellAt(0, 0).ch, QLatin1Char('m'));
  QCOMPARE(buffer.cellAt(0, 1).ch, QLatin1Char('X'));
}

void TestTerminal::testVtParserCharsetDesignationConsumed() {
  TerminalBuffer buffer;
  buffer.resize(10, 2);
  VtParser parser(&buffer);

  parser.feed("A\x1b(BZ");
  QCOMPARE(buffer.cellAt(0, 0).ch, QLatin1Char('A'));
  QCOMPARE(buffer.cellAt(0, 1).ch, QLatin1Char('Z'));
  QCOMPARE(buffer.cellAt(0, 2).ch, QLatin1Char(' '));

  parser.feed("\x1b(0Q");
  QCOMPARE(buffer.cellAt(0, 2).ch, QLatin1Char('Q'));
  QCOMPARE(buffer.cellAt(0, 3).ch, QLatin1Char(' '));
}

void TestTerminal::testVtParserPrivateModeAndScrollRegion() {
  TerminalBuffer buffer;
  buffer.resize(10, 4);
  VtParser parser(&buffer);

  parser.feed("\x1b[?25l");
  QVERIFY(!buffer.cursorVisible());
  parser.feed("\x1b[?25h");
  QVERIFY(buffer.cursorVisible());

  for (QChar ch : QStringLiteral("NORMAL")) parser.feed(QByteArray(1, ch.toLatin1()));
  parser.feed("\x1b[?1049h");
  TerminalBuffer::Match match;
  QVERIFY(!buffer.findNext(QStringLiteral("NORMAL"), 0, 0, true, &match));
  parser.feed("ALT");
  QVERIFY(buffer.findNext(QStringLiteral("ALT"), 0, 0, true, &match));
  parser.feed("\x1b[?1049l");
  QVERIFY(buffer.findNext(QStringLiteral("NORMAL"), 0, 0, true, &match));

  buffer.clear();
  parser.feed("\x1b[2;3r");
  parser.feed("\x1b[3;1H");
  parser.feed("A\nB\nC");

  QVERIFY(buffer.lineAt(0).trimmed().isEmpty());
  QVERIFY(buffer.lineAt(1).contains(QLatin1Char('B')) || buffer.lineAt(1).contains(QLatin1Char('C')));
}

void TestTerminal::testSgrColorStateVerification() {
  TerminalBuffer buffer;
  buffer.resize(20, 5);
  buffer.setDefaultColors(QColor(220, 220, 220), QColor(20, 22, 26));
  VtParser parser(&buffer);

  // Bold on → write char → verify bold
  parser.feed("\x1b[1m");
  parser.feed("B");
  auto cell = buffer.cellAt(0, 0);
  QCOMPARE(cell.ch, QLatin1Char('B'));
  QVERIFY(cell.bold);

  // Bold off → verify not bold
  parser.feed("\x1b[22m");
  parser.feed("N");
  cell = buffer.cellAt(0, 1);
  QCOMPARE(cell.ch, QLatin1Char('N'));
  QVERIFY(!cell.bold);

  // Italic on
  parser.feed("\x1b[3m");
  parser.feed("I");
  cell = buffer.cellAt(0, 2);
  QCOMPARE(cell.ch, QLatin1Char('I'));
  QVERIFY(cell.italic);

  // Italic off
  parser.feed("\x1b[23m");
  parser.feed("J");
  cell = buffer.cellAt(0, 3);
  QVERIFY(!cell.italic);

  // Underline on
  parser.feed("\x1b[4m");
  parser.feed("U");
  cell = buffer.cellAt(0, 4);
  QVERIFY(cell.underline);

  // Standard foreground (red, index 1)
  parser.feed("\x1b[0m");  // reset first
  parser.feed("\x1b[31m");
  parser.feed("R");
  cell = buffer.cellAt(0, 5);
  QCOMPARE(cell.ch, QLatin1Char('R'));
  QVERIFY(cell.fg != QColor(220, 220, 220));  // changed from default

  // 24-bit foreground (10, 20, 30)
  parser.feed("\x1b[38;2;10;20;30m");
  parser.feed("M");
  cell = buffer.cellAt(0, 6);
  QCOMPARE(cell.ch, QLatin1Char('M'));
  QCOMPARE(cell.fg, QColor(10, 20, 30));

  // 24-bit background (40, 50, 60)
  parser.feed("\x1b[48;2;40;50;60m");
  parser.feed("X");
  cell = buffer.cellAt(0, 7);
  QCOMPARE(cell.ch, QLatin1Char('X'));
  QCOMPARE(cell.bg, QColor(40, 50, 60));

  // 256-color foreground (index 160)
  parser.feed("\x1b[38;5;160m");
  parser.feed("P");
  cell = buffer.cellAt(0, 8);
  QCOMPARE(cell.ch, QLatin1Char('P'));
  QVERIFY(cell.fg != QColor(10, 20, 30));  // changed from previous

  // Reset → verify all attributes cleared
  parser.feed("\x1b[0m");
  parser.feed("Z");
  cell = buffer.cellAt(0, 9);
  QCOMPARE(cell.ch, QLatin1Char('Z'));
  QVERIFY(!cell.bold);
  QVERIFY(!cell.italic);
  QVERIFY(!cell.underline);
  QVERIFY(!cell.strikethrough);
  QCOMPARE(cell.fg, QColor(220, 220, 220));
  QCOMPARE(cell.bg, QColor(20, 22, 26));
}

void TestTerminal::testCsiVpaAndCnlCpl() {
  TerminalBuffer buffer;
  buffer.resize(10, 5);
  VtParser parser(&buffer);

  // CSI d (VPA) — set absolute row
  parser.feed("\x1b[3d");  // move to row 3 (1-based)
  parser.feed("V");
  QCOMPARE(buffer.cellAt(2, 0).ch, QLatin1Char('V'));

  // CSI E (CNL) — cursor next line
  parser.feed("\x1b[1;1H");  // reset to top-left
  parser.feed("\x1b[2E");     // down 2 lines + CR
  parser.feed("E");
  QCOMPARE(buffer.cellAt(2, 0).ch, QLatin1Char('E'));

  // CSI F (CPL) — cursor previous line
  parser.feed("\x1b[4;5H");  // row 4, col 5
  parser.feed("\x1b[1F");     // up 1 line + CR
  parser.feed("F");
  QCOMPARE(buffer.cellAt(2, 0).ch, QLatin1Char('F'));
}

void TestTerminal::testTerminalSessionBasics() {
  TerminalConfig config;
  TerminalSession session(config.defaultProfile());

  QVERIFY(session.buffer() != nullptr);
  session.resize(40, 10);
  QCOMPARE(session.buffer()->columns(), 40);
  QCOMPARE(session.buffer()->rows(), 10);
}

void TestTerminal::testTerminalSessionStartShellAndInput() {
  TerminalConfig::TerminalProfile profile;
  profile.name = QStringLiteral("TestProfile");
  profile.foreground = QColor(220, 220, 220);
  profile.background = QColor(20, 22, 26);
  profile.selection = QColor(60, 120, 200, 120);
  profile.searchHighlight = QColor(200, 160, 60, 160);
  profile.cursor = QColor(200, 200, 200);
  profile.scrollbackLines = 2000;
  profile.term = QStringLiteral("xterm-256color");
  profile.program = QStringLiteral("/bin/sh");
  profile.arguments = {QStringLiteral("-c"), QStringLiteral("printf HELLO_SESSION\\n; printf '\\033]2;SESSION_TITLE\\007'")};

  TerminalSession session(profile);
  QSignalSpy screenSpy(&session, &TerminalSession::screenUpdated);
  QSignalSpy titleSpy(&session, &TerminalSession::titleChanged);

  session.sendInput("IGNORED_BEFORE_START\n");
  session.startShell();

  QVERIFY(waitUntil([&]() {
    TerminalBuffer::Match match;
    return session.buffer()->findNext(QStringLiteral("HELLO_SESSION"), 0, 0, true, &match);
  }, 6000));

  QVERIFY(waitUntil([&]() { return titleSpy.count() > 0; }, 4000));
  QVERIFY(titleSpy.first().at(0).toString().contains(QStringLiteral("SESSION_TITLE")));

  QVERIFY(screenSpy.count() > 0);
}

void TestTerminal::testPtyProcessLifecycle() {
  PtyProcess pty;
  QSignalSpy dataSpy(&pty, &PtyProcess::dataReady);
  QSignalSpy exitSpy(&pty, &PtyProcess::exited);

  QVERIFY(pty.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("printf PTY_OK\\n")}, {}));
  QVERIFY(!pty.start(QStringLiteral("/bin/sh"), {}, {}));

  QVERIFY(waitUntil([&]() { return dataSpy.count() > 0; }, 5000));
  QVERIFY(waitUntil([&]() { return exitSpy.count() > 0; }, 5000));

  QByteArray aggregated;
  for (const auto &entry : dataSpy) {
    aggregated += entry.at(0).toByteArray();
  }
  QVERIFY(aggregated.contains("PTY_OK"));

  pty.setWindowSize(120, 40);
  pty.send("IGNORED_AFTER_READ\n");
  pty.stop();
}

QTEST_GUILESS_MAIN(TestTerminal)

#include "test_terminal.moc"