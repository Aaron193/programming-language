#include "NativePackageAPI.hpp"

#include <SDL.h>

#include <cstdint>
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

void setError(ExprPackageStringView* outError, const char* message) {
    if (outError == nullptr || message == nullptr) {
        return;
    }

    *outError = {message, std::char_traits<char>::length(message)};
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

    SDL_Surface* surface = SDL_GetWindowSurface(windowHandle->window);
    if (surface == nullptr) {
        setError(outError, SDL_GetError());
        return false;
    }

    if (SDL_FillRect(surface, nullptr,
                     SDL_MapRGB(surface->format, 0, 0, 0)) != 0) {
        setError(outError, SDL_GetError());
        return false;
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
    {"present", "fn(handle<mog:window:WindowHandle>) -> void", 1,
     presentWindow},
};

constexpr ExprPackageConstantExport kConstants[] = {
    {"PACKAGE_ID",
     "str",
     {EXPR_PACKAGE_VALUE_STR, {.string_value = {"mog:window", 10}}}},
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
