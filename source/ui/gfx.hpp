// Small drawing layer over SDL2. Shapes are drawn from cached, antialiased
// textures (computed from signed distances), text uses the Switch's system fonts,
// and images are cached from romfs.
#pragma once

#include <map>
#include <string>
#include <unordered_map>

#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>

namespace monhex::ui
{
    constexpr int SCREEN_W = 1280;
    constexpr int SCREEN_H = 720;

    struct Color
    {
        u8 r, g, b, a = 255;
        Color withAlpha(u8 alpha) const { return {r, g, b, alpha}; }
    };

    Color lerp(Color a, Color b, float t);

    enum class Align
    {
        Left,
        Center,
        Right
    };

    // Button glyphs from the system's extended font (the codes libtesla uses).
    // A/B/X/Y prefer the outlined style of the system menus and fall back to the
    // filled one if the font lacks it.
    namespace glyph
    {
        constexpr u32 A = 0xE0E0, B = 0xE0E1, X = 0xE0E2, Y = 0xE0E3;
        constexpr u32 L = 0xE0A4, R = 0xE0A5, ZL = 0xE0A6, ZR = 0xE0A7;
        constexpr u32 PLUS = 0xE0B5, MINUS = 0xE0B6;
        constexpr u32 DLEFT = 0xE07B, RS = 0xE08B;
    }

    class Gfx
    {
    public:
        bool init(std::string& error);
        void exit(void);

        void begin(Color background);
        void end(void);
        // Seconds since init, for animations
        float time(void) const;

        void fillRect(int x, int y, int w, int h, Color c);
        void fillRoundRect(int x, int y, int w, int h, int radius, Color c);
        void roundRect(int x, int y, int w, int h, int radius, Color c, int thickness);
        void fillCircle(int cx, int cy, int radius, Color c);
        // Soft shadow around a rounded rectangle
        void shadow(int x, int y, int w, int h, int radius, int blur, Color c);
        // Smooth triangle pointing left or right, centred on (cx, cy)
        void arrow(int cx, int cy, int size, bool right, Color c);
        // Any triangle, with a softened edge
        void triangle(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, Color color);
        // A thick line with square ends
        void thickLine(SDL_FPoint a, SDL_FPoint b, float thickness, Color c);

        // Draws text with its top edge at y; x is the left, centre or right edge.
        // Text wider than maxWidth (when given) is cut and ends in "...".
        // Returns the width drawn.
        int text(const std::string& str, int x, int y, int size, Color c,
            Align align = Align::Left, int maxWidth = 0);
        // Wraps on spaces into at most maxLines lines; returns the number of lines
        int textWrapped(const std::string& str, int x, int y, int size, Color c, Align align,
            int maxWidth, int maxLines, int lineHeight);
        int textWidth(const std::string& str, int size);
        // Height of a line of text at this size (what text() draws, top to bottom)
        int textHeight(int size);
        bool hasGlyph(u32 codepoint, int size);

        // A button icon from the extended font; returns its width
        int buttonGlyph(u32 codepoint, int x, int y, int size, Color c, Align align = Align::Left);
        int buttonGlyphWidth(u32 codepoint, int size);

        // Loads an image from a path once and keeps it; nullptr if it can't be loaded.
        // Pixel art should be drawn with nearest-neighbour scaling.
        SDL_Texture* texture(const std::string& path, bool pixelArt = false);
        // Pixel art cropped to its visible pixels and stored enlarged 3x, so it stays
        // crisp at any size. w/h are the cropped size at 1x. nullptr if missing.
        SDL_Texture* pixelTexture(const std::string& path, int& w, int& h);
        // Texture from a surface (kept by the caller, freed by the caller)
        SDL_Texture* textureFromSurface(SDL_Surface* surf, bool linear = true);
        // Decodes an image in memory, scales it to w x h and rounds its corners
        // (radius w/2 makes a circle). The caller owns the texture.
        SDL_Texture* roundedImage(const void* data, size_t size, int w, int h, int radius);
        void draw(SDL_Texture* tex, int x, int y, int w, int h, u8 alpha = 255);
        void drawRotated(SDL_Texture* tex, int cx, int cy, int w, int h, double degrees, u8 alpha = 255);

        // A Poke Ball drawn once at high resolution
        SDL_Texture* pokeball(void);

        SDL_Renderer* renderer(void) const { return mRenderer; }

        // Everything drawn is faded by this (0..1) and moved by the origin, which
        // lets a whole group of elements fade or slide together
        void setOpacity(float o) { mOpacity = o < 0 ? 0 : (o > 1 ? 1 : o); }
        float opacity(void) const { return mOpacity; }
        void setOrigin(int x, int y) { mOx = x, mOy = y; }
        void resetLayer(void) { mOpacity = 1, mOx = mOy = 0; }

        // Filled convex polygon (points in order around it)
        void polygon(const SDL_FPoint* points, int count, Color c);
        // Four-pointed sparkle centred on (cx, cy)
        void sparkle(int cx, int cy, int size, Color c, double degrees = 0);

        // Draws into an off-screen copy of the screen instead; endCapture() returns
        // a blurred version of what was drawn
        void beginCapture(void);
        SDL_Texture* endCapture(void);

        // An image reduced to its white parts, as an alpha mask to tint (the type
        // icons are white symbols on a coloured square)
        SDL_Texture* whiteMask(const std::string& path);
        void drawMask(SDL_Texture* mask, int x, int y, int w, int h, Color c);

    private:
        struct Cached
        {
            SDL_Texture* tex;
            int w, h;
            u64 lastUsed;
        };

        TTF_Font* font(int size, bool extended = false);
        void drawTinted(SDL_Texture* tex, int x, int y, int w, int h, Color c);
        u8 fade(u8 a) const { return u8(a * mOpacity + 0.5f); }
        std::string fitText(const std::string& str, int size, int maxWidth);
        // White antialiased rounded rectangle (border > 0 draws only the outline;
        // blur > 0 makes a soft shadow), cached by its parameters
        SDL_Texture* shape(int w, int h, int radius, int border, int blur);
        void drawCached(Cached& c, int x, int y, Align align);
        void evict(std::unordered_map<std::string, Cached>& cache);

        SDL_Window* mWindow     = nullptr;
        SDL_Renderer* mRenderer = nullptr;
        PlFontData mFontData{}, mExtFontData{};
        std::map<int, TTF_Font*> mFonts, mExtFonts;
        std::unordered_map<std::string, Cached> mTextCache;
        std::unordered_map<std::string, Cached> mShapeCache;
        std::unordered_map<std::string, SDL_Texture*> mTextures;
        struct PixelImage
        {
            SDL_Texture* tex;
            int w, h;
        };
        std::unordered_map<std::string, PixelImage> mPixelTextures;
        SDL_Texture* mPokeball = nullptr;
        SDL_Texture* mSparkle  = nullptr;
        SDL_Texture* mCapture  = nullptr;
        SDL_Texture* mBlurA    = nullptr;
        SDL_Texture* mBlurB    = nullptr;
        std::unordered_map<std::string, SDL_Texture*> mMasks;
        float mOpacity = 1;
        int mOx = 0, mOy = 0;
        u64 mFrame = 0;
        u64 mStartTick = 0;
    };
}
