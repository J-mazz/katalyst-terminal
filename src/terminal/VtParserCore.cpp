#include "QtShim.h"
import std;
import terminal.ansi.color;
import terminal.sgr.param;
 
struct VtParserCore {
  enum class State {
    Normal,
    Escape,
    Csi,
    Osc,
    OscEscape
  };

  State state = State::Normal;
  QList<int> params;
  int currentParam = -1;
  bool csiPrivate = false;
  QStringDecoder utf8Decoder{QStringDecoder::Utf8};
  QString oscString;
};

namespace {
QColor xtermColorFromIndex(int index) {
  const AnsiRgb rgb = ansiColorFromXtermIndex(index);
  return QColor(rgb.r, rgb.g, rgb.b);
}

void finalizeParam(VtParserCore *core) {
  core->params.push_back(core->currentParam >= 0 ? core->currentParam : -1);
  core->currentParam = -1;
}

bool handleOsc(VtParserCore *core, QString *titleOut) {
  const int semi = core->oscString.indexOf(QLatin1Char(';'));
  if (semi < 0) return false;
  bool ok = false;
  const int ps = core->oscString.left(semi).toInt(&ok);
  if (!ok || (ps != 0 && ps != 2)) return false;
  if (titleOut) *titleOut = core->oscString.mid(semi + 1);
  return true;
}

void handleCursorPosition(VtParserCore *core, TerminalBuffer *buffer) {
  int row = core->params.size() > 0 && core->params[0] > 0 ? core->params[0] - 1 : 0;
  int col = core->params.size() > 1 && core->params[1] > 0 ? core->params[1] - 1 : 0;
  buffer->setCursorPosition(row, col);
}

int applyExtendedColor(const QList<int> &params, TerminalBuffer *buffer, bool isFg, int i) {
  if (i + 1 >= params.size()) return 0;
  const int mode = params[i + 1];
  if (mode == 5 && i + 2 < params.size()) {
    if (isFg) buffer->setForeground(xtermColorFromIndex(params[i + 2]));
    else       buffer->setBackground(xtermColorFromIndex(params[i + 2]));
    return 2;
  }
  if (mode == 2 && i + 4 < params.size()) {
    int r = qBound(0, params[i + 2], 255);
    int g = qBound(0, params[i + 3], 255);
    int b = qBound(0, params[i + 4], 255);
    if (isFg) buffer->setForeground(QColor(r, g, b));
    else       buffer->setBackground(QColor(r, g, b));
    return 4;
  }
  return 0;
}

bool applyStandardFgParam(int param, TerminalBuffer *buffer) {
  if (param >= 30 && param <= 37) { buffer->setForeground(xtermColorFromIndex(param - 30));      return true; }
  if (param >= 90 && param <= 97) { buffer->setForeground(xtermColorFromIndex(param - 90 + 8)); return true; }
  return false;
}

bool applyStandardBgParam(int param, TerminalBuffer *buffer) {
  if (param >= 40  && param <= 47)  { buffer->setBackground(xtermColorFromIndex(param - 40));       return true; }
  if (param >= 100 && param <= 107) { buffer->setBackground(xtermColorFromIndex(param - 100 + 8)); return true; }
  return false;
}

bool applyStandardColorParam(int param, TerminalBuffer *buffer) {
  return applyStandardFgParam(param, buffer) || applyStandardBgParam(param, buffer);
}

using TermBoolFn = void (TerminalBuffer::*)(bool);
struct SgrToggle { SgrAction action; TermBoolFn fn; bool val; };
static const SgrToggle sgrToggles[] = {
  { SgrAction::BoldOn,       &TerminalBuffer::setBold,          true  },
  { SgrAction::BoldOff,      &TerminalBuffer::setBold,          false },
  { SgrAction::ItalicOn,     &TerminalBuffer::setItalic,        true  },
  { SgrAction::ItalicOff,    &TerminalBuffer::setItalic,        false },
  { SgrAction::UnderlineOn,  &TerminalBuffer::setUnderline,     true  },
  { SgrAction::UnderlineOff, &TerminalBuffer::setUnderline,     false },
  { SgrAction::InverseOn,    &TerminalBuffer::setInverse,       true  },
  { SgrAction::InverseOff,   &TerminalBuffer::setInverse,       false },
  { SgrAction::StrikeOn,     &TerminalBuffer::setStrikethrough, true  },
  { SgrAction::StrikeOff,    &TerminalBuffer::setStrikethrough, false },
};

bool applySgrToggle(SgrAction action, TerminalBuffer *buffer) {
  for (const auto &t : sgrToggles) {
    if (t.action == action) { (buffer->*(t.fn))(t.val); return true; }
  }
  return false;
}

void applySgrAction(SgrAction action, TerminalBuffer *buffer) {
  if (action == SgrAction::Reset)     { buffer->resetAttributes(); return; }
  if (action == SgrAction::FgDefault) { buffer->setForeground(buffer->defaultForeground()); return; }
  if (action == SgrAction::BgDefault) { buffer->setBackground(buffer->defaultBackground()); return; }
  applySgrToggle(action, buffer);
}

void applySgrParams(QList<int> &params, TerminalBuffer *buffer) {
  if (params.isEmpty()) params.push_back(0);
  for (int i = 0; i < params.size(); ++i) {
    const int param = params[i];
    const SgrAction action = classifySgrParam(param);
    if (action != SgrAction::Unknown) { applySgrAction(action, buffer); continue; }
    if (param == 38 || param == 48)   { i += applyExtendedColor(params, buffer, param == 38, i); continue; }
    applyStandardColorParam(param, buffer);
  }
}

void handleCsiCommand(VtParserCore *core, TerminalBuffer *buffer, char command) {
  auto param = [&](int index, int def) {
    return (index < core->params.size() && core->params[index] > 0)
               ? core->params[index] : def;
  };
  switch (command) {
    case 'A': buffer->cursorUp(param(0, 1));           break;
    case 'B': buffer->cursorDown(param(0, 1));          break;
    case 'C': buffer->cursorForward(param(0, 1));       break;
    case 'D': buffer->cursorBack(param(0, 1));          break;
    case 'G': buffer->cursorToColumn(param(0, 1) - 1); break;
    case 'f': // fallthrough — same as H
    case 'H': handleCursorPosition(core, buffer);       break;
    case 'J': {
      const int mode = core->params.isEmpty() ? 0 : core->params.first();
      if      (mode == 0) buffer->clearToEnd();
      else if (mode == 2) buffer->clear();
      break;
    }
    case 'K': {
      const int mode = core->params.isEmpty() ? 0 : core->params.first();
      if      (mode == 0) buffer->clearLineToEnd();
      else if (mode == 1) buffer->clearLineFromStart();
      else if (mode == 2) buffer->clearLine();
      break;
    }
    case 'm': applySgrParams(core->params, buffer); break;
    case 'r': {
      const int top = param(0, 1);
      const int bottom = param(1, buffer->rows());
      buffer->setScrollRegion(top - 1, bottom - 1);
      break;
    }
    default: break;
  }
  core->params.clear();
}

void handlePrivateModeCommand(VtParserCore *core, TerminalBuffer *buffer, char command) {
  if (command != 'h' && command != 'l') {
    core->params.clear();
    return;
  }
  const bool enable = (command == 'h');
  for (int mode : core->params) {
    if (mode <= 0) {
      continue;
    }
    switch (mode) {
      case 1049:
        if (enable) {
          buffer->enterAlternateScreen();
        } else {
          buffer->exitAlternateScreen();
        }
        break;
      case 25:
        buffer->setCursorVisible(enable);
        break;
      default:
        break;
    }
  }
  core->params.clear();
}

bool handleControlByte(VtParserCore *core, TerminalBuffer *buffer, unsigned char raw) {
  switch (raw) {
    case 0x1b: core->state = VtParserCore::State::Escape; return true;
    case 0x0a: buffer->newline();        return true;
    case 0x0d: buffer->carriageReturn(); return true;
    case 0x08: buffer->backspace();      return true;
    case 0x09: buffer->tab();            return true;
    case 0x7f:                           return true;
    default:                             return false;
  }
}

void handleNormalByte(VtParserCore *core, TerminalBuffer *buffer, unsigned char raw) {
  if (handleControlByte(core, buffer, raw)) return;
  const char ch = static_cast<char>(raw);
  if (raw >= 0x20 && raw < 0x80) {
    buffer->putChar(QChar(raw));
  } else if (raw >= 0x80) {
    const QString decoded = core->utf8Decoder.decode(QByteArrayView(&ch, 1));
    for (QChar out : decoded) buffer->putChar(out);
  }
}

void handleCsiByte(VtParserCore *core, TerminalBuffer *buffer, char ch) {
  if (ch == '?' && core->params.isEmpty() && core->currentParam < 0) {
    core->csiPrivate = true;
  } else if (ch >= '0' && ch <= '9') {
    if (core->currentParam < 0) core->currentParam = 0;
    core->currentParam = core->currentParam * 10 + (ch - '0');
  } else if (ch == ';') {
    finalizeParam(core);
  } else {
    finalizeParam(core);
    if (core->csiPrivate) {
      handlePrivateModeCommand(core, buffer, ch);
    } else {
      handleCsiCommand(core, buffer, ch);
    }
    core->csiPrivate = false;
    core->state = VtParserCore::State::Normal;
  }
}

bool handleOscByte(VtParserCore *core, unsigned char raw, QString *titleOut) {
  if (raw == 0x07) {
    bool changed = handleOsc(core, titleOut);
    core->state = VtParserCore::State::Normal;
    return changed;
  }
  if (raw == 0x1b) {
    core->state = VtParserCore::State::OscEscape;
  } else {
    core->oscString.append(QChar(raw));
  }
  return false;
}

void handleEscapeByte(VtParserCore *core, char ch) {
  if (ch == '[') {
    core->state = VtParserCore::State::Csi;
    core->params.clear();
    core->currentParam = -1;
    core->csiPrivate = false;
  } else if (ch == ']') {
    core->state = VtParserCore::State::Osc;
    core->oscString.clear();
  } else {
    core->state = VtParserCore::State::Normal;
  }
}

bool handleOscEscapeByte(VtParserCore *core, char ch, QString *titleOut) {
  const bool changed = (ch == '\\') && handleOsc(core, titleOut);
  core->state = VtParserCore::State::Normal;
  return changed;
}
}

VtParserCore *createVtParserCore() {
  return new VtParserCore();
}

void destroyVtParserCore(VtParserCore *core) {
  delete core;
}

void resetVtParserCore(VtParserCore *core) {
  core->state = VtParserCore::State::Normal;
  core->params.clear();
  core->currentParam = -1;
  core->csiPrivate = false;
  core->utf8Decoder.resetState();
  core->oscString.clear();
}

bool feedVtParserCore(VtParserCore *core, TerminalBuffer *buffer,
                      const QByteArray &data, QString *titleOut) {
  bool titleChanged = false;
  for (unsigned char raw : data) {
    const char ch = static_cast<char>(raw);
    switch (core->state) {
      case VtParserCore::State::Normal:     handleNormalByte(core, buffer, raw);         break;
      case VtParserCore::State::Escape:     handleEscapeByte(core, ch);                  break;
      case VtParserCore::State::Csi:        handleCsiByte(core, buffer, ch);             break;
      case VtParserCore::State::Osc:        titleChanged |= handleOscByte(core, raw, titleOut);         break;
      case VtParserCore::State::OscEscape:  titleChanged |= handleOscEscapeByte(core, ch, titleOut);    break;
    }
  }
  return titleChanged;
}
