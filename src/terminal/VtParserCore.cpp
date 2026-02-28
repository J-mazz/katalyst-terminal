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
  QStringDecoder utf8Decoder{QStringDecoder::Utf8};
  QString oscString;
};

namespace {
QColor xtermColorFromIndex(int index) {
  const AnsiRgb rgb = ansiColorFromXtermIndex(index);
  return QColor(rgb.r, rgb.g, rgb.b);
}

void finalizeParam(VtParserCore *core) {
  if (core->currentParam >= 0) {
    core->params.push_back(core->currentParam);
  } else {
    core->params.push_back(-1);
  }
  core->currentParam = -1;
}

bool handleOsc(VtParserCore *core, QString *titleOut) {
  int semi = core->oscString.indexOf(QLatin1Char(';'));
  if (semi < 0) {
    return false;
  }
  bool ok = false;
  int ps = core->oscString.left(semi).toInt(&ok);
  if (!ok) {
    return false;
  }
  QString pt = core->oscString.mid(semi + 1);

  if (ps == 0 || ps == 2) {
    if (titleOut) {
      *titleOut = pt;
    }
    return true;
  }
  return false;
}

void handleCsiCommand(VtParserCore *core, TerminalBuffer *buffer, char command) {
  switch (command) {
    case 'J': {
      int mode = core->params.isEmpty() ? 0 : core->params.first();
      if (mode == 0 || mode == 2) {
        buffer->clear();
      }
      break;
    }
    case 'H': {
      int row = core->params.size() > 0 && core->params[0] > 0
                    ? core->params[0] - 1
                    : 0;
      int col = core->params.size() > 1 && core->params[1] > 0
                    ? core->params[1] - 1
                    : 0;
      buffer->setCursorPosition(row, col);
      break;
    }
    case 'K':
      buffer->clearLine();
      break;
    case 'm': {
      if (core->params.isEmpty()) {
        core->params.push_back(0);
      }

      for (int i = 0; i < core->params.size(); ++i) {
        int param = core->params[i];
        switch (classifySgrParam(param)) {
          case SgrAction::Reset:
            buffer->resetAttributes();
            continue;
          case SgrAction::BoldOn:
            buffer->setBold(true);
            continue;
          case SgrAction::ItalicOn:
            buffer->setItalic(true);
            continue;
          case SgrAction::UnderlineOn:
            buffer->setUnderline(true);
            continue;
          case SgrAction::InverseOn:
            buffer->setInverse(true);
            continue;
          case SgrAction::StrikeOn:
            buffer->setStrikethrough(true);
            continue;
          case SgrAction::BoldOff:
            buffer->setBold(false);
            continue;
          case SgrAction::ItalicOff:
            buffer->setItalic(false);
            continue;
          case SgrAction::UnderlineOff:
            buffer->setUnderline(false);
            continue;
          case SgrAction::InverseOff:
            buffer->setInverse(false);
            continue;
          case SgrAction::StrikeOff:
            buffer->setStrikethrough(false);
            continue;
          case SgrAction::FgDefault:
            buffer->setForeground(buffer->defaultForeground());
            continue;
          case SgrAction::BgDefault:
            buffer->setBackground(buffer->defaultBackground());
            continue;
          case SgrAction::Unknown:
            break;
        }

        if (param == 38 || param == 48) {
          bool isFg = (param == 38);
          if (i + 1 < core->params.size() && core->params[i + 1] == 5) {
            if (i + 2 < core->params.size()) {
              int idx = core->params[i + 2];
              if (isFg) {
                buffer->setForeground(xtermColorFromIndex(idx));
              } else {
                buffer->setBackground(xtermColorFromIndex(idx));
              }
              i += 2;
            }
          } else if (i + 1 < core->params.size() && core->params[i + 1] == 2) {
            if (i + 4 < core->params.size()) {
              int r = qBound(0, core->params[i + 2], 255);
              int g = qBound(0, core->params[i + 3], 255);
              int b = qBound(0, core->params[i + 4], 255);
              if (isFg) {
                buffer->setForeground(QColor(r, g, b));
              } else {
                buffer->setBackground(QColor(r, g, b));
              }
              i += 4;
            }
          }
          continue;
        }

        if (param >= 30 && param <= 37) {
          buffer->setForeground(xtermColorFromIndex(param - 30));
          continue;
        }
        if (param >= 90 && param <= 97) {
          buffer->setForeground(xtermColorFromIndex(param - 90 + 8));
          continue;
        }
        if (param >= 40 && param <= 47) {
          buffer->setBackground(xtermColorFromIndex(param - 40));
          continue;
        }
        if (param >= 100 && param <= 107) {
          buffer->setBackground(xtermColorFromIndex(param - 100 + 8));
          continue;
        }
      }
      break;
    }
    default:
      break;
  }
  core->params.clear();
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
  core->utf8Decoder.resetState();
  core->oscString.clear();
}

bool feedVtParserCore(VtParserCore *core, TerminalBuffer *buffer,
                      const QByteArray &data, QString *titleOut) {
  bool titleChanged = false;
  for (unsigned char raw : data) {
    const char ch = static_cast<char>(raw);
    switch (core->state) {
      case VtParserCore::State::Normal:
        if (raw == 0x1b) {
          core->state = VtParserCore::State::Escape;
        } else if (raw == 0x0a) {
          buffer->newline();
        } else if (raw == 0x0d) {
          buffer->carriageReturn();
        } else if (raw == 0x08) {
          buffer->backspace();
        } else if (raw == 0x09) {
          buffer->tab();
        } else if (raw == 0x7f) {
          break;
        } else if (raw >= 0x20 && raw < 0x80) {
          buffer->putChar(QChar(raw));
        } else if (raw >= 0x80) {
          const QString decoded = core->utf8Decoder.decode(QByteArrayView(&ch, 1));
          for (QChar out : decoded) {
            buffer->putChar(out);
          }
        }
        break;
      case VtParserCore::State::Escape:
        if (ch == '[') {
          core->state = VtParserCore::State::Csi;
          core->params.clear();
          core->currentParam = -1;
        } else if (ch == ']') {
          core->state = VtParserCore::State::Osc;
          core->oscString.clear();
        } else {
          core->state = VtParserCore::State::Normal;
        }
        break;
      case VtParserCore::State::Csi:
        if (ch >= '0' && ch <= '9') {
          if (core->currentParam < 0) {
            core->currentParam = 0;
          }
          core->currentParam = core->currentParam * 10 + (ch - '0');
        } else if (ch == ';') {
          finalizeParam(core);
        } else {
          finalizeParam(core);
          handleCsiCommand(core, buffer, ch);
          core->state = VtParserCore::State::Normal;
        }
        break;
      case VtParserCore::State::Osc:
        if (raw == 0x07) {
          titleChanged = handleOsc(core, titleOut) || titleChanged;
          core->state = VtParserCore::State::Normal;
        } else if (raw == 0x1b) {
          core->state = VtParserCore::State::OscEscape;
        } else {
          core->oscString.append(QChar(raw));
        }
        break;
      case VtParserCore::State::OscEscape:
        if (ch == '\\') {
          titleChanged = handleOsc(core, titleOut) || titleChanged;
        }
        core->state = VtParserCore::State::Normal;
        break;
    }
  }
  return titleChanged;
}
