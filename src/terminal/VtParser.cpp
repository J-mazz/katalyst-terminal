#include "QtShim.h"
import std;

struct VtParserCore;
VtParserCore *createVtParserCore();
void destroyVtParserCore(VtParserCore *core);
void resetVtParserCore(VtParserCore *core);
bool feedVtParserCore(VtParserCore *core, TerminalBuffer *buffer,
                      const QByteArray &data, QString *titleOut);

VtParser::VtParser(TerminalBuffer *buffer, QObject *parent)
    : QObject(parent), m_buffer(buffer), m_core(createVtParserCore()) {}

VtParser::~VtParser() {
  destroyVtParserCore(m_core);
  m_core = nullptr;
}

void VtParser::reset() {
  resetVtParserCore(m_core);
}

void VtParser::feed(const QByteArray &data) {
  QString title;
  if (feedVtParserCore(m_core, m_buffer, data, &title)) {
    emit titleChanged(title);
  }
}
