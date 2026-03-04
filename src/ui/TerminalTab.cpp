#include "QtShim.h"
import std;

TerminalTab::TerminalTab(TerminalConfig *config, QWidget *parent)
    : QWidget(parent), m_config(config) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_root = createView();
  layout->addWidget(m_root);
}

TerminalViewBase *TerminalTab::activeView() const {
  return m_activeView;
}

void TerminalTab::insertNewSplitter(QSplitter *splitter, TerminalViewBase *newView,
                                    QSplitter *parentSplitter) {
  if (parentSplitter) {
    int index = parentSplitter->indexOf(m_activeView);
    m_activeView->setParent(splitter);
    splitter->addWidget(m_activeView);
    splitter->addWidget(newView);
    parentSplitter->insertWidget(index, splitter);
  } else {
    layout()->removeWidget(m_root);
    m_root->setParent(nullptr);
    splitter->addWidget(m_root);
    splitter->addWidget(newView);
    m_root = splitter;
    layout()->addWidget(m_root);
  }
}

void TerminalTab::split(Qt::Orientation orientation) {
  if (!m_activeView) return;

  TerminalViewBase *newView = createView();
  QSplitter *parentSplitter = splitterForView(m_activeView);

  if (parentSplitter && parentSplitter->orientation() == orientation) {
    int index = parentSplitter->indexOf(m_activeView);
    parentSplitter->insertWidget(index + 1, newView);
    setActiveView(newView);
    return;
  }

  auto *splitter = new QSplitter(orientation, this);
  insertNewSplitter(splitter, newView, parentSplitter);
  setActiveView(newView);
}

void TerminalTab::closeActiveSplit() {
  if (!m_activeView || m_views.size() <= 1) {
    return;
  }

  TerminalViewBase *toRemove = m_activeView;
  QSplitter *parentSplitter = splitterForView(toRemove);
  TerminalViewBase *nextActive = nullptr;
  for (TerminalViewBase *view : m_views) {
    if (view != toRemove) {
      nextActive = view;
      break;
    }
  }

  m_views.removeOne(toRemove);
  toRemove->deleteLater();

  if (parentSplitter) {
    cleanupSplitter(parentSplitter);
  }
  if (nextActive) {
    setActiveView(nextActive);
  }
}

void TerminalTab::setSearchTerm(const QString &term) {
  for (TerminalViewBase *view : m_views) {
    view->setSearchTerm(term);
  }
}

bool TerminalTab::findNext(bool forward) {
  if (!m_activeView) {
    return false;
  }
  return m_activeView->findNext(forward);
}

QString TerminalTab::tabTitle() const {
  return m_title.isEmpty() ? QStringLiteral("Shell") : m_title;
}

TerminalViewBase *TerminalTab::createView() {
  const auto profile = m_config->defaultProfile();
  auto *session = new TerminalSession(profile, this);
  TerminalViewBase *view = nullptr;
  if (m_config->renderer() == QStringLiteral("Vulkan")) {
    auto *vkView = new VulkanTerminalView(session, m_config, this);
    if (vkView->isInitialized()) {
      view = vkView;
    } else {
      qWarning("TerminalTab: Vulkan unavailable, falling back to raster view");
      delete vkView;
      view = new TerminalView(session, m_config, this);
    }
  } else {
    view = new TerminalView(session, m_config, this);
  }
  session->setParent(view);
  session->startShell();

  connect(view, &TerminalViewBase::focused, this,
          [this](TerminalViewBase *focused) {
    setActiveView(focused);
  });

  connect(session, &TerminalSession::titleChanged, this,
          [this](const QString &title) {
    m_title = title;
    emit titleChanged();
  });

  m_views.push_back(view);
  if (!m_activeView) {
    setActiveView(view);
  }

  return view;
}

void TerminalTab::setActiveView(TerminalViewBase *view) {
  if (m_activeView == view) {
    return;
  }
  m_activeView = view;
  emit activeViewChanged(view);
}

void TerminalTab::replaceSplitterWithChild(QSplitter *splitter, QWidget *remaining,
                                           QSplitter *parentSplitter, int parentIndex) {
  remaining->setParent(nullptr);
  splitter->deleteLater();
  if (parentSplitter) {
    parentSplitter->insertWidget(parentIndex, remaining);
  } else {
    layout()->removeWidget(m_root);
    m_root = remaining;
    layout()->addWidget(m_root);
  }
}

void TerminalTab::cleanupSplitter(QSplitter *splitter) {
  if (!splitter) return;

  if (splitter->count() > 1) {
    if (!m_activeView && !m_views.isEmpty()) setActiveView(m_views.first());
    return;
  }

  QWidget *remaining = splitter->widget(0);
  QSplitter *parentSplitter = qobject_cast<QSplitter *>(splitter->parentWidget());
  int parentIndex = parentSplitter ? parentSplitter->indexOf(splitter) : -1;

  replaceSplitterWithChild(splitter, remaining, parentSplitter, parentIndex);

  TerminalViewBase *nextView = findFirstView(remaining);
  if (nextView) setActiveView(nextView);
}

TerminalViewBase *TerminalTab::findFirstView(QWidget *root) const {
  if (!root) {
    return nullptr;
  }
  if (auto *view = qobject_cast<TerminalViewBase *>(root)) {
    return view;
  }
  const auto children = root->findChildren<TerminalViewBase *>();
  if (!children.isEmpty()) {
    return children.first();
  }
  return nullptr;
}

QSplitter *TerminalTab::splitterForView(TerminalViewBase *view) const {
  return qobject_cast<QSplitter *>(view->parentWidget());
}
