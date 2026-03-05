#include "QtShim.h"
import std;

VulkanTerminalView::VulkanTerminalView(TerminalSession *session,
                                       TerminalConfig *config, QWidget *parent)
    : TerminalViewCommon(session, config, parent) {

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
  m_instanceCreated = true;

  m_window = new VulkanTerminalWindow();
  m_window->setVulkanInstance(&m_instance);

  m_container = QWidget::createWindowContainer(m_window, this);
  m_container->setFocusPolicy(Qt::StrongFocus);
  setFocusProxy(m_container);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_container);

  connect(m_window, &VulkanTerminalWindow::readyForInit,
          this, &VulkanTerminalView::initRenderer);
  connectVulkanSignals();
}

bool VulkanTerminalView::isInitialized() const {
  return m_instanceCreated && m_window && m_container;
}

void VulkanTerminalView::initRenderer() {
  if (m_renderer) {
    return;
  }

  const auto profile = m_config->defaultProfile();
  m_renderer = new VulkanRenderer();
  if (!m_renderer->initialize(&m_instance, m_window, profile, profile.font)) {
    qWarning("VulkanTerminalView: Vulkan renderer initialization failed");
    delete m_renderer;
    m_renderer = nullptr;
    return;
  }
  m_window->setRenderer(m_renderer);
  updateFrame();
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

void VulkanTerminalView::requestRepaint() {
  updateFrame();
}

void VulkanTerminalView::resizeEvent(QResizeEvent *event) {
  if (!m_session || !m_session->buffer()) return;
  if (width() <= 0 || height() <= 0) return;
  int columns = qMax(1, width() / m_cellWidth);
  int rows = qMax(1, height() / m_cellHeight);
  m_session->resize(columns, rows);
}

void VulkanTerminalView::focusInEvent(QFocusEvent *event) {
  emit focused(this);
  TerminalViewBase::focusInEvent(event);
}

void VulkanTerminalView::showEvent(QShowEvent *event) {
  TerminalViewBase::showEvent(event);
  if (width() > 0 && height() > 0) {
    int columns = qMax(1, width() / m_cellWidth);
    int rows = qMax(1, height() / m_cellHeight);
    if (m_session && m_session->buffer()) {
      m_session->resize(columns, rows);
    }
  }
}

void VulkanTerminalView::updateFrame() {
  if (!m_renderer || !m_renderer->isReady()) {
    return;
  }
  if (!m_session || !m_session->buffer()) {
    return;
  }

  // First frame: ensure session knows terminal dimensions.
  // Set flag BEFORE calling resize() to prevent infinite recursion:
  // resize() emits screenUpdated → updateFrame() re-enters here.
  if (!m_firstFrameResized && width() > 0 && height() > 0) {
    m_firstFrameResized = true;
    int columns = qMax(1, width() / m_cellWidth);
    int rows = qMax(1, height() / m_cellHeight);
    m_session->resize(columns, rows);
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
