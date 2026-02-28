#include "QtShim.h"
import std;

TerminalConfig::TerminalConfig() {
  KConfig config(QStringLiteral("katalyst-terminalrc"));
  KConfigGroup general(&config, QStringLiteral("General"));
  const QString profileName =
      general.readEntry("DefaultProfile", QStringLiteral("Default"));
  m_renderer = general.readEntry("Renderer", QStringLiteral("Raster"));
  KConfigGroup profile(&config,
                       QStringLiteral("Profile %1").arg(profileName));

  QFont defaultFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  m_defaultProfile.name = profileName;
  m_defaultProfile.font = profile.readEntry("Font", defaultFont);

  m_defaultProfile.background =
      profile.readEntry("Background", QColor(20, 22, 26));
  m_defaultProfile.foreground =
      profile.readEntry("Foreground", QColor(220, 220, 220));
  m_defaultProfile.selection =
      profile.readEntry("Selection", QColor(60, 120, 200, 120));
  m_defaultProfile.searchHighlight =
      profile.readEntry("SearchHighlight", QColor(200, 160, 60, 160));
  m_defaultProfile.cursor = profile.readEntry("Cursor", QColor(200, 200, 200));
  m_defaultProfile.scrollbackLines = profile.readEntry("ScrollbackLines", 4000);

  m_defaultProfile.program = profile.readEntry("Program", QString());
  m_defaultProfile.arguments = profile.readEntry("Arguments", QStringList());
  m_defaultProfile.env = profile.readEntry("Env", QStringList());
  m_defaultProfile.term = profile.readEntry("Term", QStringLiteral("xterm-256color"));
}

TerminalConfig::TerminalProfile TerminalConfig::defaultProfile() const {
  return m_defaultProfile;
}

QString TerminalConfig::renderer() const {
  return m_renderer;
}

QFont TerminalConfig::font() const {
  return m_defaultProfile.font;
}

QColor TerminalConfig::backgroundColor() const {
  return m_defaultProfile.background;
}

QColor TerminalConfig::foregroundColor() const {
  return m_defaultProfile.foreground;
}

int TerminalConfig::scrollbackLines() const {
  return m_defaultProfile.scrollbackLines;
}
