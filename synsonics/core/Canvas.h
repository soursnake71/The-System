// A small anti-aliased software rasterizer: the panel is drawn with this into a plain pixel buffer, so the plugin
// (JUCE image) and the Android app (ANativeWindow) show exactly the same pixels. Shapes are signed distance fields
// evaluated per pixel inside their bounding box; text is a 5x7 dot-matrix font scaled up.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace syn {

struct Col {
  float r, g, b, a;
  constexpr Col(float r_ = 0, float g_ = 0, float b_ = 0, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
  static constexpr Col hex(uint32_t v, float a = 1) { return Col(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, a); }
  Col withA(float na) const { return Col(r, g, b, na); }
  Col mix(const Col& o, float t) const { return Col(r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t); }
  Col scaled(float k) const { return Col(std::min(1.f, r * k), std::min(1.f, g * k), std::min(1.f, b * k), a); }
};

class Canvas {
 public:
  uint32_t* px = nullptr;
  int w = 0, h = 0, stride = 0;  // stride in pixels
  bool rgba = false;             // false: 0xAARRGGBB words (JUCE ARGB), true: bytes R,G,B,A (Android RGBA_8888)
  // logical -> pixel transform
  float s = 1, ox = 0, oy = 0;
  // clip in pixels
  int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;

  void attach(uint32_t* p, int width, int height, int strideInPixels, bool rgbaOrder) {
    px = p; w = width; h = height; stride = strideInPixels; rgba = rgbaOrder;
    resetClip();
  }
  void setTransform(float scale, float offX, float offY) { s = scale; ox = offX; oy = offY; }
  void resetClip() { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }
  void clipLogical(float x, float y, float ww, float hh) {
    cx0 = std::max(0, int(std::floor(ox + x * s))); cy0 = std::max(0, int(std::floor(oy + y * s)));
    cx1 = std::min(w, int(std::ceil(ox + (x + ww) * s))); cy1 = std::min(h, int(std::ceil(oy + (y + hh) * s)));
  }

  uint32_t pack(const Col& c) const {
    auto q = [](float v) { return uint32_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    if (rgba) return (q(c.a) << 24) | (q(c.b) << 16) | (q(c.g) << 8) | q(c.r);
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
  }
  void fillAllPixels(const Col& c) {
    uint32_t v = pack(c);
    for (int y = 0; y < h; ++y) std::fill(px + y * stride, px + y * stride + w, v);
  }

  inline void blend(int x, int y, const Col& c, float cov) {
    float a = c.a * cov;
    if (a <= 0.002f) return;
    uint32_t& d = px[y * stride + x];
    if (a >= 0.998f) { d = pack(c); return; }
    float dr, dg, db;
    if (rgba) { dr = (d & 255) / 255.f; dg = ((d >> 8) & 255) / 255.f; db = ((d >> 16) & 255) / 255.f; }
    else { dr = ((d >> 16) & 255) / 255.f; dg = ((d >> 8) & 255) / 255.f; db = (d & 255) / 255.f; }
    d = pack(Col(dr + (c.r - dr) * a, dg + (c.g - dg) * a, db + (c.b - db) * a, 1));
  }

  // generic shape fill: sdf(px, py) in pixel units (negative inside), color(px, py)
  template <class Sdf, class Shade>
  void shape(float lx0, float ly0, float lx1, float ly1, Sdf sdf, Shade shade) {
    int x0 = std::max(cx0, int(std::floor(ox + lx0 * s)) - 1), y0 = std::max(cy0, int(std::floor(oy + ly0 * s)) - 1);
    int x1 = std::min(cx1, int(std::ceil(ox + lx1 * s)) + 1), y1 = std::min(cy1, int(std::ceil(oy + ly1 * s)) + 1);
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        float fx = x + 0.5f, fy = y + 0.5f;
        float d = sdf(fx, fy);
        float cov = std::clamp(0.5f - d, 0.f, 1.f);
        if (cov > 0) blend(x, y, shade((fx - ox) / s, (fy - oy) / s), cov);
      }
  }

  static float sdRoundRect(float px_, float py_, float cx, float cy, float hw, float hh, float r) {
    float qx = std::fabs(px_ - cx) - (hw - r), qy = std::fabs(py_ - cy) - (hh - r);
    float ox_ = std::max(qx, 0.f), oy_ = std::max(qy, 0.f);
    return std::sqrt(ox_ * ox_ + oy_ * oy_) + std::min(std::max(qx, qy), 0.f) - r;
  }

  // ---- primitives (logical coordinates)
  template <class Shade>
  void roundRectShaded(float x, float y, float ww, float hh, float r, Shade shade) {
    float pcx = ox + (x + ww / 2) * s, pcy = oy + (y + hh / 2) * s, hw = ww / 2 * s, hh2 = hh / 2 * s, pr = std::min(r * s, std::min(hw, hh2));
    shape(x, y, x + ww, y + hh, [&](float a, float b) { return sdRoundRect(a, b, pcx, pcy, hw, hh2, pr); }, shade);
  }
  void roundRect(float x, float y, float ww, float hh, float r, const Col& c) { roundRectShaded(x, y, ww, hh, r, [&](float, float) { return c; }); }
  void rect(float x, float y, float ww, float hh, const Col& c) { roundRect(x, y, ww, hh, 0, c); }
  void vGradient(float x, float y, float ww, float hh, float r, const Col& top, const Col& bot) {
    roundRectShaded(x, y, ww, hh, r, [&](float, float ly) { return top.mix(bot, std::clamp((ly - y) / hh, 0.f, 1.f)); });
  }
  void strokeRoundRect(float x, float y, float ww, float hh, float r, float th, const Col& c) {
    float pcx = ox + (x + ww / 2) * s, pcy = oy + (y + hh / 2) * s, hw = ww / 2 * s, hh2 = hh / 2 * s, pr = std::min(r * s, std::min(hw, hh2)), t = th * s / 2;
    shape(x - th, y - th, x + ww + th, y + hh + th, [&](float a, float b) { return std::fabs(sdRoundRect(a, b, pcx, pcy, hw, hh2, pr)) - t; }, [&](float, float) { return c; });
  }
  template <class Shade>
  void circleShaded(float cx, float cy, float r, Shade shade) {
    float pcx = ox + cx * s, pcy = oy + cy * s, pr = r * s;
    shape(cx - r, cy - r, cx + r, cy + r, [&](float a, float b) { return std::hypot(a - pcx, b - pcy) - pr; }, shade);
  }
  void circle(float cx, float cy, float r, const Col& c) { circleShaded(cx, cy, r, [&](float, float) { return c; }); }
  void ring(float cx, float cy, float r, float th, const Col& c) {
    float pcx = ox + cx * s, pcy = oy + cy * s, pr = r * s, t = th * s / 2;
    shape(cx - r - th, cy - r - th, cx + r + th, cy + r + th, [&](float a, float b) { return std::fabs(std::hypot(a - pcx, b - pcy) - pr) - t; }, [&](float, float) { return c; });
  }
  void line(float x0, float y0, float x1, float y1, float th, const Col& c) {
    float ax = ox + x0 * s, ay = oy + y0 * s, bx = ox + x1 * s, by = oy + y1 * s, t = th * s / 2;
    float dx = bx - ax, dy = by - ay, L2 = std::max(1e-6f, dx * dx + dy * dy);
    shape(std::min(x0, x1) - th, std::min(y0, y1) - th, std::max(x0, x1) + th, std::max(y0, y1) + th,
          [&](float a, float b) {
            float t_ = std::clamp(((a - ax) * dx + (b - ay) * dy) / L2, 0.f, 1.f);
            return std::hypot(a - ax - dx * t_, b - ay - dy * t_) - t;
          },
          [&](float, float) { return c; });
  }
  void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Col& c) {
    float P[3][2] = {{ox + x0 * s, oy + y0 * s}, {ox + x1 * s, oy + y1 * s}, {ox + x2 * s, oy + y2 * s}};
    float area = (P[1][0] - P[0][0]) * (P[2][1] - P[0][1]) - (P[1][1] - P[0][1]) * (P[2][0] - P[0][0]);
    float sg = area < 0 ? -1.f : 1.f;
    shape(std::min({x0, x1, x2}), std::min({y0, y1, y2}), std::max({x0, x1, x2}), std::max({y0, y1, y2}),
          [&](float a, float b) {
            float d = -1e9f;
            for (int i = 0; i < 3; ++i) {
              const float* p0 = P[i]; const float* p1 = P[(i + 1) % 3];
              float ex = p1[0] - p0[0], ey = p1[1] - p0[1], L = std::max(1e-6f, std::hypot(ex, ey));
              d = std::max(d, -sg * (ex * (b - p0[1]) - ey * (a - p0[0])) / L);
            }
            return d;
          },
          [&](float, float) { return c; });
  }

  // ---- text: 5x7 dot matrix. size = cap height in logical units
  enum Align { Left, Center, Right };
  static const uint8_t* glyph(char ch) {
    static const uint8_t blank[7] = {0};
    static const struct { char c; uint8_t rows[7]; } font[] = {
        {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
        {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
        {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
        {'J', {7, 2, 2, 2, 2, 18, 12}}, {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
        {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 17, 25, 21, 19, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
        {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
        {'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}}, {'U', {17, 17, 17, 17, 17, 17, 14}},
        {'V', {17, 17, 17, 17, 17, 10, 4}}, {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
        {'Y', {17, 17, 10, 4, 4, 4, 4}}, {'Z', {31, 1, 2, 4, 8, 16, 31}}, {'0', {14, 17, 19, 21, 25, 17, 14}},
        {'1', {4, 12, 4, 4, 4, 4, 14}}, {'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {31, 2, 4, 2, 1, 17, 14}},
        {'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 30, 1, 1, 17, 14}}, {'6', {6, 8, 16, 30, 17, 17, 14}},
        {'7', {31, 1, 2, 4, 8, 8, 8}}, {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},
        {'/', {1, 1, 2, 4, 8, 16, 16}}, {'-', {0, 0, 0, 31, 0, 0, 0}}, {'.', {0, 0, 0, 0, 0, 12, 12}},
        {':', {0, 12, 12, 0, 12, 12, 0}}, {'+', {0, 4, 4, 31, 4, 4, 0}}, {'(', {2, 4, 8, 8, 8, 4, 2}},
        {')', {8, 4, 2, 2, 2, 4, 8}}, {'!', {4, 4, 4, 4, 4, 0, 4}}, {'%', {24, 25, 2, 4, 8, 19, 3}},
        {'<', {2, 4, 8, 16, 8, 4, 2}}, {'>', {8, 4, 2, 1, 2, 4, 8}}, {'#', {10, 10, 31, 10, 31, 10, 10}},
        {'\'', {4, 4, 8, 0, 0, 0, 0}}, {'&', {12, 18, 20, 8, 21, 18, 13}}, {'?', {14, 17, 1, 2, 4, 0, 4}},
        {'=', {0, 0, 31, 0, 31, 0, 0}}, {',', {0, 0, 0, 0, 12, 4, 8}},
    };
    if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
    for (auto& g : font) if (g.c == ch) return g.rows;
    return blank;
  }
  static float textWidth(const std::string& t, float size, float spacing = 1.f) {
    float p = size / 7.f;
    return t.empty() ? 0 : (t.size() * (5 + spacing) - spacing) * p;
  }
  // style 0 solid dots, 1 outlined (the SYNSONICS DRUMS logo), bold widens the dots
  void text(float x, float y, float size, const std::string& t, const Col& c, Align al = Left, float spacing = 1.f, bool bold = false,
            const Col* outlineFill = nullptr) {
    float p = size / 7.f;
    float tw = textWidth(t, size, spacing);
    if (al == Center) x -= tw / 2;
    else if (al == Right) x -= tw;
    float grow = bold ? p * 0.18f : 0.f;
    for (int pass = 0; pass < (outlineFill ? 2 : 1); ++pass) {
      float cx = x;
      for (char ch : t) {
        const uint8_t* g = glyph(ch);
        for (int r = 0; r < 7; ++r)
          for (int k = 0; k < 5; ++k)
            if (g[r] & (16 >> k)) {
              float px0 = cx + k * p, py0 = y + r * p;
              if (outlineFill && pass == 0) roundRect(px0 - p * 0.32f, py0 - p * 0.32f, p * 1.64f, p * 1.64f, p * 0.3f, c);
              else if (outlineFill) roundRect(px0 + p * 0.02f, py0 + p * 0.02f, p * 0.96f, p * 0.96f, p * 0.05f, *outlineFill);
              else roundRect(px0 + p * 0.06f - grow, py0 + p * 0.06f - grow, p * 0.88f + 2 * grow, p * 0.88f + 2 * grow, p * 0.18f, c);
            }
        cx += (5 + spacing) * p;
      }
    }
  }
};

}  // namespace syn
