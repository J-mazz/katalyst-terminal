#include "QtShim.h"
import std;

TerminalViewCommon::TerminalViewCommon(TerminalSession *session, TerminalConfig *config, QWidget *parent)
    : TerminalViewBase(parent), m_session(session), m_config(config) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);
}

void TerminalViewCommon::copySelection() {
  if (hasSelection()) {
    QApplication::clipboard()->setText(selectedText());
  }
}

void TerminalViewCommon::pasteClipboard() {
  if (!m_session) {
    return;
  }
  QByteArray text = QApplication::clipboard()->text().toLocal8Bit();
  m_session->sendInput(text);
}

bool TerminalViewCommon::hasSelection() const {
  return m_selectStart.row != m_selectEnd.row ||
         m_selectStart.column != m_selectEnd.column;
}

QString TerminalViewCommon::selectedText() const {
  if (!m_session || !m_session->buffer() || !hasSelection()) return QString();

  const QStringList lines = m_session->buffer()->snapshot(m_scrollOffset);
  CellPos start = m_selectStart;
  CellPos end = m_selectEnd;
  if (isSelectionReversed(start, end)) qSwap(start, end);

  QStringList selected;
  for (int row = start.row; row <= end.row && row < lines.size(); ++row) {
    const QString &line = lines[row];
    const int startCol = (row == start.row) ? start.column : 0;
    const int endCol   = (row == end.row)   ? end.column   : line.size();
    selected.push_back(line.mid(startCol, endCol - startCol));
  }
  return selected.join(QLatin1Char('\n'));
}

void TerminalViewCommon::clearSelection() {
  m_selectStart = {};
  m_selectEnd = {};
  m_selecting = false;
}

void TerminalViewCommon::keyPressEvent(QKeyEvent *event) {
  if (!m_session) return;

  const bool ctrlShift = (event->modifiers() & Qt::ControlModifier) &&
                         (event->modifiers() & Qt::ShiftModifier);
  if (ctrlShift) {
    if (event->key() == Qt::Key_C) { copySelection(); return; }
    if (event->key() == Qt::Key_V) { pasteClipboard(); return; }
  }

  const QByteArray sequence = keyToSequence(event);
  if (!sequence.isEmpty()) m_session->sendInput(sequence);
}

QByteArray TerminalViewCommon::keyToSequence(QKeyEvent *event) const {
  static const std::pair<int, const char *> keyTable[] = {
    { Qt::Key_Backspace, "\x7f"    },
    { Qt::Key_Return,    "\r"      },
    { Qt::Key_Enter,     "\r"      },
    { Qt::Key_Tab,       "\t"      },
    { Qt::Key_Left,      "\x1b[D" },
    { Qt::Key_Right,     "\x1b[C" },
    { Qt::Key_Up,        "\x1b[A" },
    { Qt::Key_Down,      "\x1b[B" },
    { Qt::Key_PageUp,    "\x1b[5~"},
    { Qt::Key_PageDown,  "\x1b[6~"},
    { Qt::Key_Home,      "\x1b[H" },
    { Qt::Key_End,       "\x1b[F" },
  };
  for (const auto &[key, seq] : keyTable) {
    if (event->key() == key) return QByteArray(seq);
  }
  const QString text = event->text();
  if (!text.isEmpty() && !text.at(0).isNull()) return text.toLocal8Bit();
  return QByteArray();
}

void TerminalViewCommon::inputMethodEvent(QInputMethodEvent *event) {
  if (!m_session) {
    return;
  }
  const QString text = event->commitString();
  if (!text.isEmpty()) {
    m_session->sendInput(text.toLocal8Bit());
  }
}

void TerminalViewCommon::focusInEvent(QFocusEvent *event) {
  emit focused(this);
  TerminalViewBase::focusInEvent(event);
}

void TerminalViewCommon::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_selecting = true;
    m_selectStart = cellFromPoint(event->pos());
    m_selectEnd = m_selectStart;
    update();
  }
}

void TerminalViewCommon::mouseMoveEvent(QMouseEvent *event) {
  if (m_selecting) {
    updateSelection(event->pos());
  }
}

void TerminalViewCommon::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_selecting) {
    updateSelection(event->pos());
    m_selecting = false;
  }
}

void TerminalViewCommon::wheelEvent(QWheelEvent *event) {
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

TerminalViewCommon::CellPos TerminalViewCommon::cellFromPoint(const QPoint &pos) const {
  if (!m_session || !m_session->buffer()) return {};
  CellPos cell;
  cell.column = qBound(0, pos.x() / m_cellWidth,
                       m_session->buffer()->columns() - 1);
  cell.row    = qBound(0, pos.y() / m_cellHeight,
                       m_session->buffer()->rows() - 1);
  return cell;
}

void TerminalViewCommon::updateSelection(const QPoint &pos) {
  m_selectEnd = cellFromPoint(pos);
  update();
}

bool TerminalViewCommon::isSelectionReversed(const CellPos &start, const CellPos &end) const {
  return start.row > end.row ||
         (start.row == end.row && start.column > end.column);
}