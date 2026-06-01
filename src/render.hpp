#pragma once
#include "anim.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>

namespace tf {

// Immediate-mode drawing toolkit over SDL_Renderer: gradient rounded rects,
// soft shadows, capsules/arcs for icons, and cached stb_truetype text.
class Renderer {
public:
    bool init(SDL_Renderer* r);
    void shutdown();

    void fillRect(SDL_FRect r, Color c);
    void fillRoundedRect(SDL_FRect r, float radius, Color c);
    void fillRoundedRectGrad(SDL_FRect r, float radius, Color top, Color bottom);
    void roundedShadow(SDL_FRect r, float radius, Color c, float blur, Vec2 offset);
    void fillCircle(float cx, float cy, float radius, Color c);
    void fillCapsule(Vec2 a, Vec2 b, float thickness, Color c);
    void fillQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col);
    void fillTri(Vec2 a, Vec2 b, Vec2 c, Color col);
    void drawArc(float cx, float cy, float radius, float thickness, float a0, float a1, Color c);

    void drawText(const std::string& s, float cx, float cy, float pixelHeight, Color c);
    Vec2 measureText(const std::string& s, float pixelHeight);

    // Global offset added to every primitive (used for screen shake).
    void setOrigin(Vec2 o) { origin_ = o; }

    SDL_Renderer* sdl() const { return r_; }

private:
    struct Glyphs {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
    };
    const Glyphs& textTexture(const std::string& s);

    SDL_Renderer* r_ = nullptr;
    Vec2 origin_{0, 0};
    std::unordered_map<std::string, Glyphs> cache_;
};

}  // namespace tf
