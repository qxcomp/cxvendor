/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<SDL3/SDL.h>)
#    include <SDL3/SDL.h>
#    define SDL3_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef SDL3_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t Sint8;
typedef int16_t Sint16;
typedef int32_t Sint32;
typedef int64_t Sint64;
typedef int SDL_bool;
#define SDL_FALSE 0
#define SDL_TRUE 1
#define SDL_AUDIO_STOPPED 0
#define SDL_CONTROLLER_TYPE_UNKNOWN 0
#define SDL_SCANCODE_UNKNOWN 0
typedef Uint32 SDL_TLSID;

/* opaque structs */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Cursor SDL_Cursor;
typedef struct SDL_RWops SDL_RWops;
typedef struct SDL_IOStream SDL_IOStream;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_Camera SDL_Camera;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_Palette SDL_Palette;
typedef struct SDL_Mutex SDL_Mutex;
typedef struct SDL_Condition SDL_Condition;
typedef struct SDL_Semaphore SDL_Semaphore;
typedef struct SDL_RWLock SDL_RWLock;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Event SDL_Event;
typedef struct SDL_AudioSpec SDL_AudioSpec;
typedef struct SDL_CameraSpec SDL_CameraSpec;
typedef struct SDL_MessageBoxData SDL_MessageBoxData;
struct SDL_Rect { int x, y, w, h; };
typedef struct SDL_Rect SDL_Rect;
struct SDL_FRect { float x, y, w, h; };
typedef struct SDL_FRect SDL_FRect;
struct SDL_FPoint { float x, y; };
typedef struct SDL_FPoint SDL_FPoint;
/* scalar / enum */
typedef Uint32 SDL_AudioDeviceID;
typedef Uint32 SDL_CameraID;
typedef int SDL_BlendMode;
typedef int SDL_Colorspace;
typedef int SDL_EventAction;
typedef int SDL_FlipMode;
typedef int SDL_Folder;
typedef int SDL_GamepadAxis;
typedef int SDL_GamepadButton;
typedef int SDL_GamepadType;
typedef Uint32 SDL_GPUShaderFormat;
typedef Uint32 SDL_InitFlags;
typedef int SDL_IOStatus;
typedef int SDL_IOWhence;
typedef Sint32 SDL_JoystickID;
typedef Uint32 SDL_Keycode;
typedef Uint16 SDL_Keymod;
typedef Uint32 SDL_MessageBoxFlags;
typedef Uint32 SDL_MouseButtonFlags;
typedef int SDL_PenID;
typedef Uint32 SDL_PixelFormat;
typedef Uint32 SDL_PropertiesID;
typedef int SDL_ScaleMode;
typedef Uint32 SDL_Scancode;
typedef int SDL_SystemCursor;
typedef int SDL_TextureAccess;
typedef int (*SDL_ThreadFunction)(void *);
typedef Uint64 SDL_ThreadID;
typedef Uint64 SDL_TimerID;
typedef void (*SDL_FunctionPointer)(void);
typedef void *(*SDL_malloc_func)(size_t);
typedef void *(*SDL_calloc_func)(size_t, size_t);
typedef void *(*SDL_realloc_func)(void *, size_t);
typedef void (*SDL_free_func)(void *);
typedef bool (*SDL_EventFilter)(void *, SDL_Event *);
typedef bool (*SDL_NSTimerCallback)(void *, Uint32);
typedef bool (*SDL_TimerCallback)(void *, Uint32);

typedef Uint64 SDL_WindowFlags;
typedef Uint32 SDL_WindowID;

#endif /* SDL3_DYN_HAVE_HEADER */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *sdl3_handle = 0;
static int sdl3_state = 0;
static void *sdl3_load_lib(const char *p) { return dlopen(p, RTLD_LAZY | RTLD_LOCAL); }
static void *sdl3_get_sym(const char *n) { return sdl3_handle ? dlsym(sdl3_handle, n) : 0; }
static void *sdl3_get_sym2(const char *n) {
  if (!sdl3_handle)
    return 0;
  return dlsym(sdl3_handle, n);
}
static int sdl3_ensure_lib(void) {
  if (sdl3_state)
    return sdl3_state > 0;
  const char *candidates[] = {
    getenv("SDL3_DYN_LIB"),
    "libSDL3.so.0",
    "libSDL3.dylib",
    "SDL3.dll",
    "libSDL3.0.dylib",
  };
  int i;
  for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
    if (!candidates[i] || !*candidates[i])
      continue;
    sdl3_handle = sdl3_load_lib(candidates[i]);
    if (!sdl3_handle)
      continue;
    if (!sdl3_get_sym("SDL_Init")) {
      dlclose(sdl3_handle);
      sdl3_handle = 0;
      continue;
    }
    fprintf(stderr, "sdl3_dyn: loaded %d %s\n", i, candidates[i]);
    break;
  }
  if (sdl3_handle && !sdl3_get_sym("SDL_Init")) {
    dlclose(sdl3_handle);
    sdl3_handle = 0;
  }
  sdl3_state = sdl3_handle ? 1 : -1;
  return sdl3_state > 0;
}
int sdl3_libok(void) { return sdl3_ensure_lib(); }
void sdl3_dyn_cleanup(void) {
  if (sdl3_handle) {
    dlclose(sdl3_handle);
    sdl3_handle = 0;
  }
  sdl3_state = 0;
}
#define SDL3_R(v, n, f)                                                             \
  if (!(v)) {                                                                  \
    if (!sdl3_ensure_lib())                                                                  \
      return f;                                                                \
    (v) = sdl3_get_sym(n);                                                                \
    if (!(v))                                                                  \
      return f;                                                                \
  }
#define SDL3_P(v, n, f)                                                             \
  if (!(v)) {                                                                  \
    if (!sdl3_ensure_lib())                                                                  \
      return f;                                                                \
    (v) = sdl3_get_sym(n);                                                                \
    if (!(v))                                                                  \
      return f;                                                                \
  }

bool SDL_Init(SDL_InitFlags a) {
  static bool (*f)(SDL_InitFlags);
  SDL3_R(f, "SDL_Init", 0);
  return f(a);
}
bool SDL_InitSubSystem(SDL_InitFlags a) {
  static bool (*f)(SDL_InitFlags);
  SDL3_R(f, "SDL_InitSubSystem", 0);
  return f(a);
}
void SDL_QuitSubSystem(SDL_InitFlags a) {
  static void (*f)(SDL_InitFlags);
  SDL3_R(f, "SDL_QuitSubSystem", );
  f(a);
}
void SDL_Quit(void) {
  static void (*f)(void);
  SDL3_R(f, "SDL_Quit", );
  f();
}
SDL_InitFlags SDL_WasInit(SDL_InitFlags a) {
  static SDL_InitFlags (*f)(SDL_InitFlags);
  SDL3_R(f, "SDL_WasInit", 0);
  return f(a);
}
bool SDL_SetAppMetadata(const char *a, const char *b, const char *c) {
  static bool (*f)(const char *, const char *, const char *);
  SDL3_R(f, "SDL_SetAppMetadata", 0);
  return f(a, b, c);
}
bool SDL_SetAppMetadataProperty(const char *a, const char *b) {
  static bool (*f)(const char *, const char *);
  SDL3_R(f, "SDL_SetAppMetadataProperty", 0);
  return f(a, b);
}
const char *SDL_GetAppMetadataProperty(const char *a) {
  static const char *(*f)(const char *);
  SDL3_R(f, "SDL_GetAppMetadataProperty", 0);
  return f(a);
}
const char *SDL_GetError(void) {
  static const char *(*f)(void);
  SDL3_R(f, "SDL_GetError", "");
  return f();
}
bool SDL_SetError(const char *a, ...) {
  static bool (*f)(const char *, ...);
  SDL3_R(f, "SDL_SetError", 0);
  return f(a);
}
bool SDL_ClearError(void) {
  static bool (*f)(void);
  SDL3_R(f, "SDL_ClearError", 0);
  return f();
}
int SDL_GetVersion(void) {
  static int (*f)(void);
  SDL3_R(f, "SDL_GetVersion", 0);
  return f();
}
const char *SDL_GetRevision(void) {
  static const char *(*f)(void);
  SDL3_R(f, "SDL_GetRevision", "");
  return f();
}
int SDL_GetNumAllocations(void) {
  static int (*f)(void);
  SDL3_R(f, "SDL_GetNumAllocations", 0);
  return f();
}
bool SDL_SetMemoryFunctions(SDL_malloc_func a, SDL_calloc_func b,
                            SDL_realloc_func c, SDL_free_func d) {
  static bool (*f)(SDL_malloc_func, SDL_calloc_func, SDL_realloc_func,
                   SDL_free_func);
  SDL3_R(f, "SDL_SetMemoryFunctions", 0);
  return f(a, b, c, d);
}

SDL_Window *SDL_CreateWindow(const char *a, int b, int c, SDL_WindowFlags d) {
  static SDL_Window *(*f)(const char *, int, int, SDL_WindowFlags);
  SDL3_R(f, "SDL_CreateWindow", 0);
  return f(a, b, c, d);
}
SDL_Window *SDL_CreateWindowWithProperties(SDL_PropertiesID a) {
  static SDL_Window *(*f)(SDL_PropertiesID);
  SDL3_R(f, "SDL_CreateWindowWithProperties", 0);
  return f(a);
}
void SDL_DestroyWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  SDL3_R(f, "SDL_DestroyWindow", );
  f(a);
}
bool SDL_SetWindowTitle(SDL_Window *a, const char *b) {
  static bool (*f)(SDL_Window *, const char *);
  SDL3_R(f, "SDL_SetWindowTitle", 0);
  return f(a, b);
}
const char *SDL_GetWindowTitle(SDL_Window *a) {
  static const char *(*f)(SDL_Window *);
  SDL3_R(f, "SDL_GetWindowTitle", "");
  return f(a);
}
bool SDL_SetWindowPosition(SDL_Window *a, int b, int c) {
  static bool (*f)(SDL_Window *, int, int);
  SDL3_R(f, "SDL_SetWindowPosition", 0);
  return f(a, b, c);
}
bool SDL_GetWindowPosition(SDL_Window *a, int *b, int *c) {
  static bool (*f)(SDL_Window *, int *, int *);
  SDL3_R(f, "SDL_GetWindowPosition", 0);
  return f(a, b, c);
}
bool SDL_SetWindowSize(SDL_Window *a, int b, int c) {
  static bool (*f)(SDL_Window *, int, int);
  SDL3_R(f, "SDL_SetWindowSize", 0);
  return f(a, b, c);
}
bool SDL_GetWindowSize(SDL_Window *a, int *b, int *c) {
  static bool (*f)(SDL_Window *, int *, int *);
  SDL3_R(f, "SDL_GetWindowSize", 0);
  return f(a, b, c);
}
bool SDL_SetWindowMinimumSize(SDL_Window *a, int b, int c) {
  static bool (*f)(SDL_Window *, int, int);
  SDL3_R(f, "SDL_SetWindowMinimumSize", 0);
  return f(a, b, c);
}
bool SDL_GetWindowMinimumSize(SDL_Window *a, int *b, int *c) {
  static bool (*f)(SDL_Window *, int *, int *);
  SDL3_R(f, "SDL_GetWindowMinimumSize", 0);
  return f(a, b, c);
}
bool SDL_SetWindowMaximumSize(SDL_Window *a, int b, int c) {
  static bool (*f)(SDL_Window *, int, int);
  SDL3_R(f, "SDL_SetWindowMaximumSize", 0);
  return f(a, b, c);
}
bool SDL_GetWindowMaximumSize(SDL_Window *a, int *b, int *c) {
  static bool (*f)(SDL_Window *, int *, int *);
  SDL3_R(f, "SDL_GetWindowMaximumSize", 0);
  return f(a, b, c);
}
bool SDL_SetWindowBordered(SDL_Window *a, bool b) {
  static bool (*f)(SDL_Window *, bool);
  SDL3_R(f, "SDL_SetWindowBordered", 0);
  return f(a, b);
}
bool SDL_SetWindowResizable(SDL_Window *a, bool b) {
  static bool (*f)(SDL_Window *, bool);
  SDL3_R(f, "SDL_SetWindowResizable", 0);
  return f(a, b);
}
bool SDL_SetWindowAlwaysOnTop(SDL_Window *a, bool b) {
  static bool (*f)(SDL_Window *, bool);
  SDL3_R(f, "SDL_SetWindowAlwaysOnTop", 0);
  return f(a, b);
}
bool SDL_ShowWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_ShowWindow", 0);
  return f(a);
}
bool SDL_HideWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_HideWindow", 0);
  return f(a);
}
bool SDL_RaiseWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_RaiseWindow", 0);
  return f(a);
}
bool SDL_MaximizeWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_MaximizeWindow", 0);
  return f(a);
}
bool SDL_MinimizeWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_MinimizeWindow", 0);
  return f(a);
}
bool SDL_RestoreWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_RestoreWindow", 0);
  return f(a);
}
bool SDL_SetWindowFullscreen(SDL_Window *a, bool b) {
  static bool (*f)(SDL_Window *, bool);
  SDL3_R(f, "SDL_SetWindowFullscreen", 0);
  return f(a, b);
}
bool SDL_SyncWindow(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_SyncWindow", 0);
  return f(a);
}
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *a) {
  static SDL_WindowFlags (*f)(SDL_Window *);
  SDL3_R(f, "SDL_GetWindowFlags", 0);
  return f(a);
}
SDL_Window *SDL_GetWindowFromID(SDL_WindowID a) {
  static SDL_Window *(*f)(SDL_WindowID);
  SDL3_R(f, "SDL_GetWindowFromID", 0);
  return f(a);
}
SDL_WindowID SDL_GetWindowID(SDL_Window *a) {
  static SDL_WindowID (*f)(SDL_Window *);
  SDL3_R(f, "SDL_GetWindowID", 0);
  return f(a);
}
SDL_Window *SDL_GetWindowParent(SDL_Window *a) {
  static SDL_Window *(*f)(SDL_Window *);
  SDL3_R(f, "SDL_GetWindowParent", 0);
  return f(a);
}
bool SDL_SetWindowOpacity(SDL_Window *a, float b) {
  static bool (*f)(SDL_Window *, float);
  SDL3_R(f, "SDL_SetWindowOpacity", 0);
  return f(a, b);
}
float SDL_GetWindowOpacity(SDL_Window *a) {
  static float (*f)(SDL_Window *);
  SDL3_R(f, "SDL_GetWindowOpacity", 0);
  return f(a);
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *a, const char *b) {
  static SDL_Renderer *(*f)(SDL_Window *, const char *);
  SDL3_R(f, "SDL_CreateRenderer", 0);
  return f(a, b);
}
SDL_Renderer *SDL_CreateRendererWithProperties(SDL_PropertiesID a) {
  static SDL_Renderer *(*f)(SDL_PropertiesID);
  SDL3_R(f, "SDL_CreateRendererWithProperties", 0);
  return f(a);
}
void SDL_DestroyRenderer(SDL_Renderer *a) {
  static void (*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_DestroyRenderer", );
  f(a);
}
bool SDL_RenderClear(SDL_Renderer *a) {
  static bool (*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_RenderClear", 0);
  return f(a);
}
bool SDL_RenderPresent(SDL_Renderer *a) {
  static bool (*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_RenderPresent", 0);
  return f(a);
}
bool SDL_RenderTexture(SDL_Renderer *a, SDL_Texture *b, const SDL_FRect *c,
                       const SDL_FRect *d) {
  static bool (*f)(SDL_Renderer *, SDL_Texture *, const SDL_FRect *,
                   const SDL_FRect *);
  SDL3_R(f, "SDL_RenderTexture", 0);
  return f(a, b, c, d);
}
bool SDL_RenderTextureRotated(SDL_Renderer *a, SDL_Texture *b,
                              const SDL_FRect *c, const SDL_FRect *d, double e,
                              const SDL_FPoint *pt, SDL_FlipMode g) {
  static bool (*f)(SDL_Renderer *, SDL_Texture *, const SDL_FRect *,
                   const SDL_FRect *, double, const SDL_FPoint *, SDL_FlipMode);
  SDL3_R(f, "SDL_RenderTextureRotated", 0);
  return f(a, b, c, d, e, pt, g);
}
bool SDL_RenderTextureTiled(SDL_Renderer *a, SDL_Texture *b, const SDL_FRect *c,
                            float d, const SDL_FRect *d2) {
  static bool (*f)(SDL_Renderer *, SDL_Texture *, const SDL_FRect *, float,
                   const SDL_FRect *);
  SDL3_R(f, "SDL_RenderTextureTiled", 0);
  return f(a, b, c, d, d2);
}
bool SDL_RenderLine(SDL_Renderer *a, float b, float c, float d, float e) {
  static bool (*f)(SDL_Renderer *, float, float, float, float);
  SDL3_R(f, "SDL_RenderLine", 0);
  return f(a, b, c, d, e);
}
bool SDL_RenderLines(SDL_Renderer *a, const SDL_FPoint *b, int c) {
  static bool (*f)(SDL_Renderer *, const SDL_FPoint *, int);
  SDL3_R(f, "SDL_RenderLines", 0);
  return f(a, b, c);
}
bool SDL_RenderPoint(SDL_Renderer *a, float b, float c) {
  static bool (*f)(SDL_Renderer *, float, float);
  SDL3_R(f, "SDL_RenderPoint", 0);
  return f(a, b, c);
}
bool SDL_RenderPoints(SDL_Renderer *a, const SDL_FPoint *b, int c) {
  static bool (*f)(SDL_Renderer *, const SDL_FPoint *, int);
  SDL3_R(f, "SDL_RenderPoints", 0);
  return f(a, b, c);
}
bool SDL_RenderRect(SDL_Renderer *a, const SDL_FRect *b) {
  static bool (*f)(SDL_Renderer *, const SDL_FRect *);
  SDL3_R(f, "SDL_RenderRect", 0);
  return f(a, b);
}
bool SDL_RenderRects(SDL_Renderer *a, const SDL_FRect *b, int c) {
  static bool (*f)(SDL_Renderer *, const SDL_FRect *, int);
  SDL3_R(f, "SDL_RenderRects", 0);
  return f(a, b, c);
}
bool SDL_RenderFillRect(SDL_Renderer *a, const SDL_FRect *b) {
  static bool (*f)(SDL_Renderer *, const SDL_FRect *);
  SDL3_R(f, "SDL_RenderFillRect", 0);
  return f(a, b);
}
bool SDL_RenderFillRects(SDL_Renderer *a, const SDL_FRect *b, int c) {
  static bool (*f)(SDL_Renderer *, const SDL_FRect *, int);
  SDL3_R(f, "SDL_RenderFillRects", 0);
  return f(a, b, c);
}
bool SDL_SetRenderDrawColor(SDL_Renderer *a, Uint8 b, Uint8 c, Uint8 d,
                            Uint8 e) {
  static bool (*f)(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8);
  SDL3_R(f, "SDL_SetRenderDrawColor", 0);
  return f(a, b, c, d, e);
}
bool SDL_GetRenderDrawColor(SDL_Renderer *a, Uint8 *b, Uint8 *c, Uint8 *d,
                            Uint8 *e) {
  static bool (*f)(SDL_Renderer *, Uint8 *, Uint8 *, Uint8 *, Uint8 *);
  SDL3_R(f, "SDL_GetRenderDrawColor", 0);
  return f(a, b, c, d, e);
}
bool SDL_SetRenderDrawBlendMode(SDL_Renderer *a, SDL_BlendMode b) {
  static bool (*f)(SDL_Renderer *, SDL_BlendMode);
  SDL3_R(f, "SDL_SetRenderDrawBlendMode", 0);
  return f(a, b);
}
bool SDL_GetRenderDrawBlendMode(SDL_Renderer *a, SDL_BlendMode *b) {
  static bool (*f)(SDL_Renderer *, SDL_BlendMode *);
  SDL3_R(f, "SDL_GetRenderDrawBlendMode", 0);
  return f(a, b);
}
bool SDL_SetRenderTarget(SDL_Renderer *a, SDL_Texture *b) {
  static bool (*f)(SDL_Renderer *, SDL_Texture *);
  SDL3_R(f, "SDL_SetRenderTarget", 0);
  return f(a, b);
}
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *a) {
  static SDL_Texture *(*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_GetRenderTarget", 0);
  return f(a);
}
bool SDL_SetRenderVSync(SDL_Renderer *a, int b) {
  static bool (*f)(SDL_Renderer *, int);
  SDL3_R(f, "SDL_SetRenderVSync", 0);
  return f(a, b);
}
bool SDL_GetRenderVSync(SDL_Renderer *a, int *b) {
  static bool (*f)(SDL_Renderer *, int *);
  SDL3_R(f, "SDL_GetRenderVSync", 0);
  return f(a, b);
}
bool SDL_RenderClipEnabled(SDL_Renderer *a) {
  static bool (*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_RenderClipEnabled", 0);
  return f(a);
}
bool SDL_SetRenderClipRect(SDL_Renderer *a, const SDL_Rect *b) {
  static bool (*f)(SDL_Renderer *, const SDL_Rect *);
  SDL3_R(f, "SDL_SetRenderClipRect", 0);
  return f(a, b);
}
bool SDL_GetRenderClipRect(SDL_Renderer *a, SDL_Rect *b) {
  static bool (*f)(SDL_Renderer *, SDL_Rect *);
  SDL3_R(f, "SDL_GetRenderClipRect", 0);
  return f(a, b);
}
bool SDL_SetRenderScale(SDL_Renderer *a, float b, float c) {
  static bool (*f)(SDL_Renderer *, float, float);
  SDL3_R(f, "SDL_SetRenderScale", 0);
  return f(a, b, c);
}
bool SDL_GetRenderScale(SDL_Renderer *a, float *b, float *c) {
  static bool (*f)(SDL_Renderer *, float *, float *);
  SDL3_R(f, "SDL_GetRenderScale", 0);
  return f(a, b, c);
}
SDL_Surface *SDL_RenderReadPixels(SDL_Renderer *a, const SDL_Rect *b) {
  static SDL_Surface *(*f)(SDL_Renderer *, const SDL_Rect *);
  SDL3_R(f, "SDL_RenderReadPixels", 0);
  return f(a, b);
}
SDL_Window *SDL_GetRenderWindow(SDL_Renderer *a) {
  static SDL_Window *(*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_GetRenderWindow", 0);
  return f(a);
}
const char *SDL_GetRendererName(SDL_Renderer *a) {
  static const char *(*f)(SDL_Renderer *);
  SDL3_R(f, "SDL_GetRendererName", "");
  return f(a);
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *a, SDL_PixelFormat b,
                               SDL_TextureAccess c, int d, int e) {
  static SDL_Texture *(*f)(SDL_Renderer *, SDL_PixelFormat, SDL_TextureAccess,
                           int, int);
  SDL3_R(f, "SDL_CreateTexture", 0);
  return f(a, b, c, d, e);
}
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *a, SDL_Surface *b) {
  static SDL_Texture *(*f)(SDL_Renderer *, SDL_Surface *);
  SDL3_R(f, "SDL_CreateTextureFromSurface", 0);
  return f(a, b);
}
SDL_Texture *SDL_CreateTextureWithProperties(SDL_Renderer *a,
                                             SDL_PropertiesID b) {
  static SDL_Texture *(*f)(SDL_Renderer *, SDL_PropertiesID);
  SDL3_R(f, "SDL_CreateTextureWithProperties", 0);
  return f(a, b);
}
void SDL_DestroyTexture(SDL_Texture *a) {
  static void (*f)(SDL_Texture *);
  SDL3_R(f, "SDL_DestroyTexture", );
  f(a);
}
bool SDL_UpdateTexture(SDL_Texture *a, const SDL_Rect *b, const void *c,
                       int d) {
  static bool (*f)(SDL_Texture *, const SDL_Rect *, const void *, int);
  SDL3_R(f, "SDL_UpdateTexture", 0);
  return f(a, b, c, d);
}
bool SDL_UpdateYUVTexture(SDL_Texture *a, const SDL_Rect *b, const Uint8 *c,
                          int d, const Uint8 *e, int pitch2, const Uint8 *g, int h) {
  static bool (*f)(SDL_Texture *, const SDL_Rect *, const Uint8 *, int,
                   const Uint8 *, int, const Uint8 *, int);
  SDL3_R(f, "SDL_UpdateYUVTexture", 0);
  return f(a, b, c, d, e, pitch2, g, h);
}
bool SDL_LockTexture(SDL_Texture *a, const SDL_Rect *b, void **c, int *d) {
  static bool (*f)(SDL_Texture *, const SDL_Rect *, void **, int *);
  SDL3_R(f, "SDL_LockTexture", 0);
  return f(a, b, c, d);
}
void SDL_UnlockTexture(SDL_Texture *a) {
  static void (*f)(SDL_Texture *);
  SDL3_R(f, "SDL_UnlockTexture", );
  f(a);
}
bool SDL_GetTextureSize(SDL_Texture *a, float *b, float *c) {
  static bool (*f)(SDL_Texture *, float *, float *);
  SDL3_R(f, "SDL_GetTextureSize", 0);
  return f(a, b, c);
}
bool SDL_SetTextureBlendMode(SDL_Texture *a, SDL_BlendMode b) {
  static bool (*f)(SDL_Texture *, SDL_BlendMode);
  SDL3_R(f, "SDL_SetTextureBlendMode", 0);
  return f(a, b);
}
bool SDL_GetTextureBlendMode(SDL_Texture *a, SDL_BlendMode *b) {
  static bool (*f)(SDL_Texture *, SDL_BlendMode *);
  SDL3_R(f, "SDL_GetTextureBlendMode", 0);
  return f(a, b);
}
bool SDL_SetTextureScaleMode(SDL_Texture *a, SDL_ScaleMode b) {
  static bool (*f)(SDL_Texture *, SDL_ScaleMode);
  SDL3_R(f, "SDL_SetTextureScaleMode", 0);
  return f(a, b);
}
bool SDL_GetTextureScaleMode(SDL_Texture *a, SDL_ScaleMode *b) {
  static bool (*f)(SDL_Texture *, SDL_ScaleMode *);
  SDL3_R(f, "SDL_GetTextureScaleMode", 0);
  return f(a, b);
}
bool SDL_SetTextureColorMod(SDL_Texture *a, Uint8 b, Uint8 c, Uint8 d) {
  static bool (*f)(SDL_Texture *, Uint8, Uint8, Uint8);
  SDL3_R(f, "SDL_SetTextureColorMod", 0);
  return f(a, b, c, d);
}
bool SDL_GetTextureColorMod(SDL_Texture *a, Uint8 *b, Uint8 *c, Uint8 *d) {
  static bool (*f)(SDL_Texture *, Uint8 *, Uint8 *, Uint8 *);
  SDL3_R(f, "SDL_GetTextureColorMod", 0);
  return f(a, b, c, d);
}
bool SDL_SetTextureAlphaMod(SDL_Texture *a, Uint8 b) {
  static bool (*f)(SDL_Texture *, Uint8);
  SDL3_R(f, "SDL_SetTextureAlphaMod", 0);
  return f(a, b);
}
bool SDL_GetTextureAlphaMod(SDL_Texture *a, Uint8 *b) {
  static bool (*f)(SDL_Texture *, Uint8 *);
  SDL3_R(f, "SDL_GetTextureAlphaMod", 0);
  return f(a, b);
}

SDL_Surface *SDL_CreateSurface(int a, int b, SDL_PixelFormat c) {
  static SDL_Surface *(*f)(int, int, SDL_PixelFormat);
  SDL3_R(f, "SDL_CreateSurface", 0);
  return f(a, b, c);
}
SDL_Surface *SDL_CreateSurfaceFrom(int a, int b, SDL_PixelFormat c, void *d,
                                   int e) {
  static SDL_Surface *(*f)(int, int, SDL_PixelFormat, void *, int);
  SDL3_R(f, "SDL_CreateSurfaceFrom", 0);
  return f(a, b, c, d, e);
}
void SDL_DestroySurface(SDL_Surface *a) {
  static void (*f)(SDL_Surface *);
  SDL3_R(f, "SDL_DestroySurface", );
  f(a);
}
SDL_Surface *SDL_LoadBMP(const char *a) {
  static SDL_Surface *(*f)(const char *);
  SDL3_R(f, "SDL_LoadBMP", 0);
  return f(a);
}
SDL_Surface *SDL_LoadBMP_IO(SDL_IOStream *a, bool b) {
  static SDL_Surface *(*f)(SDL_IOStream *, bool);
  SDL3_R(f, "SDL_LoadBMP_IO", 0);
  return f(a, b);
}
bool SDL_SaveBMP(SDL_Surface *a, const char *b) {
  static bool (*f)(SDL_Surface *, const char *);
  SDL3_R(f, "SDL_SaveBMP", 0);
  return f(a, b);
}
bool SDL_SaveBMP_IO(SDL_Surface *a, SDL_IOStream *b, bool c) {
  static bool (*f)(SDL_Surface *, SDL_IOStream *, bool);
  SDL3_R(f, "SDL_SaveBMP_IO", 0);
  return f(a, b, c);
}
bool SDL_LockSurface(SDL_Surface *a) {
  static bool (*f)(SDL_Surface *);
  SDL3_R(f, "SDL_LockSurface", 0);
  return f(a);
}
void SDL_UnlockSurface(SDL_Surface *a) {
  static void (*f)(SDL_Surface *);
  SDL3_R(f, "SDL_UnlockSurface", );
  f(a);
}
SDL_Surface *SDL_ConvertSurface(SDL_Surface *a, SDL_PixelFormat b) {
  static SDL_Surface *(*f)(SDL_Surface *, SDL_PixelFormat);
  SDL3_R(f, "SDL_ConvertSurface", 0);
  return f(a, b);
}
SDL_Surface *SDL_ConvertSurfaceAndColorspace(SDL_Surface *a, SDL_PixelFormat b,
                                             SDL_Palette *c, SDL_Colorspace d,
                                             SDL_PropertiesID e) {
  static SDL_Surface *(*f)(SDL_Surface *, SDL_PixelFormat, SDL_Palette *,
                           SDL_Colorspace, SDL_PropertiesID);
  SDL3_R(f, "SDL_ConvertSurfaceAndColorspace", 0);
  return f(a, b, c, d, e);
}
bool SDL_FlipSurface(SDL_Surface *a, SDL_FlipMode b) {
  static bool (*f)(SDL_Surface *, SDL_FlipMode);
  SDL3_R(f, "SDL_FlipSurface", 0);
  return f(a, b);
}
bool SDL_SetSurfacePalette(SDL_Surface *a, SDL_Palette *b) {
  static bool (*f)(SDL_Surface *, SDL_Palette *);
  SDL3_R(f, "SDL_SetSurfacePalette", 0);
  return f(a, b);
}
bool SDL_SetSurfaceColorMod(SDL_Surface *a, Uint8 b, Uint8 c, Uint8 d) {
  static bool (*f)(SDL_Surface *, Uint8, Uint8, Uint8);
  SDL3_R(f, "SDL_SetSurfaceColorMod", 0);
  return f(a, b, c, d);
}
bool SDL_GetSurfaceColorMod(SDL_Surface *a, Uint8 *b, Uint8 *c, Uint8 *d) {
  static bool (*f)(SDL_Surface *, Uint8 *, Uint8 *, Uint8 *);
  SDL3_R(f, "SDL_GetSurfaceColorMod", 0);
  return f(a, b, c, d);
}
bool SDL_SetSurfaceAlphaMod(SDL_Surface *a, Uint8 b) {
  static bool (*f)(SDL_Surface *, Uint8);
  SDL3_R(f, "SDL_SetSurfaceAlphaMod", 0);
  return f(a, b);
}
bool SDL_GetSurfaceAlphaMod(SDL_Surface *a, Uint8 *b) {
  static bool (*f)(SDL_Surface *, Uint8 *);
  SDL3_R(f, "SDL_GetSurfaceAlphaMod", 0);
  return f(a, b);
}
bool SDL_SetSurfaceBlendMode(SDL_Surface *a, SDL_BlendMode b) {
  static bool (*f)(SDL_Surface *, SDL_BlendMode);
  SDL3_R(f, "SDL_SetSurfaceBlendMode", 0);
  return f(a, b);
}
bool SDL_GetSurfaceBlendMode(SDL_Surface *a, SDL_BlendMode *b) {
  static bool (*f)(SDL_Surface *, SDL_BlendMode *);
  SDL3_R(f, "SDL_GetSurfaceBlendMode", 0);
  return f(a, b);
}
bool SDL_SetSurfaceClipRect(SDL_Surface *a, const SDL_Rect *b) {
  static bool (*f)(SDL_Surface *, const SDL_Rect *);
  SDL3_R(f, "SDL_SetSurfaceClipRect", 0);
  return f(a, b);
}
bool SDL_GetSurfaceClipRect(SDL_Surface *a, SDL_Rect *b) {
  static bool (*f)(SDL_Surface *, SDL_Rect *);
  SDL3_R(f, "SDL_GetSurfaceClipRect", 0);
  return f(a, b);
}
bool SDL_BlitSurface(SDL_Surface *a, const SDL_Rect *b, SDL_Surface *c,
                     const SDL_Rect *d) {
  static bool (*f)(SDL_Surface *, const SDL_Rect *, SDL_Surface *,
                   const SDL_Rect *);
  SDL3_R(f, "SDL_BlitSurface", 0);
  return f(a, b, c, d);
}
bool SDL_BlitSurfaceScaled(SDL_Surface *a, const SDL_Rect *b, SDL_Surface *c,
                           const SDL_Rect *d, SDL_ScaleMode e) {
  static bool (*f)(SDL_Surface *, const SDL_Rect *, SDL_Surface *,
                   const SDL_Rect *, SDL_ScaleMode);
  SDL3_R(f, "SDL_BlitSurfaceScaled", 0);
  return f(a, b, c, d, e);
}
bool SDL_FillSurfaceRect(SDL_Surface *a, const SDL_Rect *b, Uint32 c) {
  static bool (*f)(SDL_Surface *, const SDL_Rect *, Uint32);
  SDL3_R(f, "SDL_FillSurfaceRect", 0);
  return f(a, b, c);
}
bool SDL_FillSurfaceRects(SDL_Surface *a, const SDL_Rect *b, int c, Uint32 d) {
  static bool (*f)(SDL_Surface *, const SDL_Rect *, int, Uint32);
  SDL3_R(f, "SDL_FillSurfaceRects", 0);
  return f(a, b, c, d);
}

bool SDL_PollEvent(SDL_Event *a) {
  static bool (*f)(SDL_Event *);
  SDL3_R(f, "SDL_PollEvent", 0);
  return f(a);
}
bool SDL_WaitEvent(SDL_Event *a) {
  static bool (*f)(SDL_Event *);
  SDL3_R(f, "SDL_WaitEvent", 0);
  return f(a);
}
bool SDL_WaitEventTimeout(SDL_Event *a, Sint32 b) {
  static bool (*f)(SDL_Event *, Sint32);
  SDL3_R(f, "SDL_WaitEventTimeout", 0);
  return f(a, b);
}
bool SDL_PushEvent(SDL_Event *a) {
  static bool (*f)(SDL_Event *);
  SDL3_R(f, "SDL_PushEvent", 0);
  return f(a);
}
void SDL_SetEventFilter(SDL_EventFilter a, void *b) {
  static void (*f)(SDL_EventFilter, void *);
  SDL3_R(f, "SDL_SetEventFilter", );
  f(a, b);
}
bool SDL_GetEventFilter(SDL_EventFilter *a, void **b) {
  static bool (*f)(SDL_EventFilter *, void **);
  SDL3_R(f, "SDL_GetEventFilter", 0);
  return f(a, b);
}
bool SDL_AddEventWatch(SDL_EventFilter a, void *b) {
  static bool (*f)(SDL_EventFilter, void *);
  SDL3_R(f, "SDL_AddEventWatch", 0);
  return f(a, b);
}
void SDL_RemoveEventWatch(SDL_EventFilter a, void *b) {
  static void (*f)(SDL_EventFilter, void *);
  SDL3_R(f, "SDL_RemoveEventWatch", );
  f(a, b);
}
void SDL_FlushEvent(Uint32 a) {
  static void (*f)(Uint32);
  SDL3_R(f, "SDL_FlushEvent", );
  f(a);
}
void SDL_FlushEvents(Uint32 a, Uint32 b) {
  static void (*f)(Uint32, Uint32);
  SDL3_R(f, "SDL_FlushEvents", );
  f(a, b);
}
int SDL_PeepEvents(SDL_Event *a, int b, SDL_EventAction c, Uint32 d,
                   Uint32 e) {
  static int (*f)(SDL_Event *, int, SDL_EventAction, Uint32, Uint32);
  SDL3_R(f, "SDL_PeepEvents", 0);
  return f(a, b, c, d, e);
}
bool SDL_HasEvent(Uint32 a) {
  static bool (*f)(Uint32);
  SDL3_R(f, "SDL_HasEvent", 0);
  return f(a);
}
bool SDL_HasEvents(Uint32 a, Uint32 b) {
  static bool (*f)(Uint32, Uint32);
  SDL3_R(f, "SDL_HasEvents", 0);
  return f(a, b);
}
bool SDL_EventEnabled(Uint32 a) {
  static bool (*f)(Uint32);
  SDL3_R(f, "SDL_EventEnabled", 0);
  return f(a);
}
void SDL_SetEventEnabled(Uint32 a, bool b) {
  static void (*f)(Uint32, bool);
  SDL3_R(f, "SDL_SetEventEnabled", );
  f(a, b);
}
Uint32 SDL_RegisterEvents(int a) {
  static Uint32 (*f)(int);
  SDL3_R(f, "SDL_RegisterEvents", 0);
  return f(a);
}

const bool *SDL_GetKeyboardState(int *a) {
  static const bool *(*f)(int *);
  SDL3_R(f, "SDL_GetKeyboardState", 0);
  return f(a);
}
SDL_Keymod SDL_GetModState(void) {
  static SDL_Keymod (*f)(void);
  SDL3_R(f, "SDL_GetModState", 0);
  return f();
}
void SDL_SetModState(SDL_Keymod a) {
  static void (*f)(SDL_Keymod);
  SDL3_R(f, "SDL_SetModState", );
  f(a);
}
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode a, SDL_Keymod b, bool c) {
  static SDL_Keycode (*f)(SDL_Scancode, SDL_Keymod, bool);
  SDL3_R(f, "SDL_GetKeyFromScancode", 0);
  return f(a, b, c);
}
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode a, SDL_Keymod *b) {
  static SDL_Scancode (*f)(SDL_Keycode, SDL_Keymod *);
  SDL3_R(f, "SDL_GetScancodeFromKey", 0);
  return f(a, b);
}
const char *SDL_GetScancodeName(SDL_Scancode a) {
  static const char *(*f)(SDL_Scancode);
  SDL3_R(f, "SDL_GetScancodeName", "");
  return f(a);
}
SDL_Scancode SDL_GetScancodeFromName(const char *a) {
  static SDL_Scancode (*f)(const char *);
  SDL3_R(f, "SDL_GetScancodeFromName", 0);
  return f(a);
}
const char *SDL_GetKeyName(SDL_Keycode a) {
  static const char *(*f)(SDL_Keycode);
  SDL3_R(f, "SDL_GetKeyName", "");
  return f(a);
}
SDL_Keycode SDL_GetKeyFromName(const char *a) {
  static SDL_Keycode (*f)(const char *);
  SDL3_R(f, "SDL_GetKeyFromName", 0);
  return f(a);
}
bool SDL_StartTextInput(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_StartTextInput", 0);
  return f(a);
}
bool SDL_StartTextInputWithProperties(SDL_Window *a, SDL_PropertiesID b) {
  static bool (*f)(SDL_Window *, SDL_PropertiesID);
  SDL3_R(f, "SDL_StartTextInputWithProperties", 0);
  return f(a, b);
}
bool SDL_TextInputActive(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_TextInputActive", 0);
  return f(a);
}
bool SDL_StopTextInput(SDL_Window *a) {
  static bool (*f)(SDL_Window *);
  SDL3_R(f, "SDL_StopTextInput", 0);
  return f(a);
}
bool SDL_SetTextInputArea(SDL_Window *a, const SDL_Rect *b, int c) {
  static bool (*f)(SDL_Window *, const SDL_Rect *, int);
  SDL3_R(f, "SDL_SetTextInputArea", 0);
  return f(a, b, c);
}
bool SDL_GetTextInputArea(SDL_Window *a, SDL_Rect *b, int *c) {
  static bool (*f)(SDL_Window *, SDL_Rect *, int *);
  SDL3_R(f, "SDL_GetTextInputArea", 0);
  return f(a, b, c);
}
bool SDL_TextInputEnabled(void) {
  static bool (*f)(void);
  SDL3_R(f, "SDL_TextInputEnabled", 0);
  return f();
}
bool SDL_SetTextInputEnabled(bool a) {
  static bool (*f)(bool);
  SDL3_R(f, "SDL_SetTextInputEnabled", 0);
  return f(a);
}

SDL_MouseButtonFlags SDL_GetMouseState(float *a, float *b) {
  static SDL_MouseButtonFlags (*f)(float *, float *);
  SDL3_R(f, "SDL_GetMouseState", 0);
  return f(a, b);
}
SDL_MouseButtonFlags SDL_GetGlobalMouseState(float *a, float *b) {
  static SDL_MouseButtonFlags (*f)(float *, float *);
  SDL3_R(f, "SDL_GetGlobalMouseState", 0);
  return f(a, b);
}
SDL_MouseButtonFlags SDL_GetRelativeMouseState(float *a, float *b) {
  static SDL_MouseButtonFlags (*f)(float *, float *);
  SDL3_R(f, "SDL_GetRelativeMouseState", 0);
  return f(a, b);
}
void SDL_WarpMouseInWindow(SDL_Window *a, float b, float c) {
  static void (*f)(SDL_Window *, float, float);
  SDL3_R(f, "SDL_WarpMouseInWindow", );
  f(a, b, c);
}
bool SDL_WarpMouseGlobal(float a, float b) {
  static bool (*f)(float, float);
  SDL3_R(f, "SDL_WarpMouseGlobal", 0);
  return f(a, b);
}
bool SDL_SetRelativeMouseMode(bool a) {
  static bool (*f)(bool);
  SDL3_R(f, "SDL_SetRelativeMouseMode", 0);
  return f(a);
}
bool SDL_GetRelativeMouseMode(void) {
  static bool (*f)(void);
  SDL3_R(f, "SDL_GetRelativeMouseMode", 0);
  return f();
}
SDL_Cursor *SDL_CreateCursor(const Uint8 *a, const Uint8 *b, int c, int d,
                             int e, int hot_y) {
  static SDL_Cursor *(*f)(const Uint8 *, const Uint8 *, int, int, int, int);
  SDL3_R(f, "SDL_CreateCursor", 0);
  return f(a, b, c, d, e, hot_y);
}
SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *a, int b, int c) {
  static SDL_Cursor *(*f)(SDL_Surface *, int, int);
  SDL3_R(f, "SDL_CreateColorCursor", 0);
  return f(a, b, c);
}
SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor a) {
  static SDL_Cursor *(*f)(SDL_SystemCursor);
  SDL3_R(f, "SDL_CreateSystemCursor", 0);
  return f(a);
}
bool SDL_SetCursor(SDL_Cursor *a) {
  static bool (*f)(SDL_Cursor *);
  SDL3_R(f, "SDL_SetCursor", 0);
  return f(a);
}
SDL_Cursor *SDL_GetCursor(void) {
  static SDL_Cursor *(*f)(void);
  SDL3_R(f, "SDL_GetCursor", 0);
  return f();
}
void SDL_DestroyCursor(SDL_Cursor *a) {
  static void (*f)(SDL_Cursor *);
  SDL3_R(f, "SDL_DestroyCursor", );
  f(a);
}
bool SDL_ShowCursor(void) {
  static bool (*f)(void);
  SDL3_R(f, "SDL_ShowCursor", 0);
  return f();
}
bool SDL_HideCursor(void) {
  static bool (*f)(void);
  SDL3_R(f, "SDL_HideCursor", 0);
  return f();
}
bool SDL_CursorVisible(void) {
  static bool (*f)(void);
  SDL3_R(f, "SDL_CursorVisible", 0);
  return f();
}

Uint64 SDL_GetTicks(void) {
  static Uint64 (*f)(void);
  SDL3_R(f, "SDL_GetTicks", 0);
  return f();
}
Uint64 SDL_GetTicksNS(void) {
  static Uint64 (*f)(void);
  SDL3_R(f, "SDL_GetTicksNS", 0);
  return f();
}
Uint64 SDL_GetPerformanceCounter(void) {
  static Uint64 (*f)(void);
  SDL3_R(f, "SDL_GetPerformanceCounter", 0);
  return f();
}
Uint64 SDL_GetPerformanceFrequency(void) {
  static Uint64 (*f)(void);
  SDL3_R(f, "SDL_GetPerformanceFrequency", 0);
  return f();
}
void SDL_Delay(Uint32 a) {
  static void (*f)(Uint32);
  SDL3_R(f, "SDL_Delay", );
  f(a);
}
void SDL_DelayNS(Uint64 a) {
  static void (*f)(Uint64);
  SDL3_R(f, "SDL_DelayNS", );
  f(a);
}
void SDL_DelayPrecise(Uint64 a) {
  static void (*f)(Uint64);
  SDL3_R(f, "SDL_DelayPrecise", );
  f(a);
}
SDL_TimerID SDL_AddTimer(Uint32 a, SDL_TimerCallback b, void *c) {
  static SDL_TimerID (*f)(Uint32, SDL_TimerCallback, void *);
  SDL3_R(f, "SDL_AddTimer", 0);
  return f(a, b, c);
}
SDL_TimerID SDL_AddTimerNS(Uint64 a, SDL_NSTimerCallback b, void *c) {
  static SDL_TimerID (*f)(Uint64, SDL_NSTimerCallback, void *);
  SDL3_R(f, "SDL_AddTimerNS", 0);
  return f(a, b, c);
}
bool SDL_RemoveTimer(SDL_TimerID a) {
  static bool (*f)(SDL_TimerID);
  SDL3_R(f, "SDL_RemoveTimer", 0);
  return f(a);
}

SDL_AudioDeviceID SDL_OpenAudioDevice(SDL_AudioDeviceID a,
                                      const SDL_AudioSpec *b) {
  static SDL_AudioDeviceID (*f)(SDL_AudioDeviceID, const SDL_AudioSpec *);
  SDL3_R(f, "SDL_OpenAudioDevice", 0);
  return f(a, b);
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID a) {
  static void (*f)(SDL_AudioDeviceID);
  SDL3_R(f, "SDL_CloseAudioDevice", );
  f(a);
}
bool SDL_PauseAudioDevice(SDL_AudioDeviceID a) {
  static bool (*f)(SDL_AudioDeviceID);
  SDL3_R(f, "SDL_PauseAudioDevice", 0);
  return f(a);
}
bool SDL_ResumeAudioDevice(SDL_AudioDeviceID a) {
  static bool (*f)(SDL_AudioDeviceID);
  SDL3_R(f, "SDL_ResumeAudioDevice", 0);
  return f(a);
}
bool SDL_AudioDevicePaused(SDL_AudioDeviceID a) {
  static bool (*f)(SDL_AudioDeviceID);
  SDL3_R(f, "SDL_AudioDevicePaused", 0);
  return f(a);
}
float SDL_GetAudioDeviceGain(SDL_AudioDeviceID a) {
  static float (*f)(SDL_AudioDeviceID);
  SDL3_R(f, "SDL_GetAudioDeviceGain", 0);
  return f(a);
}
bool SDL_SetAudioDeviceGain(SDL_AudioDeviceID a, float b) {
  static bool (*f)(SDL_AudioDeviceID, float);
  SDL3_R(f, "SDL_SetAudioDeviceGain", 0);
  return f(a, b);
}
void SDL_CloseAudio(void) {
  static void (*f)(void);
  SDL3_R(f, "SDL_CloseAudio", );
  f();
}
SDL_AudioDeviceID *SDL_GetAudioDevices(int *a) {
  static SDL_AudioDeviceID *(*f)(int *);
  SDL3_R(f, "SDL_GetAudioDevices", 0);
  return f(a);
}

/* SDL3 上游把 SDL_CreateThread 定义为宏，真实导出为 SDL_CreateThreadRuntime */
#ifdef SDL_CreateThread
#undef SDL_CreateThread
#endif
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction a, const char *b, void *c) {
  static SDL_Thread *(*f)(SDL_ThreadFunction, const char *, void *,
                          SDL_FunctionPointer, SDL_FunctionPointer);
  if (!f) {
    if (!sdl3_ensure_lib()) return 0;
    *(void **)(&f) = sdl3_get_sym("SDL_CreateThreadRuntime");
    if (!f) return 0;
  }
  return f(a, b, c, NULL, NULL);
}
void SDL_WaitThread(SDL_Thread *a, int *b) {
  static void (*f)(SDL_Thread *, int *);
  SDL3_R(f, "SDL_WaitThread", );
  f(a, b);
}
void SDL_DetachThread(SDL_Thread *a) {
  static void (*f)(SDL_Thread *);
  SDL3_R(f, "SDL_DetachThread", );
  f(a);
}
SDL_ThreadID SDL_GetCurrentThreadID(void) {
  static SDL_ThreadID (*f)(void);
  SDL3_R(f, "SDL_GetCurrentThreadID", 0);
  return f();
}
SDL_Mutex *SDL_CreateMutex(void) {
  static SDL_Mutex *(*f)(void);
  SDL3_R(f, "SDL_CreateMutex", 0);
  return f();
}
void SDL_LockMutex(SDL_Mutex *a) {
  static void (*f)(SDL_Mutex *);
  SDL3_R(f, "SDL_LockMutex", );
  f(a);
}
bool SDL_TryLockMutex(SDL_Mutex *a) {
  static bool (*f)(SDL_Mutex *);
  SDL3_R(f, "SDL_TryLockMutex", 0);
  return f(a);
}
void SDL_UnlockMutex(SDL_Mutex *a) {
  static void (*f)(SDL_Mutex *);
  SDL3_R(f, "SDL_UnlockMutex", );
  f(a);
}
void SDL_DestroyMutex(SDL_Mutex *a) {
  static void (*f)(SDL_Mutex *);
  SDL3_R(f, "SDL_DestroyMutex", );
  f(a);
}
SDL_RWLock *SDL_CreateRWLock(void) {
  static SDL_RWLock *(*f)(void);
  SDL3_R(f, "SDL_CreateRWLock", 0);
  return f();
}
void SDL_LockRWLockForReading(SDL_RWLock *a) {
  static void (*f)(SDL_RWLock *);
  SDL3_R(f, "SDL_LockRWLockForReading", );
  f(a);
}
void SDL_LockRWLockForWriting(SDL_RWLock *a) {
  static void (*f)(SDL_RWLock *);
  SDL3_R(f, "SDL_LockRWLockForWriting", );
  f(a);
}
void SDL_UnlockRWLock(SDL_RWLock *a) {
  static void (*f)(SDL_RWLock *);
  SDL3_R(f, "SDL_UnlockRWLock", );
  f(a);
}
void SDL_DestroyRWLock(SDL_RWLock *a) {
  static void (*f)(SDL_RWLock *);
  SDL3_R(f, "SDL_DestroyRWLock", );
  f(a);
}
SDL_Semaphore *SDL_CreateSemaphore(Uint32 a) {
  static SDL_Semaphore *(*f)(Uint32);
  SDL3_R(f, "SDL_CreateSemaphore", 0);
  return f(a);
}
void SDL_DestroySemaphore(SDL_Semaphore *a) {
  static void (*f)(SDL_Semaphore *);
  SDL3_R(f, "SDL_DestroySemaphore", );
  f(a);
}
void SDL_WaitSemaphore(SDL_Semaphore *a) {
  static void (*f)(SDL_Semaphore *);
  SDL3_R(f, "SDL_WaitSemaphore", );
  f(a);
}
bool SDL_TryWaitSemaphore(SDL_Semaphore *a) {
  static bool (*f)(SDL_Semaphore *);
  SDL3_R(f, "SDL_TryWaitSemaphore", 0);
  return f(a);
}
bool SDL_WaitSemaphoreTimeout(SDL_Semaphore *a, Sint32 b) {
  static bool (*f)(SDL_Semaphore *, Sint32);
  SDL3_R(f, "SDL_WaitSemaphoreTimeout", 0);
  return f(a, b);
}
void SDL_SignalSemaphore(SDL_Semaphore *a) {
  static void (*f)(SDL_Semaphore *);
  SDL3_R(f, "SDL_SignalSemaphore", );
  f(a);
}
SDL_Condition *SDL_CreateCondition(void) {
  static SDL_Condition *(*f)(void);
  SDL3_R(f, "SDL_CreateCondition", 0);
  return f();
}
void SDL_DestroyCondition(SDL_Condition *a) {
  static void (*f)(SDL_Condition *);
  SDL3_R(f, "SDL_DestroyCondition", );
  f(a);
}
void SDL_SignalCondition(SDL_Condition *a) {
  static void (*f)(SDL_Condition *);
  SDL3_R(f, "SDL_SignalCondition", );
  f(a);
}
void SDL_BroadcastCondition(SDL_Condition *a) {
  static void (*f)(SDL_Condition *);
  SDL3_R(f, "SDL_BroadcastCondition", );
  f(a);
}

const char *SDL_GetBasePath(void) {
  static const char *(*f)(void);
  SDL3_R(f, "SDL_GetBasePath", 0);
  return f();
}
char *SDL_GetPrefPath(const char *a, const char *b) {
  static char *(*f)(const char *, const char *);
  SDL3_R(f, "SDL_GetPrefPath", 0);
  return f(a, b);
}
const char *SDL_GetUserFolder(SDL_Folder a) {
  static const char *(*f)(SDL_Folder);
  SDL3_R(f, "SDL_GetUserFolder", 0);
  return f(a);
}
SDL_IOStream *SDL_IOFromFile(const char *a, const char *b) {
  static SDL_IOStream *(*f)(const char *, const char *);
  SDL3_R(f, "SDL_IOFromFile", 0);
  return f(a, b);
}
SDL_IOStream *SDL_IOFromMem(void *a, size_t b) {
  static SDL_IOStream *(*f)(void *, size_t);
  SDL3_R(f, "SDL_IOFromMem", 0);
  return f(a, b);
}
SDL_IOStream *SDL_IOFromConstMem(const void *a, size_t b) {
  static SDL_IOStream *(*f)(const void *, size_t);
  SDL3_R(f, "SDL_IOFromConstMem", 0);
  return f(a, b);
}
SDL_IOStream *SDL_IOFromDynamicMem(void) {
  static SDL_IOStream *(*f)(void);
  SDL3_R(f, "SDL_IOFromDynamicMem", 0);
  return f();
}
bool SDL_CloseIO(SDL_IOStream *a) {
  static bool (*f)(SDL_IOStream *);
  SDL3_R(f, "SDL_CloseIO", 0);
  return f(a);
}
SDL_PropertiesID SDL_GetIOProperties(SDL_IOStream *a) {
  static SDL_PropertiesID (*f)(SDL_IOStream *);
  SDL3_R(f, "SDL_GetIOProperties", 0);
  return f(a);
}
SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *a) {
  static SDL_IOStatus (*f)(SDL_IOStream *);
  SDL3_R(f, "SDL_GetIOStatus", 0);
  return f(a);
}
Sint64 SDL_GetIOSize(SDL_IOStream *a) {
  static Sint64 (*f)(SDL_IOStream *);
  SDL3_R(f, "SDL_GetIOSize", 0);
  return f(a);
}
Sint64 SDL_SeekIO(SDL_IOStream *a, Sint64 b, SDL_IOWhence c) {
  static Sint64 (*f)(SDL_IOStream *, Sint64, SDL_IOWhence);
  SDL3_R(f, "SDL_SeekIO", 0);
  return f(a, b, c);
}
Sint64 SDL_TellIO(SDL_IOStream *a) {
  static Sint64 (*f)(SDL_IOStream *);
  SDL3_R(f, "SDL_TellIO", 0);
  return f(a);
}
size_t SDL_ReadIO(SDL_IOStream *a, void *b, size_t c) {
  static size_t (*f)(SDL_IOStream *, void *, size_t);
  SDL3_R(f, "SDL_ReadIO", 0);
  return f(a, b, c);
}
size_t SDL_WriteIO(SDL_IOStream *a, const void *b, size_t c) {
  static size_t (*f)(SDL_IOStream *, const void *, size_t);
  SDL3_R(f, "SDL_WriteIO", 0);
  return f(a, b, c);
}
bool SDL_FlushIO(SDL_IOStream *a) {
  static bool (*f)(SDL_IOStream *);
  SDL3_R(f, "SDL_FlushIO", 0);
  return f(a);
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID a) {
  static SDL_Gamepad *(*f)(SDL_JoystickID);
  SDL3_R(f, "SDL_OpenGamepad", 0);
  return f(a);
}
void SDL_CloseGamepad(SDL_Gamepad *a) {
  static void (*f)(SDL_Gamepad *);
  SDL3_R(f, "SDL_CloseGamepad", );
  f(a);
}
const char *SDL_GetGamepadName(SDL_Gamepad *a) {
  static const char *(*f)(SDL_Gamepad *);
  SDL3_R(f, "SDL_GetGamepadName", "");
  return f(a);
}
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *a) {
  static SDL_GamepadType (*f)(SDL_Gamepad *);
  SDL3_R(f, "SDL_GetGamepadType", 0);
  return f(a);
}
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad *a) {
  static SDL_JoystickID (*f)(SDL_Gamepad *);
  SDL3_R(f, "SDL_GetGamepadID", 0);
  return f(a);
}
bool SDL_GetGamepadButton(SDL_Gamepad *a, SDL_GamepadButton b) {
  static bool (*f)(SDL_Gamepad *, SDL_GamepadButton);
  SDL3_R(f, "SDL_GetGamepadButton", 0);
  return f(a, b);
}
Sint16 SDL_GetGamepadAxis(SDL_Gamepad *a, SDL_GamepadAxis b) {
  static Sint16 (*f)(SDL_Gamepad *, SDL_GamepadAxis);
  SDL3_R(f, "SDL_GetGamepadAxis", 0);
  return f(a, b);
}
bool SDL_GamepadConnected(SDL_Gamepad *a) {
  static bool (*f)(SDL_Gamepad *);
  SDL3_R(f, "SDL_GamepadConnected", 0);
  return f(a);
}
bool SDL_SetGamepadPlayerIndex(SDL_Gamepad *a, int b) {
  static bool (*f)(SDL_Gamepad *, int);
  SDL3_R(f, "SDL_SetGamepadPlayerIndex", 0);
  return f(a, b);
}
int SDL_GetGamepadPlayerIndex(SDL_Gamepad *a) {
  static int (*f)(SDL_Gamepad *);
  SDL3_R(f, "SDL_GetGamepadPlayerIndex", 0);
  return f(a);
}
bool SDL_RumbleGamepad(SDL_Gamepad *a, Uint16 b, Uint16 c, Uint32 d) {
  static bool (*f)(SDL_Gamepad *, Uint16, Uint16, Uint32);
  SDL3_R(f, "SDL_RumbleGamepad", 0);
  return f(a, b, c, d);
}
bool SDL_SetGamepadLED(SDL_Gamepad *a, Uint8 b, Uint8 c, Uint8 d) {
  static bool (*f)(SDL_Gamepad *, Uint8, Uint8, Uint8);
  SDL3_R(f, "SDL_SetGamepadLED", 0);
  return f(a, b, c, d);
}

SDL_PropertiesID SDL_CreateProperties(void) {
  static SDL_PropertiesID (*f)(void);
  SDL3_R(f, "SDL_CreateProperties", 0);
  return f();
}
void SDL_DestroyProperties(SDL_PropertiesID a) {
  static void (*f)(SDL_PropertiesID);
  SDL3_R(f, "SDL_DestroyProperties", );
  f(a);
}
bool SDL_SetProperty(SDL_PropertiesID a, const char *b, void *c) {
  static bool (*f)(SDL_PropertiesID, const char *, void *);
  SDL3_R(f, "SDL_SetProperty", 0);
  return f(a, b, c);
}
void *SDL_GetProperty(SDL_PropertiesID a, const char *b, void *c) {
  static void *(*f)(SDL_PropertiesID, const char *, void *);
  SDL3_R(f, "SDL_GetProperty", 0);
  return f(a, b, c);
}
bool SDL_SetNumberProperty(SDL_PropertiesID a, const char *b, Sint64 c) {
  static bool (*f)(SDL_PropertiesID, const char *, Sint64);
  SDL3_R(f, "SDL_SetNumberProperty", 0);
  return f(a, b, c);
}
Sint64 SDL_GetNumberProperty(SDL_PropertiesID a, const char *b, Sint64 c) {
  static Sint64 (*f)(SDL_PropertiesID, const char *, Sint64);
  SDL3_R(f, "SDL_GetNumberProperty", 0);
  return f(a, b, c);
}
bool SDL_SetStringProperty(SDL_PropertiesID a, const char *b, const char *c) {
  static bool (*f)(SDL_PropertiesID, const char *, const char *);
  SDL3_R(f, "SDL_SetStringProperty", 0);
  return f(a, b, c);
}
const char *SDL_GetStringProperty(SDL_PropertiesID a, const char *b,
                                  const char *c) {
  static const char *(*f)(SDL_PropertiesID, const char *, const char *);
  SDL3_R(f, "SDL_GetStringProperty", c);
  return f(a, b, c);
}
bool SDL_SetFloatProperty(SDL_PropertiesID a, const char *b, float c) {
  static bool (*f)(SDL_PropertiesID, const char *, float);
  SDL3_R(f, "SDL_SetFloatProperty", 0);
  return f(a, b, c);
}
float SDL_GetFloatProperty(SDL_PropertiesID a, const char *b, float c) {
  static float (*f)(SDL_PropertiesID, const char *, float);
  SDL3_R(f, "SDL_GetFloatProperty", c);
  return f(a, b, c);
}
bool SDL_SetBooleanProperty(SDL_PropertiesID a, const char *b, bool c) {
  static bool (*f)(SDL_PropertiesID, const char *, bool);
  SDL3_R(f, "SDL_SetBooleanProperty", 0);
  return f(a, b, c);
}
bool SDL_GetBooleanProperty(SDL_PropertiesID a, const char *b, bool c) {
  static bool (*f)(SDL_PropertiesID, const char *, bool);
  SDL3_R(f, "SDL_GetBooleanProperty", c);
  return f(a, b, c);
}

bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags a, const char *b,
                              const char *c, SDL_Window *d) {
  static bool (*f)(SDL_MessageBoxFlags, const char *, const char *,
                   SDL_Window *);
  SDL3_R(f, "SDL_ShowSimpleMessageBox", 0);
  return f(a, b, c, d);
}
bool SDL_ShowMessageBox(const SDL_MessageBoxData *a, int *b) {
  static bool (*f)(const SDL_MessageBoxData *, int *);
  SDL3_R(f, "SDL_ShowMessageBox", 0);
  return f(a, b);
}

SDL_GPUDevice *SDL_CreateGPUDevice(SDL_GPUShaderFormat a, bool b,
                                   const char *c) {
  static SDL_GPUDevice *(*f)(SDL_GPUShaderFormat, bool, const char *);
  SDL3_R(f, "SDL_CreateGPUDevice", 0);
  return f(a, b, c);
}
SDL_GPUDevice *SDL_CreateGPUDeviceWithProperties(SDL_PropertiesID a) {
  static SDL_GPUDevice *(*f)(SDL_PropertiesID);
  SDL3_R(f, "SDL_CreateGPUDeviceWithProperties", 0);
  return f(a);
}
void SDL_DestroyGPUDevice(SDL_GPUDevice *a) {
  static void (*f)(SDL_GPUDevice *);
  SDL3_R(f, "SDL_DestroyGPUDevice", );
  f(a);
}

SDL_Camera *SDL_OpenCamera(SDL_CameraID a, const SDL_CameraSpec *b) {
  static SDL_Camera *(*f)(SDL_CameraID, const SDL_CameraSpec *);
  SDL3_R(f, "SDL_OpenCamera", 0);
  return f(a, b);
}
void SDL_CloseCamera(SDL_Camera *a) {
  static void (*f)(SDL_Camera *);
  SDL3_R(f, "SDL_CloseCamera", );
  f(a);
}

SDL_PenID *SDL_GetPens(int *a) {
  static SDL_PenID *(*f)(int *);
  SDL3_R(f, "SDL_GetPens", 0);
  return f(a);
}
bool SDL_PenConnected(SDL_PenID a) {
  static bool (*f)(SDL_PenID);
  SDL3_R(f, "SDL_PenConnected", 0);
  return f(a);
}

void SDL_free(void *a) {
  static void (*f)(void *);
  SDL3_R(f, "SDL_free", );
  f(a);
}
void *SDL_malloc(size_t a) {
  static void *(*f)(size_t);
  SDL3_R(f, "SDL_malloc", 0);
  return f(a);
}
void *SDL_calloc(size_t a, size_t b) {
  static void *(*f)(size_t, size_t);
  SDL3_R(f, "SDL_calloc", 0);
  return f(a, b);
}
void *SDL_realloc(void *a, size_t b) {
  static void *(*f)(void *, size_t);
  SDL3_R(f, "SDL_realloc", 0);
  return f(a, b);
}
char *SDL_strdup(const char *a) {
  static char *(*f)(const char *);
  SDL3_R(f, "SDL_strdup", 0);
  return f(a);
}
char *SDL_strndup(const char *a, size_t b) {
  static char *(*f)(const char *, size_t);
  SDL3_R(f, "SDL_strndup", 0);
  return f(a, b);
}
size_t SDL_strlcpy(char *a, const char *b, size_t c) {
  static size_t (*f)(char *, const char *, size_t);
  SDL3_R(f, "SDL_strlcpy", 0);
  return f(a, b, c);
}
size_t SDL_strlcat(char *a, const char *b, size_t c) {
  static size_t (*f)(char *, const char *, size_t);
  SDL3_R(f, "SDL_strlcat", 0);
  return f(a, b, c);
}
int SDL_strcmp(const char *a, const char *b) {
  static int (*f)(const char *, const char *);
  SDL3_R(f, "SDL_strcmp", 0);
  return f(a, b);
}
int SDL_strncmp(const char *a, const char *b, size_t c) {
  static int (*f)(const char *, const char *, size_t);
  SDL3_R(f, "SDL_strncmp", 0);
  return f(a, b, c);
}
size_t SDL_strlen(const char *a) {
  static size_t (*f)(const char *);
  SDL3_R(f, "SDL_strlen", 0);
  return f(a);
}
char *SDL_strstr(const char *a, const char *b) {
  static char *(*f)(const char *, const char *);
  SDL3_R(f, "SDL_strstr", 0);
  return f(a, b);
}
char *SDL_strcasestr(const char *a, const char *b) {
  static char *(*f)(const char *, const char *);
  SDL3_R(f, "SDL_strcasestr", 0);
  return f(a, b);
}
char *SDL_strtokr(char *a, const char *b, char **c) {
  static char *(*f)(char *, const char *, char **);
  SDL3_R(f, "SDL_strtokr", 0);
  return f(a, b, c);
}
int SDL_atoi(const char *a) {
  static int (*f)(const char *);
  SDL3_R(f, "SDL_atoi", 0);
  return f(a);
}
double SDL_atof(const char *a) {
  static double (*f)(const char *);
  SDL3_R(f, "SDL_atof", 0);
  return f(a);
}
long SDL_strtol(const char *a, char **b, int c) {
  static long (*f)(const char *, char **, int);
  SDL3_R(f, "SDL_strtol", 0);
  return f(a, b, c);
}
unsigned long SDL_strtoul(const char *a, char **b, int c) {
  static unsigned long (*f)(const char *, char **, int);
  SDL3_R(f, "SDL_strtoul", 0);
  return f(a, b, c);
}
long long SDL_strtoll(const char *a, char **b, int c) {
  static long long (*f)(const char *, char **, int);
  SDL3_R(f, "SDL_strtoll", 0);
  return f(a, b, c);
}
unsigned long long SDL_strtoull(const char *a, char **b, int c) {
  static unsigned long long (*f)(const char *, char **, int);
  SDL3_R(f, "SDL_strtoull", 0);
  return f(a, b, c);
}
