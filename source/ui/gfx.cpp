#include "ui/gfx.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <SDL_image.h>

namespace monhex::ui
{
    namespace
    {
        // Cached text and shapes not drawn for this many frames are freed
        constexpr u64 CACHE_FRAMES = 240;

        void setColor(SDL_Renderer* r, Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }

        float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

        // Signed distance from a point to a rounded rectangle centred on the origin
        float roundRectDistance(float px, float py, float halfW, float halfH, float radius)
        {
            float qx = std::fabs(px) - (halfW - radius);
            float qy = std::fabs(py) - (halfH - radius);
            float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
        }

        SDL_Surface* newSurface(int w, int h)
        {
            return SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        }

        // Filled-ball fallback for A/B/X/Y when the outlined glyph is missing
        u32 filledGlyph(u32 cp)
        {
            if (cp >= 0xE0E0 && cp <= 0xE0E3)
                return cp - 0x40;
            return cp;
        }
    }

    Color lerp(Color a, Color b, float t)
    {
        t = clamp01(t);
        return {u8(a.r + (b.r - a.r) * t), u8(a.g + (b.g - a.g) * t), u8(a.b + (b.b - a.b) * t),
            u8(a.a + (b.a - a.a) * t)};
    }

    bool Gfx::init(std::string& error)
    {
        if (R_FAILED(plInitialize(PlServiceType_User)) ||
            R_FAILED(plGetSharedFontByType(&mFontData, PlSharedFontType_Standard)))
        {
            error = "Nao foi possivel carregar a fonte do sistema.";
            return false;
        }
        plGetSharedFontByType(&mExtFontData, PlSharedFontType_NintendoExt);

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            error = std::string("SDL_Init: ") + SDL_GetError();
            return false;
        }
        mWindow = SDL_CreateWindow("MonHex", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
        if (!mWindow)
        {
            error = std::string("SDL_CreateWindow: ") + SDL_GetError();
            return false;
        }
        mRenderer = SDL_CreateRenderer(
            mWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!mRenderer)
        {
            error = std::string("SDL_CreateRenderer: ") + SDL_GetError();
            return false;
        }
        SDL_RenderSetLogicalSize(mRenderer, SCREEN_W, SCREEN_H);
        SDL_SetRenderDrawBlendMode(mRenderer, SDL_BLENDMODE_BLEND);
        IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP);
        if (TTF_Init() != 0)
        {
            error = std::string("TTF_Init: ") + TTF_GetError();
            return false;
        }
        mStartTick = armGetSystemTick();
        return true;
    }

    void Gfx::exit(void)
    {
        for (auto* cache : {&mTextCache, &mShapeCache})
        {
            for (auto& [key, c] : *cache)
                SDL_DestroyTexture(c.tex);
            cache->clear();
        }
        for (auto& [path, tex] : mTextures)
        {
            if (tex)
                SDL_DestroyTexture(tex);
        }
        mTextures.clear();
        for (auto& [path, img] : mPixelTextures)
        {
            if (img.tex)
                SDL_DestroyTexture(img.tex);
        }
        mPixelTextures.clear();
        for (SDL_Texture* tex : {mPokeball, mSparkle, mCapture, mBlurA, mBlurB})
        {
            if (tex)
                SDL_DestroyTexture(tex);
        }
        for (auto& [path, tex] : mMasks)
        {
            if (tex)
                SDL_DestroyTexture(tex);
        }
        mMasks.clear();
        for (auto* fonts : {&mFonts, &mExtFonts})
        {
            for (auto& [size, f] : *fonts)
            {
                if (f)
                    TTF_CloseFont(f);
            }
            fonts->clear();
        }
        TTF_Quit();
        IMG_Quit();
        if (mRenderer)
            SDL_DestroyRenderer(mRenderer);
        if (mWindow)
            SDL_DestroyWindow(mWindow);
        SDL_Quit();
        plExit();
    }

    float Gfx::time(void) const
    {
        return float(armTicksToNs(armGetSystemTick() - mStartTick)) / 1e9f;
    }

    void Gfx::begin(Color background)
    {
        setColor(mRenderer, background);
        SDL_RenderClear(mRenderer);
    }

    void Gfx::evict(std::unordered_map<std::string, Cached>& cache)
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (mFrame - it->second.lastUsed > CACHE_FRAMES)
            {
                SDL_DestroyTexture(it->second.tex);
                it = cache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Gfx::end(void)
    {
        SDL_RenderPresent(mRenderer);
        mFrame++;
        if (mFrame % 60 == 0)
        {
            evict(mTextCache);
            evict(mShapeCache);
        }
    }

    // ------------------------------------------------------------------ shapes

    SDL_Texture* Gfx::shape(int w, int h, int radius, int border, int blur)
    {
        if (w <= 0 || h <= 0)
            return nullptr;
        char key[64];
        snprintf(key, sizeof(key), "%d|%d|%d|%d|%d", w, h, radius, border, blur);
        auto it = mShapeCache.find(key);
        if (it != mShapeCache.end())
        {
            it->second.lastUsed = mFrame;
            return it->second.tex;
        }

        SDL_Surface* surf = newSurface(w, h);
        if (!surf)
            return nullptr;
        u32* px     = static_cast<u32*>(surf->pixels);
        int pitch   = surf->pitch / 4;
        float halfW = w / 2.0f, halfH = h / 2.0f;
        // A shadow's shape sits inside the blur margin
        float shapeHalfW = halfW - blur, shapeHalfH = halfH - blur;
        float r = std::min<float>(radius, std::min(shapeHalfW, shapeHalfH));
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float d = roundRectDistance(x + 0.5f - halfW, y + 0.5f - halfH, shapeHalfW,
                    shapeHalfH, r);
                float a;
                if (blur > 0)
                {
                    float t = clamp01((d + blur * 0.5f) / (blur * 1.5f));
                    a = (1 - t) * (1 - t) * (1 - t);
                }
                else if (border > 0)
                {
                    a = clamp01(0.5f - d) * clamp01(0.5f + d + border);
                }
                else
                {
                    a = clamp01(0.5f - d);
                }
                px[y * pitch + x] = SDL_MapRGBA(surf->format, 255, 255, 255, u8(a * 255 + 0.5f));
            }
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(mRenderer, surf);
        SDL_FreeSurface(surf);
        if (!tex)
            return nullptr;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        mShapeCache[key] = {tex, w, h, mFrame};
        return tex;
    }

    void Gfx::fillRect(int x, int y, int w, int h, Color c)
    {
        SDL_Rect r{x + mOx, y + mOy, w, h};
        setColor(mRenderer, c.withAlpha(fade(c.a)));
        SDL_RenderFillRect(mRenderer, &r);
    }

    void Gfx::drawTinted(SDL_Texture* tex, int x, int y, int w, int h, Color c)
    {
        if (!tex)
            return;
        SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(tex, fade(c.a));
        SDL_Rect dst{x + mOx, y + mOy, w, h};
        SDL_RenderCopy(mRenderer, tex, nullptr, &dst);
    }

    void Gfx::fillRoundRect(int x, int y, int w, int h, int radius, Color c)
    {
        drawTinted(shape(w, h, radius, 0, 0), x, y, w, h, c);
    }

    void Gfx::roundRect(int x, int y, int w, int h, int radius, Color c, int thickness)
    {
        drawTinted(shape(w, h, radius, thickness, 0), x, y, w, h, c);
    }

    void Gfx::fillCircle(int cx, int cy, int radius, Color c)
    {
        fillRoundRect(cx - radius, cy - radius, radius * 2, radius * 2, radius, c);
    }

    void Gfx::shadow(int x, int y, int w, int h, int radius, int blur, Color c)
    {
        drawTinted(shape(w + 2 * blur, h + 2 * blur, radius, 0, blur), x - blur,
            y - blur, w + 2 * blur, h + 2 * blur, c);
    }

    void Gfx::arrow(int cx, int cy, int size, bool right, Color c)
    {
        float dir = right ? 1.0f : -1.0f;
        float s   = float(size);
        triangle({cx + dir * s * 0.5f, float(cy)}, {cx - dir * s * 0.5f, cy - s * 0.6f},
            {cx - dir * s * 0.5f, cy + s * 0.6f}, c);
    }

    void Gfx::thickLine(SDL_FPoint a, SDL_FPoint b, float thickness, Color c)
    {
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0)
            return;
        float nx = -dy / len * thickness / 2, ny = dx / len * thickness / 2;
        SDL_FPoint p1{a.x + nx, a.y + ny}, p2{b.x + nx, b.y + ny}, p3{b.x - nx, b.y - ny}, p4{a.x - nx, a.y - ny};
        triangle(p1, p2, p3, c);
        triangle(p1, p3, p4, c);
    }

    void Gfx::triangle(SDL_FPoint tip, SDL_FPoint top, SDL_FPoint bot, Color c)
    {
        // The solid triangle plus a transparent rim around it, so the edge is soft
        SDL_Color solid{c.r, c.g, c.b, fade(c.a)};
        SDL_Color clear{c.r, c.g, c.b, 0};
        for (SDL_FPoint* p : {&tip, &top, &bot})
            p->x += mOx, p->y += mOy;
        SDL_FPoint centre{(tip.x + top.x + bot.x) / 3, (tip.y + top.y + bot.y) / 3};
        auto outward = [&](SDL_FPoint p) {
            float dx = p.x - centre.x, dy = p.y - centre.y;
            float len = std::sqrt(dx * dx + dy * dy);
            return SDL_FPoint{p.x + dx / len * 1.2f, p.y + dy / len * 1.2f};
        };
        SDL_FPoint tipO = outward(tip), topO = outward(top), botO = outward(bot);
        SDL_Vertex v[] = {
            {tip, solid, {0, 0}}, {top, solid, {0, 0}}, {bot, solid, {0, 0}},
            // rim quads, each split into two triangles
            {tip, solid, {0, 0}}, {tipO, clear, {0, 0}}, {top, solid, {0, 0}},
            {top, solid, {0, 0}}, {tipO, clear, {0, 0}}, {topO, clear, {0, 0}},
            {top, solid, {0, 0}}, {topO, clear, {0, 0}}, {bot, solid, {0, 0}},
            {bot, solid, {0, 0}}, {topO, clear, {0, 0}}, {botO, clear, {0, 0}},
            {bot, solid, {0, 0}}, {botO, clear, {0, 0}}, {tip, solid, {0, 0}},
            {tip, solid, {0, 0}}, {botO, clear, {0, 0}}, {tipO, clear, {0, 0}},
        };
        SDL_RenderGeometry(mRenderer, nullptr, v, int(sizeof(v) / sizeof(v[0])), nullptr, 0);
    }

    // ------------------------------------------------------------------ text

    TTF_Font* Gfx::font(int size, bool extended)
    {
        auto& fonts = extended ? mExtFonts : mFonts;
        auto it     = fonts.find(size);
        if (it != fonts.end())
            return it->second;
        const PlFontData& data = extended ? mExtFontData : mFontData;
        TTF_Font* f = nullptr;
        if (data.address)
        {
            SDL_RWops* rw = SDL_RWFromConstMem(data.address, data.size);
            f             = rw ? TTF_OpenFontRW(rw, 1, size) : nullptr;
        }
        fonts[size] = f;
        return f;
    }

    bool Gfx::hasGlyph(u32 codepoint, int size)
    {
        TTF_Font* f = font(size);
        return f && TTF_GlyphIsProvided32(f, codepoint);
    }

    int Gfx::textWidth(const std::string& str, int size)
    {
        TTF_Font* f = font(size);
        int w = 0, h = 0;
        if (!f || str.empty() || TTF_SizeUTF8(f, str.c_str(), &w, &h) != 0)
            return 0;
        return w;
    }

    int Gfx::textHeight(int size)
    {
        TTF_Font* f = font(size);
        return f ? TTF_FontHeight(f) : size;
    }

    std::string Gfx::fitText(const std::string& str, int size, int maxWidth)
    {
        if (maxWidth <= 0 || textWidth(str, size) <= maxWidth)
            return str;
        std::string cut = str;
        while (!cut.empty())
        {
            // Drop one whole UTF-8 character from the end
            size_t pos = cut.size() - 1;
            while (pos > 0 && (static_cast<unsigned char>(cut[pos]) & 0xC0) == 0x80)
                pos--;
            cut.erase(pos);
            while (!cut.empty() && cut.back() == ' ')
                cut.pop_back();
            if (textWidth(cut + "...", size) <= maxWidth)
                return cut + "...";
        }
        return "";
    }

    void Gfx::drawCached(Cached& c, int x, int y, Align align)
    {
        c.lastUsed = mFrame;
        int drawX  = align == Align::Center ? x - c.w / 2 : (align == Align::Right ? x - c.w : x);
        SDL_SetTextureAlphaMod(c.tex, fade(255));
        SDL_Rect dst{drawX + mOx, y + mOy, c.w, c.h};
        SDL_RenderCopy(mRenderer, c.tex, nullptr, &dst);
    }

    int Gfx::text(const std::string& str, int x, int y, int size, Color c, Align align, int maxWidth)
    {
        if (str.empty())
            return 0;
        std::string shown = fitText(str, size, maxWidth);
        if (shown.empty())
            return 0;
        char key[32];
        snprintf(key, sizeof(key), "t%d|%02x%02x%02x%02x|", size, c.r, c.g, c.b, c.a);
        std::string cacheKey = key + shown;

        auto it = mTextCache.find(cacheKey);
        if (it == mTextCache.end())
        {
            TTF_Font* f = font(size);
            if (!f)
                return 0;
            SDL_Surface* surf = TTF_RenderUTF8_Blended(f, shown.c_str(), {c.r, c.g, c.b, c.a});
            if (!surf)
                return 0;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(mRenderer, surf);
            Cached cached{tex, surf->w, surf->h, mFrame};
            SDL_FreeSurface(surf);
            if (!tex)
                return 0;
            it = mTextCache.emplace(cacheKey, cached).first;
        }
        drawCached(it->second, x, y, align);
        return it->second.w;
    }

    int Gfx::textWrapped(const std::string& str, int x, int y, int size, Color c, Align align,
        int maxWidth, int maxLines, int lineHeight)
    {
        std::vector<std::string> words;
        size_t start = 0;
        while (start < str.size())
        {
            size_t end = str.find(' ', start);
            if (end == std::string::npos)
                end = str.size();
            if (end > start)
                words.push_back(str.substr(start, end - start));
            start = end + 1;
        }
        int lines = 0;
        size_t i  = 0;
        while (i < words.size() && lines < maxLines)
        {
            std::string line = words[i++];
            // The last allowed line takes everything left and is cut with "..."
            bool last = lines == maxLines - 1;
            while (i < words.size() && (last || textWidth(line + " " + words[i], size) <= maxWidth))
                line += " " + words[i++];
            text(line, x, y + lines * lineHeight, size, c, align, maxWidth);
            lines++;
        }
        return lines;
    }

    int Gfx::buttonGlyphWidth(u32 codepoint, int size)
    {
        TTF_Font* f = font(size, true);
        if (!f)
            return 0;
        if (!TTF_GlyphIsProvided32(f, codepoint))
            codepoint = filledGlyph(codepoint);
        int minx, maxx, miny, maxy, advance = 0;
        TTF_GlyphMetrics32(f, codepoint, &minx, &maxx, &miny, &maxy, &advance);
        return advance;
    }

    int Gfx::buttonGlyph(u32 codepoint, int x, int y, int size, Color c, Align align)
    {
        TTF_Font* f = font(size, true);
        if (!f)
            return 0;
        if (!TTF_GlyphIsProvided32(f, codepoint))
            codepoint = filledGlyph(codepoint);
        char key[48];
        snprintf(key, sizeof(key), "g%d|%02x%02x%02x%02x|%x", size, c.r, c.g, c.b, c.a, codepoint);
        auto it = mTextCache.find(key);
        if (it == mTextCache.end())
        {
            SDL_Surface* surf = TTF_RenderGlyph32_Blended(f, codepoint, {c.r, c.g, c.b, c.a});
            if (!surf)
                return 0;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(mRenderer, surf);
            Cached cached{tex, surf->w, surf->h, mFrame};
            SDL_FreeSurface(surf);
            if (!tex)
                return 0;
            it = mTextCache.emplace(key, cached).first;
        }
        drawCached(it->second, x, y, align);
        return it->second.w;
    }

    // ------------------------------------------------------------------ images

    SDL_Texture* Gfx::texture(const std::string& path, bool pixelArt)
    {
        auto it = mTextures.find(path);
        if (it != mTextures.end())
            return it->second;
        SDL_Texture* tex = IMG_LoadTexture(mRenderer, path.c_str());
        if (tex)
        {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, pixelArt ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
        }
        mTextures[path] = tex; // a missing file is remembered as nullptr
        return tex;
    }

    SDL_Texture* Gfx::textureFromSurface(SDL_Surface* surf, bool linear)
    {
        SDL_Texture* tex = surf ? SDL_CreateTextureFromSurface(mRenderer, surf) : nullptr;
        if (tex)
        {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        }
        return tex;
    }

    SDL_Texture* Gfx::pixelTexture(const std::string& path, int& w, int& h)
    {
        auto it = mPixelTextures.find(path);
        if (it != mPixelTextures.end())
        {
            w = it->second.w, h = it->second.h;
            return it->second.tex;
        }
        PixelImage img{nullptr, 0, 0};
        SDL_Surface* loaded = IMG_Load(path.c_str());
        SDL_Surface* rgba   = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
        if (loaded)
            SDL_FreeSurface(loaded);
        if (rgba)
        {
            // Bounds of the visible pixels
            const u32* px = static_cast<const u32*>(rgba->pixels);
            int pitch     = rgba->pitch / 4;
            int minX = rgba->w, minY = rgba->h, maxX = -1, maxY = -1;
            for (int y = 0; y < rgba->h; y++)
            {
                for (int x = 0; x < rgba->w; x++)
                {
                    u8 r, g, b, a;
                    SDL_GetRGBA(px[y * pitch + x], rgba->format, &r, &g, &b, &a);
                    if (a > 8)
                    {
                        minX = std::min(minX, x), maxX = std::max(maxX, x);
                        minY = std::min(minY, y), maxY = std::max(maxY, y);
                    }
                }
            }
            if (maxX >= 0)
            {
                // One pixel of margin keeps the linear filter from clipping edges
                minX = std::max(0, minX - 1), minY = std::max(0, minY - 1);
                maxX = std::min(rgba->w - 1, maxX + 1), maxY = std::min(rgba->h - 1, maxY + 1);
                img.w = maxX - minX + 1, img.h = maxY - minY + 1;
                constexpr int UP = 3;
                SDL_Surface* big = newSurface(img.w * UP, img.h * UP);
                if (big)
                {
                    u32* dst      = static_cast<u32*>(big->pixels);
                    int dstPitch  = big->pitch / 4;
                    for (int y = 0; y < img.h * UP; y++)
                        for (int x = 0; x < img.w * UP; x++)
                            dst[y * dstPitch + x] = px[(minY + y / UP) * pitch + (minX + x / UP)];
                    img.tex = textureFromSurface(big, true);
                    SDL_FreeSurface(big);
                }
            }
            SDL_FreeSurface(rgba);
        }
        mPixelTextures[path] = img;
        w = img.w, h = img.h;
        return img.tex;
    }

    SDL_Texture* Gfx::roundedImage(const void* data, size_t size, int w, int h, int radius)
    {
        SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
        SDL_Surface* loaded = rw ? IMG_Load_RW(rw, 1) : nullptr;
        if (!loaded)
            return nullptr;
        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if (!rgba)
            return nullptr;
        SDL_Surface* scaled = newSurface(w, h);
        if (!scaled || SDL_SoftStretchLinear(rgba, nullptr, scaled, nullptr) != 0)
        {
            // Linear stretch needs matching formats; fall back to plain scaling
            if (scaled)
                SDL_BlitScaled(rgba, nullptr, scaled, nullptr);
        }
        SDL_FreeSurface(rgba);
        if (!scaled)
            return nullptr;

        // Fade the alpha out past the rounded edge
        SDL_LockSurface(scaled);
        u32* px   = static_cast<u32*>(scaled->pixels);
        int pitch = scaled->pitch / 4;
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float d = roundRectDistance(x + 0.5f - w / 2.0f, y + 0.5f - h / 2.0f, w / 2.0f,
                    h / 2.0f, float(radius));
                float a = clamp01(0.5f - d);
                u8 r, g, b, alpha;
                SDL_GetRGBA(px[y * pitch + x], scaled->format, &r, &g, &b, &alpha);
                px[y * pitch + x] = SDL_MapRGBA(scaled->format, r, g, b, u8(alpha * a));
            }
        }
        SDL_UnlockSurface(scaled);
        SDL_Texture* tex = SDL_CreateTextureFromSurface(mRenderer, scaled);
        SDL_FreeSurface(scaled);
        if (tex)
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        return tex;
    }

    void Gfx::draw(SDL_Texture* tex, int x, int y, int w, int h, u8 alpha)
    {
        if (!tex)
            return;
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, fade(alpha));
        SDL_Rect dst{x + mOx, y + mOy, w, h};
        SDL_RenderCopy(mRenderer, tex, nullptr, &dst);
    }

    void Gfx::drawRotated(SDL_Texture* tex, int cx, int cy, int w, int h, double degrees, u8 alpha)
    {
        if (!tex)
            return;
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, fade(alpha));
        SDL_Rect dst{cx - w / 2 + mOx, cy - h / 2 + mOy, w, h};
        SDL_RenderCopyEx(mRenderer, tex, nullptr, &dst, degrees, nullptr, SDL_FLIP_NONE);
    }

    SDL_Texture* Gfx::pokeball(void)
    {
        if (mPokeball)
            return mPokeball;
        constexpr int S = 256;
        SDL_Surface* surf = newSurface(S, S);
        if (!surf)
            return nullptr;
        u32* px    = static_cast<u32*>(surf->pixels);
        int pitch  = surf->pitch / 4;
        float c    = S / 2.0f;
        float R    = S / 2.0f - 2;
        float band = S * 0.045f;   // half height of the black band
        float btnO = S * 0.17f;    // button outer radius (black ring)
        float btnI = S * 0.11f;    // button inner radius (white)
        for (int y = 0; y < S; y++)
        {
            for (int x = 0; x < S; x++)
            {
                float dx = x + 0.5f - c, dy = y + 0.5f - c;
                float dist  = std::sqrt(dx * dx + dy * dy);
                float alpha = clamp01(R - dist + 0.5f);
                // outer black rim
                float rim = clamp01(dist - (R - S * 0.035f) + 0.5f);
                float bandA = clamp01(band - std::fabs(dy) + 0.5f);
                float ringA = clamp01(btnO - dist + 0.5f);
                float innerA = clamp01(btnI - dist + 0.5f);
                // base colour: red top, white bottom (soft line between them)
                float top = clamp01(0.5f - dy);
                float r = 233 * top + 245 * (1 - top);
                float g = 69 * top + 245 * (1 - top);
                float b = 96 * top + 248 * (1 - top);
                // subtle shading towards the lower right
                float shade = 1.0f - 0.18f * clamp01((dx + dy) / (2 * R) + 0.5f);
                r *= shade, g *= shade, b *= shade;
                float black = std::max({rim, bandA, ringA});
                r = r * (1 - black) + 24 * black;
                g = g * (1 - black) + 24 * black;
                b = b * (1 - black) + 30 * black;
                r = r * (1 - innerA) + 250 * innerA;
                g = g * (1 - innerA) + 250 * innerA;
                b = b * (1 - innerA) + 250 * innerA;
                px[y * pitch + x] = SDL_MapRGBA(surf->format, u8(r), u8(g), u8(b), u8(alpha * 255));
            }
        }
        mPokeball = SDL_CreateTextureFromSurface(mRenderer, surf);
        SDL_FreeSurface(surf);
        if (mPokeball)
        {
            SDL_SetTextureBlendMode(mPokeball, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(mPokeball, SDL_ScaleModeLinear);
        }
        return mPokeball;
    }

    void Gfx::polygon(const SDL_FPoint* points, int count, Color c)
    {
        if (count < 3)
            return;
        SDL_Color col{c.r, c.g, c.b, fade(c.a)};
        std::vector<SDL_Vertex> v;
        v.reserve((count - 2) * 3);
        for (int i = 1; i + 1 < count; i++)
        {
            for (int k : {0, i, i + 1})
                v.push_back({{points[k].x + mOx, points[k].y + mOy}, col, {0, 0}});
        }
        SDL_RenderGeometry(mRenderer, nullptr, v.data(), int(v.size()), nullptr, 0);
    }

    void Gfx::sparkle(int cx, int cy, int size, Color c, double degrees)
    {
        if (!mSparkle)
        {
            // A four-pointed star (an astroid, |x|^2/3 + |y|^2/3 < 1) with a faint
            // halo, antialiased by sampling each pixel 4x4 times
            constexpr int S = 96;
            if (SDL_Surface* surf = newSurface(S, S))
            {
                u32* px = static_cast<u32*>(surf->pixels);
                for (int y = 0; y < S; y++)
                {
                    for (int x = 0; x < S; x++)
                    {
                        float inside = 0;
                        for (int sy = 0; sy < 4; sy++)
                        {
                            for (int sx = 0; sx < 4; sx++)
                            {
                                float u = ((x + (sx + 0.5f) / 4) / S) * 2 - 1;
                                float v = ((y + (sy + 0.5f) / 4) / S) * 2 - 1;
                                float f = std::pow(std::fabs(u), 2.0f / 3) + std::pow(std::fabs(v), 2.0f / 3);
                                inside += f < 0.92f ? 1 : 0;
                            }
                        }
                        float u = (x + 0.5f) / S * 2 - 1, v = (y + 0.5f) / S * 2 - 1;
                        float d    = std::sqrt(u * u + v * v);
                        float halo = d < 0.6f ? (1 - d / 0.6f) * (1 - d / 0.6f) * 0.45f : 0;
                        float a    = std::max(inside / 16, halo);
                        px[y * (surf->pitch / 4) + x] = SDL_MapRGBA(surf->format, 255, 255, 255, u8(a * 255));
                    }
                }
                mSparkle = SDL_CreateTextureFromSurface(mRenderer, surf);
                SDL_FreeSurface(surf);
                if (mSparkle)
                {
                    SDL_SetTextureBlendMode(mSparkle, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(mSparkle, SDL_ScaleModeLinear);
                }
            }
        }
        if (!mSparkle)
            return;
        SDL_SetTextureColorMod(mSparkle, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(mSparkle, fade(c.a));
        SDL_Rect dst{cx - size / 2 + mOx, cy - size / 2 + mOy, size, size};
        SDL_RenderCopyEx(mRenderer, mSparkle, nullptr, &dst, degrees, nullptr, SDL_FLIP_NONE);
    }

    void Gfx::beginCapture(void)
    {
        auto target = [&](SDL_Texture*& tex, int w, int h) {
            if (!tex)
            {
                tex = SDL_CreateTexture(mRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
                if (tex)
                    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            }
        };
        target(mCapture, SCREEN_W, SCREEN_H);
        target(mBlurA, SCREEN_W / 4, SCREEN_H / 4);
        target(mBlurB, SCREEN_W / 8, SCREEN_H / 8);
        SDL_SetRenderTarget(mRenderer, mCapture);
        SDL_SetRenderDrawColor(mRenderer, 0, 0, 0, 255);
        SDL_RenderClear(mRenderer);
    }

    SDL_Texture* Gfx::endCapture(void)
    {
        // Shrinking in two linear steps and stretching back up is a cheap blur
        for (auto [from, to] : {std::pair{mCapture, mBlurA}, std::pair{mBlurA, mBlurB}})
        {
            if (!from || !to)
                break;
            SDL_SetRenderTarget(mRenderer, to);
            SDL_SetTextureBlendMode(from, SDL_BLENDMODE_NONE);
            SDL_SetTextureColorMod(from, 255, 255, 255);
            SDL_SetTextureAlphaMod(from, 255);
            SDL_RenderCopy(mRenderer, from, nullptr, nullptr);
        }
        SDL_SetRenderTarget(mRenderer, nullptr);
        return mBlurB ? mBlurB : mCapture;
    }

    SDL_Texture* Gfx::whiteMask(const std::string& path)
    {
        auto it = mMasks.find(path);
        if (it != mMasks.end())
            return it->second;
        SDL_Texture* tex    = nullptr;
        SDL_Surface* loaded = IMG_Load(path.c_str());
        SDL_Surface* rgba   = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
        if (loaded)
            SDL_FreeSurface(loaded);
        if (rgba)
        {
            u32* px   = static_cast<u32*>(rgba->pixels);
            int pitch = rgba->pitch / 4;
            // The square's colour, read from the top edge; anything whiter is the symbol
            u8 br, bg, bb, ba;
            SDL_GetRGBA(px[2 * pitch + rgba->w / 2], rgba->format, &br, &bg, &bb, &ba);
            u8 floorMin = std::min({br, bg, bb});
            for (int y = 0; y < rgba->h; y++)
            {
                for (int x = 0; x < rgba->w; x++)
                {
                    u8 r, g, b, a;
                    SDL_GetRGBA(px[y * pitch + x], rgba->format, &r, &g, &b, &a);
                    float lo    = std::min({r, g, b});
                    float white = floorMin >= 250 ? 0 : clamp01((lo - floorMin) / (255.0f - floorMin));
                    px[y * pitch + x] = SDL_MapRGBA(rgba->format, 255, 255, 255, u8(white * a));
                }
            }
            tex = SDL_CreateTextureFromSurface(mRenderer, rgba);
            SDL_FreeSurface(rgba);
            if (tex)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            }
        }
        mMasks[path] = tex;
        return tex;
    }

    void Gfx::drawMask(SDL_Texture* mask, int x, int y, int w, int h, Color c)
    {
        drawTinted(mask, x, y, w, h, c);
    }
}
