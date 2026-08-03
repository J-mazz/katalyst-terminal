#include "QtShim.h"
import std;

namespace {
TerminalConfig::TerminalProfile baseProfile(const QString &name) {
  TerminalConfig::TerminalProfile profile;
  QFont defaultFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  profile.name = name;
  profile.font = defaultFont;
  profile.background = QColor(20, 22, 26);
  profile.foreground = QColor(220, 220, 220);
  profile.selection = QColor(60, 120, 200, 120);
  profile.searchHighlight = QColor(200, 160, 60, 160);
  profile.cursor = QColor(200, 200, 200);
  profile.scrollbackLines = 4000;
  profile.term = QStringLiteral("xterm-256color");
  return profile;
}

TerminalConfig::TerminalProfile presetProfile(const QString &name) {
  auto profile = baseProfile(name);
  if (name == QStringLiteral("Midnight")) {
    profile.background = QColor(9, 14, 26);
    profile.foreground = QColor(214, 224, 240);
    profile.selection = QColor(56, 139, 253, 135);
    profile.searchHighlight = QColor(245, 158, 11, 175);
    profile.cursor = QColor(96, 165, 250);
  } else if (name == QStringLiteral("Daylight")) {
    profile.background = QColor(246, 248, 250);
    profile.foreground = QColor(31, 35, 40);
    profile.selection = QColor(9, 105, 218, 85);
    profile.searchHighlight = QColor(255, 205, 64, 180);
    profile.cursor = QColor(9, 105, 218);
  }
  return profile;
}

TerminalConfig::TerminalProfile readProfile(KConfig &config,
                                             const QString &name) {
  const auto fallback = presetProfile(name);
  KConfigGroup group(&config, QStringLiteral("Profile %1").arg(name));
  TerminalConfig::TerminalProfile profile = fallback;
  profile.name = name;
  profile.font = group.readEntry("Font", fallback.font);
  profile.background = group.readEntry("Background", fallback.background);
  profile.foreground = group.readEntry("Foreground", fallback.foreground);
  profile.selection = group.readEntry("Selection", fallback.selection);
  profile.searchHighlight =
      group.readEntry("SearchHighlight", fallback.searchHighlight);
  profile.cursor = group.readEntry("Cursor", fallback.cursor);
  profile.scrollbackLines =
      qMax(100, group.readEntry("ScrollbackLines", fallback.scrollbackLines));
  profile.program = group.readEntry("Program", fallback.program);
  profile.arguments = group.readEntry("Arguments", fallback.arguments);
  profile.env = group.readEntry("Env", fallback.env);
  profile.term = group.readEntry("Term", fallback.term);
  return profile;
}

void writeProfile(KConfig &config,
                  const TerminalConfig::TerminalProfile &profile) {
  KConfigGroup group(&config,
                     QStringLiteral("Profile %1").arg(profile.name));
  group.writeEntry("Font", profile.font);
  group.writeEntry("Background", profile.background);
  group.writeEntry("Foreground", profile.foreground);
  group.writeEntry("Selection", profile.selection);
  group.writeEntry("SearchHighlight", profile.searchHighlight);
  group.writeEntry("Cursor", profile.cursor);
  group.writeEntry("ScrollbackLines", profile.scrollbackLines);
  group.writeEntry("Program", profile.program);
  group.writeEntry("Arguments", profile.arguments);
  group.writeEntry("Env", profile.env);
  group.writeEntry("Term", profile.term);
}
}

TerminalConfig::TerminalConfig() {
  KConfig config(QStringLiteral("katalyst-terminalrc"));
  KConfigGroup general(&config, QStringLiteral("General"));
  m_defaultProfileName =
      general.readEntry("DefaultProfile", QStringLiteral("Default"));

  QStringList profileNames;
  for (const QString &group : config.groupList()) {
    if (group.startsWith(QStringLiteral("Profile "))) {
      profileNames.push_back(group.sliced(8));
    }
  }

  if (profileNames.isEmpty()) {
    profileNames = {QStringLiteral("Default"), QStringLiteral("Midnight"),
                    QStringLiteral("Daylight")};
  }
  if (!profileNames.contains(m_defaultProfileName)) {
    profileNames.prepend(m_defaultProfileName);
  }

  profileNames.removeDuplicates();
  for (const QString &name : profileNames) {
    m_profiles.push_back(readProfile(config, name));
  }
  m_defaultProfile = profile(m_defaultProfileName);
}

TerminalConfig::TerminalProfile TerminalConfig::defaultProfile() const {
  return m_defaultProfile;
}

TerminalConfig::TerminalProfile TerminalConfig::profile(
    const QString &name) const {
  for (const TerminalProfile &profile : m_profiles) {
    if (profile.name == name) {
      return profile;
    }
  }
  return m_profiles.isEmpty() ? baseProfile(QStringLiteral("Default"))
                              : m_profiles.first();
}

QList<TerminalConfig::TerminalProfile> TerminalConfig::profiles() const {
  return m_profiles;
}

QString TerminalConfig::defaultProfileName() const {
  return m_defaultProfileName;
}

void TerminalConfig::saveProfiles(const QList<TerminalProfile> &profiles,
                                  const QString &defaultProfileName) {
  if (profiles.isEmpty()) {
    return;
  }

  KConfig config(QStringLiteral("katalyst-terminalrc"));
  for (const QString &groupName : config.groupList()) {
    if (groupName.startsWith(QStringLiteral("Profile "))) {
      KConfigGroup(&config, groupName).deleteGroup();
    }
  }
  for (const TerminalProfile &profile : profiles) {
    writeProfile(config, profile);
  }

  m_profiles = profiles;
  m_defaultProfileName = defaultProfileName;
  bool foundDefault = false;
  for (const TerminalProfile &profile : m_profiles) {
    if (profile.name == m_defaultProfileName) {
      foundDefault = true;
      break;
    }
  }
  if (!foundDefault) {
    m_defaultProfileName = m_profiles.first().name;
  }
  m_defaultProfile = profile(m_defaultProfileName);

  KConfigGroup general(&config, QStringLiteral("General"));
  general.writeEntry("DefaultProfile", m_defaultProfileName);
  config.sync();
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
