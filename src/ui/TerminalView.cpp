#include "QtShim.h"
import std;

TerminalView::TerminalView(TerminalSession *session, TerminalConfig *config,
                           QWidget *parent)
    : TerminalViewCommon(session, config, parent) {

  const auto profile = m_config->defaultProfile();
  m_font = profile.font;
  m_background = profile.background;
  m_foreground = profile.foreground;
  m_selection = profile.selection;
  m_searchHighlight = profile.searchHighlight;
  m_cursorColor = profile.cursor;

  updateMetrics();

  if (m_session && m_session->buffer()) {
    m_session->buffer()->setScrollbackLimit(profile.scrollbackLines);
  }

  connect(m_session, &TerminalSession::screenUpdated, this, [this]() {
    if (!m_userScroll) {
      m_scrollOffset = 0;
    }
    update();
  });
}

void TerminalView::setSearchTerm(const QString &term) {
  m_searchTerm = term;
  m_searchMatch = {};
  update();
}

bool TerminalView::findNext(bool forward) {
  if (!m_session || !m_session->buffer()) {
    return false;
  }
  TerminalBuffer *buffer = m_session->buffer();
  TerminalBuffer::Match match;

  int startLine = 0;
  int startColumn = 0;
  if (m_searchMatch.line >= 0) {
    startLine = m_searchMatch.line;
    startColumn = m_searchMatch.column + (forward ? 1 : -1);
  }

  if (!buffer->findNext(m_searchTerm, startLine, startColumn, forward, &match)) {
    return false;
  }

  m_searchMatch = match;
  scrollToLine(match.line);
  update();
  return true;
}

bool TerminalView::isCellVisuallyEmpty(const TerminalBuffer::Cell &cell) {
  return cell.ch == QLatin1Char(' ') && !cell.underline && !cell.strikethrough;
}

void TerminalView::paintRowBackgrounds(QPainter &painter, const TerminalBuffer *buffer, int row, int cols) {
  for (int col = 0; col < cols; ++col) {
    const TerminalBuffer::Cell cell = buffer->cellAtVisible(row, col, m_scrollOffset);
    if (cell.bg != m_background) {
      painter.fillRect(QRect(col * m_cellWidth, row * m_cellHeight, m_cellWidth, m_cellHeight), cell.bg);
    }
  }
}

void TerminalView::paintRowSelection(QPainter &painter, int row, int cols) {
  if (!hasSelection()) return;
  CellPos start = m_selectStart;
  CellPos end = m_selectEnd;
  if (isSelectionReversed(start, end)) qSwap(start, end);
  if (row < start.row || row > end.row) return;
  int startCol = (row == start.row) ? start.column : 0;
  int endCol   = (row == end.row)   ? end.column   : cols;
  int width = qMax(0, endCol - startCol) * m_cellWidth;
  painter.fillRect(QRect(startCol * m_cellWidth, row * m_cellHeight, width, m_cellHeight), m_selection);
}

void TerminalView::setCellFont(QPainter &painter, const TerminalBuffer::Cell &cell) const {
  QFont cellFont = m_font;
  if (cell.bold)   cellFont.setBold(true);
  if (cell.italic) cellFont.setItalic(true);
  painter.setFont(cellFont);
}

void TerminalView::drawCellGlyph(QPainter &painter, const TerminalBuffer::Cell &cell, int x, int y) const {
  if (cell.ch == QLatin1Char(' ')) return;
  painter.setPen(cell.fg);
  painter.drawText(x, y, QString(cell.ch));
}

void TerminalView::drawCellDecorations(QPainter &painter, const TerminalBuffer::Cell &cell, int x, int row) const {
  if (!cell.underline && !cell.strikethrough) return;
  painter.setPen(cell.fg);
  if (cell.underline)
    painter.drawLine(x, row * m_cellHeight + m_cellHeight - 2, x + m_cellWidth, row * m_cellHeight + m_cellHeight - 2);
  if (cell.strikethrough)
    painter.drawLine(x, row * m_cellHeight + m_cellHeight / 2, x + m_cellWidth, row * m_cellHeight + m_cellHeight / 2);
}

void TerminalView::paintRowText(QPainter &painter, const TerminalBuffer *buffer, int row, int cols, int y) {
  for (int col = 0; col < cols; ++col) {
    const TerminalBuffer::Cell cell = buffer->cellAtVisible(row, col, m_scrollOffset);
    if (isCellVisuallyEmpty(cell)) continue;
    const int x = col * m_cellWidth;
    setCellFont(painter, cell);
    drawCellGlyph(painter, cell, x, y);
    drawCellDecorations(painter, cell, x, row);
  }
}

void TerminalView::paintEvent(QPaintEvent *) {
  if (!m_session || !m_session->buffer()) return;

  QPainter painter(this);
  painter.fillRect(rect(), m_background);
  painter.setFont(m_font);

  const TerminalBuffer *buffer = m_session->buffer();
  const int rows = buffer->rows();
  const int cols = buffer->columns();
  const int startLine = qMax(0, buffer->totalLines() - rows - m_scrollOffset);

  for (int row = 0; row < rows; ++row) {
    const int y = (row + 1) * m_cellHeight - m_cellHeight / 4;
    paintRowBackgrounds(painter, buffer, row, cols);
    paintRowSelection(painter, row, cols);
    if (!m_searchTerm.isEmpty()) drawSearchHighlights(painter, buffer->lineAt(startLine + row), row);
    paintRowText(painter, buffer, row, cols, y);
  }

  drawCursor(painter, startLine);
}

void TerminalView::resizeEvent(QResizeEvent *) {
  updateMetrics();
  if (m_session && m_session->buffer()) {
    int columns = qMax(1, width() / m_cellWidth);
    int rows = qMax(1, height() / m_cellHeight);
    m_session->resize(columns, rows);
  }
}

void TerminalView::updateMetrics() {
  QFontMetrics metrics(m_font);
  m_cellWidth = qMax(1, metrics.horizontalAdvance(QLatin1Char('M')));
  m_cellHeight = qMax(1, metrics.height());
}

void TerminalView::scrollToLine(int line) {
  if (!m_session || !m_session->buffer()) {
    return;
  }
  const int total = m_session->buffer()->totalLines();
  const int rows = m_session->buffer()->rows();
  if (rows <= 0 || total <= rows) {
    return;
  }

  int start = qBound(0, line - rows / 2, total - rows);
  m_scrollOffset = total - rows - start;
  m_userScroll = (m_scrollOffset != 0);
}

void TerminalView::drawSearchHighlights(QPainter &painter,
                                        const QString &line, int row) {
  if (m_searchTerm.isEmpty()) {
    return;
  }
  int index = line.indexOf(m_searchTerm, 0, Qt::CaseInsensitive);
  while (index >= 0) {
    int x = index * m_cellWidth;
    int width = m_searchTerm.size() * m_cellWidth;
    painter.fillRect(QRect(x, row * m_cellHeight, width, m_cellHeight),
                     m_searchHighlight);
    index = line.indexOf(m_searchTerm, index + m_searchTerm.size(),
                         Qt::CaseInsensitive);
  }
}

void TerminalView::drawCursor(QPainter &painter, int startLine) {
  if (!m_session || !m_session->buffer()) {
    return;
  }
  const TerminalBuffer *buffer = m_session->buffer();
  const int cursorLine = buffer->cursorRow() +
                         (buffer->totalLines() - buffer->rows());
  if (cursorLine < startLine ||
      cursorLine >= startLine + buffer->rows()) {
    return;
  }
  const int row = cursorLine - startLine;
  const int col = buffer->cursorColumn();
  QRect rect(col * m_cellWidth, row * m_cellHeight, m_cellWidth, m_cellHeight);
  painter.fillRect(rect, m_cursorColor);

  const QString line = buffer->lineAt(cursorLine);
  if (col < line.size()) {
    int y = (row + 1) * m_cellHeight - m_cellHeight / 4;
    painter.setPen(m_background);
    painter.drawText(col * m_cellWidth, y, QString(line[col]));
    painter.setPen(m_foreground);
  }
}
