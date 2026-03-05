#include "QtShim.h"
import std;

VulkanTerminalWindow::VulkanTerminalWindow() {
  setSurfaceType(QSurface::VulkanSurface);
}

void VulkanTerminalWindow::setVulkanInstance(QVulkanInstance *instance) {
  m_instance = instance;
  if (m_instance) {
    QWindow::setVulkanInstance(m_instance);
  }
}

void VulkanTerminalWindow::setRenderer(VulkanRenderer *renderer) {
  m_renderer = renderer;
}

void VulkanTerminalWindow::exposeEvent(QExposeEvent *) {
  if (!isExposed()) {
    return;
  }
  if (!m_exposed) {
    m_exposed = true;
    emit readyForInit();
  }
  if (m_renderer && m_renderer->isReady()) {
    m_renderer->render();
  }
}

void VulkanTerminalWindow::resizeEvent(QResizeEvent *) {
  if (!m_exposed || !m_renderer || !m_renderer->isReady()) {
    return;
  }
  m_renderer->resize(width(), height());
}

void VulkanTerminalWindow::keyPressEvent(QKeyEvent *event) {
  emit keyInput(event);
}

void VulkanTerminalWindow::mousePressEvent(QMouseEvent *event) {
  requestActivate();
  emit mousePressed(event);
}

void VulkanTerminalWindow::mouseMoveEvent(QMouseEvent *event) {
  emit mouseMoved(event);
}

void VulkanTerminalWindow::mouseReleaseEvent(QMouseEvent *event) {
  emit mouseReleased(event);
}

void VulkanTerminalWindow::wheelEvent(QWheelEvent *event) {
  emit wheelScrolled(event);
}

void VulkanTerminalWindow::focusInEvent(QFocusEvent *) {
  emit windowFocused();
}
