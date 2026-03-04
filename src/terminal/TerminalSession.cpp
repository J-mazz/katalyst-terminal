#include "QtShim.h"
import std;

TerminalSession::TerminalSession(const TerminalConfig::TerminalProfile &profile,
                                 QObject *parent)
    : QObject(parent), m_profile(profile) {
  m_pty = new PtyProcess(this);
  m_buffer = std::make_unique<TerminalBuffer>();
  m_parser = new VtParser(m_buffer.get(), this);

  m_buffer->setDefaultColors(m_profile.foreground, m_profile.background);

  connect(m_pty, &PtyProcess::dataReady, this,
          &TerminalSession::handlePtyData);
  connect(m_parser, &VtParser::titleChanged, this,
          &TerminalSession::titleChanged);
}


namespace {
QStringList buildEnv(const TerminalConfig::TerminalProfile &profile) {
  QStringList env = profile.env;
  if (!profile.term.isEmpty())
    env.push_back(QStringLiteral("TERM=") + profile.term);
  return env;
}
}

void TerminalSession::startShell() {
  QString program = m_profile.program;
  if (program.isEmpty()) program = QString::fromLocal8Bit(qgetenv("SHELL"));
  if (program.isEmpty()) program = QStringLiteral("/bin/bash");
  m_pty->start(program, m_profile.arguments, buildEnv(m_profile));
}

void TerminalSession::sendInput(const QByteArray &data) {
  m_pty->send(data);
}

void TerminalSession::resize(int columns, int rows) {
  m_buffer->resize(columns, rows);
  m_pty->setWindowSize(columns, rows);
  emit screenUpdated();
}

TerminalBuffer *TerminalSession::buffer() const {
  return m_buffer.get();
}

void TerminalSession::handlePtyData(const QByteArray &data) {
  m_parser->feed(data);
  emit screenUpdated();
}
