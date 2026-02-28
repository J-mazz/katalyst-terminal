#include "QtShim.h"
import std;

TerminalView::TerminalView(TerminalSession *session, TerminalConfig *config,
                           QWidget *parent)
    : TerminalViewBase(parent), m_session(session), m_config(config) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);

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

void TerminalView::copySelection() {
  if (hasSelection()) {
    QApplication::clipboard()->setText(selectedText());
  }
}

void TerminalView::pasteClipboard() {
  if (!m_session) {
    return;
  }
  QByteArray text = QApplication::clipboard()->text().toLocal8Bit();
  m_session->sendInput(text);
}

TerminalSession *TerminalView::session() const {
  return m_session;
}

void TerminalView::paintEvent(QPaintEvent *) {
  if (!m_session || !m_session->buffer()) {
    return;
  }

  QPainter painter(this);
  painter.fillRect(rect(), m_background);
  painter.setFont(m_font);

  const TerminalBuffer *buffer = m_session->buffer();
  const int rows = buffer->rows();
  const int cols = buffer->columns();
  const int total = buffer->totalLines();
  const int startLine = qMax(0, total - rows - m_scrollOffset);

  for (int row = 0; row < rows; ++row) {
    int y = (row + 1) * m_cellHeight - m_cellHeight / 4;

    // Draw per-cell backgrounds and foregrounds
    for (int col = 0; col < cols; ++col) {
      const TerminalBuffer::Cell cell =
          buffer->cellAtVisible(row, col, m_scrollOffset);
      int x = col * m_cellWidth;

      // Cell background
      if (cell.bg != m_background) {
        painter.fillRect(QRect(x, row * m_cellHeight, m_cellWidth, m_cellHeight),
                         cell.bg);
      }
    }

    // Selection highlight
    if (hasSelection()) {
      CellPos start = m_selectStart;
      CellPos end = m_selectEnd;
      if (start.row > end.row ||
          (start.row == end.row && start.column > end.column)) {
        qSwap(start, end);
      }

      if (row >= start.row && row <= end.row) {
        int startCol = (row == start.row) ? start.column : 0;
        int endCol = (row == end.row) ? end.column : cols;
        int x = startCol * m_cellWidth;
        int width = qMax(0, (endCol - startCol)) * m_cellWidth;
        painter.fillRect(QRect(x, row * m_cellHeight, width, m_cellHeight),
                         m_selection);
      }
    }

    // Search highlights
    if (!m_searchTerm.isEmpty()) {
      drawSearchHighlights(painter, buffer->lineAt(startLine + row), row);
    }

    // Draw per-cell text
    for (int col = 0; col < cols; ++col) {
      const TerminalBuffer::Cell cell =
          buffer->cellAtVisible(row, col, m_scrollOffset);
      if (cell.ch == QLatin1Char(' ') && !cell.underline && !cell.strikethrough) {
        continue;
      }
      int x = col * m_cellWidth;

      QFont cellFont = m_font;
      if (cell.bold) {
        cellFont.setBold(true);
      }
      if (cell.italic) {
        cellFont.setItalic(true);
      }
      painter.setFont(cellFont);

      if (cell.ch != QLatin1Char(' ')) {
        painter.setPen(cell.fg);
        painter.drawText(x, y, QString(cell.ch));
      }

      if (cell.underline) {
        painter.setPen(cell.fg);
        int underY = row * m_cellHeight + m_cellHeight - 2;
        painter.drawLine(x, underY, x + m_cellWidth, underY);
      }
      if (cell.strikethrough) {
        painter.setPen(cell.fg);
        int strikeY = row * m_cellHeight + m_cellHeight / 2;
        painter.drawLine(x, strikeY, x + m_cellWidth, strikeY);
      }
    }
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

void TerminalView::keyPressEvent(QKeyEvent *event) {
  if (!m_session) {
    return;
  }

  if ((event->modifiers() & Qt::ControlModifier) &&
      (event->modifiers() & Qt::ShiftModifier)) {
    if (event->key() == Qt::Key_C) {
      copySelection();
      return;
    }
    if (event->key() == Qt::Key_V) {
      pasteClipboard();
      return;
    }
  }

  QByteArray sequence = keyToSequence(event);
  if (!sequence.isEmpty()) {
    m_session->sendInput(sequence);
  }
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event) {
  if (!m_session) {
    return;
  }
  const QString text = event->commitString();
  if (!text.isEmpty()) {
    m_session->sendInput(text.toLocal8Bit());
  }
}

void TerminalView::focusInEvent(QFocusEvent *event) {
  emit focused(this);
  TerminalViewBase::focusInEvent(event);
}

void TerminalView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_selecting = true;
    m_selectStart = cellFromPoint(event->pos());
    m_selectEnd = m_selectStart;
    update();
  }
}

void TerminalView::mouseMoveEvent(QMouseEvent *event) {
  if (m_selecting) {
    updateSelection(event->pos());
  }
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_selecting) {
    updateSelection(event->pos());
    m_selecting = false;
  }
}

void TerminalView::wheelEvent(QWheelEvent *event) {
  if (!m_session || !m_session->buffer()) {
    return;
  }

  const int delta = event->angleDelta().y() / 120;
  if (delta == 0) {
    return;
  }

  int maxOffset = qMax(0, m_session->buffer()->totalLines() -
                              m_session->buffer()->rows());
  m_scrollOffset = qBound(0, m_scrollOffset - delta * 3, maxOffset);
  m_userScroll = (m_scrollOffset != 0);
  update();
}

void TerminalView::updateMetrics() {
  QFontMetrics metrics(m_font);
  m_cellWidth = qMax(1, metrics.horizontalAdvance(QLatin1Char('M')));
  m_cellHeight = qMax(1, metrics.height());
}

void TerminalView::updateSelection(const QPoint &pos) {
  m_selectEnd = cellFromPoint(pos);
  update();
}

bool TerminalView::hasSelection() const {
  return m_selectStart.row != m_selectEnd.row ||
         m_selectStart.column != m_selectEnd.column;
}

QString TerminalView::selectedText() const {
  if (!hasSelection() || !m_session || !m_session->buffer()) {
    return QString();
  }

  QStringList lines = m_session->buffer()->snapshot(m_scrollOffset);
  CellPos start = m_selectStart;
  CellPos end = m_selectEnd;
  if (start.row > end.row ||
      (start.row == end.row && start.column > end.column)) {
    qSwap(start, end);
  }

  QStringList selected;
  for (int row = start.row; row <= end.row && row < lines.size(); ++row) {
    const QString &line = lines[row];
    int startCol = (row == start.row) ? start.column : 0;
    int endCol = (row == end.row) ? end.column : line.size();
    selected.push_back(line.mid(startCol, endCol - startCol));
  }
  return selected.join(QLatin1Char('\n'));
}

void TerminalView::clearSelection() {
  m_selectStart = {};
  m_selectEnd = {};
  m_selecting = false;
}

TerminalView::CellPos TerminalView::cellFromPoint(const QPoint &pos) const {
  CellPos cell;
  cell.column = qBound(0, pos.x() / m_cellWidth,
                       m_session->buffer()->columns() - 1);
  cell.row = qBound(0, pos.y() / m_cellHeight,
                    m_session->buffer()->rows() - 1);
  return cell;
}

QByteArray TerminalView::keyToSequence(QKeyEvent *event) const {
  switch (event->key()) {
    case Qt::Key_Backspace:
      return QByteArray("\x7f");
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return QByteArray("\r");
    case Qt::Key_Tab:
      return QByteArray("\t");
    case Qt::Key_Left:
      return QByteArray("\x1b[D");
    case Qt::Key_Right:
      return QByteArray("\x1b[C");
    case Qt::Key_Up:
      return QByteArray("\x1b[A");
    case Qt::Key_Down:
      return QByteArray("\x1b[B");
    case Qt::Key_PageUp:
      return QByteArray("\x1b[5~");
    case Qt::Key_PageDown:
      return QByteArray("\x1b[6~");
    case Qt::Key_Home:
      return QByteArray("\x1b[H");
    case Qt::Key_End:
      return QByteArray("\x1b[F");
    default:
      break;
  }

  const QString text = event->text();
  if (!text.isEmpty() && !event->text().at(0).isNull()) {
    return text.toLocal8Bit();
  }

  return QByteArray();
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
