#include "QtShim.h"
import std;

TerminalBuffer::TerminalBuffer() {
  ensureScreenSize();
  resetScrollRegion();
}

void TerminalBuffer::resize(int columns, int rows) {
  m_columns = qMax(1, columns);
  m_rows = qMax(1, rows);
  m_scrollBottom = qBound(0, m_scrollBottom, m_rows - 1);
  if (m_scrollTop >= m_scrollBottom) {
    resetScrollRegion();
  }
  ensureScreenSize();
  clampCursor();
}

void TerminalBuffer::clear() {
  for (int i = 0; i < m_rows; ++i) {
    screenRow(i) = blankRow(m_currentFg, m_currentBg);
  }
  m_cursorRow = 0;
  m_cursorColumn = 0;
}

void TerminalBuffer::clearLine() {
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) {
    return;
  }
  screenRow(m_cursorRow) = blankRow(m_currentFg, m_currentBg);
  clampCursor();
}

void TerminalBuffer::clearLineToEnd() {
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) {
    return;
  }
  auto &line = screenRow(m_cursorRow);
  for (int col = m_cursorColumn; col < line.size(); ++col) {
    line[col] = Cell{QLatin1Char(' '), m_currentFg, m_currentBg};
  }
}

void TerminalBuffer::clearLineFromStart() {
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) {
    return;
  }
  auto &line = screenRow(m_cursorRow);
  for (int col = 0; col <= qMin(m_cursorColumn, line.size() - 1); ++col) {
    line[col] = Cell{QLatin1Char(' '), m_currentFg, m_currentBg};
  }
}

void TerminalBuffer::clearToEnd() {
  if (m_cursorRow >= 0 && m_cursorRow < m_rows) {
    auto &line = screenRow(m_cursorRow);
    for (int col = m_cursorColumn; col < line.size(); ++col) {
      line[col] = Cell{QLatin1Char(' '), m_currentFg, m_currentBg};
    }
  }
  for (int row = m_cursorRow + 1; row < m_rows; ++row) {
    screenRow(row) = blankRow(m_currentFg, m_currentBg);
  }
}

void TerminalBuffer::clearFromTop() {
  for (int row = 0; row < m_cursorRow; ++row) {
    screenRow(row) = blankRow(m_currentFg, m_currentBg);
  }
  if (m_cursorRow >= 0 && m_cursorRow < m_rows) {
    auto &line = screenRow(m_cursorRow);
    for (int col = 0; col <= qMin(m_cursorColumn, line.size() - 1); ++col) {
      line[col] = Cell{QLatin1Char(' '), m_currentFg, m_currentBg};
    }
  }
}

void TerminalBuffer::insertChars(int n) {
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) return;
  auto &line = screenRow(m_cursorRow);
  n = qMin(n, m_columns - m_cursorColumn);
  for (int i = 0; i < n; ++i) {
    line.insert(m_cursorColumn, Cell{QLatin1Char(' '), m_currentFg, m_currentBg});
  }
  line.resize(m_columns);
}

void TerminalBuffer::deleteChars(int n) {
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) return;
  auto &line = screenRow(m_cursorRow);
  n = qMin(n, m_columns - m_cursorColumn);
  line.remove(m_cursorColumn, n);
  while (line.size() < m_columns) {
    line.push_back(Cell{QLatin1Char(' '), m_currentFg, m_currentBg});
  }
}

void TerminalBuffer::eraseChars(int n) {
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) return;
  auto &line = screenRow(m_cursorRow);
  const int end = qMin(m_cursorColumn + n, m_columns);
  for (int col = m_cursorColumn; col < end; ++col) {
    line[col] = Cell{QLatin1Char(' '), m_currentFg, m_currentBg};
  }
}

void TerminalBuffer::insertLines(int n) {
  if (m_cursorRow < m_scrollTop || m_cursorRow > m_scrollBottom) return;
  n = qMin(n, m_scrollBottom - m_cursorRow + 1);
  for (int i = 0; i < n; ++i) {
    scrollRegionDown(m_cursorRow, m_scrollBottom);
  }
  m_cursorColumn = 0;
  m_pendingWrap = false;
}

void TerminalBuffer::deleteLines(int n) {
  if (m_cursorRow < m_scrollTop || m_cursorRow > m_scrollBottom) return;
  n = qMin(n, m_scrollBottom - m_cursorRow + 1);
  for (int i = 0; i < n; ++i) {
    scrollRegionUp(m_cursorRow, m_scrollBottom);
  }
  m_cursorColumn = 0;
  m_pendingWrap = false;
}

void TerminalBuffer::reverseIndex() {
  m_pendingWrap = false;
  if (m_cursorRow == m_scrollTop) {
    scrollRegionDown(m_scrollTop, m_scrollBottom);
  } else if (m_cursorRow > 0) {
    m_cursorRow--;
  }
}

void TerminalBuffer::saveCursor() {
  m_savedCursorRow = m_cursorRow;
  m_savedCursorColumn = m_cursorColumn;
  m_savedFg = m_currentFg;
  m_savedBg = m_currentBg;
  m_savedBold = m_currentBold;
  m_savedItalic = m_currentItalic;
  m_savedUnderline = m_currentUnderline;
  m_savedStrikethrough = m_currentStrikethrough;
  m_savedInverse = m_currentInverse;
}

void TerminalBuffer::restoreCursor() {
  m_cursorRow = qBound(0, m_savedCursorRow, m_rows - 1);
  m_cursorColumn = qBound(0, m_savedCursorColumn, m_columns - 1);
  m_currentFg = m_savedFg;
  m_currentBg = m_savedBg;
  m_currentBold = m_savedBold;
  m_currentItalic = m_savedItalic;
  m_currentUnderline = m_savedUnderline;
  m_currentStrikethrough = m_savedStrikethrough;
  m_currentInverse = m_savedInverse;
  m_pendingWrap = false;
}

bool TerminalBuffer::bracketedPasteMode() const {
  return m_bracketedPaste;
}

void TerminalBuffer::setBracketedPasteMode(bool enabled) {
  m_bracketedPaste = enabled;
}

void TerminalBuffer::setScrollbackLimit(int lines) {
  m_scrollbackLimit = qMax(0, lines);
  while (m_scrollback.size() > m_scrollbackLimit) {
    m_scrollback.pop_front();
  }
}

void TerminalBuffer::setDefaultColors(const QColor &foreground,
                                      const QColor &background) {
  m_defaultFg = foreground;
  m_defaultBg = background;
  resetAttributes();
}

void TerminalBuffer::setForeground(const QColor &foreground) {
  m_currentFg = foreground;
}

void TerminalBuffer::setBackground(const QColor &background) {
  m_currentBg = background;
}

void TerminalBuffer::setBold(bool bold) {
  m_currentBold = bold;
}

void TerminalBuffer::setItalic(bool italic) {
  m_currentItalic = italic;
}

void TerminalBuffer::setUnderline(bool underline) {
  m_currentUnderline = underline;
}

void TerminalBuffer::setStrikethrough(bool strikethrough) {
  m_currentStrikethrough = strikethrough;
}

void TerminalBuffer::setInverse(bool inverse) {
  m_currentInverse = inverse;
}

void TerminalBuffer::resetAttributes() {
  m_currentFg = m_defaultFg;
  m_currentBg = m_defaultBg;
  m_currentBold = false;
  m_currentItalic = false;
  m_currentUnderline = false;
  m_currentStrikethrough = false;
  m_currentInverse = false;
}

QColor TerminalBuffer::defaultForeground() const {
  return m_defaultFg;
}

QColor TerminalBuffer::defaultBackground() const {
  return m_defaultBg;
}

void TerminalBuffer::putChar(QChar ch) {
  if (m_pendingWrap) {
    newline();
    m_pendingWrap = false;
  }
  if (m_cursorRow < 0 || m_cursorRow >= m_rows) {
    return;
  }

  Cell cell;
  cell.ch = ch;
  cell.bold = m_currentBold;
  cell.italic = m_currentItalic;
  cell.underline = m_currentUnderline;
  cell.strikethrough = m_currentStrikethrough;
  if (m_currentInverse) {
    cell.fg = m_currentBg;
    cell.bg = m_currentFg;
  } else {
    cell.fg = m_currentFg;
    cell.bg = m_currentBg;
  }

  screenRow(m_cursorRow)[m_cursorColumn] = cell;
  m_cursorColumn++;
  if (m_cursorColumn >= m_columns) {
    m_pendingWrap = true;
    m_cursorColumn = m_columns - 1;
  }
}

void TerminalBuffer::newline() {
  m_pendingWrap = false;
  m_cursorColumn = 0;
  if (m_cursorRow >= m_scrollTop && m_cursorRow <= m_scrollBottom) {
    if (m_cursorRow == m_scrollBottom) {
      scrollRegionUp(m_scrollTop, m_scrollBottom);
    } else {
      m_cursorRow++;
    }
  } else {
    if (m_cursorRow == m_rows - 1) {
      // Only scroll the whole screen when there is no restricted scroll region.
      // If a scroll region is active, the cursor is outside it — just stay put.
      if (m_scrollTop == 0 && m_scrollBottom == m_rows - 1) {
        if (!m_useAlternateScreen) {
          pushScrollback(screenRow(0));
        }
        screenRow(0) = blankRow(m_currentFg, m_currentBg);
        activeScreenStart() = (activeScreenStart() + 1) % m_rows;
      }
    } else {
      m_cursorRow++;
    }
  }
}

void TerminalBuffer::carriageReturn() {
  m_pendingWrap = false;
  m_cursorColumn = 0;
}

void TerminalBuffer::backspace() {
  m_pendingWrap = false;
  if (m_cursorColumn > 0) {
    m_cursorColumn--;
  }
}

void TerminalBuffer::tab() {
  m_pendingWrap = false;
  const int tabStop = 8;
  const int nextStop = ((m_cursorColumn / tabStop) + 1) * tabStop;
  m_cursorColumn = qMin(nextStop, m_columns - 1);
}

void TerminalBuffer::setCursorPosition(int row, int column) {
  m_pendingWrap = false;
  m_cursorRow = qBound(0, row, m_rows - 1);
  m_cursorColumn = qBound(0, column, m_columns - 1);
}

void TerminalBuffer::cursorUp(int n) {
  m_pendingWrap = false;
  m_cursorRow = qMax(0, m_cursorRow - qMax(1, n));
}

void TerminalBuffer::cursorDown(int n) {
  m_pendingWrap = false;
  m_cursorRow = qMin(m_rows - 1, m_cursorRow + qMax(1, n));
}

void TerminalBuffer::cursorForward(int n) {
  m_pendingWrap = false;
  m_cursorColumn = qMin(m_columns - 1, m_cursorColumn + qMax(1, n));
}

void TerminalBuffer::cursorBack(int n) {
  m_pendingWrap = false;
  m_cursorColumn = qMax(0, m_cursorColumn - qMax(1, n));
}

void TerminalBuffer::cursorToColumn(int col) {
  m_pendingWrap = false;
  m_cursorColumn = qBound(0, col, m_columns - 1);
}

void TerminalBuffer::setScrollRegion(int top, int bottom) {
  const int minTop = qBound(0, top, m_rows - 1);
  const int minBottom = qBound(0, bottom, m_rows - 1);
  if (minTop >= minBottom) {
    resetScrollRegion();
    return;
  }
  m_scrollTop = minTop;
  m_scrollBottom = minBottom;
  m_cursorRow = 0;
  m_cursorColumn = 0;
  m_pendingWrap = false;
}

void TerminalBuffer::resetScrollRegion() {
  m_scrollTop = 0;
  m_scrollBottom = qMax(0, m_rows - 1);
  m_cursorRow = 0;
  m_cursorColumn = 0;
  m_pendingWrap = false;
}

void TerminalBuffer::enterAlternateScreen() {
  if (m_useAlternateScreen) {
    return;
  }
  m_altSavedCursorRow = m_cursorRow;
  m_altSavedCursorColumn = m_cursorColumn;
  m_useAlternateScreen = true;
  ensureScreenSize();
  m_alternateScreenStart = 0;
  for (int i = 0; i < m_rows; ++i) {
    screenRow(i) = blankRow(m_currentFg, m_currentBg);
  }
  resetScrollRegion();
}

void TerminalBuffer::exitAlternateScreen() {
  if (!m_useAlternateScreen) {
    return;
  }
  m_useAlternateScreen = false;
  ensureScreenSize();
  m_cursorRow = qBound(0, m_altSavedCursorRow, m_rows - 1);
  m_cursorColumn = qBound(0, m_altSavedCursorColumn, m_columns - 1);
  m_pendingWrap = false;
  resetScrollRegion();
}

void TerminalBuffer::setCursorVisible(bool visible) {
  m_cursorVisible = visible;
}

bool TerminalBuffer::cursorVisible() const {
  return m_cursorVisible;
}

int TerminalBuffer::cursorRow() const {
  return m_cursorRow;
}

int TerminalBuffer::cursorColumn() const {
  return m_cursorColumn;
}

int TerminalBuffer::rows() const {
  return m_rows;
}

int TerminalBuffer::columns() const {
  return m_columns;
}

int TerminalBuffer::totalLines() const {
  if (m_useAlternateScreen) {
    return m_rows;
  }
  return static_cast<int>(m_scrollback.size()) + m_normalScreen.size();
}

QString TerminalBuffer::lineAt(int index) const {
  if (index < 0 || index >= totalLines()) {
    return QString();
  }
  const auto sbSize = static_cast<int>(m_scrollback.size());
  if (index < sbSize) {
    return lineToString(m_scrollback[index]);
  }
  const int base = m_useAlternateScreen ? 0 : sbSize;
  return lineToString(screenRow(index - base));
}

TerminalBuffer::Cell TerminalBuffer::cellAt(int index, int column) const {
  if (index < 0 || index >= totalLines()) {
    return Cell{};
  }
  const auto sbSize = static_cast<int>(m_scrollback.size());
  const QVector<Cell> &line =
      (!m_useAlternateScreen && index < sbSize)
          ? m_scrollback[index]
        : screenRow(index - (m_useAlternateScreen ? 0 : sbSize));
  if (column < 0 || column >= line.size()) {
    return Cell{};
  }
  return line[column];
}

TerminalBuffer::Cell TerminalBuffer::cellAtVisible(int row, int column,
                                                   int scrollOffset) const {
  int total = totalLines();
  int start = qMax(0, total - m_rows - scrollOffset);
  return cellAt(start + row, column);
}

bool TerminalBuffer::findForward(const QString &term, int startLine,
                                  int startColumn, int total,
                                  Match *match) const {
  for (int line = qMax(0, startLine); line < total; ++line) {
    const QString current = lineAt(line);
    const int start = (line == startLine) ? qMax(0, startColumn) : 0;
    const int found = current.indexOf(term, start, Qt::CaseInsensitive);
    if (found >= 0) { match->line = line; match->column = found; return true; }
  }
  return false;
}

bool TerminalBuffer::findBackward(const QString &term, int startLine,
                                   int startColumn, int total,
                                   Match *match) const {
  for (int line = qMin(startLine, total - 1); line >= 0; --line) {
    const QString current = lineAt(line);
    const int start = (line == startLine) ? qBound(0, startColumn, current.size()) : current.size();
    const int found = current.lastIndexOf(term, start, Qt::CaseInsensitive);
    if (found >= 0) { match->line = line; match->column = found; return true; }
  }
  return false;
}

bool TerminalBuffer::findNext(const QString &term, int startLine,
                               int startColumn, bool forward,
                               Match *match) const {
  if (!match || term.isEmpty()) return false;
  match->line = -1;
  match->column = -1;
  const int total = totalLines();
  if (total <= 0) return false;
  return forward ? findForward(term, startLine, startColumn, total, match)
                 : findBackward(term, startLine, startColumn, total, match);
}

QStringList TerminalBuffer::snapshot(int scrollOffset) const {
  QStringList allLines;
  const int extra = m_useAlternateScreen ? 0 : (int)m_scrollback.size();
  allLines.reserve(extra + m_rows);
  if (!m_useAlternateScreen) {
    for (const auto &line : m_scrollback) {
      allLines.push_back(lineToString(line));
    }
  }
  for (int i = 0; i < m_rows; ++i) {
    allLines.push_back(lineToString(screenRow(i)));
  }

  int total = allLines.size();
  int start = qMax(0, total - m_rows - scrollOffset);
  return allLines.mid(start, m_rows);
}

QVector<TerminalBuffer::Cell> TerminalBuffer::blankRow(const QColor &fg,
                                                       const QColor &bg) const {
  QVector<Cell> row;
  row.reserve(m_columns);
  for (int i = 0; i < m_columns; ++i) {
    Cell cell;
    cell.ch = QLatin1Char(' ');
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = false;
    row.push_back(cell);
  }
  return row;
}

void TerminalBuffer::ensureScreenSize() {
  auto normalizeScreen = [&](QVector<QVector<Cell>> &screen, int &screenStart) {
    if (screenStart != 0 && !screen.isEmpty()) {
      QVector<QVector<Cell>> ordered;
      ordered.reserve(screen.size());
      const int oldSize = screen.size();
      screenStart = ((screenStart % oldSize) + oldSize) % oldSize;
      for (int i = 0; i < (int)screen.size(); ++i) {
        ordered.push_back(std::move(screen[(screenStart + i) % oldSize]));
      }
      screen = std::move(ordered);
      screenStart = 0;
    }
    while (screen.size() < m_rows) {
      screen.push_back(blankRow(m_defaultFg, m_defaultBg));
    }
    while (screen.size() > m_rows) {
      screen.pop_back();
    }
    for (auto &line : screen) {
      if (line.size() != m_columns) {
        line = blankRow(m_defaultFg, m_defaultBg);
      }
    }
  };

  normalizeScreen(m_normalScreen, m_normalScreenStart);
  normalizeScreen(m_alternateScreen, m_alternateScreenStart);
}

void TerminalBuffer::clampCursor() {
  m_cursorRow = qBound(0, m_cursorRow, m_rows - 1);
  m_cursorColumn = qBound(0, m_cursorColumn, m_columns - 1);
}

void TerminalBuffer::pushScrollback(const QVector<Cell> &line) {
  if (m_scrollbackLimit <= 0) {
    return;
  }
  m_scrollback.push_back(line);
  while (m_scrollback.size() > m_scrollbackLimit) {
    m_scrollback.pop_front();
  }
}

QString TerminalBuffer::lineToString(const QVector<Cell> &line) const {
  QString text;
  text.reserve(line.size());
  for (const Cell &cell : line) {
    text.append(cell.ch);
  }
  return text;
}

void TerminalBuffer::scrollRegionUp(int top, int bottom) {
  if (top < 0 || bottom >= m_rows || top >= bottom) {
    return;
  }
  if (top == 0 && bottom == m_rows - 1) {
    if (!m_useAlternateScreen) {
      pushScrollback(screenRow(0));
    }
    screenRow(0) = blankRow(m_currentFg, m_currentBg);
    activeScreenStart() = (activeScreenStart() + 1) % m_rows;
    return;
  }

  for (int row = top; row < bottom; ++row) {
    screenRow(row) = screenRow(row + 1);
  }
  screenRow(bottom) = blankRow(m_currentFg, m_currentBg);
}

void TerminalBuffer::scrollRegionDown(int top, int bottom) {
  if (top < 0 || bottom >= m_rows || top >= bottom) {
    return;
  }
  for (int row = bottom; row > top; --row) {
    screenRow(row) = screenRow(row - 1);
  }
  screenRow(top) = blankRow(m_currentFg, m_currentBg);
}

void TerminalBuffer::scrollUp() {
  scrollRegionUp(m_scrollTop, m_scrollBottom);
}

void TerminalBuffer::scrollDown() {
  scrollRegionDown(m_scrollTop, m_scrollBottom);
}

QVector<QVector<TerminalBuffer::Cell>> &TerminalBuffer::activeScreen() {
  return m_useAlternateScreen ? m_alternateScreen : m_normalScreen;
}

const QVector<QVector<TerminalBuffer::Cell>> &TerminalBuffer::activeScreen() const {
  return m_useAlternateScreen ? m_alternateScreen : m_normalScreen;
}

int &TerminalBuffer::activeScreenStart() {
  return m_useAlternateScreen ? m_alternateScreenStart : m_normalScreenStart;
}

int TerminalBuffer::activeScreenStart() const {
  return m_useAlternateScreen ? m_alternateScreenStart : m_normalScreenStart;
}
