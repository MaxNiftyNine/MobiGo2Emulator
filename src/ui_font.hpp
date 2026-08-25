#pragma once

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace mobigo::desktop::ui_font {

namespace detail {

inline bool continuation(unsigned char byte) { return (byte & 0xc0) == 0x80; }

// Advance over exactly one UTF-8 scalar value. Invalid input consumes one byte
// and is displayed with the same single replacement glyph as a valid Unicode
// character outside this deliberately small bitmap face.
inline char next_bitmap_glyph(std::string_view text, size_t &cursor) {
  const auto byte = [&](size_t offset) {
    return static_cast<unsigned char>(text[cursor + offset]);
  };
  const unsigned char first = byte(0);
  if (first < 0x80) {
    ++cursor;
    return static_cast<char>(first);
  }

  size_t length = 0;
  bool valid = false;
  if (first >= 0xc2 && first <= 0xdf && cursor + 1 < text.size()) {
    length = 2;
    valid = continuation(byte(1));
  } else if (first >= 0xe0 && first <= 0xef && cursor + 2 < text.size()) {
    length = 3;
    valid = continuation(byte(1)) && continuation(byte(2)) &&
            (first != 0xe0 || byte(1) >= 0xa0) &&
            (first != 0xed || byte(1) < 0xa0);
  } else if (first >= 0xf0 && first <= 0xf4 && cursor + 3 < text.size()) {
    length = 4;
    valid = continuation(byte(1)) && continuation(byte(2)) &&
            continuation(byte(3)) && (first != 0xf0 || byte(1) >= 0x90) &&
            (first != 0xf4 || byte(1) < 0x90);
  }
  cursor += valid ? length : 1;
  return '?';
}

inline size_t prefix_bytes(std::string_view text, size_t glyph_limit) {
  size_t cursor = 0;
  for (size_t glyph = 0; glyph < glyph_limit && cursor < text.size(); ++glyph)
    (void)next_bitmap_glyph(text, cursor);
  return cursor;
}

} // namespace detail

// A small built-in 5x7 face keeps the desktop frontend dependency-free and
// makes error dialogs usable even when no OS font service is available. The
// glyphs were drawn for this project and cover the printable UI character set.
inline std::array<uint8_t, 7> glyph(char input) {
  const char c = char(std::toupper(static_cast<unsigned char>(input)));
  switch (c) {
  case 'A':
    return {14, 17, 17, 31, 17, 17, 17};
  case 'B':
    return {30, 17, 17, 30, 17, 17, 30};
  case 'C':
    return {14, 17, 16, 16, 16, 17, 14};
  case 'D':
    return {30, 17, 17, 17, 17, 17, 30};
  case 'E':
    return {31, 16, 16, 30, 16, 16, 31};
  case 'F':
    return {31, 16, 16, 30, 16, 16, 16};
  case 'G':
    return {14, 17, 16, 23, 17, 17, 15};
  case 'H':
    return {17, 17, 17, 31, 17, 17, 17};
  case 'I':
    return {14, 4, 4, 4, 4, 4, 14};
  case 'J':
    return {7, 2, 2, 2, 18, 18, 12};
  case 'K':
    return {17, 18, 20, 24, 20, 18, 17};
  case 'L':
    return {16, 16, 16, 16, 16, 16, 31};
  case 'M':
    return {17, 27, 21, 21, 17, 17, 17};
  case 'N':
    return {17, 25, 21, 19, 17, 17, 17};
  case 'O':
    return {14, 17, 17, 17, 17, 17, 14};
  case 'P':
    return {30, 17, 17, 30, 16, 16, 16};
  case 'Q':
    return {14, 17, 17, 17, 21, 18, 13};
  case 'R':
    return {30, 17, 17, 30, 20, 18, 17};
  case 'S':
    return {15, 16, 16, 14, 1, 1, 30};
  case 'T':
    return {31, 4, 4, 4, 4, 4, 4};
  case 'U':
    return {17, 17, 17, 17, 17, 17, 14};
  case 'V':
    return {17, 17, 17, 17, 17, 10, 4};
  case 'W':
    return {17, 17, 17, 21, 21, 21, 10};
  case 'X':
    return {17, 17, 10, 4, 10, 17, 17};
  case 'Y':
    return {17, 17, 10, 4, 4, 4, 4};
  case 'Z':
    return {31, 1, 2, 4, 8, 16, 31};
  case '0':
    return {14, 17, 19, 21, 25, 17, 14};
  case '1':
    return {4, 12, 4, 4, 4, 4, 14};
  case '2':
    return {14, 17, 1, 2, 4, 8, 31};
  case '3':
    return {30, 1, 1, 14, 1, 1, 30};
  case '4':
    return {2, 6, 10, 18, 31, 2, 2};
  case '5':
    return {31, 16, 16, 30, 1, 1, 30};
  case '6':
    return {14, 16, 16, 30, 17, 17, 14};
  case '7':
    return {31, 1, 2, 4, 8, 8, 8};
  case '8':
    return {14, 17, 17, 14, 17, 17, 14};
  case '9':
    return {14, 17, 17, 15, 1, 1, 14};
  case '.':
    return {0, 0, 0, 0, 0, 6, 6};
  case ',':
    return {0, 0, 0, 0, 6, 6, 4};
  case ':':
    return {0, 6, 6, 0, 6, 6, 0};
  case ';':
    return {0, 6, 6, 0, 6, 6, 4};
  case '!':
    return {4, 4, 4, 4, 4, 0, 4};
  case '?':
    return {14, 17, 1, 2, 4, 0, 4};
  case '-':
    return {0, 0, 0, 31, 0, 0, 0};
  case '_':
    return {0, 0, 0, 0, 0, 0, 31};
  case '/':
    return {1, 2, 2, 4, 8, 8, 16};
  case '\\':
    return {16, 8, 8, 4, 2, 2, 1};
  case '(':
    return {2, 4, 8, 8, 8, 4, 2};
  case ')':
    return {8, 4, 2, 2, 2, 4, 8};
  case '[':
    return {14, 8, 8, 8, 8, 8, 14};
  case ']':
    return {14, 2, 2, 2, 2, 2, 14};
  case '+':
    return {0, 4, 4, 31, 4, 4, 0};
  case '=':
    return {0, 0, 31, 0, 31, 0, 0};
  case '<':
    return {2, 4, 8, 16, 8, 4, 2};
  case '>':
    return {8, 4, 2, 1, 2, 4, 8};
  case '#':
    return {10, 31, 10, 10, 31, 10, 0};
  case '%':
    return {17, 2, 4, 8, 17, 0, 0};
  case '&':
    return {12, 18, 20, 8, 21, 18, 13};
  case '@':
    return {14, 17, 23, 21, 23, 16, 14};
  case '*':
    return {0, 21, 14, 31, 14, 21, 0};
  case '\'':
    return {4, 4, 8, 0, 0, 0, 0};
  case '"':
    return {10, 10, 0, 0, 0, 0, 0};
  case '|':
    return {4, 4, 4, 4, 4, 4, 4};
  case ' ':
    return {0, 0, 0, 0, 0, 0, 0};
  default:
    return {14, 17, 1, 2, 4, 0, 4};
  }
}

inline size_t glyph_count(std::string_view text) {
  size_t count = 0;
  size_t cursor = 0;
  while (cursor < text.size()) {
    (void)detail::next_bitmap_glyph(text, cursor);
    ++count;
  }
  return count;
}

inline std::string bitmap_glyphs(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  size_t cursor = 0;
  while (cursor < text.size())
    result.push_back(detail::next_bitmap_glyph(text, cursor));
  return result;
}

inline std::string abbreviate(std::string_view text, size_t maximum_glyphs) {
  if (glyph_count(text) <= maximum_glyphs)
    return std::string(text);
  if (maximum_glyphs <= 3)
    return std::string(
        text.substr(0, detail::prefix_bytes(text, maximum_glyphs)));
  const size_t prefix = detail::prefix_bytes(text, maximum_glyphs - 3);
  return std::string(text.substr(0, prefix)) + "...";
}

inline int width(std::string_view text, int scale = 2) {
  int widest = 0;
  int current = 0;
  size_t cursor = 0;
  while (cursor < text.size()) {
    const char display = detail::next_bitmap_glyph(text, cursor);
    if (display == '\n') {
      widest = std::max(widest, current);
      current = 0;
    } else {
      current += 6 * scale;
    }
  }
  return std::max(widest, current);
}

inline void draw(SDL_Renderer *renderer, int x, int y, std::string_view text,
                 SDL_Color color, int scale = 2) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  std::vector<SDL_Rect> pixels;
  pixels.reserve(text.size() * 16);
  int cursor = x;
  size_t text_cursor = 0;
  while (text_cursor < text.size()) {
    const char c = detail::next_bitmap_glyph(text, text_cursor);
    if (c == '\n') {
      cursor = x;
      y += 9 * scale;
      continue;
    }
    const auto rows = glyph(c);
    for (int row = 0; row < 7; ++row) {
      int run_start = -1;
      for (int column = 0; column <= 5; ++column) {
        const bool filled =
            column < 5 && (rows[size_t(row)] & (1 << (4 - column)));
        if (filled && run_start < 0)
          run_start = column;
        if (!filled && run_start >= 0) {
          pixels.push_back({cursor + run_start * scale, y + row * scale,
                            (column - run_start) * scale, scale});
          run_start = -1;
        }
      }
    }
    cursor += 6 * scale;
  }
  if (!pixels.empty())
    SDL_RenderFillRects(renderer, pixels.data(), int(pixels.size()));
}

} // namespace mobigo::desktop::ui_font
