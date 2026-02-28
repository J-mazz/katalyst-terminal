export module terminal.ansi.color;

import std;

export struct AnsiRgb {
  int r = 0;
  int g = 0;
  int b = 0;
};

export constexpr AnsiRgb ansiColorFromXtermIndex(int index) {
  constexpr AnsiRgb kBaseColors[16] = {
      {0, 0, 0},       {170, 0, 0},     {0, 170, 0},      {170, 85, 0},
      {0, 0, 170},     {170, 0, 170},   {0, 170, 170},    {170, 170, 170},
      {85, 85, 85},    {255, 85, 85},   {85, 255, 85},    {255, 255, 85},
      {85, 85, 255},   {255, 85, 255},  {85, 255, 255},   {255, 255, 255},
  };

  const int idx = (index < 0) ? 0 : (index > 255 ? 255 : index);
  if (idx < 16) {
    return kBaseColors[idx];
  }

  if (idx < 232) {
    constexpr int levels[6] = {0, 95, 135, 175, 215, 255};
    const int cube = idx - 16;
    const int r = cube / 36;
    const int g = (cube / 6) % 6;
    const int b = cube % 6;
    return {levels[r], levels[g], levels[b]};
  }

  const int gray = 8 + (idx - 232) * 10;
  return {gray, gray, gray};
}
