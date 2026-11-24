/* ============================================================
 * opendal_dyn.c - dlopen 模拟链接：Apache OpenDAL C Binding
 * ------------------------------------------------------------
 * 纯手写封装，逐条对照 /usr/include/opendal.h（161 个导出函数）。
 * 类型定义按头文件原样内嵌，字段顺序即 ABI；无 .h、无 wrapper。
 * 按值返回 struct 的函数以零值结构体作 fallback。
 * 库无 SONAME、无数据符号，候选表包含 libopendal_c.so。
 *
 * 单文件；链接期不需要 libopendal_c，仅需 -ldl。
 * 加载顺序：显式路径 > OPENDAL_DYN_LIB > LD_LIBRARY_PATH > soname >
 *           .so > Nix > 平台默认；成功后不卸载。
 *
 * 用法：
 *   opendal_dyn_init(NULL);
 *   ... 原版 opendal_* 直接调用 ...
 *   opendal_dyn_cleanup();
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- 平台适配层 ---------- */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) { return (void *)GetProcAddress(h, name); }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif

/* ---------- 类型（opendal.h 原样内嵌，字段顺序即 ABI） ---------- */
#define OPENDAL_SEEK_SET 0
#define OPENDAL_SEEK_CUR 1
#define OPENDAL_SEEK_END 2
typedef enum opendal_code {
  OPENDAL_UNEXPECTED,
  OPENDAL_UNSUPPORTED,
  OPENDAL_CONFIG_INVALID,
  OPENDAL_NOT_FOUND,
  OPENDAL_PERMISSION_DENIED,
  OPENDAL_IS_A_DIRECTORY,
  OPENDAL_NOT_A_DIRECTORY,
  OPENDAL_ALREADY_EXISTS,
  OPENDAL_RATE_LIMITED,
  OPENDAL_IS_SAME_FILE,
  OPENDAL_CONDITION_NOT_MATCH,
  OPENDAL_RANGE_NOT_SATISFIED,
  OPENDAL_CONFLICT,
} opendal_code;
typedef struct opendal_presigned_request_inner opendal_presigned_request_inner;
typedef struct opendal_bytes {
  uint8_t *data;
  uintptr_t len;
  uintptr_t capacity;
} opendal_bytes;
typedef struct opendal_error {
  enum opendal_code code;
  struct opendal_bytes message;
} opendal_error;
typedef struct opendal_entry {
  void *inner;
} opendal_entry;
typedef struct opendal_result_lister_next {
  struct opendal_entry *entry;
  struct opendal_error *error;
} opendal_result_lister_next;
typedef struct opendal_lister {
  void *inner;
} opendal_lister;
typedef struct opendal_operator_layers {
  void *inner;
} opendal_operator_layers;
typedef struct opendal_metadata {
  void *inner;
} opendal_metadata;
typedef struct opendal_metadata_user_metadata {
  void *inner;
} opendal_metadata_user_metadata;
typedef struct opendal_metadata_user_metadata_pair {
  const char *key;
  const char *value;
} opendal_metadata_user_metadata_pair;
typedef struct opendal_operator {
  void *inner;
} opendal_operator;
typedef struct opendal_result_operator_new {
  struct opendal_operator *op;
  struct opendal_error *error;
} opendal_result_operator_new;
typedef struct opendal_operator_options {
  void *inner;
} opendal_operator_options;
typedef struct opendal_write_user_metadata_pair {
  const char *key;
  const char *value;
} opendal_write_user_metadata_pair;
typedef struct opendal_write_options {
  bool append;
  const char *cache_control;
  const char *content_type;
  const char *content_disposition;
  const char *content_encoding;
  const char *if_match;
  const char *if_none_match;
  bool if_not_exists;
  uintptr_t concurrent;
  bool has_chunk;
  uintptr_t chunk;
  const struct opendal_write_user_metadata_pair *user_metadata;
  uintptr_t user_metadata_len;
} opendal_write_options;
typedef struct opendal_result_write {
  struct opendal_metadata *meta;
  struct opendal_error *error;
} opendal_result_write;
typedef struct opendal_result_read {
  struct opendal_bytes data;
  struct opendal_error *error;
} opendal_result_read;
typedef struct opendal_read_options {
  uint64_t offset;
  bool has_length;
  uint64_t length;
  const char *version;
  const char *if_match;
  const char *if_none_match;
  bool has_if_modified_since;
  int64_t if_modified_since;
  bool has_if_unmodified_since;
  int64_t if_unmodified_since;
  uintptr_t concurrent;
  bool has_chunk;
  uintptr_t chunk;
  bool has_gap;
  uintptr_t gap;
  bool has_content_length_hint;
  uint64_t content_length_hint;
  const char *override_content_type;
  const char *override_cache_control;
  const char *override_content_disposition;
} opendal_read_options;
typedef struct opendal_reader {
  void *inner;
} opendal_reader;
typedef struct opendal_result_operator_reader {
  struct opendal_reader *reader;
  struct opendal_error *error;
} opendal_result_operator_reader;
typedef struct opendal_reader_options {
  const char *version;
  const char *if_match;
  const char *if_none_match;
  bool has_if_modified_since;
  int64_t if_modified_since;
  bool has_if_unmodified_since;
  int64_t if_unmodified_since;
  bool has_content_length_hint;
  uint64_t content_length_hint;
  uintptr_t concurrent;
  bool has_chunk;
  uintptr_t chunk;
  bool has_gap;
  uintptr_t gap;
  uintptr_t prefetch;
} opendal_reader_options;
typedef struct opendal_writer {
  void *inner;
} opendal_writer;
typedef struct opendal_result_operator_writer {
  struct opendal_writer *writer;
  struct opendal_error *error;
} opendal_result_operator_writer;
typedef struct opendal_delete_options {
  char *version;
  bool recursive;
} opendal_delete_options;
typedef struct opendal_result_is_exist {
  bool is_exist;
  struct opendal_error *error;
} opendal_result_is_exist;
typedef struct opendal_result_exists {
  bool exists;
  struct opendal_error *error;
} opendal_result_exists;
typedef struct opendal_result_stat {
  struct opendal_metadata *meta;
  struct opendal_error *error;
} opendal_result_stat;
typedef struct opendal_stat_options {
  const char *version;
  const char *if_match;
  const char *if_none_match;
  bool has_if_modified_since;
  int64_t if_modified_since;
  bool has_if_unmodified_since;
  int64_t if_unmodified_since;
  const char *override_content_type;
  const char *override_cache_control;
  const char *override_content_disposition;
} opendal_stat_options;
typedef struct opendal_result_list {
  struct opendal_lister *lister;
  struct opendal_error *error;
} opendal_result_list;
typedef struct opendal_list_options {
  bool recursive;
  uintptr_t limit;
  char *start_after;
  bool versions;
  bool deleted;
} opendal_list_options;
typedef struct opendal_copy_options {
  bool if_not_exists;
  const char *if_match;
  const char *source_version;
  bool has_source_content_length_hint;
  uint64_t source_content_length_hint;
  uintptr_t concurrent;
  bool has_chunk;
  uintptr_t chunk;
} opendal_copy_options;
typedef struct opendal_copier {
  void *inner;
} opendal_copier;
typedef struct opendal_result_operator_copier {
  struct opendal_copier *copier;
  struct opendal_error *error;
} opendal_result_operator_copier;
typedef struct opendal_operator_info {
  void *inner;
} opendal_operator_info;
typedef struct opendal_capability {
  bool stat;
  bool stat_with_if_match;
  bool stat_with_if_none_match;
  bool stat_with_if_modified_since;
  bool stat_with_if_unmodified_since;
  bool stat_with_override_cache_control;
  bool stat_with_override_content_disposition;
  bool stat_with_override_content_type;
  bool stat_with_version;
  bool read;
  bool read_with_if_match;
  bool read_with_if_none_match;
  bool read_with_override_cache_control;
  bool read_with_override_content_disposition;
  bool read_with_override_content_type;
  bool read_with_if_modified_since;
  bool read_with_if_unmodified_since;
  bool read_with_version;
  bool write;
  bool write_can_multi;
  bool write_can_empty;
  bool write_can_append;
  bool write_with_content_type;
  bool write_with_content_disposition;
  bool write_with_content_encoding;
  bool write_with_cache_control;
  bool write_with_if_match;
  bool write_with_if_none_match;
  bool write_with_if_not_exists;
  bool write_with_user_metadata;
  uintptr_t write_multi_max_size;
  uintptr_t write_multi_min_size;
  uintptr_t write_total_max_size;
  bool create_dir;
  bool delete_;
  bool delete_with_version;
  bool delete_with_recursive;
  bool copy;
  bool copy_with_if_not_exists;
  bool copy_with_if_match;
  bool copy_with_source_version;
  bool copy_can_multi;
  uintptr_t copy_multi_max_size;
  uintptr_t copy_multi_min_size;
  bool rename;
  bool list;
  bool list_with_limit;
  bool list_with_start_after;
  bool list_with_recursive;
  bool list_with_versions;
  bool list_with_deleted;
  bool presign;
  bool presign_read;
  bool presign_stat;
  bool presign_write;
  bool presign_delete;
  bool shared;
} opendal_capability;
typedef struct opendal_presigned_request {
  struct opendal_presigned_request_inner *inner;
} opendal_presigned_request;
typedef struct opendal_result_presign {
  struct opendal_presigned_request *req;
  struct opendal_error *error;
} opendal_result_presign;
typedef struct opendal_http_header_pair {
  const char *key;
  const char *value;
} opendal_http_header_pair;
typedef struct opendal_result_reader_read {
  uintptr_t size;
  struct opendal_error *error;
} opendal_result_reader_read;
typedef struct opendal_result_reader_seek {
  uint64_t pos;
  struct opendal_error *error;
} opendal_result_reader_seek;
typedef struct opendal_result_writer_write {
  uintptr_t size;
  struct opendal_error *error;
} opendal_result_writer_write;
typedef struct opendal_result_copier_next {
  uintptr_t size;
  bool has_next;
  struct opendal_error *error;
} opendal_result_copier_next;

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t opendal_handle = NULL;
static int opendal_init_state = 0;

static void *opendal_sym(const char *name) {
    return opendal_handle ? get_sym(opendal_handle, name) : NULL;
}

static int opendal_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;

    if (opendal_handle) return 0;

    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("OPENDAL_DYN_LIB");
        if (e && *e) {
            snprintf(bufs[n], sizeof bufs[0], "%s", e);
            candidates[n] = bufs[n]; n++;
        }
    }
    {
#ifdef _WIN32
        const char *env_path = getenv("PATH");
#elif defined(__APPLE__)
        const char *env_path = getenv("DYLD_LIBRARY_PATH");
#else
        const char *env_path = getenv("LD_LIBRARY_PATH");
#endif
        if (env_path && *env_path) {
            char *copy = strdup(env_path);
            if (copy) {
                char *q = copy;
                while (q && *q && n < 80) {
                    char *sep = strchr(q, PATH_SEP);
                    if (sep) *sep = 0;
                    if (*q) {
                        snprintf(bufs[n], sizeof bufs[0], "%s/libopendal_c.so", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libopendal.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libopendal_c.so";
    candidates[n++] = "libopendal.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libopendal_c.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libopendal_c.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libopendal_c.dylib";
    candidates[n++] = "/usr/local/lib/libopendal_c.dylib";
#elif defined(_WIN32)
    candidates[n++] = "libopendal_c.dll";
#else
    candidates[n++] = "/usr/local/lib/libopendal_c.so";
    candidates[n++] = "/usr/lib/libopendal_c.so";
#endif

    for (i = 0; i < n; i++) {
        opendal_handle = load_lib(candidates[i]);
        if (opendal_handle) break;
    }
    if (!opendal_handle || !opendal_sym("opendal_operator_new")) {
        fprintf(stderr, "opendal_dyn: cannot load opendal (%s)\n", lib_error());
        if (opendal_handle) { free_lib(opendal_handle); opendal_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "opendal_dyn: loaded opendal\n");
    return 0;
}

int opendal_dyn_libok(void) {
    if (opendal_init_state == 0)
        opendal_init_state = (opendal_do_load(NULL) == 0) ? 1 : -1;
    return (opendal_init_state == 1 && opendal_handle != NULL) ? 1 : 0;
}

int opendal_dyn_init(const char *library_path) {
    if (opendal_do_load(library_path) == 0) {
        opendal_init_state = 1;
        return 0;
    }
    if (opendal_init_state == 0) opendal_init_state = -1;
    return -1;
}

void opendal_dyn_cleanup(void) {
    if (!opendal_handle) opendal_init_state = 0;
    else if (opendal_init_state == -1) opendal_init_state = 0;
}

int opendal_dyn_has_symbol(const char *name) {
    if (!opendal_dyn_libok()) return 0;
    return opendal_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                          \
    static ret (*fn) params = NULL;                                        \
    if (!fn) {                                                             \
        if (!opendal_dyn_libok()) return fallback;                         \
        *(void **)(&fn) = opendal_sym(#name);                              \
        if (!fn) {                                                         \
            fprintf(stderr, "opendal_dyn: dlsym failed: %s\n", #name);     \
            return fallback;                                               \
        }                                                                  \
    }                                                                      \
    return fn args;                                                        \
}

#define WRAP_VOID(name, params, args)                                      \
void name params {                                                         \
    static void (*fn) params = NULL;                                       \
    if (!fn) {                                                             \
        if (!opendal_dyn_libok()) return;                                  \
        *(void **)(&fn) = opendal_sym(#name);                              \
        if (!fn) {                                                         \
            fprintf(stderr, "opendal_dyn: dlsym failed: %s\n", #name);     \
            return;                                                        \
        }                                                                  \
    }                                                                      \
    fn args;                                                               \
}

/* ============================================================
 * 导出函数封装（opendal.h 全部 161 个，顺序与头文件一致）
 * ============================================================ */

/* --- 错误、lister、layers --- */
WRAP_VOID(opendal_error_free, (struct opendal_error *ptr), (ptr))
WRAP_RET(struct opendal_result_lister_next, opendal_lister_next, (struct opendal_lister *self), (self), ((struct opendal_result_lister_next){0}))
WRAP_VOID(opendal_lister_free, (struct opendal_lister *ptr), (ptr))
WRAP_RET(struct opendal_operator_layers *, opendal_operator_layers_new, (void), (), NULL)
WRAP_VOID(opendal_operator_layers_add_retry, (struct opendal_operator_layers *self, bool jitter, float factor, uint64_t min_delay_ns, uint64_t max_delay_ns, uint64_t max_times), (self, jitter, factor, min_delay_ns, max_delay_ns, max_times))
WRAP_VOID(opendal_operator_layers_add_timeout, (struct opendal_operator_layers *self, uint64_t timeout_ns, uint64_t io_timeout_ns), (self, timeout_ns, io_timeout_ns))
WRAP_VOID(opendal_operator_layers_free, (struct opendal_operator_layers *ptr), (ptr))

/* --- metadata --- */
WRAP_VOID(opendal_metadata_free, (struct opendal_metadata *ptr), (ptr))
WRAP_RET(uint8_t, opendal_metadata_mode, (const struct opendal_metadata *self), (self), 0)
WRAP_RET(uint64_t, opendal_metadata_content_length, (const struct opendal_metadata *self), (self), 0)
WRAP_RET(bool, opendal_metadata_is_file, (const struct opendal_metadata *self), (self), false)
WRAP_RET(bool, opendal_metadata_is_dir, (const struct opendal_metadata *self), (self), false)
WRAP_RET(uint8_t, opendal_metadata_is_current, (const struct opendal_metadata *self), (self), 0)
WRAP_RET(bool, opendal_metadata_is_deleted, (const struct opendal_metadata *self), (self), false)
WRAP_RET(char *, opendal_metadata_cache_control, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(char *, opendal_metadata_content_disposition, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(char *, opendal_metadata_content_md5, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(char *, opendal_metadata_content_type, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(char *, opendal_metadata_content_encoding, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(char *, opendal_metadata_etag, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(char *, opendal_metadata_version, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(struct opendal_metadata_user_metadata *, opendal_metadata_get_user_metadata, (const struct opendal_metadata *self), (self), NULL)
WRAP_RET(int64_t, opendal_metadata_last_modified_ms, (const struct opendal_metadata *self), (self), 0)
WRAP_RET(const struct opendal_metadata_user_metadata_pair *, opendal_metadata_user_metadata_pairs, (const struct opendal_metadata_user_metadata *metadata), (metadata), NULL)
WRAP_RET(uintptr_t, opendal_metadata_user_metadata_len, (const struct opendal_metadata_user_metadata *metadata), (metadata), 0)
WRAP_VOID(opendal_metadata_user_metadata_free, (struct opendal_metadata_user_metadata *metadata), (metadata))

/* --- operator 与 info/capability --- */
WRAP_VOID(opendal_operator_free, (const struct opendal_operator *ptr), (ptr))
WRAP_RET(struct opendal_result_operator_new, opendal_operator_new, (const char *scheme, const struct opendal_operator_options *options), (scheme, options), ((struct opendal_result_operator_new){0}))
WRAP_RET(struct opendal_result_operator_new, opendal_operator_new_with_layers, (const char *scheme, const struct opendal_operator_options *options, const struct opendal_operator_layers *layers), (scheme, options, layers), ((struct opendal_result_operator_new){0}))
WRAP_RET(struct opendal_error *, opendal_operator_write, (const struct opendal_operator *op, const char *path, const struct opendal_bytes *bytes), (op, path, bytes), NULL)
WRAP_RET(struct opendal_error *, opendal_operator_write_with, (const struct opendal_operator *op, const char *path, const struct opendal_bytes *bytes, const struct opendal_write_options *opts), (op, path, bytes, opts), NULL)
WRAP_RET(struct opendal_result_write, opendal_operator_write_with_metadata, (const struct opendal_operator *op, const char *path, const struct opendal_bytes *bytes, const struct opendal_write_options *opts), (op, path, bytes, opts), ((struct opendal_result_write){0}))
WRAP_RET(struct opendal_result_read, opendal_operator_read, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_read){0}))
WRAP_RET(struct opendal_result_read, opendal_operator_read_with, (const struct opendal_operator *op, const char *path, const struct opendal_read_options *opts), (op, path, opts), ((struct opendal_result_read){0}))
WRAP_RET(struct opendal_result_operator_reader, opendal_operator_reader, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_operator_reader){0}))
WRAP_RET(struct opendal_result_operator_reader, opendal_operator_reader_with, (const struct opendal_operator *op, const char *path, const struct opendal_reader_options *opts), (op, path, opts), ((struct opendal_result_operator_reader){0}))
WRAP_RET(struct opendal_result_operator_writer, opendal_operator_writer, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_operator_writer){0}))
WRAP_RET(struct opendal_result_operator_writer, opendal_operator_writer_with, (const struct opendal_operator *op, const char *path, const struct opendal_write_options *opts), (op, path, opts), ((struct opendal_result_operator_writer){0}))
WRAP_RET(struct opendal_error *, opendal_operator_delete, (const struct opendal_operator *op, const char *path), (op, path), NULL)
WRAP_RET(struct opendal_error *, opendal_operator_delete_with, (const struct opendal_operator *op, const char *path, const struct opendal_delete_options *opts), (op, path, opts), NULL)
WRAP_RET(struct opendal_result_is_exist, opendal_operator_is_exist, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_is_exist){0}))
WRAP_RET(struct opendal_result_exists, opendal_operator_exists, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_exists){0}))
WRAP_RET(struct opendal_result_stat, opendal_operator_stat, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_stat){0}))
WRAP_RET(struct opendal_result_stat, opendal_operator_stat_with, (const struct opendal_operator *op, const char *path, const struct opendal_stat_options *opts), (op, path, opts), ((struct opendal_result_stat){0}))
WRAP_RET(struct opendal_result_list, opendal_operator_list, (const struct opendal_operator *op, const char *path), (op, path), ((struct opendal_result_list){0}))
WRAP_RET(struct opendal_result_list, opendal_operator_list_with, (const struct opendal_operator *op, const char *path, const struct opendal_list_options *opts), (op, path, opts), ((struct opendal_result_list){0}))
WRAP_RET(struct opendal_error *, opendal_operator_create_dir, (const struct opendal_operator *op, const char *path), (op, path), NULL)
WRAP_RET(struct opendal_error *, opendal_operator_rename, (const struct opendal_operator *op, const char *src, const char *dest), (op, src, dest), NULL)
WRAP_RET(struct opendal_error *, opendal_operator_copy, (const struct opendal_operator *op, const char *src, const char *dest), (op, src, dest), NULL)
WRAP_RET(struct opendal_error *, opendal_operator_copy_with, (const struct opendal_operator *op, const char *src, const char *dest, const struct opendal_copy_options *opts), (op, src, dest, opts), NULL)
WRAP_RET(struct opendal_error *, opendal_operator_check, (const struct opendal_operator *op), (op), NULL)
WRAP_RET(struct opendal_result_operator_copier, opendal_operator_copier, (const struct opendal_operator *op, const char *src, const char *dest), (op, src, dest), ((struct opendal_result_operator_copier){0}))
WRAP_RET(struct opendal_result_operator_copier, opendal_operator_copier_with, (const struct opendal_operator *op, const char *src, const char *dest, const struct opendal_copy_options *opts), (op, src, dest, opts), ((struct opendal_result_operator_copier){0}))
WRAP_RET(struct opendal_operator_info *, opendal_operator_info_new, (const struct opendal_operator *op), (op), NULL)
WRAP_VOID(opendal_operator_info_free, (struct opendal_operator_info *ptr), (ptr))
WRAP_RET(char *, opendal_operator_info_get_scheme, (const struct opendal_operator_info *self), (self), NULL)
WRAP_RET(char *, opendal_operator_info_get_root, (const struct opendal_operator_info *self), (self), NULL)
WRAP_RET(char *, opendal_operator_info_get_name, (const struct opendal_operator_info *self), (self), NULL)
WRAP_RET(struct opendal_capability, opendal_operator_info_get_capability, (const struct opendal_operator_info *self), (self), ((struct opendal_capability){0}))

/* --- presign 与 presigned_request --- */
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_read, (const struct opendal_operator *op, const char *path, uint64_t expire_secs), (op, path, expire_secs), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_read_with, (const struct opendal_operator *op, const char *path, uint64_t expire_secs, const struct opendal_read_options *opts), (op, path, expire_secs, opts), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_write, (const struct opendal_operator *op, const char *path, uint64_t expire_secs), (op, path, expire_secs), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_write_with, (const struct opendal_operator *op, const char *path, uint64_t expire_secs, const struct opendal_write_options *opts), (op, path, expire_secs, opts), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_delete, (const struct opendal_operator *op, const char *path, uint64_t expire_secs), (op, path, expire_secs), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_delete_with, (const struct opendal_operator *op, const char *path, uint64_t expire_secs, const struct opendal_delete_options *opts), (op, path, expire_secs, opts), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_stat, (const struct opendal_operator *op, const char *path, uint64_t expire_secs), (op, path, expire_secs), ((struct opendal_result_presign){0}))
WRAP_RET(struct opendal_result_presign, opendal_operator_presign_stat_with, (const struct opendal_operator *op, const char *path, uint64_t expire_secs, const struct opendal_stat_options *opts), (op, path, expire_secs, opts), ((struct opendal_result_presign){0}))
WRAP_RET(const char *, opendal_presigned_request_method, (const struct opendal_presigned_request *req), (req), NULL)
WRAP_RET(const char *, opendal_presigned_request_uri, (const struct opendal_presigned_request *req), (req), NULL)
WRAP_RET(const struct opendal_http_header_pair *, opendal_presigned_request_headers, (const struct opendal_presigned_request *req), (req), NULL)
WRAP_RET(uintptr_t, opendal_presigned_request_headers_len, (const struct opendal_presigned_request *req), (req), 0)
WRAP_VOID(opendal_presigned_request_free, (struct opendal_presigned_request *req), (req))

/* --- 通用释放 --- */
WRAP_VOID(opendal_string_free, (char *ptr), (ptr))
WRAP_VOID(opendal_bytes_free, (struct opendal_bytes *ptr), (ptr))

/* --- list_options --- */
WRAP_RET(struct opendal_list_options *, opendal_list_options_new, (void), (), NULL)
WRAP_VOID(opendal_list_options_set_recursive, (struct opendal_list_options *opts, bool recursive), (opts, recursive))
WRAP_VOID(opendal_list_options_set_limit, (struct opendal_list_options *opts, uintptr_t limit), (opts, limit))
WRAP_VOID(opendal_list_options_set_start_after, (struct opendal_list_options *opts, const char *start_after), (opts, start_after))
WRAP_VOID(opendal_list_options_set_versions, (struct opendal_list_options *opts, bool versions), (opts, versions))
WRAP_VOID(opendal_list_options_set_deleted, (struct opendal_list_options *opts, bool deleted), (opts, deleted))
WRAP_VOID(opendal_list_options_free, (struct opendal_list_options *opts), (opts))

/* --- delete_options --- */
WRAP_RET(struct opendal_delete_options *, opendal_delete_options_new, (void), (), NULL)
WRAP_VOID(opendal_delete_options_set_version, (struct opendal_delete_options *opts, const char *version), (opts, version))
WRAP_VOID(opendal_delete_options_set_recursive, (struct opendal_delete_options *opts, bool recursive), (opts, recursive))
WRAP_VOID(opendal_delete_options_free, (struct opendal_delete_options *opts), (opts))

/* --- write_options --- */
WRAP_RET(struct opendal_write_options *, opendal_write_options_new, (void), (), NULL)
WRAP_VOID(opendal_write_options_free, (struct opendal_write_options *opts), (opts))
WRAP_VOID(opendal_write_options_set_append, (struct opendal_write_options *opts, bool append), (opts, append))
WRAP_VOID(opendal_write_options_set_cache_control, (struct opendal_write_options *opts, const char *cache_control), (opts, cache_control))
WRAP_VOID(opendal_write_options_set_content_type, (struct opendal_write_options *opts, const char *content_type), (opts, content_type))
WRAP_VOID(opendal_write_options_set_content_disposition, (struct opendal_write_options *opts, const char *content_disposition), (opts, content_disposition))
WRAP_VOID(opendal_write_options_set_content_encoding, (struct opendal_write_options *opts, const char *content_encoding), (opts, content_encoding))
WRAP_VOID(opendal_write_options_set_if_match, (struct opendal_write_options *opts, const char *if_match), (opts, if_match))
WRAP_VOID(opendal_write_options_set_if_none_match, (struct opendal_write_options *opts, const char *if_none_match), (opts, if_none_match))
WRAP_VOID(opendal_write_options_set_if_not_exists, (struct opendal_write_options *opts, bool if_not_exists), (opts, if_not_exists))
WRAP_VOID(opendal_write_options_set_concurrent, (struct opendal_write_options *opts, uintptr_t concurrent), (opts, concurrent))
WRAP_VOID(opendal_write_options_set_chunk, (struct opendal_write_options *opts, uintptr_t chunk), (opts, chunk))
WRAP_VOID(opendal_write_options_set_user_metadata, (struct opendal_write_options *opts, const struct opendal_write_user_metadata_pair *pairs, uintptr_t len), (opts, pairs, len))

/* --- stat_options --- */
WRAP_RET(struct opendal_stat_options *, opendal_stat_options_new, (void), (), NULL)
WRAP_VOID(opendal_stat_options_free, (struct opendal_stat_options *opts), (opts))
WRAP_VOID(opendal_stat_options_set_version, (struct opendal_stat_options *opts, const char *version), (opts, version))
WRAP_VOID(opendal_stat_options_set_if_match, (struct opendal_stat_options *opts, const char *if_match), (opts, if_match))
WRAP_VOID(opendal_stat_options_set_if_none_match, (struct opendal_stat_options *opts, const char *if_none_match), (opts, if_none_match))
WRAP_VOID(opendal_stat_options_set_if_modified_since, (struct opendal_stat_options *opts, int64_t if_modified_since), (opts, if_modified_since))
WRAP_VOID(opendal_stat_options_set_if_unmodified_since, (struct opendal_stat_options *opts, int64_t if_unmodified_since), (opts, if_unmodified_since))
WRAP_VOID(opendal_stat_options_set_override_content_type, (struct opendal_stat_options *opts, const char *override_content_type), (opts, override_content_type))
WRAP_VOID(opendal_stat_options_set_override_cache_control, (struct opendal_stat_options *opts, const char *override_cache_control), (opts, override_cache_control))
WRAP_VOID(opendal_stat_options_set_override_content_disposition, (struct opendal_stat_options *opts, const char *override_content_disposition), (opts, override_content_disposition))

/* --- read_options --- */
WRAP_RET(struct opendal_read_options *, opendal_read_options_new, (void), (), NULL)
WRAP_VOID(opendal_read_options_free, (struct opendal_read_options *opts), (opts))
WRAP_VOID(opendal_read_options_set_range, (struct opendal_read_options *opts, uint64_t offset, uint64_t length), (opts, offset, length))
WRAP_VOID(opendal_read_options_set_range_from, (struct opendal_read_options *opts, uint64_t offset), (opts, offset))
WRAP_VOID(opendal_read_options_set_version, (struct opendal_read_options *opts, const char *version), (opts, version))
WRAP_VOID(opendal_read_options_set_if_match, (struct opendal_read_options *opts, const char *if_match), (opts, if_match))
WRAP_VOID(opendal_read_options_set_if_none_match, (struct opendal_read_options *opts, const char *if_none_match), (opts, if_none_match))
WRAP_VOID(opendal_read_options_set_if_modified_since, (struct opendal_read_options *opts, int64_t if_modified_since), (opts, if_modified_since))
WRAP_VOID(opendal_read_options_set_if_unmodified_since, (struct opendal_read_options *opts, int64_t if_unmodified_since), (opts, if_unmodified_since))
WRAP_VOID(opendal_read_options_set_concurrent, (struct opendal_read_options *opts, uintptr_t concurrent), (opts, concurrent))
WRAP_VOID(opendal_read_options_set_chunk, (struct opendal_read_options *opts, uintptr_t chunk), (opts, chunk))
WRAP_VOID(opendal_read_options_set_gap, (struct opendal_read_options *opts, uintptr_t gap), (opts, gap))
WRAP_VOID(opendal_read_options_set_content_length_hint, (struct opendal_read_options *opts, uint64_t content_length_hint), (opts, content_length_hint))
WRAP_VOID(opendal_read_options_set_override_content_type, (struct opendal_read_options *opts, const char *override_content_type), (opts, override_content_type))
WRAP_VOID(opendal_read_options_set_override_cache_control, (struct opendal_read_options *opts, const char *override_cache_control), (opts, override_cache_control))
WRAP_VOID(opendal_read_options_set_override_content_disposition, (struct opendal_read_options *opts, const char *override_content_disposition), (opts, override_content_disposition))

/* --- reader_options --- */
WRAP_RET(struct opendal_reader_options *, opendal_reader_options_new, (void), (), NULL)
WRAP_VOID(opendal_reader_options_free, (struct opendal_reader_options *opts), (opts))
WRAP_VOID(opendal_reader_options_set_version, (struct opendal_reader_options *opts, const char *version), (opts, version))
WRAP_VOID(opendal_reader_options_set_if_match, (struct opendal_reader_options *opts, const char *if_match), (opts, if_match))
WRAP_VOID(opendal_reader_options_set_if_none_match, (struct opendal_reader_options *opts, const char *if_none_match), (opts, if_none_match))
WRAP_VOID(opendal_reader_options_set_if_modified_since, (struct opendal_reader_options *opts, int64_t if_modified_since), (opts, if_modified_since))
WRAP_VOID(opendal_reader_options_set_if_unmodified_since, (struct opendal_reader_options *opts, int64_t if_unmodified_since), (opts, if_unmodified_since))
WRAP_VOID(opendal_reader_options_set_content_length_hint, (struct opendal_reader_options *opts, uint64_t content_length_hint), (opts, content_length_hint))
WRAP_VOID(opendal_reader_options_set_concurrent, (struct opendal_reader_options *opts, uintptr_t concurrent), (opts, concurrent))
WRAP_VOID(opendal_reader_options_set_chunk, (struct opendal_reader_options *opts, uintptr_t chunk), (opts, chunk))
WRAP_VOID(opendal_reader_options_set_gap, (struct opendal_reader_options *opts, uintptr_t gap), (opts, gap))
WRAP_VOID(opendal_reader_options_set_prefetch, (struct opendal_reader_options *opts, uintptr_t prefetch), (opts, prefetch))

/* --- copy_options --- */
WRAP_RET(struct opendal_copy_options *, opendal_copy_options_new, (void), (), NULL)
WRAP_VOID(opendal_copy_options_free, (struct opendal_copy_options *opts), (opts))
WRAP_VOID(opendal_copy_options_set_if_not_exists, (struct opendal_copy_options *opts, bool if_not_exists), (opts, if_not_exists))
WRAP_VOID(opendal_copy_options_set_if_match, (struct opendal_copy_options *opts, const char *if_match), (opts, if_match))
WRAP_VOID(opendal_copy_options_set_source_version, (struct opendal_copy_options *opts, const char *source_version), (opts, source_version))
WRAP_VOID(opendal_copy_options_set_source_content_length_hint, (struct opendal_copy_options *opts, uint64_t source_content_length_hint), (opts, source_content_length_hint))
WRAP_VOID(opendal_copy_options_set_concurrent, (struct opendal_copy_options *opts, uintptr_t concurrent), (opts, concurrent))
WRAP_VOID(opendal_copy_options_set_chunk, (struct opendal_copy_options *opts, uintptr_t chunk), (opts, chunk))

/* --- operator_options --- */
WRAP_RET(struct opendal_operator_options *, opendal_operator_options_new, (void), (), NULL)
WRAP_VOID(opendal_operator_options_set, (struct opendal_operator_options *self, const char *key, const char *value), (self, key, value))
WRAP_VOID(opendal_operator_options_free, (struct opendal_operator_options *ptr), (ptr))

/* --- entry --- */
WRAP_RET(char *, opendal_entry_path, (const struct opendal_entry *self), (self), NULL)
WRAP_RET(char *, opendal_entry_name, (const struct opendal_entry *self), (self), NULL)
WRAP_RET(struct opendal_metadata *, opendal_entry_metadata, (const struct opendal_entry *self), (self), NULL)
WRAP_VOID(opendal_entry_free, (struct opendal_entry *ptr), (ptr))

/* --- reader / writer / copier --- */
WRAP_RET(struct opendal_result_reader_read, opendal_reader_read, (struct opendal_reader *self, uint8_t *buf, uintptr_t len), (self, buf, len), ((struct opendal_result_reader_read){0}))
WRAP_RET(struct opendal_result_reader_seek, opendal_reader_seek, (struct opendal_reader *self, int64_t offset, int32_t whence), (self, offset, whence), ((struct opendal_result_reader_seek){0}))
WRAP_VOID(opendal_reader_free, (struct opendal_reader *ptr), (ptr))
WRAP_RET(struct opendal_result_writer_write, opendal_writer_write, (struct opendal_writer *self, const struct opendal_bytes *bytes), (self, bytes), ((struct opendal_result_writer_write){0}))
WRAP_RET(struct opendal_error *, opendal_writer_close, (struct opendal_writer *ptr), (ptr), NULL)
WRAP_RET(struct opendal_result_write, opendal_writer_close_with_metadata, (struct opendal_writer *ptr), (ptr), ((struct opendal_result_write){0}))
WRAP_VOID(opendal_writer_free, (struct opendal_writer *ptr), (ptr))
WRAP_RET(struct opendal_result_copier_next, opendal_copier_next, (struct opendal_copier *self), (self), ((struct opendal_result_copier_next){0}))
WRAP_RET(struct opendal_error *, opendal_copier_abort, (struct opendal_copier *self), (self), NULL)
WRAP_VOID(opendal_copier_free, (struct opendal_copier *ptr), (ptr))
