#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 加载器 ================= */
static void *H = 0;
static int S = 0;
static void *L(const char *p) { return dlopen(p, RTLD_LAZY | RTLD_LOCAL); }
static void *Y(const char *n) { return H ? dlsym(H, n) : 0; }
static int O(void) {
  if (S)
    return S > 0;
  const char *e = getenv("SDL2_DYN_LIB");
  if (e && *e)
    H = L(e);
  if (!H)
    H = L("libSDL2-2.0.so.0");
  if (!H)
    H = L("libSDL2-2.0.dylib");
  if (!H)
    H = L("SDL2.dll");
  if (!H)
    H = L("libSDL2.dylib");
  if (H && !Y("SDL_Init")) {
    dlclose(H);
    H = 0;
  }
  S = H ? 1 : -1;
  return S > 0;
}
void sdl2_dyn_cleanup(void) {
  if (H) {
    dlclose(H);
    H = 0;
  }
  S = 0;
}
#define R(v, n, f)                                                             \
  if (!(v)) {                                                                  \
    if (!O())                                                                  \
      return f;                                                                \
    (v) = Y(n);                                                                \
    if (!(v))                                                                  \
      return f;                                                                \
  }

/* ================= 1. 初始化与错误 (SDL_init.h / SDL_error.h)
 * ================= */
int SDL_Init(Uint32 a) {
  static int (*f)(Uint32);
  R(f, "SDL_Init", -1);
  return f(a);
}
int SDL_InitSubSystem(Uint32 a) {
  static int (*f)(Uint32);
  R(f, "SDL_InitSubSystem", -1);
  return f(a);
}
void SDL_QuitSubSystem(Uint32 a) {
  static void (*f)(Uint32);
  R(f, "SDL_QuitSubSystem", );
  f(a);
}
void SDL_Quit(void) {
  static void (*f)(void);
  R(f, "SDL_Quit", );
  f();
}
Uint32 SDL_WasInit(Uint32 a) {
  static Uint32 (*f)(Uint32);
  R(f, "SDL_WasInit", 0);
  return f(a);
}
const char *SDL_GetError(void) {
  static const char *(*f)(void);
  R(f, "SDL_GetError", "");
  return f();
}
void SDL_ClearError(void) {
  static void (*f)(void);
  R(f, "SDL_ClearError", );
  f();
}
int SDL_SetError(const char *a, ...) {
  static int (*f)(const char *, ...);
  R(f, "SDL_SetError", -1);
  return f(a);
}
void SDL_GetVersion(SDL_version *a) {
  static void (*f)(SDL_version *);
  R(f, "SDL_GetVersion", );
  f(a);
}
const char *SDL_GetRevision(void) {
  static const char *(*f)(void);
  R(f, "SDL_GetRevision", "");
  return f();
}

/* ================= 2. 窗口 (SDL_video.h) ================= */
SDL_Window *SDL_CreateWindow(const char *a, int b, int c, int d, int e,
                             Uint32 f) {
  static SDL_Window *(*fn)(const char *, int, int, int, int, Uint32);
  R(fn, "SDL_CreateWindow", 0);
  return fn(a, b, c, d, e, f);
}
void SDL_DestroyWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_DestroyWindow", );
  f(a);
}
void SDL_SetWindowTitle(SDL_Window *a, const char *b) {
  static void (*f)(SDL_Window *, const char *);
  R(f, "SDL_SetWindowTitle", );
  f(a, b);
}
const char *SDL_GetWindowTitle(SDL_Window *a) {
  static const char *(*f)(SDL_Window *);
  R(f, "SDL_GetWindowTitle", "");
  return f(a);
}
void SDL_SetWindowPosition(SDL_Window *a, int b, int c) {
  static void (*f)(SDL_Window *, int, int);
  R(f, "SDL_SetWindowPosition", );
  f(a, b, c);
}
void SDL_GetWindowPosition(SDL_Window *a, int *b, int *c) {
  static void (*f)(SDL_Window *, int *, int *);
  R(f, "SDL_GetWindowPosition", );
  f(a, b, c);
}
void SDL_SetWindowSize(SDL_Window *a, int b, int c) {
  static void (*f)(SDL_Window *, int, int);
  R(f, "SDL_SetWindowSize", );
  f(a, b, c);
}
void SDL_GetWindowSize(SDL_Window *a, int *b, int *c) {
  static void (*f)(SDL_Window *, int *, int *);
  R(f, "SDL_GetWindowSize", );
  f(a, b, c);
}
void SDL_ShowWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_ShowWindow", );
  f(a);
}
void SDL_HideWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_HideWindow", );
  f(a);
}
void SDL_RaiseWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_RaiseWindow", );
  f(a);
}
void SDL_MaximizeWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_MaximizeWindow", );
  f(a);
}
void SDL_MinimizeWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_MinimizeWindow", );
  f(a);
}
void SDL_RestoreWindow(SDL_Window *a) {
  static void (*f)(SDL_Window *);
  R(f, "SDL_RestoreWindow", );
  f(a);
}
int SDL_SetWindowFullscreen(SDL_Window *a, Uint32 b) {
  static int (*f)(SDL_Window *, Uint32);
  R(f, "SDL_SetWindowFullscreen", -1);
  return f(a, b);
}
Uint32 SDL_GetWindowFlags(SDL_Window *a) {
  static Uint32 (*f)(SDL_Window *);
  R(f, "SDL_GetWindowFlags", 0);
  return f(a);
}
SDL_Window *SDL_GetWindowFromID(Uint32 a) {
  static SDL_Window *(*f)(Uint32);
  R(f, "SDL_GetWindowFromID", 0);
  return f(a);
}
Uint32 SDL_GetWindowID(SDL_Window *a) {
  static Uint32 (*f)(SDL_Window *);
  R(f, "SDL_GetWindowID", 0);
  return f(a);
}
SDL_Window *SDL_GetWindowParent(SDL_Window *a) {
  static SDL_Window *(*f)(SDL_Window *);
  R(f, "SDL_GetWindowParent", 0);
  return f(a);
}

/* ================= 3. 渲染器 (SDL_render.h) ================= */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *a, int b, Uint32 c) {
  static SDL_Renderer *(*f)(SDL_Window *, int, Uint32);
  R(f, "SDL_CreateRenderer", 0);
  return f(a, b, c);
}
void SDL_DestroyRenderer(SDL_Renderer *a) {
  static void (*f)(SDL_Renderer *);
  R(f, "SDL_DestroyRenderer", );
  f(a);
}
int SDL_RenderClear(SDL_Renderer *a) {
  static int (*f)(SDL_Renderer *);
  R(f, "SDL_RenderClear", -1);
  return f(a);
}
void SDL_RenderPresent(SDL_Renderer *a) {
  static void (*f)(SDL_Renderer *);
  R(f, "SDL_RenderPresent", );
  f(a);
}
int SDL_RenderCopy(SDL_Renderer *a, SDL_Texture *b, const SDL_Rect *c,
                   const SDL_Rect *d) {
  static int (*f)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *,
                  const SDL_Rect *);
  R(f, "SDL_RenderCopy", -1);
  return f(a, b, c, d);
}
int SDL_RenderCopyEx(SDL_Renderer *a, SDL_Texture *b, const SDL_Rect *c,
                     const SDL_Rect *d, double e, const SDL_Point *f,
                     SDL_RendererFlip g) {
  static int (*f)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *,
                  const SDL_Rect *, double, const SDL_Point *,
                  SDL_RendererFlip);
  R(f, "SDL_RenderCopyEx", -1);
  return f(a, b, c, d, e, f, g);
}
int SDL_RenderDrawLine(SDL_Renderer *a, int b, int c, int d, int e) {
  static int (*f)(SDL_Renderer *, int, int, int, int);
  R(f, "SDL_RenderDrawLine", -1);
  return f(a, b, c, d, e);
}
int SDL_RenderDrawPoint(SDL_Renderer *a, int b, int c) {
  static int (*f)(SDL_Renderer *, int, int);
  R(f, "SDL_RenderDrawPoint", -1);
  return f(a, b, c);
}
int SDL_RenderDrawRect(SDL_Renderer *a, const SDL_Rect *b) {
  static int (*f)(SDL_Renderer *, const SDL_Rect *);
  R(f, "SDL_RenderDrawRect", -1);
  return f(a, b);
}
int SDL_RenderFillRect(SDL_Renderer *a, const SDL_Rect *b) {
  static int (*f)(SDL_Renderer *, const SDL_Rect *);
  R(f, "SDL_RenderFillRect", -1);
  return f(a, b);
}
int SDL_SetRenderDrawColor(SDL_Renderer *a, Uint8 b, Uint8 c, Uint8 d,
                           Uint8 e) {
  static int (*f)(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8);
  R(f, "SDL_SetRenderDrawColor", -1);
  return f(a, b, c, d, e);
}
int SDL_GetRenderDrawColor(SDL_Renderer *a, Uint8 *b, Uint8 *c, Uint8 *d,
                           Uint8 *e) {
  static int (*f)(SDL_Renderer *, Uint8 *, Uint8 *, Uint8 *, Uint8 *);
  R(f, "SDL_GetRenderDrawColor", -1);
  return f(a, b, c, d, e);
}
int SDL_SetRenderDrawBlendMode(SDL_Renderer *a, SDL_BlendMode b) {
  static int (*f)(SDL_Renderer *, SDL_BlendMode);
  R(f, "SDL_SetRenderDrawBlendMode", -1);
  return f(a, b);
}
int SDL_GetRenderDrawBlendMode(SDL_Renderer *a, SDL_BlendMode *b) {
  static int (*f)(SDL_Renderer *, SDL_BlendMode *);
  R(f, "SDL_GetRenderDrawBlendMode", -1);
  return f(a, b);
}
int SDL_SetRenderTarget(SDL_Renderer *a, SDL_Texture *b) {
  static int (*f)(SDL_Renderer *, SDL_Texture *);
  R(f, "SDL_SetRenderTarget", -1);
  return f(a, b);
}
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *a) {
  static SDL_Texture *(*f)(SDL_Renderer *);
  R(f, "SDL_GetRenderTarget", 0);
  return f(a);
}
int SDL_RenderSetVSync(SDL_Renderer *a, int b) {
  static int (*f)(SDL_Renderer *, int);
  R(f, "SDL_RenderSetVSync", -1);
  return f(a, b);
}
int SDL_RenderSetClipRect(SDL_Renderer *a, const SDL_Rect *b) {
  static int (*f)(SDL_Renderer *, const SDL_Rect *);
  R(f, "SDL_RenderSetClipRect", -1);
  return f(a, b);
}
void SDL_RenderGetClipRect(SDL_Renderer *a, SDL_Rect *b) {
  static void (*f)(SDL_Renderer *, SDL_Rect *);
  R(f, "SDL_RenderGetClipRect", );
  f(a, b);
}
int SDL_RenderSetScale(SDL_Renderer *a, float b, float c) {
  static int (*f)(SDL_Renderer *, float, float);
  R(f, "SDL_RenderSetScale", -1);
  return f(a, b, c);
}
void SDL_RenderGetScale(SDL_Renderer *a, float *b, float *c) {
  static void (*f)(SDL_Renderer *, float *, float *);
  R(f, "SDL_RenderGetScale", );
  f(a, b, c);
}
int SDL_RenderReadPixels(SDL_Renderer *a, const SDL_Rect *b, Uint32 c, void *d,
                         int e) {
  static int (*f)(SDL_Renderer *, const SDL_Rect *, Uint32, void *, int);
  R(f, "SDL_RenderReadPixels", -1);
  return f(a, b, c, d, e);
}
void SDL_RenderGetLogicalSize(SDL_Renderer *a, int *b, int *c) {
  static void (*f)(SDL_Renderer *, int *, int *);
  R(f, "SDL_RenderGetLogicalSize", );
  f(a, b, c);
}
int SDL_RenderSetLogicalSize(SDL_Renderer *a, int b, int c) {
  static int (*f)(SDL_Renderer *, int, int);
  R(f, "SDL_RenderSetLogicalSize", -1);
  return f(a, b, c);
}

/* ================= 4. 纹理 (SDL_render.h) ================= */
SDL_Texture *SDL_CreateTexture(SDL_Renderer *a, Uint32 b, int c, int d, int e) {
  static SDL_Texture *(*f)(SDL_Renderer *, Uint32, int, int, int);
  R(f, "SDL_CreateTexture", 0);
  return f(a, b, c, d, e);
}
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *a, SDL_Surface *b) {
  static SDL_Texture *(*f)(SDL_Renderer *, SDL_Surface *);
  R(f, "SDL_CreateTextureFromSurface", 0);
  return f(a, b);
}
void SDL_DestroyTexture(SDL_Texture *a) {
  static void (*f)(SDL_Texture *);
  R(f, "SDL_DestroyTexture", );
  f(a);
}
int SDL_UpdateTexture(SDL_Texture *a, const SDL_Rect *b, const void *c, int d) {
  static int (*f)(SDL_Texture *, const SDL_Rect *, const void *, int);
  R(f, "SDL_UpdateTexture", -1);
  return f(a, b, c, d);
}
int SDL_LockTexture(SDL_Texture *a, const SDL_Rect *b, void **c, int *d) {
  static int (*f)(SDL_Texture *, const SDL_Rect *, void **, int *);
  R(f, "SDL_LockTexture", -1);
  return f(a, b, c, d);
}
void SDL_UnlockTexture(SDL_Texture *a) {
  static void (*f)(SDL_Texture *);
  R(f, "SDL_UnlockTexture", );
  f(a);
}
void SDL_QueryTexture(SDL_Texture *a, Uint32 *b, int *c, int *d, int *e) {
  static void (*f)(SDL_Texture *, Uint32 *, int *, int *, int *);
  R(f, "SDL_QueryTexture", );
  f(a, b, c, d, e);
}
int SDL_SetTextureBlendMode(SDL_Texture *a, SDL_BlendMode b) {
  static int (*f)(SDL_Texture *, SDL_BlendMode);
  R(f, "SDL_SetTextureBlendMode", -1);
  return f(a, b);
}
int SDL_GetTextureBlendMode(SDL_Texture *a, SDL_BlendMode *b) {
  static int (*f)(SDL_Texture *, SDL_BlendMode *);
  R(f, "SDL_GetTextureBlendMode", -1);
  return f(a, b);
}
int SDL_SetTextureColorMod(SDL_Texture *a, Uint8 b, Uint8 c, Uint8 d) {
  static int (*f)(SDL_Texture *, Uint8, Uint8, Uint8);
  R(f, "SDL_SetTextureColorMod", -1);
  return f(a, b, c, d);
}
int SDL_GetTextureColorMod(SDL_Texture *a, Uint8 *b, Uint8 *c, Uint8 *d) {
  static int (*f)(SDL_Texture *, Uint8 *, Uint8 *, Uint8 *);
  R(f, "SDL_GetTextureColorMod", -1);
  return f(a, b, c, d);
}
int SDL_SetTextureAlphaMod(SDL_Texture *a, Uint8 b) {
  static int (*f)(SDL_Texture *, Uint8);
  R(f, "SDL_SetTextureAlphaMod", -1);
  return f(a, b);
}
int SDL_GetTextureAlphaMod(SDL_Texture *a, Uint8 *b) {
  static int (*f)(SDL_Texture *, Uint8 *);
  R(f, "SDL_GetTextureAlphaMod", -1);
  return f(a, b);
}

/* ================= 5. 表面 (SDL_surface.h) ================= */
SDL_Surface *SDL_CreateRGBSurface(Uint32 a, int b, int c, int d, Uint32 e,
                                  Uint32 f, Uint32 g, Uint32 h) {
  static SDL_Surface *(*fn)(Uint32, int, int, int, Uint32, Uint32, Uint32,
                            Uint32);
  R(fn, "SDL_CreateRGBSurface", 0);
  return fn(a, b, c, d, e, f, g, h);
}
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 a, int b, int c, int d,
                                            Uint32 e) {
  static SDL_Surface *(*fn)(Uint32, int, int, int, Uint32);
  R(fn, "SDL_CreateRGBSurfaceWithFormat", 0);
  return fn(a, b, c, d, e);
}
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *a, int b, int c, int d, int e,
                                      Uint32 f, Uint32 g, Uint32 h, Uint32 i) {
  static SDL_Surface *(*fn)(void *, int, int, int, int, Uint32, Uint32, Uint32,
                            Uint32);
  R(fn, "SDL_CreateRGBSurfaceFrom", 0);
  return fn(a, b, c, d, e, f, g, h, i);
}
void SDL_FreeSurface(SDL_Surface *a) {
  static void (*f)(SDL_Surface *);
  R(f, "SDL_FreeSurface", );
  f(a);
}
int SDL_LockSurface(SDL_Surface *a) {
  static int (*f)(SDL_Surface *);
  R(f, "SDL_LockSurface", -1);
  return f(a);
}
void SDL_UnlockSurface(SDL_Surface *a) {
  static void (*f)(SDL_Surface *);
  R(f, "SDL_UnlockSurface", );
  f(a);
}
SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *a, int b) {
  static SDL_Surface *(*f)(SDL_RWops *, int);
  R(f, "SDL_LoadBMP_RW", 0);
  return f(a, b);
}
int SDL_SaveBMP_RW(SDL_Surface *a, SDL_RWops *b, int c) {
  static int (*f)(SDL_Surface *, SDL_RWops *, int);
  R(f, "SDL_SaveBMP_RW", -1);
  return f(a, b, c);
}
int SDL_SetSurfaceBlendMode(SDL_Surface *a, SDL_BlendMode b) {
  static int (*f)(SDL_Surface *, SDL_BlendMode);
  R(f, "SDL_SetSurfaceBlendMode", -1);
  return f(a, b);
}
int SDL_GetSurfaceBlendMode(SDL_Surface *a, SDL_BlendMode *b) {
  static int (*f)(SDL_Surface *, SDL_BlendMode *);
  R(f, "SDL_GetSurfaceBlendMode", -1);
  return f(a, b);
}
int SDL_SetSurfaceColorMod(SDL_Surface *a, Uint8 b, Uint8 c, Uint8 d) {
  static int (*f)(SDL_Surface *, Uint8, Uint8, Uint8);
  R(f, "SDL_SetSurfaceColorMod", -1);
  return f(a, b, c, d);
}
int SDL_SetSurfaceAlphaMod(SDL_Surface *a, Uint8 b) {
  static int (*f)(SDL_Surface *, Uint8);
  R(f, "SDL_SetSurfaceAlphaMod", -1);
  return f(a, b);
}
int SDL_BlitSurface(SDL_Surface *a, const SDL_Rect *b, SDL_Surface *c,
                    SDL_Rect *d) {
  static int (*f)(SDL_Surface *, const SDL_Rect *, SDL_Surface *, SDL_Rect *);
  R(f, "SDL_BlitSurface", -1);
  return f(a, b, c, d);
}
int SDL_BlitScaled(SDL_Surface *a, const SDL_Rect *b, SDL_Surface *c,
                   SDL_Rect *d) {
  static int (*f)(SDL_Surface *, const SDL_Rect *, SDL_Surface *, SDL_Rect *);
  R(f, "SDL_BlitScaled", -1);
  return f(a, b, c, d);
}
int SDL_FillRect(SDL_Surface *a, const SDL_Rect *b, Uint32 c) {
  static int (*f)(SDL_Surface *, const SDL_Rect *, Uint32);
  R(f, "SDL_FillRect", -1);
  return f(a, b, c);
}
int SDL_ConvertPixels(int a, int b, Uint32 c, const void *d, int e, Uint32 f,
                      void *g, int h) {
  static int (*fn)(int, int, Uint32, const void *, int, Uint32, void *, int);
  R(fn, "SDL_ConvertPixels", -1);
  return fn(a, b, c, d, e, f, g, h);
}

/* ================= 6. 事件 (SDL_events.h) ================= */
int SDL_PollEvent(SDL_Event *a) {
  static int (*f)(SDL_Event *);
  R(f, "SDL_PollEvent", 0);
  return f(a);
}
int SDL_WaitEvent(SDL_Event *a) {
  static int (*f)(SDL_Event *);
  R(f, "SDL_WaitEvent", 0);
  return f(a);
}
int SDL_WaitEventTimeout(SDL_Event *a, int b) {
  static int (*f)(SDL_Event *, int);
  R(f, "SDL_WaitEventTimeout", 0);
  return f(a, b);
}
int SDL_PushEvent(SDL_Event *a) {
  static int (*f)(SDL_Event *);
  R(f, "SDL_PushEvent", -1);
  return f(a);
}
void SDL_SetEventFilter(SDL_EventFilter a, void *b) {
  static void (*f)(SDL_EventFilter, void *);
  R(f, "SDL_SetEventFilter", );
  f(a, b);
}
int SDL_GetEventFilter(SDL_EventFilter *a, void **b) {
  static int (*f)(SDL_EventFilter *, void **);
  R(f, "SDL_GetEventFilter", 0);
  return f(a, b);
}
void SDL_AddEventWatch(SDL_EventFilter a, void *b) {
  static void (*f)(SDL_EventFilter, void *);
  R(f, "SDL_AddEventWatch", );
  f(a, b);
}
void SDL_DelEventWatch(SDL_EventFilter a, void *b) {
  static void (*f)(SDL_EventFilter, void *);
  R(f, "SDL_DelEventWatch", );
  f(a, b);
}
void SDL_FlushEvent(Uint32 a) {
  static void (*f)(Uint32);
  R(f, "SDL_FlushEvent", );
  f(a);
}
void SDL_FlushEvents(Uint32 a, Uint32 b) {
  static void (*f)(Uint32, Uint32);
  R(f, "SDL_FlushEvents", );
  f(a, b);
}
int SDL_PeepEvents(SDL_Event *a, int b, SDL_eventaction c, Uint32 d, Uint32 e) {
  static int (*f)(SDL_Event *, int, SDL_eventaction, Uint32, Uint32);
  R(f, "SDL_PeepEvents", 0);
  return f(a, b, c, d, e);
}
SDL_bool SDL_HasEvent(Uint32 a) {
  static SDL_bool (*f)(Uint32);
  R(f, "SDL_HasEvent", SDL_FALSE);
  return f(a);
}
SDL_bool SDL_HasEvents(Uint32 a, Uint32 b) {
  static SDL_bool (*f)(Uint32, Uint32);
  R(f, "SDL_HasEvents", SDL_FALSE);
  return f(a, b);
}
Uint8 SDL_EventState(Uint32 a, int b) {
  static Uint8 (*f)(Uint32, int);
  R(f, "SDL_EventState", 0);
  return f(a, b);
}
Uint32 SDL_RegisterEvents(int a) {
  static Uint32 (*f)(int);
  R(f, "SDL_RegisterEvents", 0);
  return f(a);
}

/* ================= 7. 键盘 (SDL_keyboard.h) ================= */
const Uint8 *SDL_GetKeyboardState(int *a) {
  static const Uint8 *(*f)(int *);
  R(f, "SDL_GetKeyboardState", 0);
  return f(a);
}
SDL_Keymod SDL_GetModState(void) {
  static SDL_Keymod (*f)(void);
  R(f, "SDL_GetModState", KMOD_NONE);
  return f();
}
void SDL_SetModState(SDL_Keymod a) {
  static void (*f)(SDL_Keymod);
  R(f, "SDL_SetModState", );
  f(a);
}
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode a) {
  static SDL_Keycode (*f)(SDL_Scancode);
  R(f, "SDL_GetKeyFromScancode", SDLK_UNKNOWN);
  return f(a);
}
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode a) {
  static SDL_Scancode (*f)(SDL_Keycode);
  R(f, "SDL_GetScancodeFromKey", SDL_SCANCODE_UNKNOWN);
  return f(a);
}
const char *SDL_GetScancodeName(SDL_Scancode a) {
  static const char *(*f)(SDL_Scancode);
  R(f, "SDL_GetScancodeName", "");
  return f(a);
}
SDL_Scancode SDL_GetScancodeFromName(const char *a) {
  static SDL_Scancode (*f)(const char *);
  R(f, "SDL_GetScancodeFromName", SDL_SCANCODE_UNKNOWN);
  return f(a);
}
const char *SDL_GetKeyName(SDL_Keycode a) {
  static const char *(*f)(SDL_Keycode);
  R(f, "SDL_GetKeyName", "");
  return f(a);
}
SDL_Keycode SDL_GetKeyFromName(const char *a) {
  static SDL_Keycode (*f)(const char *);
  R(f, "SDL_GetKeyFromName", SDLK_UNKNOWN);
  return f(a);
}
void SDL_StartTextInput(void) {
  static void (*f)(void);
  R(f, "SDL_StartTextInput", );
  f();
}
SDL_bool SDL_IsTextInputActive(void) {
  static SDL_bool (*f)(void);
  R(f, "SDL_IsTextInputActive", SDL_FALSE);
  return f();
}
void SDL_StopTextInput(void) {
  static void (*f)(void);
  R(f, "SDL_StopTextInput", );
  f();
}
void SDL_SetTextInputRect(SDL_Rect *a) {
  static void (*f)(SDL_Rect *);
  R(f, "SDL_SetTextInputRect", );
  f(a);
}

/* ================= 8. 鼠标 (SDL_mouse.h) ================= */
Uint32 SDL_GetMouseState(int *a, int *b) {
  static Uint32 (*f)(int *, int *);
  R(f, "SDL_GetMouseState", 0);
  return f(a, b);
}
Uint32 SDL_GetGlobalMouseState(int *a, int *b) {
  static Uint32 (*f)(int *, int *);
  R(f, "SDL_GetGlobalMouseState", 0);
  return f(a, b);
}
Uint32 SDL_GetRelativeMouseState(int *a, int *b) {
  static Uint32 (*f)(int *, int *);
  R(f, "SDL_GetRelativeMouseState", 0);
  return f(a, b);
}
void SDL_WarpMouseInWindow(SDL_Window *a, int b, int c) {
  static void (*f)(SDL_Window *, int, int);
  R(f, "SDL_WarpMouseInWindow", );
  f(a, b, c);
}
int SDL_SetRelativeMouseMode(SDL_bool a) {
  static int (*f)(SDL_bool);
  R(f, "SDL_SetRelativeMouseMode", -1);
  return f(a);
}
SDL_bool SDL_GetRelativeMouseMode(void) {
  static SDL_bool (*f)(void);
  R(f, "SDL_GetRelativeMouseMode", SDL_FALSE);
  return f();
}
SDL_Cursor *SDL_CreateCursor(const Uint8 *a, const Uint8 *b, int c, int d,
                             int e, int f) {
  static SDL_Cursor *(*fn)(const Uint8 *, const Uint8 *, int, int, int, int);
  R(fn, "SDL_CreateCursor", 0);
  return fn(a, b, c, d, e, f);
}
SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *a, int b, int c) {
  static SDL_Cursor *(*f)(SDL_Surface *, int, int);
  R(f, "SDL_CreateColorCursor", 0);
  return f(a, b, c);
}
SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor a) {
  static SDL_Cursor *(*f)(SDL_SystemCursor);
  R(f, "SDL_CreateSystemCursor", 0);
  return f(a);
}
void SDL_SetCursor(SDL_Cursor *a) {
  static void (*f)(SDL_Cursor *);
  R(f, "SDL_SetCursor", );
  f(a);
}
SDL_Cursor *SDL_GetCursor(void) {
  static SDL_Cursor *(*f)(void);
  R(f, "SDL_GetCursor", 0);
  return f();
}
void SDL_FreeCursor(SDL_Cursor *a) {
  static void (*f)(SDL_Cursor *);
  R(f, "SDL_FreeCursor", );
  f(a);
}
int SDL_ShowCursor(int a) {
  static int (*f)(int);
  R(f, "SDL_ShowCursor", -1);
  return f(a);
}

/* ================= 9. 计时器 (SDL_timer.h) ================= */
Uint32 SDL_GetTicks(void) {
  static Uint32 (*f)(void);
  R(f, "SDL_GetTicks", 0);
  return f();
}
Uint64 SDL_GetTicks64(void) {
  static Uint64 (*f)(void);
  R(f, "SDL_GetTicks64", 0);
  return f();
}
Uint64 SDL_GetPerformanceCounter(void) {
  static Uint64 (*f)(void);
  R(f, "SDL_GetPerformanceCounter", 0);
  return f();
}
Uint64 SDL_GetPerformanceFrequency(void) {
  static Uint64 (*f)(void);
  R(f, "SDL_GetPerformanceFrequency", 0);
  return f();
}
void SDL_Delay(Uint32 a) {
  static void (*f)(Uint32);
  R(f, "SDL_Delay", );
  f(a);
}
SDL_TimerID SDL_AddTimer(Uint32 a, SDL_TimerCallback b, void *c) {
  static SDL_TimerID (*f)(Uint32, SDL_TimerCallback, void *);
  R(f, "SDL_AddTimer", 0);
  return f(a, b, c);
}
SDL_bool SDL_RemoveTimer(SDL_TimerID a) {
  static SDL_bool (*f)(SDL_TimerID);
  R(f, "SDL_RemoveTimer", SDL_FALSE);
  return f(a);
}

/* ================= 10. 音频 (SDL_audio.h) ================= */
int SDL_OpenAudio(SDL_AudioSpec *a, SDL_AudioSpec *b) {
  static int (*f)(SDL_AudioSpec *, SDL_AudioSpec *);
  R(f, "SDL_OpenAudio", -1);
  return f(a, b);
}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *a, int b,
                                      const SDL_AudioSpec *c, SDL_AudioSpec *d,
                                      int e) {
  static SDL_AudioDeviceID (*fn)(const char *, int, const SDL_AudioSpec *,
                                 SDL_AudioSpec *, int);
  R(fn, "SDL_OpenAudioDevice", 0);
  return fn(a, b, c, d, e);
}
void SDL_CloseAudio(void) {
  static void (*f)(void);
  R(f, "SDL_CloseAudio", );
  f();
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID a) {
  static void (*f)(SDL_AudioDeviceID);
  R(f, "SDL_CloseAudioDevice", );
  f(a);
}
void SDL_PauseAudio(int a) {
  static void (*f)(int);
  R(f, "SDL_PauseAudio", );
  f(a);
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID a, int b) {
  static void (*f)(SDL_AudioDeviceID, int);
  R(f, "SDL_PauseAudioDevice", );
  f(a, b);
}
SDL_AudioStatus SDL_GetAudioStatus(void) {
  static SDL_AudioStatus (*f)(void);
  R(f, "SDL_GetAudioStatus", SDL_AUDIO_STOPPED);
  return f();
}
SDL_AudioStatus SDL_GetAudioDeviceStatus(SDL_AudioDeviceID a) {
  static SDL_AudioStatus (*f)(SDL_AudioDeviceID);
  R(f, "SDL_GetAudioDeviceStatus", SDL_AUDIO_STOPPED);
  return f(a);
}
void SDL_LockAudio(void) {
  static void (*f)(void);
  R(f, "SDL_LockAudio", );
  f();
}
void SDL_UnlockAudio(void) {
  static void (*f)(void);
  R(f, "SDL_UnlockAudio", );
  f();
}
void SDL_LockAudioDevice(SDL_AudioDeviceID a) {
  static void (*f)(SDL_AudioDeviceID);
  R(f, "SDL_LockAudioDevice", );
  f(a);
}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID a) {
  static void (*f)(SDL_AudioDeviceID);
  R(f, "SDL_UnlockAudioDevice", );
  f(a);
}
int SDL_BuildAudioCVT(SDL_AudioCVT *a, SDL_AudioFormat b, Uint8 c, int d,
                      SDL_AudioFormat e, Uint8 f, int g) {
  static int (*fn)(SDL_AudioCVT *, SDL_AudioFormat, Uint8, int, SDL_AudioFormat,
                   Uint8, int);
  R(fn, "SDL_BuildAudioCVT", -1);
  return fn(a, b, c, d, e, f, g);
}
int SDL_ConvertAudio(SDL_AudioCVT *a) {
  static int (*f)(SDL_AudioCVT *);
  R(f, "SDL_ConvertAudio", -1);
  return f(a);
}
void SDL_MixAudio(Uint8 *a, const Uint8 *b, Uint32 c, int d) {
  static void (*fn)(Uint8 *, const Uint8 *, Uint32, int);
  R(fn, "SDL_MixAudio", );
  fn(a, b, c, d);
}

/* ================= 11. 线程 (SDL_thread.h) ================= */
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction a, const char *b, void *c) {
  static SDL_Thread *(*f)(SDL_ThreadFunction, const char *, void *);
  R(f, "SDL_CreateThread", 0);
  return f(a, b, c);
}
void SDL_WaitThread(SDL_Thread *a, int *b) {
  static void (*f)(SDL_Thread *, int *);
  R(f, "SDL_WaitThread", );
  f(a, b);
}
void SDL_DetachThread(SDL_Thread *a) {
  static void (*f)(SDL_Thread *);
  R(f, "SDL_DetachThread", );
  f(a);
}
SDL_threadID SDL_GetThreadID(SDL_Thread *a) {
  static SDL_threadID (*f)(SDL_Thread *);
  R(f, "SDL_GetThreadID", 0);
  return f(a);
}
SDL_TLSID SDL_TLSCreate(void) {
  static SDL_TLSID (*f)(void);
  R(f, "SDL_TLSCreate", 0);
  return f();
}
void *SDL_TLSGet(SDL_TLSID a) {
  static void *(*f)(SDL_TLSID);
  R(f, "SDL_TLSGet", 0);
  return f(a);
}
int SDL_TLSSet(SDL_TLSID a, const void *b, void (*c)(void *)) {
  static int (*f)(SDL_TLSID, const void *, void (*)(void *));
  R(f, "SDL_TLSSet", -1);
  return f(a, b, c);
}
SDL_mutex *SDL_CreateMutex(void) {
  static SDL_mutex *(*f)(void);
  R(f, "SDL_CreateMutex", 0);
  return f();
}
int SDL_LockMutex(SDL_mutex *a) {
  static int (*f)(SDL_mutex *);
  R(f, "SDL_LockMutex", -1);
  return f(a);
}
int SDL_TryLockMutex(SDL_mutex *a) {
  static int (*f)(SDL_mutex *);
  R(f, "SDL_TryLockMutex", -1);
  return f(a);
}
int SDL_UnlockMutex(SDL_mutex *a) {
  static int (*f)(SDL_mutex *);
  R(f, "SDL_UnlockMutex", -1);
  return f(a);
}
void SDL_DestroyMutex(SDL_mutex *a) {
  static void (*f)(SDL_mutex *);
  R(f, "SDL_DestroyMutex", );
  f(a);
}
SDL_sem *SDL_CreateSemaphore(Uint32 a) {
  static SDL_sem *(*f)(Uint32);
  R(f, "SDL_CreateSemaphore", 0);
  return f(a);
}
void SDL_DestroySemaphore(SDL_sem *a) {
  static void (*f)(SDL_sem *);
  R(f, "SDL_DestroySemaphore", );
  f(a);
}
int SDL_SemWait(SDL_sem *a) {
  static int (*f)(SDL_sem *);
  R(f, "SDL_SemWait", -1);
  return f(a);
}
int SDL_SemTryWait(SDL_sem *a) {
  static int (*f)(SDL_sem *);
  R(f, "SDL_SemTryWait", -1);
  return f(a);
}
int SDL_SemWaitTimeout(SDL_sem *a, Uint32 b) {
  static int (*f)(SDL_sem *, Uint32);
  R(f, "SDL_SemWaitTimeout", -1);
  return f(a, b);
}
int SDL_SemPost(SDL_sem *a) {
  static int (*f)(SDL_sem *);
  R(f, "SDL_SemPost", -1);
  return f(a);
}
Uint32 SDL_SemValue(SDL_sem *a) {
  static Uint32 (*f)(SDL_sem *);
  R(f, "SDL_SemValue", 0);
  return f(a);
}
SDL_cond *SDL_CreateCond(void) {
  static SDL_cond *(*f)(void);
  R(f, "SDL_CreateCond", 0);
  return f();
}
void SDL_DestroyCond(SDL_cond *a) {
  static void (*f)(SDL_cond *);
  R(f, "SDL_DestroyCond", );
  f(a);
}
int SDL_CondSignal(SDL_cond *a) {
  static int (*f)(SDL_cond *);
  R(f, "SDL_CondSignal", -1);
  return f(a);
}
int SDL_CondBroadcast(SDL_cond *a) {
  static int (*f)(SDL_cond *);
  R(f, "SDL_CondBroadcast", -1);
  return f(a);
}
int SDL_CondWait(SDL_cond *a, SDL_mutex *b) {
  static int (*f)(SDL_cond *, SDL_mutex *);
  R(f, "SDL_CondWait", -1);
  return f(a, b);
}
int SDL_CondWaitTimeout(SDL_cond *a, SDL_mutex *b, Uint32 c) {
  static int (*f)(SDL_cond *, SDL_mutex *, Uint32);
  R(f, "SDL_CondWaitTimeout", -1);
  return f(a, b, c);
}

/* ================= 12. 文件 I/O (SDL_rwops.h) ================= */
SDL_RWops *SDL_RWFromFile(const char *a, const char *b) {
  static SDL_RWops *(*f)(const char *, const char *);
  R(f, "SDL_RWFromFile", 0);
  return f(a, b);
}
SDL_RWops *SDL_RWFromMem(void *a, int b) {
  static SDL_RWops *(*f)(void *, int);
  R(f, "SDL_RWFromMem", 0);
  return f(a, b);
}
SDL_RWops *SDL_RWFromConstMem(const void *a, int b) {
  static SDL_RWops *(*f)(const void *, int);
  R(f, "SDL_RWFromConstMem", 0);
  return f(a, b);
}
SDL_RWops *SDL_AllocRW(void) {
  static SDL_RWops *(*f)(void);
  R(f, "SDL_AllocRW", 0);
  return f();
}
void SDL_FreeRW(SDL_RWops *a) {
  static void (*f)(SDL_RWops *);
  R(f, "SDL_FreeRW", );
  f(a);
}
Sint64 SDL_RWsize(SDL_RWops *a) {
  static Sint64 (*f)(SDL_RWops *);
  R(f, "SDL_RWsize", -1);
  return f(a);
}
Sint64 SDL_RWseek(SDL_RWops *a, Sint64 b, int c) {
  static Sint64 (*f)(SDL_RWops *, Sint64, int);
  R(f, "SDL_RWseek", -1);
  return f(a, b, c);
}
Sint64 SDL_RWtell(SDL_RWops *a) {
  static Sint64 (*f)(SDL_RWops *);
  R(f, "SDL_RWtell", -1);
  return f(a);
}
size_t SDL_RWread(SDL_RWops *a, void *b, size_t c, size_t d) {
  static size_t (*f)(SDL_RWops *, void *, size_t, size_t);
  R(f, "SDL_RWread", 0);
  return f(a, b, c, d);
}
size_t SDL_RWwrite(SDL_RWops *a, const void *b, size_t c, size_t d) {
  static size_t (*f)(SDL_RWops *, const void *, size_t, size_t);
  R(f, "SDL_RWwrite", 0);
  return f(a, b, c, d);
}
int SDL_RWclose(SDL_RWops *a) {
  static int (*f)(SDL_RWops *);
  R(f, "SDL_RWclose", -1);
  return f(a);
}
char *SDL_GetBasePath(void) {
  static char *(*f)(void);
  R(f, "SDL_GetBasePath", 0);
  return f();
}
char *SDL_GetPrefPath(const char *a, const char *b) {
  static char *(*f)(const char *, const char *);
  R(f, "SDL_GetPrefPath", 0);
  return f(a, b);
}

/* ================= 13. 游戏控制器 (SDL_gamecontroller.h) ================= */
SDL_GameController *SDL_GameControllerOpen(int a) {
  static SDL_GameController *(*f)(int);
  R(f, "SDL_GameControllerOpen", 0);
  return f(a);
}
void SDL_GameControllerClose(SDL_GameController *a) {
  static void (*f)(SDL_GameController *);
  R(f, "SDL_GameControllerClose", );
  f(a);
}
const char *SDL_GameControllerName(SDL_GameController *a) {
  static const char *(*f)(SDL_GameController *);
  R(f, "SDL_GameControllerName", "");
  return f(a);
}
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *a) {
  static SDL_GameControllerType (*f)(SDL_GameController *);
  R(f, "SDL_GameControllerGetType", SDL_CONTROLLER_TYPE_UNKNOWN);
  return f(a);
}
int SDL_GameControllerGetPlayerIndex(SDL_GameController *a) {
  static int (*f)(SDL_GameController *);
  R(f, "SDL_GameControllerGetPlayerIndex", -1);
  return f(a);
}
Uint8 SDL_GameControllerGetButton(SDL_GameController *a,
                                  SDL_GameControllerButton b) {
  static Uint8 (*f)(SDL_GameController *, SDL_GameControllerButton);
  R(f, "SDL_GameControllerGetButton", 0);
  return f(a, b);
}
Sint16 SDL_GameControllerGetAxis(SDL_GameController *a,
                                 SDL_GameControllerAxis b) {
  static Sint16 (*f)(SDL_GameController *, SDL_GameControllerAxis);
  R(f, "SDL_GameControllerGetAxis", 0);
  return f(a, b);
}
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *a) {
  static SDL_bool (*f)(SDL_GameController *);
  R(f, "SDL_GameControllerGetAttached", SDL_FALSE);
  return f(a);
}
int SDL_GameControllerRumble(SDL_GameController *a, Uint16 b, Uint16 c,
                             Uint32 d) {
  static int (*f)(SDL_GameController *, Uint16, Uint16, Uint32);
  R(f, "SDL_GameControllerRumble", -1);
  return f(a, b, c, d);
}
int SDL_GameControllerNumMappings(void) {
  static int (*f)(void);
  R(f, "SDL_GameControllerNumMappings", 0);
  return f();
}
char *SDL_GameControllerMappingForIndex(int a) {
  static char *(*f)(int);
  R(f, "SDL_GameControllerMappingForIndex", 0);
  return f(a);
}
SDL_bool SDL_IsGameController(int a) {
  static SDL_bool (*f)(int);
  R(f, "SDL_IsGameController", SDL_FALSE);
  return f(a);
}

/* ================= 14. 属性 (SDL_hints.h) ================= */
SDL_bool SDL_SetHint(const char *a, const char *b) {
  static SDL_bool (*f)(const char *, const char *);
  R(f, "SDL_SetHint", SDL_FALSE);
  return f(a, b);
}
const char *SDL_GetHint(const char *a) {
  static const char *(*f)(const char *);
  R(f, "SDL_GetHint", 0);
  return f(a);
}
void SDL_AddHintCallback(const char *a, SDL_HintCallback b, void *c) {
  static void (*fn)(const char *, SDL_HintCallback, void *);
  R(fn, "SDL_AddHintCallback", );
  fn(a, b, c);
}
void SDL_DelHintCallback(const char *a, SDL_HintCallback b, void *c) {
  static void (*fn)(const char *, SDL_HintCallback, void *);
  R(fn, "SDL_DelHintCallback", );
  fn(a, b, c);
}
void SDL_ClearHints(void) {
  static void (*f)(void);
  R(f, "SDL_ClearHints", );
  f();
}

/* ================= 15. 消息框 (SDL_messagebox.h) ================= */
int SDL_ShowSimpleMessageBox(Uint32 a, const char *b, const char *c,
                             SDL_Window *d) {
  static int (*f)(Uint32, const char *, const char *, SDL_Window *);
  R(f, "SDL_ShowSimpleMessageBox", -1);
  return f(a, b, c, d);
}
int SDL_ShowMessageBox(const SDL_MessageBoxData *a, int *b) {
  static int (*f)(const SDL_MessageBoxData *, int *);
  R(f, "SDL_ShowMessageBox", -1);
  return f(a, b);
}
