#include "QtShim.h"
import std;

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  QCoreApplication::setApplicationName(QStringLiteral("katalyst-terminal"));
  QCoreApplication::setOrganizationName(QStringLiteral("Katalyst"));

  TerminalDBus dbus;
  QDBusConnection connection = QDBusConnection::sessionBus();
  connection.registerService(QStringLiteral("org.katalyst.Terminal"));
  connection.registerObject(QStringLiteral("/org.katalyst.Terminal"), &dbus,
                            QDBusConnection::ExportAllSlots);

  QList<MainWindow *> windows;

  auto createWindow = [&windows]() {
    auto *window = new MainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);
    windows.push_back(window);
    QObject::connect(window, &QObject::destroyed, window,
                     [&windows, window]() {
      windows.removeAll(window);
    });
    window->show();
    return window;
  };

  QObject::connect(&dbus, &TerminalDBus::newWindowRequested, &app,
                   createWindow);
  QObject::connect(&dbus, &TerminalDBus::newTabRequested, &app,
                   [&windows, &createWindow]() {
    if (!windows.isEmpty()) {
      windows.last()->openTab();
    } else {
      createWindow();
    }
  });

  createWindow();

  return app.exec();
}
