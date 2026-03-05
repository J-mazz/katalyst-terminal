#include "QtShim.h"
import std;

namespace {
struct PreparedExec {
  QByteArray program;
  QVector<QByteArray> argStorage;
  std::vector<char *> argv;
  QVector<QByteArray> envKeys;
  QVector<QByteArray> envVals;
};

PreparedExec prepareExec(const QString &program, const QStringList &args,
                         const QStringList &env) {
  PreparedExec pe;
  pe.program = program.toLocal8Bit();
  pe.argStorage.reserve(args.size() + 1);
  pe.argStorage.push_back(pe.program);
  for (const QString &a : args) pe.argStorage.push_back(a.toLocal8Bit());
  pe.argv.reserve(static_cast<size_t>(pe.argStorage.size()) + 1);
  for (QByteArray &b : pe.argStorage) pe.argv.push_back(b.data());
  pe.argv.push_back(nullptr);
  for (const QString &entry : env) {
    const int eq = entry.indexOf(QLatin1Char('='));
    if (eq > 0) {
      pe.envKeys.push_back(entry.left(eq).toLocal8Bit());
      pe.envVals.push_back(entry.mid(eq + 1).toLocal8Bit());
    }
  }
  return pe;
}
}

PtyProcess::PtyProcess(QObject *parent) : QObject(parent) {}

PtyProcess::~PtyProcess() {
  stop();
}

bool PtyProcess::start(const QString &program, const QStringList &args,
                       const QStringList &env) {
  if (m_masterFd >= 0) return false;

  // Prepare all byte arrays before fork — avoids heap allocations in child
  PreparedExec pe = prepareExec(program, args, env);

  struct winsize win = {};
  win.ws_col = 80;
  win.ws_row = 24;

  m_childPid = forkpty(&m_masterFd, nullptr, nullptr, &win);
  if (m_childPid == 0) {
    // Child: only async-signal-safe operations (plus setenv/execvp)
    for (int i = 0; i < pe.envKeys.size(); ++i)
      setenv(pe.envKeys[i].constData(), pe.envVals[i].constData(), 1);
    execvp(pe.program.constData(), pe.argv.data());
    _exit(127);
  }
  if (m_childPid < 0) { closeMaster(); return false; }

  m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this, &PtyProcess::handleReadyRead);
  return true;
}

void PtyProcess::stop() {
  if (m_childPid > 0) {
    kill(m_childPid, SIGHUP);
    // Reap the child to prevent zombie accumulation
    int status = 0;
    if (waitpid(m_childPid, &status, WNOHANG) == 0) {
      // Child hasn't exited yet — give it a brief chance, then force
      kill(m_childPid, SIGTERM);
      waitpid(m_childPid, &status, WNOHANG);
    }
    m_childPid = -1;
  }
  closeMaster();
}

void PtyProcess::send(const QByteArray &data) {
  if (m_masterFd < 0) {
    return;
  }
  ssize_t remaining = data.size();
  const char *ptr = data.constData();
  while (remaining > 0) {
    ssize_t written = ::write(m_masterFd, ptr, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    remaining -= written;
    ptr += written;
  }
}

void PtyProcess::setWindowSize(int columns, int rows) {
  if (m_masterFd < 0) {
    return;
  }
  struct winsize win = {};
  win.ws_col = static_cast<unsigned short>(qMax(1, columns));
  win.ws_row = static_cast<unsigned short>(qMax(1, rows));
  ioctl(m_masterFd, TIOCSWINSZ, &win);
}

void PtyProcess::handleReadyRead() {
  if (m_masterFd < 0) {
    return;
  }
  char buffer[4096];
  ssize_t bytes = ::read(m_masterFd, buffer, sizeof(buffer));
  if (bytes > 0) {
    emit dataReady(QByteArray(buffer, static_cast<int>(bytes)));
  } else if (bytes == 0 || (bytes < 0 && errno == EIO)) {
    emit exited();
    stop();
  }
}

void PtyProcess::closeMaster() {
  if (m_notifier) {
    m_notifier->setEnabled(false);
    m_notifier->disconnect(this);
    delete m_notifier;
    m_notifier = nullptr;
  }
  if (m_masterFd >= 0) {
    ::close(m_masterFd);
    m_masterFd = -1;
  }
}
