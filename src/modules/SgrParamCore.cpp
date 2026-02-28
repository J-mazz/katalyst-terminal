export module terminal.sgr.param;

import std;

export enum class SgrAction {
  Reset,
  BoldOn,
  ItalicOn,
  UnderlineOn,
  InverseOn,
  StrikeOn,
  BoldOff,
  ItalicOff,
  UnderlineOff,
  InverseOff,
  StrikeOff,
  FgDefault,
  BgDefault,
  Unknown,
};

export constexpr SgrAction classifySgrParam(int param) {
  if (param <= 0) {
    return SgrAction::Reset;
  }

  switch (param) {
    case 1:
      return SgrAction::BoldOn;
    case 3:
      return SgrAction::ItalicOn;
    case 4:
      return SgrAction::UnderlineOn;
    case 7:
      return SgrAction::InverseOn;
    case 9:
      return SgrAction::StrikeOn;
    case 22:
      return SgrAction::BoldOff;
    case 23:
      return SgrAction::ItalicOff;
    case 24:
      return SgrAction::UnderlineOff;
    case 27:
      return SgrAction::InverseOff;
    case 29:
      return SgrAction::StrikeOff;
    case 39:
      return SgrAction::FgDefault;
    case 49:
      return SgrAction::BgDefault;
    default:
      return SgrAction::Unknown;
  }
}
