#include "QtShim.h"
import std;

VulkanTerminalView::VulkanTerminalView(TerminalSession *session,
                                       TerminalConfig *config, QWidget *parent)
    : TerminalViewBase(parent), m_session(session), m_config(config) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);

  const auto profile = m_config->defaultProfile();

  {
    const QFontMetrics metrics(profile.font);
    m_cellWidth  = qMax(1, metrics.horizontalAdvance(QLatin1Char('M')));
    m_cellHeight = qMax(1, metrics.height());
  }

  m_instance.setApiVersion(QVersionNumber(1, 2, 0));
  if (!m_instance.create()) {
    return;
  }

  m_window = new VulkanTerminalWindow();
  m_window->setVulkanInstance(&m_instance);
  m_window->create();

  m_renderer = new VulkanRenderer();
  if (!m_renderer->initialize(&m_instance, m_window, profile, profile.font)) {
    delete m_renderer;
    m_renderer = nullptr;
    delete m_window;
    m_window = nullptr;
    return;
  }
  m_window->setRenderer(m_renderer);

  m_container = QWidget::createWindowContainer(m_window, this);
  m_container->setFocusPolicy(Qt::NoFocus);
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_container);

connectVulkanSignals();
}

void VulkanTerminalView::connectVulkanSignals() {
    connect(m_window, &VulkanTerminalWindow::keyInput,       this, &VulkanTerminalView::keyPressEvent);
    connect(m_window, &VulkanTerminalWindow::mousePressed,   this, &VulkanTerminalView::mousePressEvent);
    connect(m_window, &VulkanTerminalWindow::mouseMoved,     this, &VulkanTerminalView::mouseMoveEvent);
    connect(m_window, &VulkanTerminalWindow::mouseReleased,  this, &VulkanTerminalView::mouseReleaseEvent);
    connect(m_window, &VulkanTerminalWindow::wheelScrolled,  this, &VulkanTerminalView::wheelEvent);
    connect(m_window, &VulkanTerminalWindow::windowFocused,  this, [this]() { emit focused(this); });
    connect(m_session, &TerminalSession::screenUpdated,      this, &VulkanTerminalView::updateFrame);
}

void VulkanTerminalView::setSearchTerm(const QString &) {}

bool VulkanTerminalView::findNext(bool) {
  return false;
}

void VulkanTerminalView::copySelection() {
  if (!hasSelection()) {
    return;
  }
  QApplication::clipboard()->setText(selectedText());
}

void VulkanTerminalView::pasteClipboard() {
  if (!m_session) {
    return;
  }
  QByteArray text = QApplication::clipboard()->text().toLocal8Bit();
  m_session->sendInput(text);
}

TerminalSession *VulkanTerminalView::session() const {
  return m_session;
}

void VulkanTerminalView::keyPressEvent(QKeyEvent *event) {
  if (!m_session) {
    return;
  }

  if ((event->modifiers() & Qt::ControlModifier) &&
      (event->modifiers() & Qt::ShiftModifier)) {
    if (event->key() == Qt::Key_C) { copySelection(); return; }
    if (event->key() == Qt::Key_V) { pasteClipboard(); return; }
  }

  const QString text = event->text();
  if (!text.isEmpty()) {
    m_session->sendInput(text.toLocal8Bit());
  }
}

void VulkanTerminalView::inputMethodEvent(QInputMethodEvent *event) {
  if (!m_session) {
    return;
  }
  const QString text = event->commitString();
  if (!text.isEmpty()) {
    m_session->sendInput(text.toLocal8Bit());
  }
}

void VulkanTerminalView::wheelEvent(QWheelEvent *event) {
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
  updateFrame();
}

void VulkanTerminalView::focusInEvent(QFocusEvent *event) {
  emit focused(this);
  TerminalViewBase::focusInEvent(event);
}

void VulkanTerminalView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_selecting = true;
    m_selectStart = cellFromPoint(event->pos());
    m_selectEnd = m_selectStart;
    updateFrame();
  }
}

void VulkanTerminalView::mouseMoveEvent(QMouseEvent *event) {
  if (m_selecting) {
    updateSelection(event->pos());
  }
}

void VulkanTerminalView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_selecting) {
    updateSelection(event->pos());
    m_selecting = false;
  }
}

void VulkanTerminalView::updateFrame() {
  if (!m_renderer || !m_renderer->isReady()) {
    return;
  }
  if (!m_session || !m_session->buffer()) {
    return;
  }

  if (!m_userScroll) {
    m_scrollOffset = 0;
  }

  VulkanRenderer::Selection selection{};
  if (hasSelection()) {
    selection.active = true;
    selection.startRow = m_selectStart.row;
    selection.startCol = m_selectStart.column;
    selection.endRow = m_selectEnd.row;
    selection.endCol = m_selectEnd.column;
  }

  m_renderer->updateFromBuffer(m_session->buffer(), m_scrollOffset, selection);
  m_renderer->render();
}

VulkanTerminalView::CellPos VulkanTerminalView::cellFromPoint(
    const QPoint &pos) const {
  CellPos cell;
  int column = qMax(0, pos.x() / m_cellWidth);
  int row    = qMax(0, pos.y() / m_cellHeight);

  if (m_session && m_session->buffer()) {
    column = qBound(0, column, m_session->buffer()->columns() - 1);
    row    = qBound(0, row,    m_session->buffer()->rows() - 1);
  }

  cell.column = column;
  cell.row    = row;
  return cell;
}

void VulkanTerminalView::updateSelection(const QPoint &pos) {
  m_selectEnd = cellFromPoint(pos);
  updateFrame();
}

bool VulkanTerminalView::hasSelection() const {
  return m_selectStart.row != m_selectEnd.row ||
         m_selectStart.column != m_selectEnd.column;
}

bool VulkanTerminalView::isSelectionReversed(const CellPos &start, const CellPos &end) const {
  return start.row > end.row || (start.row == end.row && start.column > end.column);
}

QString VulkanTerminalView::selectedRow(const QStringList &lines, int row,
                                         const CellPos &start, const CellPos &end) const {
  const QString &line = lines[row];
  const int startCol = (row == start.row) ? start.column : 0;
  const int endCol   = (row == end.row)   ? end.column   : line.size();
  return line.mid(startCol, endCol - startCol);
}

QString VulkanTerminalView::selectedText() const {
  if (!m_session || !m_session->buffer() || !hasSelection()) return QString();

  const QStringList lines = m_session->buffer()->snapshot(m_scrollOffset);
  CellPos start = m_selectStart;
  CellPos end = m_selectEnd;
  if (isSelectionReversed(start, end)) qSwap(start, end);

  QStringList selected;
  for (int row = start.row; row <= end.row && row < lines.size(); ++row)
    selected.push_back(selectedRow(lines, row, start, end));
  return selected.join(QLatin1Char('\n'));
}
