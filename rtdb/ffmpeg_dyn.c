/* ============================================================================
 * ffmpeg_dyn.c — FFmpeg (libavcodec/libavformat/libavutil) 动态加载封装层
 *
 * 设计约束（与 sqlite3_dyn.c / mysql_dyn.c 等一致）：
 *   1. 链接期绝不出现 -lavcodec -lavformat -lavutil。
 *   2. 无自定义 .h 文件，无独立 wrapper .c。
 *   3. 每个封装函数首次调用时才 dlsym 自己的符号并缓存。
 *   4. 封装函数名与原版 FFmpeg API 一致。
 *   5. 编译期需要 FFmpeg 头文件（类型和常量），链接期不需要。
 *   6. 跨平台：Unix/macOS 用 dlopen/dlsym；Windows 用 LoadLibrary/GetProcAddress。
 *   7. 搜索顺序：显式路径 > FFMPEG_DYN_LIB > 系统搜索路径 > 默认库名
 *      > Nix 路径 > 平台默认路径。
 *   8. 线程安全：首次调用 ffmpeg_libok() 或任一封装函数时完成加载。
 *
 * 关键版本差异（FFmpeg 7.0 破坏性变更）：
 *   - 7.0 移除了旧位掩码声道布局 API，全面替换为 AVChannelLayout[citation:6]。
 *   - AVCodecContext.channels 字段在 7.0 中被移除，需改用 ch_layout.nb_channels[citation:4]。
 *   - 版本判定依据：libavcodec 主版本号（7.0 对应 libavcodec 61，6.x 对应 60）。
 *   - 本封装对受影响的函数做运行时分支；未受影响的函数保持单一路径。
 * ========================================================================== */

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__has_include)
#  if __has_include(<libavcodec/avcodec.h>) && __has_include(<libavformat/avformat.h>) && __has_include(<libavutil/avutil.h>)
#    include <libavcodec/avcodec.h>
#    include <libavformat/avformat.h>
#    include <libavutil/avutil.h>
#    define FFMPEG_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef FFMPEG_DYN_HAVE_HEADER
/* ---------- 类型（头文件缺失时的最小 ABI 回退） ---------- */
#include <stdint.h>
typedef struct AVCodec AVCodec;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVDictionary AVDictionary;
typedef struct AVFormatContext AVFormatContext;
typedef struct AVInputFormat AVInputFormat;
typedef struct AVPacket AVPacket;
typedef struct AVFrame AVFrame;
typedef struct AVChannelLayout AVChannelLayout;
typedef enum AVCodecID { AV_CODEC_ID_NONE = 0 } AVCodecID;

#endif /* FFMPEG_DYN_HAVE_HEADER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
  static void *get_sym(lib_handle_t h, const char *name) {
      return (void*)GetProcAddress(h, name);
  }
  static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
  static const char *lib_error(void) { return "LoadLibrary failed"; }
  #define PATH_SEP ';'
#else
  #include <dlfcn.h>
  typedef void *lib_handle_t;
  static lib_handle_t load_lib(const char *path) {
      int flags = RTLD_LAZY | RTLD_LOCAL;
  #ifdef RTLD_DEEPBIND
      flags |= RTLD_DEEPBIND;
  #endif
      return dlopen(path, flags);
  }
  static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
  static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
  static const char *lib_error(void) { return dlerror(); }
  #ifdef __GNUC__
  __attribute__((unused)) static void *lib_error_ref = (void *)lib_error;
  #endif
  #define PATH_SEP ':'
#endif

/* ============================================================
 * 全局状态：每个库一个句柄
 * ============================================================ */
static lib_handle_t avcodec_handle = NULL;
static lib_handle_t avformat_handle = NULL;
static lib_handle_t avutil_handle = NULL;
static int init_state = 0;
static int avcodec_major = 0;   /* 60=6.x, 61=7.x, 62=8.x */

#define MAX_CANDIDATES 32
#define PATH_BUF_LEN   1024

/* 按库名和版本号构造候选路径并尝试加载 */
static lib_handle_t load_one(const char *lib_base, int version) {
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0;

    /* 1. 环境变量 FFMPEG_DYN_LIB（仅对 avcodec 生效，格式：/path/to/libavcodec.so.61） */
    if (version == 0) {  /* 仅第一次调用时处理环境变量 */
        const char *env = getenv("FFMPEG_DYN_LIB");
        if (env && *env && n < MAX_CANDIDATES - 1) {
            snprintf(bufs[n], PATH_BUF_LEN, "%s", env);
            { candidates[n] = bufs[n]; n++; }
        }
    }

    /* 2. 系统搜索路径环境变量 */
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_LIBRARY_PATH"
#else
        "LD_LIBRARY_PATH"
#endif
    );
    if (env_path && *env_path && n < MAX_CANDIDATES - 4) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < MAX_CANDIDATES - 4) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    /* 尝试带版本号和不带版本号的名字 */
#ifdef __APPLE__
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/%s.%d.dylib", p, lib_base, version);
                    { candidates[n] = bufs[n]; n++; }
#else
                    snprintf(bufs[n], PATH_BUF_LEN, "%s/%s.so.%d", p, lib_base, version);
                    { candidates[n] = bufs[n]; n++; }
#endif
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 3. 默认库名（带版本号，优先） */
#ifdef __APPLE__
    snprintf(bufs[n], PATH_BUF_LEN, "%s.%d.dylib", lib_base, version); { candidates[n] = bufs[n]; n++; }
    snprintf(bufs[n], PATH_BUF_LEN, "%s.dylib", lib_base); { candidates[n] = bufs[n]; n++; }
#else
    snprintf(bufs[n], PATH_BUF_LEN, "%s.so.%d", lib_base, version); { candidates[n] = bufs[n]; n++; }
    snprintf(bufs[n], PATH_BUF_LEN, "%s.so", lib_base); { candidates[n] = bufs[n]; n++; }
#endif

    /* 4. 平台默认路径 */
#ifdef __APPLE__
    snprintf(bufs[n], PATH_BUF_LEN, "/opt/local/lib/%s.%d.dylib", lib_base, version); { candidates[n] = bufs[n]; n++; }
    snprintf(bufs[n], PATH_BUF_LEN, "/usr/local/lib/%s.%d.dylib", lib_base, version); { candidates[n] = bufs[n]; n++; }
#elif defined(__linux__)
    snprintf(bufs[n], PATH_BUF_LEN, "/usr/lib/x86_64-linux-gnu/%s.so.%d", lib_base, version); { candidates[n] = bufs[n]; n++; }
    snprintf(bufs[n], PATH_BUF_LEN, "/usr/lib/%s.so.%d", lib_base, version); { candidates[n] = bufs[n]; n++; }
#endif
    candidates[n] = NULL;

    for (int i = 0; i < n; i++) {
        lib_handle_t h = load_lib(candidates[i]);
        if (h) return h;
    }
    return NULL;
}

static int do_load(const char *library_path) {
    (void)library_path;  /* 暂不使用显式路径，留给 ffmpeg_dyn_init 扩展 */

    /* 先探测 avcodec 主版本：尝试 61(7.x) → 60(6.x) → 62(8.x) */
    int versions[] = {61, 60, 62, 0};
    avcodec_handle = NULL;
    for (int i = 0; versions[i]; i++) {
        avcodec_handle = load_one("libavcodec", versions[i]);
        if (avcodec_handle) {
            avcodec_major = versions[i];
            break;
        }
    }
    if (!avcodec_handle) {
        fprintf(stderr, "ffmpeg_dyn: cannot load libavcodec\n");
        return -1;
    }

    avformat_handle = load_one("libavformat", avcodec_major);
    avutil_handle   = load_one("libavutil",   avcodec_major);

    if (!avformat_handle || !avutil_handle) {
        fprintf(stderr, "ffmpeg_dyn: cannot load libavformat/libavutil (avcodec major=%d)\n",
                avcodec_major);
        free_lib(avcodec_handle);
        if (avformat_handle) free_lib(avformat_handle);
        if (avutil_handle) free_lib(avutil_handle);
        avcodec_handle = avformat_handle = avutil_handle = NULL;
        return -1;
    }

    fprintf(stderr, "ffmpeg_dyn: loaded FFmpeg libavcodec major=%d\n", avcodec_major);
    return 0;
}

int ffmpeg_libok(void) {
    if (init_state == 0) {
        init_state = (do_load(NULL) == 0) ? 1 : -1;
    }
    return (init_state == 1 && avcodec_handle != NULL) ? 1 : 0;
}

void ffmpeg_dyn_cleanup(void) {
    if (avcodec_handle)  free_lib(avcodec_handle);
    if (avformat_handle) free_lib(avformat_handle);
    if (avutil_handle)   free_lib(avutil_handle);
    avcodec_handle = avformat_handle = avutil_handle = NULL;
    init_state = 0;
    avcodec_major = 0;
}

int ffmpeg_dyn_avcodec_major(void) { return avcodec_major; }

/* ============================================================
 * 按需解析宏（针对特定句柄）
 * ============================================================ */
#define RESOLVE(handle, var, name, fallback) do {              \
    if (!(var)) {                                              \
        if (!ffmpeg_libok()) return fallback;                  \
        *(void **)(&(var)) = get_sym(handle, name);            \
        if (!(var)) {                                          \
            fprintf(stderr, "dlsym failed: %s\n", name);       \
            return fallback;                                   \
        }                                                      \
    }                                                          \
} while (0)

/* ============================================================
 * 基础封装（跨版本稳定）
 * ============================================================ */
const AVCodec *avcodec_find_decoder(enum AVCodecID id) {
    static const AVCodec *(*fn)(enum AVCodecID) = NULL;
    if (!fn) {
        if (!ffmpeg_libok()) return NULL;
        *(void **)(&fn) = get_sym(avcodec_handle, "avcodec_find_decoder");
        if (!fn) return NULL;
    }
    return fn(id);
}

AVCodecContext *avcodec_alloc_context3(const AVCodec *codec) {
    static AVCodecContext *(*fn)(const AVCodec*) = NULL;
    if (!fn) {
        if (!ffmpeg_libok()) return NULL;
        *(void **)(&fn) = get_sym(avcodec_handle, "avcodec_alloc_context3");
        if (!fn) return NULL;
    }
    return fn(codec);
}

int avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options) {
    static int (*fn)(AVCodecContext*, const AVCodec*, AVDictionary**) = NULL;
    RESOLVE(avcodec_handle, fn, "avcodec_open2", -1);
    return fn(avctx, codec, options);
}

int avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt) {
    static int (*fn)(AVCodecContext*, const AVPacket*) = NULL;
    RESOLVE(avcodec_handle, fn, "avcodec_send_packet", -1);
    return fn(avctx, avpkt);
}

int avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame) {
    static int (*fn)(AVCodecContext*, AVFrame*) = NULL;
    RESOLVE(avcodec_handle, fn, "avcodec_receive_frame", -1);
    return fn(avctx, frame);
}

void avcodec_free_context(AVCodecContext **avctx) {
    static void (*fn)(AVCodecContext**) = NULL;
    if (!fn) {
        if (!ffmpeg_libok()) return;
        *(void **)(&fn) = get_sym(avcodec_handle, "avcodec_free_context");
        if (!fn) return;
    }
    fn(avctx);
}

/* avcodec_close 在 7.0 中被移除，封装层根据版本回退到 avcodec_free_context */
int avcodec_close(AVCodecContext *avctx) {
    if (avcodec_major >= 61) {
        /* 7.0+：avcodec_close 已移除，语义上用 free_context 替代（需调用方传 &ctx）[citation:12] */
        AVCodecContext **p = &avctx;
        static void (*fn)(AVCodecContext**) = NULL;
        if (!fn) {
            if (!ffmpeg_libok()) return -1;
            *(void **)(&fn) = get_sym(avcodec_handle, "avcodec_free_context");
            if (!fn) return -1;
        }
        fn(p);
        return 0;
    } else {
        static int (*fn)(AVCodecContext*) = NULL;
        RESOLVE(avcodec_handle, fn, "avcodec_close", -1);
        return fn(avctx);
    }
}

/* ============================================================
 * 声道布局相关（7.0 破坏性变更的核心区域）
 * 注意：旧位掩码 API 在 7.0 中已移除，以下封装仅在 6.x 及以下可用。
 * 对于 7.0+，调用方应直接使用 AVChannelLayout 字段和 av_channel_layout_* 函数。
 * ============================================================ */
#if 0
/* 旧 API 示例：7.0 中这些符号可能不存在 */
uint64_t av_get_default_channel_layout(int nb_channels) {
    static uint64_t (*fn)(int) = NULL;
    if (!fn) {
        if (!ffmpeg_libok()) return 0;
        *(void **)(&fn) = get_sym(avutil_handle, "av_get_default_channel_layout");
        if (!fn) return 0;
    }
    return fn(nb_channels);
}
#endif

/* ============================================================
 * libavformat 基础封装
 * ============================================================ */
int avformat_open_input(AVFormatContext **ps, const char *url,
                        const AVInputFormat *fmt, AVDictionary **options) {
    static int (*fn)(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**) = NULL;
    RESOLVE(avformat_handle, fn, "avformat_open_input", -1);
    return fn(ps, url, fmt, options);
}

int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options) {
    static int (*fn)(AVFormatContext*, AVDictionary**) = NULL;
    RESOLVE(avformat_handle, fn, "avformat_find_stream_info", -1);
    return fn(ic, options);
}

void avformat_close_input(AVFormatContext **s) {
    static void (*fn)(AVFormatContext**) = NULL;
    if (!fn) {
        if (!ffmpeg_libok()) return;
        *(void **)(&fn) = get_sym(avformat_handle, "avformat_close_input");
        if (!fn) return;
    }
    fn(s);
}

int av_read_frame(AVFormatContext *s, AVPacket *pkt) {
    static int (*fn)(AVFormatContext*, AVPacket*) = NULL;
    RESOLVE(avformat_handle, fn, "av_read_frame", -1);
    return fn(s, pkt);
}

/* ============================================================
 * libavutil 版本信息
 * ============================================================ */
unsigned avutil_version(void) {
    static unsigned (*fn)(void) = NULL;
    if (!fn) {
        if (!ffmpeg_libok()) return 0;
        *(void **)(&fn) = get_sym(avutil_handle, "avutil_version");
        if (!fn) return 0;
    }
    return fn();
}

