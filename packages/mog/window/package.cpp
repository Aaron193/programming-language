#include "NativePackageAPI.hpp"

#include <SDL.h>

#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace {

struct WindowHandle {
    SDL_Window* window = nullptr;
    bool ownsSdlRef = false;
};

struct EventHandle {
    SDL_Event event{};
};

int gSdlRefCount = 0;

constexpr int64_t kBuiltinGlyphWidth = 8;
constexpr int64_t kBuiltinGlyphHeight = 8;
constexpr int64_t kBuiltinGlyphAdvance = 8;
constexpr int64_t kBuiltinLineAdvance = 8;

constexpr uint8_t kBuiltinFont[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18}, // 33 '!'
    {0x00, 0xEE, 0x66, 0xCC, 0x00, 0x00, 0x00, 0x00}, // 34 '"'
    {0x00, 0x6C, 0xFE, 0x6C, 0x6C, 0x6C, 0xFE, 0x6C}, // 35 '#'
    {0x18, 0x7E, 0xDB, 0xD8, 0x7E, 0x1B, 0xDB, 0x7E}, // 36 '$'
    {0x00, 0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6}, // 37 '%'
    {0x00, 0x38, 0x6C, 0x3A, 0x7E, 0xCC, 0xCC, 0x76}, // 38 '&'
    {0x00, 0x38, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00}, // 39 '\''
    {0x00, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C}, // 40 '('
    {0x00, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30}, // 41 ')'
    {0x00, 0x00, 0x6C, 0x38, 0xFE, 0x38, 0x6C, 0x00}, // 42 '*'
    {0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00}, // 43 '+'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x18, 0x70}, // 44 ','
    {0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00}, // 45 '-'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18}, // 46 '.'
    {0x00, 0x06, 0x0C, 0x18, 0x18, 0x30, 0x60, 0xC0}, // 47 '/'
    {0x00, 0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C}, // 48 '0'
    {0x00, 0x18, 0x78, 0x18, 0x18, 0x18, 0x18, 0x7E}, // 49 '1'
    {0x00, 0x7C, 0xC6, 0x8C, 0x38, 0x60, 0xC2, 0xFE}, // 50 '2'
    {0x00, 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C}, // 51 '3'
    {0x00, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E}, // 52 '4'
    {0x00, 0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C}, // 53 '5'
    {0x00, 0x7C, 0xC6, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C}, // 54 '6'
    {0x00, 0xFE, 0xC6, 0x06, 0x0C, 0x18, 0x30, 0x30}, // 55 '7'
    {0x00, 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C}, // 56 '8'
    {0x00, 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, 0x7C}, // 57 '9'
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00}, // 58 ':'
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x70}, // 59 ';'
    {0x00, 0x00, 0x18, 0x30, 0x60, 0x30, 0x18, 0x00}, // 60 '<'
    {0x00, 0x00, 0x00, 0xFE, 0x00, 0xFE, 0x00, 0x00}, // 61 '='
    {0x00, 0x00, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x00}, // 62 '>'
    {0x00, 0x7C, 0xC6, 0xCC, 0x18, 0x18, 0x00, 0x18}, // 63 '?'
    {0x00, 0x7C, 0xC6, 0xC6, 0xDE, 0xDC, 0xC0, 0x7E}, // 64 '@'
    {0x00, 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6}, // 65 'A'
    {0x00, 0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC}, // 66 'B'
    {0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC2, 0x66, 0x3C}, // 67 'C'
    {0x00, 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8}, // 68 'D'
    {0x00, 0xFE, 0x66, 0x60, 0x7C, 0x60, 0x66, 0xFE}, // 69 'E'
    {0x00, 0xFE, 0x66, 0x60, 0x7C, 0x60, 0x60, 0xF0}, // 70 'F'
    {0x00, 0x7C, 0xC6, 0xC0, 0xCE, 0xC6, 0xC6, 0x7C}, // 71 'G'
    {0x00, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6}, // 72 'H'
    {0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C}, // 73 'I'
    {0x00, 0x3C, 0x18, 0x18, 0x18, 0xD8, 0xD8, 0x70}, // 74 'J'
    {0x00, 0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6}, // 75 'K'
    {0x00, 0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE}, // 76 'L'
    {0x00, 0xC6, 0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6}, // 77 'M'
    {0x00, 0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6}, // 78 'N'
    {0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C}, // 79 'O'
    {0x00, 0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0}, // 80 'P'
    {0x00, 0x7C, 0xC6, 0xC6, 0xD6, 0xD6, 0x7C, 0x06}, // 81 'Q'
    {0x00, 0xFC, 0x66, 0x66, 0x7C, 0x78, 0x66, 0xE6}, // 82 'R'
    {0x00, 0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C}, // 83 'S'
    {0x00, 0xFF, 0xDB, 0x99, 0x18, 0x18, 0x18, 0x3C}, // 84 'T'
    {0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C}, // 85 'U'
    {0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10}, // 86 'V'
    {0x00, 0xC6, 0xC6, 0xD6, 0xD6, 0xFE, 0xEE, 0x6C}, // 87 'W'
    {0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6}, // 88 'X'
    {0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C}, // 89 'Y'
    {0x00, 0xFE, 0xC6, 0x8C, 0x38, 0x62, 0xC6, 0xFE}, // 90 'Z'
    {0x00, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7C}, // 91 '['
    {0x00, 0xC0, 0x60, 0x30, 0x30, 0x18, 0x0C, 0x06}, // 92 '\\'
    {0x00, 0x7C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x7C}, // 93 ']'
    {0x00, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00}, // 94 '^'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // 95 '_'
    {0x00, 0x38, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00}, // 96 '`'
    {0x00, 0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76}, // 97 'a'
    {0x00, 0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xFC}, // 98 'b'
    {0x00, 0x00, 0x00, 0x78, 0xCC, 0xC0, 0xCC, 0x78}, // 99 'c'
    {0x00, 0x0E, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x7E}, // 100 'd'
    {0x00, 0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C}, // 101 'e'
    {0x00, 0x1C, 0x36, 0x30, 0xFC, 0x30, 0x30, 0x78}, // 102 'f'
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8}, // 103 'g'
    {0x00, 0x00, 0xE0, 0x60, 0x7C, 0x66, 0x66, 0xE6}, // 104 'h'
    {0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C}, // 105 'i'
    {0x0C, 0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0xCC, 0x78}, // 106 'j'
    {0x00, 0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6}, // 107 'k'
    {0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C}, // 108 'l'
    {0x00, 0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6}, // 109 'm'
    {0x00, 0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66}, // 110 'n'
    {0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C}, // 111 'o'
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0}, // 112 'p'
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0E, 0x0E}, // 113 'q'
    {0x00, 0x00, 0x00, 0xDC, 0x66, 0x60, 0x60, 0xF0}, // 114 'r'
    {0x00, 0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0xFC}, // 115 's'
    {0x00, 0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C}, // 116 't'
    {0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76}, // 117 'u'
    {0x00, 0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x10}, // 118 'v'
    {0x00, 0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C}, // 119 'w'
    {0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6}, // 120 'x'
    {0x00, 0x00, 0x00, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8}, // 121 'y'
    {0x00, 0x00, 0x00, 0xFE, 0x8C, 0x38, 0x62, 0xFE}, // 122 'z'
    {0x00, 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E}, // 123 '{'
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}, // 124 '|'
    {0x00, 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70}, // 125 '}'
    {0x00, 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00}, // 126 '~'
};

void setError(ExprPackageStringView* outError, const char* message) {
    if (outError == nullptr || message == nullptr) {
        return;
    }

    *outError = {message, std::char_traits<char>::length(message)};
}

bool setStaticError(ExprPackageStringView* outError, const char* message,
                    size_t length) {
    if (outError != nullptr) {
        *outError = {message, length};
    }
    return false;
}

bool acquireSdl(ExprPackageStringView* outError) {
    if (gSdlRefCount == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            setError(outError, SDL_GetError());
            return false;
        }
    }

    ++gSdlRefCount;
    return true;
}

void releaseSdl() {
    if (gSdlRefCount <= 0) {
        return;
    }

    --gSdlRefCount;
    if (gSdlRefCount == 0) {
        SDL_Quit();
    }
}

void releaseWindowHandle(void* handleData) {
    auto* handle = static_cast<WindowHandle*>(handleData);
    if (handle == nullptr) {
        return;
    }

    if (handle->window != nullptr) {
        SDL_DestroyWindow(handle->window);
        handle->window = nullptr;
    }

    if (handle->ownsSdlRef) {
        handle->ownsSdlRef = false;
        releaseSdl();
    }

    delete handle;
}

void releaseEventHandle(void* handleData) {
    auto* handle = static_cast<EventHandle*>(handleData);
    delete handle;
}

bool expectWindowHandle(const ExprPackageValue& value, WindowHandle*& outHandle,
                        ExprPackageStringView* outError) {
    outHandle = nullptr;
    if (value.kind != EXPR_PACKAGE_VALUE_HANDLE) {
        setError(outError, "expected WindowHandle");
        return false;
    }

    const ExprPackageHandleValue& handle = value.as.handle_value;
    if (handle.package_namespace == nullptr || handle.package_name == nullptr ||
        handle.type_name == nullptr || handle.handle_data == nullptr) {
        setError(outError, "invalid WindowHandle metadata");
        return false;
    }

    if (std::string_view(handle.package_namespace) != "mog" ||
        std::string_view(handle.package_name) != "window" ||
        std::string_view(handle.type_name) != "WindowHandle") {
        setError(outError, "expected mog:window WindowHandle");
        return false;
    }

    outHandle = static_cast<WindowHandle*>(handle.handle_data);
    return true;
}

bool expectOpenWindowHandle(const ExprPackageValue& value,
                            WindowHandle*& outHandle,
                            ExprPackageStringView* outError) {
    if (!expectWindowHandle(value, outHandle, outError)) {
        return false;
    }

    if (outHandle == nullptr || outHandle->window == nullptr) {
        setError(outError, "window handle is closed");
        return false;
    }

    return true;
}

bool expectEventHandle(const ExprPackageValue& value, EventHandle*& outHandle,
                       ExprPackageStringView* outError) {
    outHandle = nullptr;
    if (value.kind != EXPR_PACKAGE_VALUE_HANDLE) {
        setError(outError, "expected EventHandle");
        return false;
    }

    const ExprPackageHandleValue& handle = value.as.handle_value;
    if (handle.package_namespace == nullptr || handle.package_name == nullptr ||
        handle.type_name == nullptr || handle.handle_data == nullptr) {
        setError(outError, "invalid EventHandle metadata");
        return false;
    }

    if (std::string_view(handle.package_namespace) != "mog" ||
        std::string_view(handle.package_name) != "window" ||
        std::string_view(handle.type_name) != "EventHandle") {
        setError(outError, "expected mog:window EventHandle");
        return false;
    }

    outHandle = static_cast<EventHandle*>(handle.handle_data);
    return true;
}

bool validateColorChannel(int64_t value, const char* name,
                          ExprPackageStringView* outError) {
    if (value < 0 || value > 255) {
        static thread_local std::string message;
        message = std::string(name) + " must be in range 0..255";
        setError(outError, message.c_str());
        return false;
    }

    return true;
}

bool validateDelayMs(int64_t value, ExprPackageStringView* outError) {
    if (value < 0) {
        return setStaticError(outError, "delay expects a non-negative duration",
                              37);
    }

    if (value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        return setStaticError(outError, "delay duration is too large", 27);
    }

    return true;
}

bool validateTextScale(int64_t value, ExprPackageStringView* outError) {
    if (value < 1) {
        return setStaticError(outError, "drawText expects scale >= 1", 28);
    }

    return true;
}

SDL_Surface* getWindowSurface(WindowHandle* windowHandle,
                              ExprPackageStringView* outError) {
    SDL_Surface* surface = SDL_GetWindowSurface(windowHandle->window);
    if (surface == nullptr) {
        setError(outError, SDL_GetError());
    }
    return surface;
}

bool clipRectToSurface(SDL_Surface* surface, int64_t x, int64_t y, int64_t width,
                       int64_t height, SDL_Rect& outRect) {
    if (surface == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    int64_t clippedX = x;
    int64_t clippedY = y;
    int64_t clippedWidth = width;
    int64_t clippedHeight = height;

    if (clippedX < 0) {
        clippedWidth += clippedX;
        clippedX = 0;
    }
    if (clippedY < 0) {
        clippedHeight += clippedY;
        clippedY = 0;
    }

    const int64_t surfaceWidth = static_cast<int64_t>(surface->w);
    const int64_t surfaceHeight = static_cast<int64_t>(surface->h);
    if (clippedX >= surfaceWidth || clippedY >= surfaceHeight ||
        clippedWidth <= 0 || clippedHeight <= 0) {
        return false;
    }

    if (clippedWidth > surfaceWidth - clippedX) {
        clippedWidth = surfaceWidth - clippedX;
    }
    if (clippedHeight > surfaceHeight - clippedY) {
        clippedHeight = surfaceHeight - clippedY;
    }

    if (clippedWidth <= 0 || clippedHeight <= 0) {
        return false;
    }

    outRect.x = static_cast<int>(clippedX);
    outRect.y = static_cast<int>(clippedY);
    outRect.w = static_cast<int>(clippedWidth);
    outRect.h = static_cast<int>(clippedHeight);
    return true;
}

bool mapSurfaceColor(SDL_Surface* surface, int64_t r, int64_t g, int64_t b,
                     Uint32& outColor, ExprPackageStringView* outError) {
    if (!validateColorChannel(r, "red", outError) ||
        !validateColorChannel(g, "green", outError) ||
        !validateColorChannel(b, "blue", outError)) {
        return false;
    }

    outColor = SDL_MapRGB(surface->format, static_cast<Uint8>(r),
                          static_cast<Uint8>(g), static_cast<Uint8>(b));
    return true;
}

bool fillSurfaceMapped(SDL_Surface* surface, const SDL_Rect* rect, Uint32 color,
                       ExprPackageStringView* outError) {
    if (SDL_FillRect(surface, rect, color) != 0) {
        setError(outError, SDL_GetError());
        return false;
    }

    return true;
}

bool fillSurface(SDL_Surface* surface, const SDL_Rect* rect, int64_t r, int64_t g,
                 int64_t b, ExprPackageStringView* outError) {
    Uint32 color = 0;
    if (!mapSurfaceColor(surface, r, g, b, color, outError)) {
        return false;
    }

    return fillSurfaceMapped(surface, rect, color, outError);
}

const uint8_t* glyphForCodeUnit(unsigned char codeUnit) {
    if (codeUnit >= 32 && codeUnit <= 126) {
        return kBuiltinFont[codeUnit - 32];
    }

    return kBuiltinFont['?' - 32];
}

bool drawGlyph(SDL_Surface* surface, int64_t x, int64_t y, int64_t scale,
               const uint8_t* glyph, Uint32 color,
               ExprPackageStringView* outError) {
    if (surface == nullptr || glyph == nullptr) {
        return true;
    }

    for (int64_t row = 0; row < kBuiltinGlyphHeight; ++row) {
        const uint8_t bits = glyph[row];
        for (int64_t column = 0; column < kBuiltinGlyphWidth; ++column) {
            if ((bits & (1u << (7 - column))) == 0) {
                continue;
            }

            SDL_Rect rect{};
            if (!clipRectToSurface(surface, x + (column * scale),
                                   y + (row * scale), scale, scale, rect)) {
                continue;
            }

            if (!fillSurfaceMapped(surface, &rect, color, outError)) {
                return false;
            }
        }
    }

    return true;
}

bool createWindow(const ExprHostApi* hostApi, const ExprPackageValue* args,
                  size_t argc, ExprPackageValue* outResult,
                  ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 3 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 3 arguments");
        return false;
    }

    if (args[0].kind != EXPR_PACKAGE_VALUE_STR ||
        args[1].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[2].kind != EXPR_PACKAGE_VALUE_I64) {
        setError(outError, "create expects (str, i64, i64)");
        return false;
    }

    const int64_t width = args[1].as.i64_value;
    const int64_t height = args[2].as.i64_value;
    if (width <= 0 || height <= 0) {
        setError(outError, "create expects positive width and height");
        return false;
    }

    if (!acquireSdl(outError)) {
        return false;
    }

    auto* handle = new (std::nothrow) WindowHandle();
    if (handle == nullptr) {
        releaseSdl();
        setError(outError, "allocation failed");
        return false;
    }

    std::string title(args[0].as.string_value.data, args[0].as.string_value.length);
    handle->window = SDL_CreateWindow(title.c_str(),
                                      SDL_WINDOWPOS_UNDEFINED,
                                      SDL_WINDOWPOS_UNDEFINED,
                                      static_cast<int>(width),
                                      static_cast<int>(height),
                                      SDL_WINDOW_HIDDEN);
    if (handle->window == nullptr) {
        delete handle;
        releaseSdl();
        setError(outError, SDL_GetError());
        return false;
    }

    handle->ownsSdlRef = true;
    outResult->kind = EXPR_PACKAGE_VALUE_HANDLE;
    outResult->as.handle_value = {"mog", "window", "WindowHandle", handle,
                                  releaseWindowHandle};
    return true;
}

bool closeWindow(const ExprHostApi* hostApi, const ExprPackageValue* args,
                 size_t argc, ExprPackageValue* outResult,
                 ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* handle = nullptr;
    if (!expectWindowHandle(args[0], handle, outError)) {
        return false;
    }

    if (handle != nullptr && handle->window != nullptr) {
        SDL_DestroyWindow(handle->window);
        handle->window = nullptr;
    }
    if (handle != nullptr && handle->ownsSdlRef) {
        handle->ownsSdlRef = false;
        releaseSdl();
    }

    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool showWindow(const ExprHostApi* hostApi, const ExprPackageValue* args,
                size_t argc, ExprPackageValue* outResult,
                ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    SDL_ShowWindow(windowHandle->window);
    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool hideWindow(const ExprHostApi* hostApi, const ExprPackageValue* args,
                size_t argc, ExprPackageValue* outResult,
                ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    SDL_HideWindow(windowHandle->window);
    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool pollEvent(const ExprHostApi* hostApi, const ExprPackageValue* args,
               size_t argc, ExprPackageValue* outResult,
               ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    (void)windowHandle;
    SDL_Event event;
    if (SDL_PollEvent(&event) == 0) {
        outResult->kind = EXPR_PACKAGE_VALUE_NULL;
        return true;
    }

    auto* eventHandle = new (std::nothrow) EventHandle();
    if (eventHandle == nullptr) {
        setError(outError, "allocation failed");
        return false;
    }

    eventHandle->event = event;
    outResult->kind = EXPR_PACKAGE_VALUE_HANDLE;
    outResult->as.handle_value = {"mog", "window", "EventHandle", eventHandle,
                                  releaseEventHandle};
    return true;
}

bool eventKind(const ExprHostApi* hostApi, const ExprPackageValue* args,
               size_t argc, ExprPackageValue* outResult,
               ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    EventHandle* eventHandle = nullptr;
    if (!expectEventHandle(args[0], eventHandle, outError)) {
        return false;
    }

    static thread_local std::string kind;
    switch (eventHandle->event.type) {
        case SDL_QUIT:
            kind = "quit";
            break;
        case SDL_KEYDOWN:
            kind = "key_down";
            break;
        case SDL_KEYUP:
            kind = "key_up";
            break;
        case SDL_MOUSEMOTION:
            kind = "mouse_move";
            break;
        case SDL_MOUSEBUTTONDOWN:
            kind = "mouse_down";
            break;
        case SDL_MOUSEBUTTONUP:
            kind = "mouse_up";
            break;
        default:
            kind = "unknown";
            break;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_STR;
    outResult->as.string_value = {kind.c_str(), kind.size()};
    return true;
}

bool eventKeyCode(const ExprHostApi* hostApi, const ExprPackageValue* args,
                  size_t argc, ExprPackageValue* outResult,
                  ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    EventHandle* eventHandle = nullptr;
    if (!expectEventHandle(args[0], eventHandle, outError)) {
        return false;
    }

    int64_t keyCode = -1;
    if (eventHandle->event.type == SDL_KEYDOWN ||
        eventHandle->event.type == SDL_KEYUP) {
        keyCode = static_cast<int64_t>(eventHandle->event.key.keysym.sym);
    }

    outResult->kind = EXPR_PACKAGE_VALUE_I64;
    outResult->as.i64_value = keyCode;
    return true;
}

bool isKeyDown(const ExprHostApi* hostApi, const ExprPackageValue* args,
               size_t argc, ExprPackageValue* outResult,
               ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 2 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 2 arguments");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }
    (void)windowHandle;

    if (args[1].kind != EXPR_PACKAGE_VALUE_I64) {
        setError(outError, "isKeyDown expects an i64 key code");
        return false;
    }

    const SDL_Keycode keyCode = static_cast<SDL_Keycode>(args[1].as.i64_value);
    const SDL_Scancode scanCode = SDL_GetScancodeFromKey(keyCode);
    if (scanCode == SDL_SCANCODE_UNKNOWN) {
        outResult->kind = EXPR_PACKAGE_VALUE_BOOL;
        outResult->as.boolean_value = false;
        return true;
    }

    int count = 0;
    const Uint8* state = SDL_GetKeyboardState(&count);
    const bool pressed =
        state != nullptr && static_cast<int>(scanCode) >= 0 &&
        static_cast<int>(scanCode) < count && state[scanCode] != 0;
    outResult->kind = EXPR_PACKAGE_VALUE_BOOL;
    outResult->as.boolean_value = pressed;
    return true;
}

bool mouseX(const ExprHostApi* hostApi, const ExprPackageValue* args,
            size_t argc, ExprPackageValue* outResult,
            ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }
    (void)windowHandle;

    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);
    outResult->kind = EXPR_PACKAGE_VALUE_I64;
    outResult->as.i64_value = static_cast<int64_t>(x);
    return true;
}

bool mouseY(const ExprHostApi* hostApi, const ExprPackageValue* args,
            size_t argc, ExprPackageValue* outResult,
            ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }
    (void)windowHandle;

    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);
    outResult->kind = EXPR_PACKAGE_VALUE_I64;
    outResult->as.i64_value = static_cast<int64_t>(y);
    return true;
}

bool clearWindow(const ExprHostApi* hostApi, const ExprPackageValue* args,
                 size_t argc, ExprPackageValue* outResult,
                 ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    SDL_Surface* surface = getWindowSurface(windowHandle, outError);
    if (surface == nullptr) {
        return false;
    }

    if (!fillSurface(surface, nullptr, 0, 0, 0, outError)) {
        return false;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool clearWindowRgb(const ExprHostApi* hostApi, const ExprPackageValue* args,
                    size_t argc, ExprPackageValue* outResult,
                    ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 4 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 4 arguments");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    for (size_t index = 1; index < 4; ++index) {
        if (args[index].kind != EXPR_PACKAGE_VALUE_I64) {
            setError(outError, "clearRgb expects (WindowHandle, i64, i64, i64)");
            return false;
        }
    }

    SDL_Surface* surface = getWindowSurface(windowHandle, outError);
    if (surface == nullptr) {
        return false;
    }

    if (!fillSurface(surface, nullptr, args[1].as.i64_value, args[2].as.i64_value,
                     args[3].as.i64_value, outError)) {
        return false;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool fillRect(const ExprHostApi* hostApi, const ExprPackageValue* args, size_t argc,
              ExprPackageValue* outResult,
              ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 8 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 8 arguments");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    for (size_t index = 1; index < 8; ++index) {
        if (args[index].kind != EXPR_PACKAGE_VALUE_I64) {
            setError(outError,
                     "fillRect expects (WindowHandle, i64, i64, i64, i64, i64, "
                     "i64, i64)");
            return false;
        }
    }

    const int64_t x = args[1].as.i64_value;
    const int64_t y = args[2].as.i64_value;
    const int64_t width = args[3].as.i64_value;
    const int64_t height = args[4].as.i64_value;

    SDL_Surface* surface = getWindowSurface(windowHandle, outError);
    if (surface == nullptr) {
        return false;
    }

    SDL_Rect rect{};
    if (!clipRectToSurface(surface, x, y, width, height, rect)) {
        outResult->kind = EXPR_PACKAGE_VALUE_NULL;
        return true;
    }

    if (!fillSurface(surface, &rect, args[5].as.i64_value, args[6].as.i64_value,
                     args[7].as.i64_value, outError)) {
        return false;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool drawText(const ExprHostApi* hostApi, const ExprPackageValue* args, size_t argc,
              ExprPackageValue* outResult,
              ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 8 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 8 arguments");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    if (args[1].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[2].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[3].kind != EXPR_PACKAGE_VALUE_STR ||
        args[4].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[5].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[6].kind != EXPR_PACKAGE_VALUE_I64 ||
        args[7].kind != EXPR_PACKAGE_VALUE_I64) {
        setError(outError,
                 "drawText expects (WindowHandle, i64, i64, str, i64, i64, "
                 "i64, i64)");
        return false;
    }

    const int64_t x = args[1].as.i64_value;
    const int64_t y = args[2].as.i64_value;
    const int64_t scale = args[4].as.i64_value;
    if (!validateTextScale(scale, outError)) {
        return false;
    }

    SDL_Surface* surface = getWindowSurface(windowHandle, outError);
    if (surface == nullptr) {
        return false;
    }

    Uint32 color = 0;
    if (!mapSurfaceColor(surface, args[5].as.i64_value, args[6].as.i64_value,
                         args[7].as.i64_value, color, outError)) {
        return false;
    }

    const std::string_view text(args[3].as.string_value.data,
                                args[3].as.string_value.length);
    const int64_t glyphAdvance = kBuiltinGlyphAdvance * scale;
    const int64_t lineAdvance = kBuiltinLineAdvance * scale;
    int64_t cursorX = x;
    int64_t cursorY = y;

    for (const unsigned char codeUnit : text) {
        if (codeUnit == '\n') {
            cursorX = x;
            cursorY += lineAdvance;
            continue;
        }

        const uint8_t* glyph = glyphForCodeUnit(codeUnit);
        if (!drawGlyph(surface, cursorX, cursorY, scale, glyph, color,
                       outError)) {
            return false;
        }

        cursorX += glyphAdvance;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool presentWindow(const ExprHostApi* hostApi, const ExprPackageValue* args,
                   size_t argc, ExprPackageValue* outResult,
                   ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    WindowHandle* windowHandle = nullptr;
    if (!expectOpenWindowHandle(args[0], windowHandle, outError)) {
        return false;
    }

    if (SDL_UpdateWindowSurface(windowHandle->window) != 0) {
        setError(outError, SDL_GetError());
        return false;
    }

    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

bool delayMs(const ExprHostApi* hostApi, const ExprPackageValue* args, size_t argc,
             ExprPackageValue* outResult,
             ExprPackageStringView* outError) {
    (void)hostApi;
    if (argc != 1 || args == nullptr || outResult == nullptr) {
        setError(outError, "expected exactly 1 argument");
        return false;
    }

    if (args[0].kind != EXPR_PACKAGE_VALUE_I64) {
        setError(outError, "delay expects an i64 duration in milliseconds");
        return false;
    }

    const int64_t durationMs = args[0].as.i64_value;
    if (!validateDelayMs(durationMs, outError)) {
        return false;
    }

    SDL_Delay(static_cast<uint32_t>(durationMs));
    outResult->kind = EXPR_PACKAGE_VALUE_NULL;
    return true;
}

constexpr ExprPackageFunctionExport kFunctions[] = {
    {"create", "fn(str, i64, i64) -> handle<mog:window:WindowHandle>", 3,
     createWindow},
    {"close", "fn(handle<mog:window:WindowHandle>) -> void", 1, closeWindow},
    {"show", "fn(handle<mog:window:WindowHandle>) -> void", 1, showWindow},
    {"hide", "fn(handle<mog:window:WindowHandle>) -> void", 1, hideWindow},
    {"pollEvent",
     "fn(handle<mog:window:WindowHandle>) -> handle<mog:window:EventHandle>?",
     1, pollEvent},
    {"eventKind", "fn(handle<mog:window:EventHandle>) -> str", 1, eventKind},
    {"eventKeyCode", "fn(handle<mog:window:EventHandle>) -> i64", 1,
     eventKeyCode},
    {"isKeyDown", "fn(handle<mog:window:WindowHandle>, i64) -> bool", 2,
     isKeyDown},
    {"mouseX", "fn(handle<mog:window:WindowHandle>) -> i64", 1, mouseX},
    {"mouseY", "fn(handle<mog:window:WindowHandle>) -> i64", 1, mouseY},
    {"clear", "fn(handle<mog:window:WindowHandle>) -> void", 1, clearWindow},
    {"clearRgb",
     "fn(handle<mog:window:WindowHandle>, i64, i64, i64) -> void", 4,
     clearWindowRgb},
    {"fillRect",
     "fn(handle<mog:window:WindowHandle>, i64, i64, i64, i64, i64, i64, i64) "
     "-> void",
     8, fillRect},
    {"drawText",
     "fn(handle<mog:window:WindowHandle>, i64, i64, str, i64, i64, i64, i64) "
     "-> void",
     8, drawText},
    {"present", "fn(handle<mog:window:WindowHandle>) -> void", 1,
     presentWindow},
    {"delay", "fn(i64) -> void", 1, delayMs},
};

constexpr ExprPackageConstantExport kConstants[] = {
    {"PACKAGE_ID",
     "str",
     {EXPR_PACKAGE_VALUE_STR, {.string_value = {"mog:window", 10}}}},
    {"KEY_ESCAPE", "i64", {EXPR_PACKAGE_VALUE_I64, {.i64_value = SDLK_ESCAPE}}},
    {"KEY_SPACE", "i64", {EXPR_PACKAGE_VALUE_I64, {.i64_value = SDLK_SPACE}}},
};

constexpr ExprPackageRegistration kRegistration = {
    EXPR_NATIVE_PACKAGE_ABI_VERSION,
    "mog",
    "window",
    kFunctions,
    sizeof(kFunctions) / sizeof(kFunctions[0]),
    kConstants,
    sizeof(kConstants) / sizeof(kConstants[0]),
};

}  // namespace

extern "C" const ExprPackageRegistration* exprRegisterPackage(void) {
    return &kRegistration;
}
