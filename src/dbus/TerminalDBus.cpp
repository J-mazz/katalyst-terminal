#include "QtShim.h"
import std;

TerminalDBus::TerminalDBus(QObject *parent) : QObject(parent) {}

void TerminalDBus::NewWindow() {
  emit newWindowRequested();
}

void TerminalDBus::OpenTab() {
  emit newTabRequested();
}
