#include "QtShim.h"
import std;

namespace {
class ProfileEditorDialog final : public QDialog {
public:
  ProfileEditorDialog(const QList<TerminalConfig::TerminalProfile> &profiles,
                      const QString &defaultName, QWidget *parent)
      : QDialog(parent), m_profiles(profiles), m_defaultName(defaultName) {
    setWindowTitle(tr("Terminal Profiles"));
    resize(780, 480);

    auto *root = new QVBoxLayout(this);
    auto *content = new QHBoxLayout();
    root->addLayout(content, 1);

    auto *sidebar = new QVBoxLayout();
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(180);
    for (const auto &profile : m_profiles) {
      m_list->addItem(profile.name);
    }
    sidebar->addWidget(m_list, 1);

    auto *listButtons = new QHBoxLayout();
    auto *addButton = new QPushButton(tr("Add"), this);
    auto *duplicateButton = new QPushButton(tr("Duplicate"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    listButtons->addWidget(addButton);
    listButtons->addWidget(duplicateButton);
    listButtons->addWidget(m_removeButton);
    sidebar->addLayout(listButtons);

    auto *defaultButton = new QPushButton(tr("Set as Default"), this);
    m_defaultLabel = new QLabel(this);
    sidebar->addWidget(defaultButton);
    sidebar->addWidget(m_defaultLabel);
    content->addLayout(sidebar);

    auto *form = new QFormLayout();
    m_name = new QLineEdit(this);
    m_program = new QLineEdit(this);
    m_program->setPlaceholderText(tr("Use the login shell when empty"));
    m_arguments = new QLineEdit(this);
    m_arguments->setPlaceholderText(tr("Shell-style arguments"));
    m_environment = new QLineEdit(this);
    m_environment->setPlaceholderText(tr("KEY=value; OTHER=value"));
    m_term = new QLineEdit(this);
    m_scrollback = new QSpinBox(this);
    m_scrollback->setRange(100, 1000000);
    m_scrollback->setSingleStep(1000);
    m_fontButton = new QPushButton(this);
    m_backgroundButton = new QPushButton(this);
    m_foregroundButton = new QPushButton(this);
    m_cursorButton = new QPushButton(this);

    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Program"), m_program);
    form->addRow(tr("Arguments"), m_arguments);
    form->addRow(tr("Environment"), m_environment);
    form->addRow(tr("TERM"), m_term);
    form->addRow(tr("Scrollback lines"), m_scrollback);
    form->addRow(tr("Font"), m_fontButton);
    form->addRow(tr("Background"), m_backgroundButton);
    form->addRow(tr("Text"), m_foregroundButton);
    form->addRow(tr("Cursor"), m_cursorButton);
    content->addLayout(form, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                         QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int row) {
              if (m_loading) return;
              storeCurrent();
              loadProfile(row);
            });
    connect(addButton, &QPushButton::clicked, this, [this]() {
      storeCurrent();
      auto profile = m_profiles.isEmpty()
                         ? TerminalConfig::TerminalProfile{}
                         : m_profiles.first();
      profile.name = uniqueName(tr("New Profile"));
      m_profiles.push_back(profile);
      m_list->addItem(profile.name);
      m_list->setCurrentRow(m_profiles.size() - 1);
    });
    connect(duplicateButton, &QPushButton::clicked, this, [this]() {
      storeCurrent();
      if (m_currentRow < 0 || m_currentRow >= m_profiles.size()) return;
      auto profile = m_profiles[m_currentRow];
      profile.name = uniqueName(profile.name + tr(" Copy"));
      m_profiles.push_back(profile);
      m_list->addItem(profile.name);
      m_list->setCurrentRow(m_profiles.size() - 1);
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
      if (m_profiles.size() <= 1 || m_currentRow < 0) return;
      const QString removedName = m_profiles[m_currentRow].name;
      const int nextRow = qMin(m_currentRow, m_profiles.size() - 2);
      m_loading = true;
      delete m_list->takeItem(m_currentRow);
      m_profiles.removeAt(m_currentRow);
      m_currentRow = -1;
      m_loading = false;
      if (m_defaultName == removedName) m_defaultName = m_profiles.first().name;
      m_list->setCurrentRow(nextRow);
      updateDefaultLabel();
    });
    connect(defaultButton, &QPushButton::clicked, this, [this]() {
      storeCurrent();
      if (m_currentRow >= 0) {
        m_defaultName = m_profiles[m_currentRow].name;
        updateDefaultLabel();
      }
    });
    connect(m_fontButton, &QPushButton::clicked, this, [this]() {
      bool accepted = false;
      const QFont current = m_fontButton->property("profileFont").value<QFont>();
      const QFont selected = QFontDialog::getFont(&accepted, current, this,
                                                   tr("Choose Terminal Font"));
      if (accepted) setFontButton(selected);
    });
    connectColorButton(m_backgroundButton, tr("Choose Background Color"));
    connectColorButton(m_foregroundButton, tr("Choose Text Color"));
    connectColorButton(m_cursorButton, tr("Choose Cursor Color"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
      storeCurrent();
      normalizeNames();
      accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (!m_profiles.isEmpty()) m_list->setCurrentRow(0);
    updateDefaultLabel();
  }

  QList<TerminalConfig::TerminalProfile> profiles() const {
    return m_profiles;
  }

  QString defaultProfileName() const {
    return m_defaultName;
  }

private:
  static QString joinArguments(const QStringList &arguments) {
    QStringList quoted;
    for (QString argument : arguments) {
      const bool needsQuotes = argument.isEmpty() ||
                               argument.contains(QLatin1Char(' ')) ||
                               argument.contains(QLatin1Char('\t')) ||
                               argument.contains(QLatin1Char('"'));
      argument.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
      argument.replace(QStringLiteral("\""), QStringLiteral("\\\""));
      quoted.push_back(needsQuotes ? QStringLiteral("\"%1\"").arg(argument)
                                   : argument);
    }
    return quoted.join(QLatin1Char(' '));
  }

  void loadProfile(int row) {
    if (row < 0 || row >= m_profiles.size()) return;
    m_loading = true;
    m_currentRow = row;
    const auto &profile = m_profiles[row];
    m_name->setText(profile.name);
    m_program->setText(profile.program);
    m_arguments->setText(joinArguments(profile.arguments));
    m_environment->setText(profile.env.join(QStringLiteral("; ")));
    m_term->setText(profile.term);
    m_scrollback->setValue(profile.scrollbackLines);
    setFontButton(profile.font);
    setColorButton(m_backgroundButton, profile.background);
    setColorButton(m_foregroundButton, profile.foreground);
    setColorButton(m_cursorButton, profile.cursor);
    m_removeButton->setEnabled(m_profiles.size() > 1);
    m_loading = false;
  }

  void storeCurrent() {
    if (m_loading || m_currentRow < 0 ||
        m_currentRow >= m_profiles.size()) return;
    auto &profile = m_profiles[m_currentRow];
    const QString oldName = profile.name;
    const QString enteredName = m_name->text().trimmed();
    profile.name = enteredName.isEmpty() ? oldName : enteredName;
    profile.program = m_program->text().trimmed();
    profile.arguments = QProcess::splitCommand(m_arguments->text());
    profile.env = m_environment->text().split(QLatin1Char(';'),
                                               Qt::SkipEmptyParts);
    for (QString &entry : profile.env) entry = entry.trimmed();
    profile.term = m_term->text().trimmed();
    profile.scrollbackLines = m_scrollback->value();
    profile.font = m_fontButton->property("profileFont").value<QFont>();
    profile.background = colorFromButton(m_backgroundButton);
    profile.foreground = colorFromButton(m_foregroundButton);
    profile.cursor = colorFromButton(m_cursorButton);
    if (m_defaultName == oldName) m_defaultName = profile.name;
    if (auto *item = m_list->item(m_currentRow)) item->setText(profile.name);
    updateDefaultLabel();
  }

  QString uniqueName(const QString &base) const {
    QString candidate = base;
    int suffix = 2;
    auto exists = [this](const QString &name) {
      for (const auto &profile : m_profiles) {
        if (profile.name.compare(name, Qt::CaseInsensitive) == 0) return true;
      }
      return false;
    };
    while (exists(candidate)) candidate = QStringLiteral("%1 %2").arg(base).arg(suffix++);
    return candidate;
  }

  void normalizeNames() {
    QStringList used;
    for (auto &profile : m_profiles) {
      QString base = profile.name.trimmed();
      if (base.isEmpty()) base = tr("Profile");
      QString candidate = base;
      int suffix = 2;
      while (used.contains(candidate, Qt::CaseInsensitive)) {
        candidate = QStringLiteral("%1 %2").arg(base).arg(suffix++);
      }
      if (m_defaultName == profile.name) m_defaultName = candidate;
      profile.name = candidate;
      used.push_back(candidate);
    }
    if (!used.contains(m_defaultName)) m_defaultName = m_profiles.first().name;
  }

  void updateDefaultLabel() {
    m_defaultLabel->setText(tr("Default: %1").arg(m_defaultName));
  }

  void setFontButton(const QFont &font) {
    m_fontButton->setProperty("profileFont", font);
    m_fontButton->setText(QStringLiteral("%1, %2 pt")
                              .arg(font.family())
                              .arg(font.pointSize()));
  }

  static QColor colorFromButton(const QPushButton *button) {
    return button->property("profileColor").value<QColor>();
  }

  static void setColorButton(QPushButton *button, const QColor &color) {
    button->setProperty("profileColor", color);
    button->setText(color.name(QColor::HexRgb));
    const QString textColor = color.lightness() < 128
                                  ? QStringLiteral("white")
                                  : QStringLiteral("black");
    button->setStyleSheet(
        QStringLiteral("background:%1;color:%2;").arg(color.name(), textColor));
  }

  void connectColorButton(QPushButton *button, const QString &title) {
    connect(button, &QPushButton::clicked, this, [this, button, title]() {
      const QColor selected = QColorDialog::getColor(
          colorFromButton(button), this, title, QColorDialog::ShowAlphaChannel);
      if (selected.isValid()) setColorButton(button, selected);
    });
  }

  QList<TerminalConfig::TerminalProfile> m_profiles;
  QString m_defaultName;
  QListWidget *m_list = nullptr;
  QLineEdit *m_name = nullptr;
  QLineEdit *m_program = nullptr;
  QLineEdit *m_arguments = nullptr;
  QLineEdit *m_environment = nullptr;
  QLineEdit *m_term = nullptr;
  QSpinBox *m_scrollback = nullptr;
  QPushButton *m_fontButton = nullptr;
  QPushButton *m_backgroundButton = nullptr;
  QPushButton *m_foregroundButton = nullptr;
  QPushButton *m_cursorButton = nullptr;
  QPushButton *m_removeButton = nullptr;
  QLabel *m_defaultLabel = nullptr;
  int m_currentRow = -1;
  bool m_loading = false;
};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Katalyst Terminal"));
  resize(960, 640);
  setupUi();
}

MainWindow::~MainWindow() = default;

void MainWindow::openTab() {
  newTab();
}

void MainWindow::setupUi() {
  m_config = std::make_unique<TerminalConfig>();

  auto *central = new QWidget(this);
  central->setObjectName(QStringLiteral("terminalChrome"));
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  setupActions();

  auto *header = new QFrame(central);
  header->setObjectName(QStringLiteral("terminalHeader"));
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(8, 6, 8, 6);
  headerLayout->setSpacing(6);

  m_newTabButton = new QToolButton(header);
  m_newTabButton->setObjectName(QStringLiteral("newTabButton"));
  m_newTabButton->setDefaultAction(m_newTabAction);
  m_newTabButton->setPopupMode(QToolButton::MenuButtonPopup);
  m_newTabMenu = new QMenu(m_newTabButton);
  m_newTabButton->setMenu(m_newTabMenu);
  headerLayout->addWidget(m_newTabButton);

  auto addActionButton = [header, headerLayout](QAction *action) {
    auto *button = new QToolButton(header);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    headerLayout->addWidget(button);
  };
  addActionButton(m_splitHorizontalAction);
  addActionButton(m_splitVerticalAction);
  headerLayout->addStretch();
  addActionButton(m_findAction);
  layout->addWidget(header);

  m_tabs = new QTabWidget(central);
  m_tabs->setObjectName(QStringLiteral("terminalTabs"));
  m_tabs->setTabsClosable(true);
  m_tabs->setMovable(true);
  m_tabs->setDocumentMode(true);
  m_tabs->setElideMode(Qt::ElideRight);
  m_tabs->setUsesScrollButtons(true);
  m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(m_tabs, 1);

  m_searchBar = new QFrame(central);
  m_searchBar->setObjectName(QStringLiteral("terminalSearchBar"));
  auto *searchLayout = new QHBoxLayout(m_searchBar);
  searchLayout->setContentsMargins(8, 5, 8, 5);
  searchLayout->addWidget(new QLabel(tr("Find:"), m_searchBar));
  m_searchEdit = new QLineEdit(m_searchBar);
  m_searchEdit->setObjectName(QStringLiteral("terminalSearchEdit"));
  m_searchEdit->setClearButtonEnabled(true);
  m_searchEdit->setPlaceholderText(tr("Search terminal output"));
  searchLayout->addWidget(m_searchEdit, 1);
  auto *previousButton = new QToolButton(m_searchBar);
  previousButton->setText(QStringLiteral("↑"));
  previousButton->setToolTip(tr("Previous match (Shift+Enter)"));
  auto *nextButton = new QToolButton(m_searchBar);
  nextButton->setText(QStringLiteral("↓"));
  nextButton->setToolTip(tr("Next match (Enter)"));
  m_searchStatus = new QLabel(m_searchBar);
  m_searchStatus->setMinimumWidth(90);
  auto *closeSearchButton = new QToolButton(m_searchBar);
  closeSearchButton->setText(QStringLiteral("×"));
  closeSearchButton->setToolTip(tr("Close search (Escape)"));
  searchLayout->addWidget(previousButton);
  searchLayout->addWidget(nextButton);
  searchLayout->addWidget(m_searchStatus);
  searchLayout->addWidget(closeSearchButton);
  m_searchBar->hide();
  layout->addWidget(m_searchBar);

  setCentralWidget(central);

  m_profileStatus = new QLabel(this);
  statusBar()->addPermanentWidget(m_profileStatus);
  statusBar()->showMessage(tr("Ready"), 1500);

  connect(m_searchEdit, &QLineEdit::textChanged, this,
    [this](const QString &term) {
      if (auto *tab = currentTab()) tab->setSearchTerm(term);
      m_searchStatus->clear();
    });
  connect(m_searchEdit, &QLineEdit::returnPressed,
    this, &MainWindow::findNextMatch);
  connect(previousButton, &QToolButton::clicked,
    this, &MainWindow::findPreviousMatch);
  connect(nextButton, &QToolButton::clicked,
    this, &MainWindow::findNextMatch);
  connect(closeSearchButton, &QToolButton::clicked,
    this, &MainWindow::hideSearch);
    auto *closeSearchAction = new QAction(m_searchBar);
    closeSearchAction->setShortcut(QKeySequence(Qt::Key_Escape));
    closeSearchAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_searchBar->addAction(closeSearchAction);
    connect(closeSearchAction, &QAction::triggered,
      this, &MainWindow::hideSearch);

  connect(m_tabs, &QTabWidget::tabCloseRequested,
          this, &MainWindow::closeTabAt);
  connect(m_tabs, &QTabWidget::currentChanged, this,
          &MainWindow::updateTabTitle);
  connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested,
    this, &MainWindow::showTabContextMenu);

  refreshProfileMenus();
  newTab();

  central->setStyleSheet(QStringLiteral(
      "#terminalHeader, #terminalSearchBar {"
      " border-bottom: 1px solid palette(mid);"
      " background: palette(alternate-base);"
      "}"
      "#terminalSearchBar { border-top: 1px solid palette(mid);"
      " border-bottom: 0; }"
      "QToolButton { padding: 4px; }"));
}

void MainWindow::setupActions() {
  menuBar()->clear();
  QMenu *fileMenu = menuBar()->addMenu(tr("File"));
  QMenu *editMenu = menuBar()->addMenu(tr("Edit"));
  QMenu *viewMenu = menuBar()->addMenu(tr("View"));
  m_profilesMenu = menuBar()->addMenu(tr("Profiles"));
  QMenu *settingsMenu = menuBar()->addMenu(tr("Settings"));

  auto prepareAction = [this](QAction *action, const QString &shortcut,
            const QString &iconName) {
    if (!shortcut.isEmpty()) action->setShortcut(QKeySequence(shortcut));
    action->setShortcutContext(Qt::ApplicationShortcut);
    const QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) action->setIcon(icon);
    addAction(action);
  };

  m_newTabAction = new QAction(tr("New Tab"), this);
  prepareAction(m_newTabAction, QStringLiteral("Ctrl+Shift+T"),
    QStringLiteral("tab-new"));
  connect(m_newTabAction, &QAction::triggered, this, &MainWindow::newTab);

  m_closeTabAction = new QAction(tr("Close Tab"), this);
  prepareAction(m_closeTabAction, QStringLiteral("Ctrl+Shift+W"),
    QStringLiteral("tab-close"));
  connect(m_closeTabAction, &QAction::triggered, this, &MainWindow::closeTab);

  m_duplicateTabAction = new QAction(tr("Duplicate Tab"), this);
  prepareAction(m_duplicateTabAction, QStringLiteral("Ctrl+Shift+D"),
    QStringLiteral("edit-copy"));
  connect(m_duplicateTabAction, &QAction::triggered,
    this, &MainWindow::duplicateTab);

  m_renameTabAction = new QAction(tr("Rename Tab"), this);
  prepareAction(m_renameTabAction, QStringLiteral("F2"),
    QStringLiteral("edit-rename"));
  connect(m_renameTabAction, &QAction::triggered,
    this, &MainWindow::renameTab);

  m_nextTabAction = new QAction(tr("Next Tab"), this);
  prepareAction(m_nextTabAction, QStringLiteral("Ctrl+Tab"),
    QStringLiteral("go-next"));
  connect(m_nextTabAction, &QAction::triggered, this, &MainWindow::nextTab);

  m_previousTabAction = new QAction(tr("Previous Tab"), this);
  prepareAction(m_previousTabAction, QStringLiteral("Ctrl+Shift+Tab"),
    QStringLiteral("go-previous"));
  connect(m_previousTabAction, &QAction::triggered,
    this, &MainWindow::previousTab);

  m_splitHorizontalAction = new QAction(tr("Split Horizontally"), this);
  prepareAction(m_splitHorizontalAction, QStringLiteral("Ctrl+Shift+H"),
    QStringLiteral("view-split-left-right"));
  connect(m_splitHorizontalAction, &QAction::triggered, this,
          &MainWindow::splitHorizontal);

  m_splitVerticalAction = new QAction(tr("Split Vertically"), this);
  prepareAction(m_splitVerticalAction, QStringLiteral("Ctrl+Alt+V"),
    QStringLiteral("view-split-top-bottom"));
  connect(m_splitVerticalAction, &QAction::triggered, this,
          &MainWindow::splitVertical);

  m_closeSplitAction = new QAction(tr("Close Split"), this);
  prepareAction(m_closeSplitAction, QStringLiteral("Ctrl+Shift+Q"),
    QStringLiteral("view-close"));
  connect(m_closeSplitAction, &QAction::triggered, this,
          &MainWindow::closeSplit);

  m_copyAction = new QAction(tr("Copy"), this);
  prepareAction(m_copyAction, QStringLiteral("Ctrl+Shift+C"),
    QStringLiteral("edit-copy"));
  connect(m_copyAction, &QAction::triggered, this, [this]() {
    if (auto *view = activeView()) {
      view->copySelection();
    }
  });

  m_pasteAction = new QAction(tr("Paste"), this);
  prepareAction(m_pasteAction, QStringLiteral("Ctrl+Shift+V"),
                QStringLiteral("edit-paste"));
  connect(m_pasteAction, &QAction::triggered, this, [this]() {
    if (auto *view = activeView()) {
      view->pasteClipboard();
    }
  });

  m_findAction = new QAction(tr("Find"), this);
  prepareAction(m_findAction, QStringLiteral("Ctrl+Shift+F"),
                QStringLiteral("edit-find"));
  connect(m_findAction, &QAction::triggered, this, &MainWindow::showSearch);

    m_findNextAction = new QAction(tr("Find Next"), this);
    prepareAction(m_findNextAction, QStringLiteral("F3"),
      QStringLiteral("go-down-search"));
    connect(m_findNextAction, &QAction::triggered,
      this, &MainWindow::findNextMatch);

    m_findPreviousAction = new QAction(tr("Find Previous"), this);
    prepareAction(m_findPreviousAction, QStringLiteral("Shift+F3"),
      QStringLiteral("go-up-search"));
    connect(m_findPreviousAction, &QAction::triggered,
      this, &MainWindow::findPreviousMatch);

  fileMenu->addAction(m_newTabAction);
  fileMenu->addAction(m_duplicateTabAction);
  fileMenu->addAction(m_renameTabAction);
  fileMenu->addSeparator();
  fileMenu->addAction(m_closeTabAction);

  editMenu->addAction(m_copyAction);
  editMenu->addAction(m_pasteAction);
  editMenu->addSeparator();
  editMenu->addAction(m_findAction);
  editMenu->addAction(m_findNextAction);
  editMenu->addAction(m_findPreviousAction);

  viewMenu->addAction(m_nextTabAction);
  viewMenu->addAction(m_previousTabAction);
  viewMenu->addSeparator();
  viewMenu->addAction(m_splitHorizontalAction);
  viewMenu->addAction(m_splitVerticalAction);
  viewMenu->addAction(m_closeSplitAction);

  QAction *profilesAction = settingsMenu->addAction(tr("Manage Profiles..."));
  connect(profilesAction, &QAction::triggered,
          this, &MainWindow::configureProfiles);
  QAction *shortcutAction = settingsMenu->addAction(tr("Keyboard Shortcuts..."));
  connect(shortcutAction, &QAction::triggered, this,
          &MainWindow::configureShortcuts);

  loadSavedShortcuts();
}

void MainWindow::loadSavedShortcuts() {
  KConfig config(QStringLiteral("katalyst-terminalrc"));
  KConfigGroup shortcuts(&config, QStringLiteral("Shortcuts"));
  auto applyShortcut = [&shortcuts](QAction *action, const QString &key,
                                    const QKeySequence &fallback) {
    const QString value = shortcuts.readEntry(
        key, fallback.toString(QKeySequence::PortableText));
    action->setShortcut(QKeySequence::fromString(
        value, QKeySequence::PortableText));
  };

  applyShortcut(m_newTabAction,           QStringLiteral("NewTab"),           m_newTabAction->shortcut());
  applyShortcut(m_closeTabAction,         QStringLiteral("CloseTab"),         m_closeTabAction->shortcut());
  applyShortcut(m_duplicateTabAction,     QStringLiteral("DuplicateTab"),     m_duplicateTabAction->shortcut());
  applyShortcut(m_renameTabAction,        QStringLiteral("RenameTab"),        m_renameTabAction->shortcut());
  applyShortcut(m_nextTabAction,          QStringLiteral("NextTab"),          m_nextTabAction->shortcut());
  applyShortcut(m_previousTabAction,      QStringLiteral("PreviousTab"),      m_previousTabAction->shortcut());
  applyShortcut(m_splitHorizontalAction,  QStringLiteral("SplitHorizontal"),  m_splitHorizontalAction->shortcut());
  applyShortcut(m_splitVerticalAction,    QStringLiteral("SplitVertical"),    m_splitVerticalAction->shortcut());
  applyShortcut(m_closeSplitAction,       QStringLiteral("CloseSplit"),       m_closeSplitAction->shortcut());
  applyShortcut(m_copyAction,             QStringLiteral("Copy"),             m_copyAction->shortcut());
  applyShortcut(m_pasteAction,            QStringLiteral("Paste"),            m_pasteAction->shortcut());
  applyShortcut(m_findAction,             QStringLiteral("Find"),             m_findAction->shortcut());
  applyShortcut(m_findNextAction,         QStringLiteral("FindNext"),         m_findNextAction->shortcut());
  applyShortcut(m_findPreviousAction,     QStringLiteral("FindPrevious"),     m_findPreviousAction->shortcut());
}

TerminalTab *MainWindow::currentTab() const {
  return qobject_cast<TerminalTab *>(m_tabs->currentWidget());
}

TerminalViewBase *MainWindow::activeView() const {
  TerminalTab *tab = currentTab();
  return tab ? tab->activeView() : nullptr;
}

void MainWindow::connectTabSignals(TerminalTab *tab) {
  connect(tab, &TerminalTab::activeViewChanged, this, [this](TerminalViewBase *) {
    updateTabTitle(m_tabs->currentIndex());
    updateStatus();
  });
  connect(tab, &TerminalTab::titleChanged, this, [this, tab]() {
    int idx = m_tabs->indexOf(tab);
    if (idx >= 0) {
      m_tabs->setTabText(idx, tab->tabTitle());
      m_tabs->setTabToolTip(idx, tr("%1 profile").arg(tab->profileName()));
    }
  });
  connect(tab, &TerminalTab::sessionClosed, this, [this, tab]() {
    int idx = m_tabs->indexOf(tab);
    if (idx < 0) return;
    if (m_tabs->count() == 1) newTab();
    closeTabAt(idx);
  });
}

void MainWindow::newTab() {
  newTabWithProfile(m_config->defaultProfileName());
}

void MainWindow::newTabWithProfile(const QString &profileName) {
  const auto profile = m_config->profile(profileName);
  auto *tab = new TerminalTab(m_config.get(), profile, this);
  QIcon icon = QIcon::fromTheme(QStringLiteral("utilities-terminal"));
  if (icon.isNull()) icon = style()->standardIcon(QStyle::SP_ComputerIcon);
  int index = m_tabs->addTab(tab, icon, tab->tabTitle());
  m_tabs->setTabToolTip(index, tr("%1 profile").arg(profile.name));
  m_tabs->setCurrentIndex(index);
  connectTabSignals(tab);
  if (m_searchBar->isVisible()) tab->setSearchTerm(m_searchEdit->text());
  statusBar()->showMessage(tr("Opened %1 profile").arg(profile.name), 1800);
  updateStatus();
}

void MainWindow::closeTab() {
  closeTabAt(m_tabs->currentIndex());
}

void MainWindow::closeTabAt(int index) {
  if (index >= 0 && m_tabs->count() > 1) {
    QWidget *widget = m_tabs->widget(index);
    m_tabs->removeTab(index);
    widget->deleteLater();
    statusBar()->showMessage(tr("Tab closed"), 1200);
    updateStatus();
  }
}

void MainWindow::duplicateTab() {
  duplicateTabAt(m_tabs->currentIndex());
}

void MainWindow::duplicateTabAt(int index) {
  auto *source = qobject_cast<TerminalTab *>(m_tabs->widget(index));
  if (!source) return;
  newTabWithProfile(source->profileName());
  if (auto *duplicate = currentTab(); !source->customTitle().isEmpty()) {
    duplicate->setCustomTitle(tr("%1 Copy").arg(source->customTitle()));
  }
}

void MainWindow::renameTab() {
  renameTabAt(m_tabs->currentIndex());
}

void MainWindow::renameTabAt(int index) {
  auto *tab = qobject_cast<TerminalTab *>(m_tabs->widget(index));
  if (!tab) return;
  bool accepted = false;
  const QString title = QInputDialog::getText(
      this, tr("Rename Tab"), tr("Tab title:"), QLineEdit::Normal,
      tab->customTitle().isEmpty() ? tab->tabTitle() : tab->customTitle(),
      &accepted);
  if (accepted) tab->setCustomTitle(title);
}

void MainWindow::nextTab() {
  if (m_tabs->count() > 1) {
    m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
  }
}

void MainWindow::previousTab() {
  if (m_tabs->count() > 1) {
    const int next = (m_tabs->currentIndex() - 1 + m_tabs->count()) %
                     m_tabs->count();
    m_tabs->setCurrentIndex(next);
  }
}

void MainWindow::splitHorizontal() {
  if (auto *tab = currentTab()) {
    tab->split(Qt::Horizontal);
    statusBar()->showMessage(tr("Pane split horizontally"), 1200);
    updateStatus();
  }
}

void MainWindow::splitVertical() {
  if (auto *tab = currentTab()) {
    tab->split(Qt::Vertical);
    statusBar()->showMessage(tr("Pane split vertically"), 1200);
    updateStatus();
  }
}

void MainWindow::closeSplit() {
  if (auto *tab = currentTab()) {
    tab->closeActiveSplit();
    updateStatus();
  }
}

void MainWindow::refreshProfileMenus() {
  auto populate = [this](QMenu *menu, bool includeManager) {
    if (!menu) return;
    menu->clear();
    const QString defaultName = m_config->defaultProfileName();
    for (const auto &profile : m_config->profiles()) {
      QString label = profile.name;
      if (profile.name == defaultName) label += tr(" (Default)");
      QAction *action = menu->addAction(label);
      action->setData(profile.name);
      const QIcon icon = QIcon::fromTheme(QStringLiteral("utilities-terminal"));
      if (!icon.isNull()) action->setIcon(icon);
      connect(action, &QAction::triggered, this,
              [this, name = profile.name]() { newTabWithProfile(name); });
    }
    if (includeManager) {
      menu->addSeparator();
      QAction *manage = menu->addAction(tr("Manage Profiles..."));
      connect(manage, &QAction::triggered,
              this, &MainWindow::configureProfiles);
    }
  };
  populate(m_newTabMenu, false);
  populate(m_profilesMenu, true);
}

void MainWindow::configureProfiles() {
  ProfileEditorDialog dialog(m_config->profiles(),
                             m_config->defaultProfileName(), this);
  if (dialog.exec() != QDialog::Accepted) return;
  m_config->saveProfiles(dialog.profiles(), dialog.defaultProfileName());
  refreshProfileMenus();
  updateStatus();
  statusBar()->showMessage(tr("Profiles saved; changes apply to new tabs"),
                           3000);
}

void MainWindow::showTabContextMenu(const QPoint &position) {
  const int index = m_tabs->tabBar()->tabAt(position);
  if (index < 0) return;
  m_tabs->setCurrentIndex(index);

  QMenu menu(this);
  QMenu *newProfileMenu = menu.addMenu(tr("New Tab with Profile"));
  for (const auto &profile : m_config->profiles()) {
    QAction *action = newProfileMenu->addAction(profile.name);
    connect(action, &QAction::triggered, this,
            [this, name = profile.name]() { newTabWithProfile(name); });
  }
  menu.addSeparator();
  menu.addAction(m_duplicateTabAction);
  menu.addAction(m_renameTabAction);
  menu.addSeparator();
  menu.addAction(m_closeTabAction);
  QAction *closeOthers = menu.addAction(tr("Close Other Tabs"));
  closeOthers->setEnabled(m_tabs->count() > 1);
  connect(closeOthers, &QAction::triggered, this, [this, index]() {
    QWidget *keep = m_tabs->widget(index);
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
      if (m_tabs->widget(i) != keep) closeTabAt(i);
    }
  });
  menu.exec(m_tabs->tabBar()->mapToGlobal(position));
}

void MainWindow::showSearch() {
  m_searchBar->show();
  m_searchEdit->setFocus(Qt::ShortcutFocusReason);
  m_searchEdit->selectAll();
}

void MainWindow::hideSearch() {
  m_searchBar->hide();
  m_searchStatus->clear();
  m_searchEdit->clear();
  if (auto *view = activeView()) view->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::findNextMatch() {
  if (m_searchEdit->text().isEmpty()) {
    showSearch();
    return;
  }
  const bool found = currentTab() && currentTab()->findNext(true);
  m_searchStatus->setText(found ? tr("Match found") : tr("No matches"));
}

void MainWindow::findPreviousMatch() {
  if (m_searchEdit->text().isEmpty()) {
    showSearch();
    return;
  }
  const bool found = currentTab() && currentTab()->findNext(false);
  m_searchStatus->setText(found ? tr("Match found") : tr("No matches"));
}

void MainWindow::updateStatus() {
  if (!m_profileStatus) return;
  if (auto *tab = currentTab()) {
    m_profileStatus->setText(
        tr("%1  •  %n pane(s)", nullptr, tab->viewCount())
            .arg(tab->profileName()));
  } else {
    m_profileStatus->clear();
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
  addField(tr("Duplicate Tab"), QStringLiteral("DuplicateTab"),
           m_duplicateTabAction);
  addField(tr("Rename Tab"), QStringLiteral("RenameTab"),
           m_renameTabAction);
  addField(tr("Next Tab"), QStringLiteral("NextTab"), m_nextTabAction);
  addField(tr("Previous Tab"), QStringLiteral("PreviousTab"),
           m_previousTabAction);
  addField(tr("Split Horizontally"), QStringLiteral("SplitHorizontal"),
           m_splitHorizontalAction);
  addField(tr("Split Vertically"), QStringLiteral("SplitVertical"),
           m_splitVerticalAction);
  addField(tr("Close Split"), QStringLiteral("CloseSplit"), m_closeSplitAction);
  addField(tr("Copy"), QStringLiteral("Copy"), m_copyAction);
  addField(tr("Paste"), QStringLiteral("Paste"), m_pasteAction);
  addField(tr("Find"), QStringLiteral("Find"), m_findAction);
  addField(tr("Find Next"), QStringLiteral("FindNext"), m_findNextAction);
  addField(tr("Find Previous"), QStringLiteral("FindPrevious"),
           m_findPreviousAction);

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
    updateStatus();
    return;
  }
  if (auto *tab = qobject_cast<TerminalTab *>(m_tabs->widget(index))) {
    m_tabs->setTabText(index, tab->tabTitle());
    if (m_searchBar->isVisible()) tab->setSearchTerm(m_searchEdit->text());
  }
  updateStatus();
}
