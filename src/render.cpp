#include "render.hpp"
#include "font_generated.h"
#include "stb_truetype.h"

#include <cmath>
#include <vector>

namespace tf {

namespace {
constexpr float kNativePx = 192.0f;  // text is rasterised once at this height
stbtt_fontinfo gFont;
bool gFontReady = false;

inline SDL_FColor toFC(Color c) { return SDL_FColor{c.r, c.g, c.b, c.a}; }
}  // namespace

bool Renderer::init(SDL_Renderer* r) {
    r_ = r;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    if (!stbtt_InitFont(&gFont, kFontData, stbtt_GetFontOffsetForIndex(kFontData, 0))) {
        SDL_Log("font init failed");
        return false;
    }
    gFontReady = true;
    return true;
}

void Renderer::shutdown() {
    for (auto& [k, g] : cache_)
        if (g.tex) SDL_DestroyTexture(g.tex);
    cache_.clear();
}

// --- primitives ----------------------------------------------------------

void Renderer::fillRect(SDL_FRect r, Color c) {
    r.x += origin_.x; r.y += origin_.y;
    SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r_, &r);
}

void Renderer::fillRoundedRectGrad(SDL_FRect r, float radius, Color top, Color bottom) {
    r.x += origin_.x; r.y += origin_.y;
    radius = SDL_min(radius, SDL_min(r.w, r.h) * 0.5f);
    auto colAtY = [&](float y) {
        float t = r.h > 0 ? saturate((y - r.y) / r.h) : 0.0f;
        return toFC(lerp(top, bottom, t));
    };
    struct Corner { float cx, cy, a0, a1; };
    const float d2r = kPi / 180.0f;
    Corner corners[4] = {
        {r.x + r.w - radius, r.y + radius, -90, 0},
        {r.x + r.w - radius, r.y + r.h - radius, 0, 90},
        {r.x + radius, r.y + r.h - radius, 90, 180},
        {r.x + radius, r.y + radius, 180, 270},
    };
    const int seg = 6;
    std::vector<SDL_Vertex> verts;
    SDL_FPoint center{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
    verts.push_back({center, colAtY(center.y), {0, 0}});
    for (auto& cr : corners)
        for (int i = 0; i <= seg; ++i) {
            float a = (cr.a0 + (cr.a1 - cr.a0) * i / seg) * d2r;
            SDL_FPoint p{cr.cx + std::cos(a) * radius, cr.cy + std::sin(a) * radius};
            verts.push_back({p, colAtY(p.y), {0, 0}});
        }
    std::vector<int> idx;
    for (int i = 1; i + 1 < (int)verts.size(); ++i) { idx.push_back(0); idx.push_back(i); idx.push_back(i + 1); }
    idx.push_back(0); idx.push_back((int)verts.size() - 1); idx.push_back(1);
    SDL_RenderGeometry(r_, nullptr, verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
}

void Renderer::fillRoundedRect(SDL_FRect r, float radius, Color c) {
    fillRoundedRectGrad(r, radius, c, c);
}

void Renderer::roundedShadow(SDL_FRect r, float radius, Color c, float blur, Vec2 offset) {
    const int layers = 6;
    for (int i = layers; i >= 1; --i) {
        float t = i / (float)layers;
        float grow = blur * t;
        SDL_FRect rr{r.x - grow + offset.x, r.y - grow + offset.y, r.w + 2 * grow, r.h + 2 * grow};
        fillRoundedRect(rr, radius + grow, withAlpha(c, 1.0f / layers));
    }
}

void Renderer::fillCircle(float cx, float cy, float radius, Color c) {
    cx += origin_.x; cy += origin_.y;
    const int seg = 28;
    std::vector<SDL_Vertex> verts;
    SDL_FColor fc = toFC(c);
    verts.push_back({{cx, cy}, fc, {0, 0}});
    for (int i = 0; i <= seg; ++i) {
        float a = (2 * kPi) * i / seg;
        verts.push_back({{cx + std::cos(a) * radius, cy + std::sin(a) * radius}, fc, {0, 0}});
    }
    std::vector<int> idx;
    for (int i = 1; i + 1 < (int)verts.size(); ++i) { idx.push_back(0); idx.push_back(i); idx.push_back(i + 1); }
    SDL_RenderGeometry(r_, nullptr, verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
}

void Renderer::fillTri(Vec2 a, Vec2 b, Vec2 c, Color col) {
    a = a + origin_; b = b + origin_; c = c + origin_;
    SDL_FColor fc = toFC(col);
    SDL_Vertex v[3] = {{{a.x, a.y}, fc, {0, 0}}, {{b.x, b.y}, fc, {0, 0}}, {{c.x, c.y}, fc, {0, 0}}};
    SDL_RenderGeometry(r_, nullptr, v, 3, nullptr, 0);
}

void Renderer::fillQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col) {
    fillTri(a, b, c, col);
    fillTri(a, c, d, col);
}

void Renderer::fillCapsule(Vec2 a, Vec2 b, float thickness, Color c) {
    Vec2 dir = b - a;
    float len = length(dir);
    if (len < 1e-4f) { fillCircle(a.x, a.y, thickness * 0.5f, c); return; }
    Vec2 n{-dir.y / len, dir.x / len};
    float h = thickness * 0.5f;
    fillQuad(a + n * h, b + n * h, b - n * h, a - n * h, c);
    fillCircle(a.x, a.y, h, c);
    fillCircle(b.x, b.y, h, c);
}

void Renderer::drawArc(float cx, float cy, float radius, float thickness, float a0, float a1, Color c) {
    int seg = SDL_max(2, (int)(std::fabs(a1 - a0) / (kPi / 24)));
    float ri = radius - thickness * 0.5f, ro = radius + thickness * 0.5f;
    for (int i = 0; i < seg; ++i) {
        float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
        Vec2 i0{cx + std::cos(t0) * ri, cy + std::sin(t0) * ri};
        Vec2 o0{cx + std::cos(t0) * ro, cy + std::sin(t0) * ro};
        Vec2 i1{cx + std::cos(t1) * ri, cy + std::sin(t1) * ri};
        Vec2 o1{cx + std::cos(t1) * ro, cy + std::sin(t1) * ro};
        fillQuad(i0, o0, o1, i1, c);
    }
}

// --- text ----------------------------------------------------------------

const Renderer::Glyphs& Renderer::textTexture(const std::string& s) {
    auto it = cache_.find(s);
    if (it != cache_.end()) return it->second;

    Glyphs g;
    if (!gFontReady || s.empty()) return cache_.emplace(s, g).first->second;

    float scale = stbtt_ScaleForPixelHeight(&gFont, kNativePx);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&gFont, &ascent, &descent, &lineGap);
    float baseline = ascent * scale;

    struct GP { int cp, x0, y0, x1, y1; float xpos; };
    std::vector<GP> gps;
    float xpos = 0;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (unsigned char ch : s) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&gFont, ch, &adv, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&gFont, ch, scale, scale, &x0, &y0, &x1, &y1);
        gps.push_back({ch, x0, y0, x1, y1, xpos});
        if (x1 > x0 && y1 > y0) {
            minX = SDL_min(minX, xpos + x0); maxX = SDL_max(maxX, xpos + x1);
            minY = SDL_min(minY, baseline + y0); maxY = SDL_max(maxY, baseline + y1);
        }
        xpos += adv * scale;
    }
    if (maxX < minX) return cache_.emplace(s, g).first->second;

    int W = (int)std::ceil(maxX - minX) + 2;
    int H = (int)std::ceil(maxY - minY) + 2;
    float shiftX = 1 - minX, shiftY = 1 - minY;
    std::vector<unsigned char> alpha(W * H, 0);
    for (auto& gp : gps) {
        int gw = gp.x1 - gp.x0, gh = gp.y1 - gp.y0;
        if (gw <= 0 || gh <= 0) continue;
        std::vector<unsigned char> tmp(gw * gh);
        stbtt_MakeCodepointBitmap(&gFont, tmp.data(), gw, gh, gw, scale, scale, gp.cp);
        int dx = (int)std::floor(gp.xpos + gp.x0 + shiftX);
        int dy = (int)std::floor(baseline + gp.y0 + shiftY);
        for (int yy = 0; yy < gh; ++yy) {
            int ty = dy + yy;
            if (ty < 0 || ty >= H) continue;
            for (int xx = 0; xx < gw; ++xx) {
                int tx = dx + xx;
                if (tx < 0 || tx >= W) continue;
                int v = alpha[ty * W + tx] + tmp[yy * gw + xx];
                alpha[ty * W + tx] = (unsigned char)(v > 255 ? 255 : v);
            }
        }
    }
    std::vector<unsigned char> rgba(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = alpha[i];
    }
    SDL_Texture* tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, rgba.data(), W * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        g.tex = tex; g.w = W; g.h = H;
    }
    return cache_.emplace(s, g).first->second;
}

void Renderer::drawText(const std::string& s, float cx, float cy, float pixelHeight, Color c) {
    const Glyphs& g = textTexture(s);
    if (!g.tex) return;
    cx += origin_.x; cy += origin_.y;
    float scale = pixelHeight / kNativePx;
    float w = g.w * scale, h = g.h * scale;
    SDL_FRect dst{cx - w * 0.5f, cy - h * 0.5f, w, h};
    SDL_SetTextureColorModFloat(g.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaModFloat(g.tex, c.a);
    SDL_RenderTexture(r_, g.tex, nullptr, &dst);
}

Vec2 Renderer::measureText(const std::string& s, float pixelHeight) {
    const Glyphs& g = textTexture(s);
    float scale = pixelHeight / kNativePx;
    return {g.w * scale, g.h * scale};
}

}  // namespace tf
