#include "QtShim.h"
import std;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Katalyst Terminal"));
  resize(960, 640);
  setupUi();
}

MainWindow::~MainWindow() {
  delete m_config;
}

void MainWindow::openTab() {
  newTab();
}

void MainWindow::setupUi() {
  m_config = new TerminalConfig();

  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *header = new QWidget(central);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(8, 4, 8, 4);
  m_newTabButton = new QPushButton(tr("New Tab"), header);
  headerLayout->addWidget(m_newTabButton);
  headerLayout->addStretch();
  layout->addWidget(header);

  m_tabs = new QTabWidget(central);
  m_tabs->setTabsClosable(true);
  m_tabs->setMovable(true);
  layout->addWidget(m_tabs);

  setCentralWidget(central);
  setupActions();
  connect(m_newTabButton, &QPushButton::clicked, this, &MainWindow::newTab);

  newTab();

  connect(m_tabs, &QTabWidget::tabCloseRequested, this,
          [this](int index) {
            if (m_tabs->count() > 1) {
              QWidget *widget = m_tabs->widget(index);
              m_tabs->removeTab(index);
              widget->deleteLater();
            }
          });
  connect(m_tabs, &QTabWidget::currentChanged, this,
          &MainWindow::updateTabTitle);
}

void MainWindow::setupActions() {
  menuBar()->clear();
  QMenu *settingsMenu = menuBar()->addMenu(tr("Settings"));

  m_newTabAction = new QAction(tr("New Tab"), this);
  m_newTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
  m_newTabAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_newTabAction);
  connect(m_newTabAction, &QAction::triggered, this, &MainWindow::newTab);

  m_closeTabAction = new QAction(tr("Close Tab"), this);
  m_closeTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
  m_closeTabAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_closeTabAction);
  connect(m_closeTabAction, &QAction::triggered, this, &MainWindow::closeTab);

  m_splitHorizontalAction = new QAction(tr("Split Horizontally"), this);
  m_splitHorizontalAction->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+H")));
  m_splitHorizontalAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_splitHorizontalAction);
  connect(m_splitHorizontalAction, &QAction::triggered, this,
          &MainWindow::splitHorizontal);

  m_splitVerticalAction = new QAction(tr("Split Vertically"), this);
  m_splitVerticalAction->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Alt+V")));
  m_splitVerticalAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_splitVerticalAction);
  connect(m_splitVerticalAction, &QAction::triggered, this,
          &MainWindow::splitVertical);

  m_closeSplitAction = new QAction(tr("Close Split"), this);
  m_closeSplitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Q")));
  m_closeSplitAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_closeSplitAction);
  connect(m_closeSplitAction, &QAction::triggered, this,
          &MainWindow::closeSplit);

  m_copyAction = new QAction(tr("Copy"), this);
  m_copyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
  m_copyAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_copyAction);
  connect(m_copyAction, &QAction::triggered, this, [this]() {
    if (auto *view = activeView()) {
      view->copySelection();
    }
  });

  m_pasteAction = new QAction(tr("Paste"), this);
  m_pasteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
  m_pasteAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(m_pasteAction);
  connect(m_pasteAction, &QAction::triggered, this, [this]() {
    if (auto *view = activeView()) {
      view->pasteClipboard();
    }
  });

  QAction *shortcutAction = settingsMenu->addAction(tr("Keyboard Shortcuts..."));
  connect(shortcutAction, &QAction::triggered, this,
          &MainWindow::configureShortcuts);

  KConfig config(QStringLiteral("katalyst-terminalrc"));
  KConfigGroup shortcuts(&config, QStringLiteral("Shortcuts"));
  auto applyShortcut = [&shortcuts](QAction *action, const QString &key,
                                    const QKeySequence &fallback) {
    const QString value = shortcuts.readEntry(
        key, fallback.toString(QKeySequence::PortableText));
    action->setShortcut(QKeySequence::fromString(
        value, QKeySequence::PortableText));
  };

  applyShortcut(m_newTabAction, QStringLiteral("NewTab"),
                m_newTabAction->shortcut());
  applyShortcut(m_closeTabAction, QStringLiteral("CloseTab"),
                m_closeTabAction->shortcut());
  applyShortcut(m_splitHorizontalAction, QStringLiteral("SplitHorizontal"),
                m_splitHorizontalAction->shortcut());
  applyShortcut(m_splitVerticalAction, QStringLiteral("SplitVertical"),
                m_splitVerticalAction->shortcut());
  applyShortcut(m_closeSplitAction, QStringLiteral("CloseSplit"),
                m_closeSplitAction->shortcut());
  applyShortcut(m_copyAction, QStringLiteral("Copy"), m_copyAction->shortcut());
  applyShortcut(m_pasteAction, QStringLiteral("Paste"),
                m_pasteAction->shortcut());
}

TerminalTab *MainWindow::currentTab() const {
  return qobject_cast<TerminalTab *>(m_tabs->currentWidget());
}

TerminalViewBase *MainWindow::activeView() const {
  TerminalTab *tab = currentTab();
  return tab ? tab->activeView() : nullptr;
}

void MainWindow::newTab() {
  auto *tab = new TerminalTab(m_config, this);
  int index = m_tabs->addTab(tab, tab->tabTitle());
  m_tabs->setCurrentIndex(index);

  connect(tab, &TerminalTab::activeViewChanged, this,
          [this](TerminalViewBase *) {
    updateTabTitle(m_tabs->currentIndex());
  });
  connect(tab, &TerminalTab::titleChanged, this, [this, tab]() {
    int idx = m_tabs->indexOf(tab);
    if (idx >= 0) {
      m_tabs->setTabText(idx, tab->tabTitle());
    }
  });
}

void MainWindow::closeTab() {
  int index = m_tabs->currentIndex();
  if (index >= 0 && m_tabs->count() > 1) {
    QWidget *widget = m_tabs->widget(index);
    m_tabs->removeTab(index);
    widget->deleteLater();
  }
}

void MainWindow::splitHorizontal() {
  if (auto *tab = currentTab()) {
    tab->split(Qt::Horizontal);
  }
}

void MainWindow::splitVertical() {
  if (auto *tab = currentTab()) {
    tab->split(Qt::Vertical);
  }
}

void MainWindow::closeSplit() {
  if (auto *tab = currentTab()) {
    tab->closeActiveSplit();
  }
}

void MainWindow::configureShortcuts() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Keyboard Shortcuts"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout();

  struct ShortcutField {
    QString key;
    QAction *action;
    QKeySequenceEdit *editor;
  };

  std::vector<ShortcutField> fields;
  auto addField = [&](const QString &label, const QString &key, QAction *action) {
    auto *editor = new QKeySequenceEdit(action->shortcut(), &dialog);
    form->addRow(label, editor);
    fields.push_back({key, action, editor});
  };

  addField(tr("New Tab"), QStringLiteral("NewTab"), m_newTabAction);
  addField(tr("Close Tab"), QStringLiteral("CloseTab"), m_closeTabAction);
  addField(tr("Split Horizontally"), QStringLiteral("SplitHorizontal"),
           m_splitHorizontalAction);
  addField(tr("Split Vertically"), QStringLiteral("SplitVertical"),
           m_splitVerticalAction);
  addField(tr("Close Split"), QStringLiteral("CloseSplit"), m_closeSplitAction);
  addField(tr("Copy"), QStringLiteral("Copy"), m_copyAction);
  addField(tr("Paste"), QStringLiteral("Paste"), m_pasteAction);

  layout->addLayout(form);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
  layout->addWidget(buttonBox);
  connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  KConfig config(QStringLiteral("katalyst-terminalrc"));
  KConfigGroup shortcuts(&config, QStringLiteral("Shortcuts"));
  for (const ShortcutField &field : fields) {
    const QKeySequence sequence = field.editor->keySequence();
    field.action->setShortcut(sequence);
    shortcuts.writeEntry(field.key,
                         sequence.toString(QKeySequence::PortableText));
  }
  config.sync();
}

void MainWindow::updateTabTitle(int index) {
  if (index < 0) {
    return;
  }
  if (auto *tab = qobject_cast<TerminalTab *>(m_tabs->widget(index))) {
    m_tabs->setTabText(index, tab->tabTitle());
  }
}
