#pragma once
namespace mapzoom {
struct Pair { float x, y; };
inline bool ExpectedMapRows(const Pair* rows) {
    return rows[0].x == 800.0f && rows[0].y == 0.0f &&
           rows[1].x == 1600.0f && rows[1].y == 0.0f &&
           rows[2].x == 2.0f && rows[2].y == 4.0f;
}
inline bool ValidPercent(int percent) { return percent >= 100 && percent <= 400; }
inline float InitialRange(int percent) { return 800.0f / (percent / 100.0f); }
}
