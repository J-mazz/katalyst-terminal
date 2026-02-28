#include "QtShim.h"
import std;

PtyProcess::PtyProcess(QObject *parent) : QObject(parent) {}

PtyProcess::~PtyProcess() {
  stop();
}

bool PtyProcess::start(const QString &program, const QStringList &args,
                       const QStringList &env) {
  if (m_masterFd >= 0) {
    return false;
  }

  struct winsize win = {};
  win.ws_col = 80;
  win.ws_row = 24;

  m_childPid = forkpty(&m_masterFd, nullptr, nullptr, &win);
  if (m_childPid == 0) {
    for (const QString &entry : env) {
      QByteArray bytes = entry.toLocal8Bit();
      putenv(strdup(bytes.constData()));
    }

    QByteArray programBytes = program.toLocal8Bit();
    QVector<QByteArray> argBytes;
    argBytes.reserve(args.size() + 1);
    argBytes.push_back(programBytes);
    for (const QString &arg : args) {
      argBytes.push_back(arg.toLocal8Bit());
    }

    QVector<char *> argv;
    argv.reserve(argBytes.size() + 1);
    for (QByteArray &arg : argBytes) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    execvp(programBytes.constData(), argv.data());
    _exit(127);
  }

  if (m_childPid < 0) {
    closeMaster();
    return false;
  }

  m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this,
          &PtyProcess::handleReadyRead);
  return true;
}

void PtyProcess::stop() {
  if (m_childPid > 0) {
    kill(m_childPid, SIGHUP);
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
  } else if (bytes == 0) {
    emit exited();
    stop();
  }
}

void PtyProcess::closeMaster() {
  if (m_notifier) {
    m_notifier->deleteLater();
    m_notifier = nullptr;
  }
  if (m_masterFd >= 0) {
    ::close(m_masterFd);
    m_masterFd = -1;
  }
}
