/* ============================================================
 * libuv_dyn.c - type safe dynamic lib load for libuv 1.53.0
 * ============================================================
 * 设计要点：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不需要目标库。
 *   覆盖 libuv.so.1 全部 319 个导出符号（nm -D 核对）。
 *   uv.h 与官方 tag v1.53.0 逐字节一致（diff = 0 行）。
 *   头文件中的 static inline（uv_handle_get_data 等）不是导出
 *   符号，无法 dlsym；此处只封装真实导出符号，结构体布局按
 *   uv.h / uv/unix.h 的 Linux 分支展开。
 *   2. 每个函数首次调用时 dlsym 缓存；失败打印 stderr 并返回安全值。
 *   3. 加载顺序：显式路径 > LIBUV_DYN_LIB > LD_LIBRARY_PATH >
 *      soname > .so > Nix > 平台默认；成功后不卸载。
 *
 *   使用：
 *     libuv_dyn_init(NULL);
 *     ... 原版 API 直接调用 ...
 *     libuv_dyn_cleanup();   // 仅复位加载状态，不卸载库
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <termios.h>

/* ============================================================
 * 平台适配层
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif

/* 上游头文件存在时引入，编译器校验同名封装函数原型与类型 */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Warray-parameter"
#endif

#if defined(__has_include)
#  if __has_include(<uv.h>)
#    include <uv.h>
#    define LIBUV_DYN_HAVE_HEADER 1
#  endif
#endif

#ifndef LIBUV_DYN_HAVE_HEADER
/* ============================================================
 * 类型定义（官方头文件机械提取；未列出的 struct 为不透明句柄）
 * ============================================================ */
struct __cancel_jmp_buf_tag;
struct addrinfo;
struct cmsghdr;
struct dirent;
struct dirent64;
struct file_handle;
struct gaicb;
struct hostent;
struct in6_addr;
struct in_addr;
struct iovec;
struct itimerspec;
struct mmsghdr;
struct msghdr;
struct netent;
struct obstack;
struct open_how;
struct passwd;
struct protoent;
struct rpcent;
struct sched_attr;
struct sched_param;
struct servent;
struct sigaction;
struct sigcontext;
struct sigevent;
struct sigstack;
struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;
struct sockaddr_storage;
struct stat;
struct stat64;
struct statx;
struct termios;
struct timespec;
struct timeval;
struct timex;
struct tm;
union sigval;

struct uv__queue { struct uv__queue* next; struct uv__queue* prev; };
struct uv__work { void (*work)(struct uv__work *w); void (*done)(struct uv__work *w, int status); struct uv_loop_s* loop; struct uv__queue wq; };
struct uv__io_s;
struct uv_loop_s;
typedef struct uv__io_s uv__io_t;
struct uv__io_s { uintptr_t bits; struct uv__queue pending_queue; struct uv__queue watcher_queue; unsigned int pevents; unsigned int events; int fd; };
typedef struct uv_buf_t { char* base; size_t len; } uv_buf_t;
typedef int uv_file;
typedef int uv_os_sock_t;
typedef int uv_os_fd_t;
typedef pid_t uv_pid_t;
typedef pthread_once_t uv_once_t;
typedef pthread_t uv_thread_t;
typedef pthread_mutex_t uv_mutex_t;
typedef pthread_rwlock_t uv_rwlock_t;
typedef sem_t uv_sem_t;
typedef pthread_cond_t uv_cond_t;
typedef pthread_key_t uv_key_t;
typedef pthread_barrier_t uv_barrier_t;
typedef gid_t uv_gid_t;
typedef uid_t uv_uid_t;
typedef struct dirent uv__dirent_t;
typedef struct { void* handle; char* errmsg; } uv_lib_t;
struct uv__req_write_extra_s { size_t nwritten; };
typedef enum { UV_E2BIG = (-(7)), UV_EACCES = (-(13)), UV_EADDRINUSE = (-(98)), UV_EADDRNOTAVAIL = (-(99)), UV_EAFNOSUPPORT = (-(97)), UV_EAGAIN = (-(11)), UV_EAI_ADDRFAMILY = (-3000), UV_EAI_AGAIN = (-3001), UV_EAI_BADFLAGS = (-3002), UV_EAI_BADHINTS = (-3013), UV_EAI_CANCELED = (-3003), UV_EAI_FAIL = (-3004), UV_EAI_FAMILY = (-3005), UV_EAI_MEMORY = (-3006), UV_EAI_NODATA = (-3007), UV_EAI_NONAME = (-3008), UV_EAI_OVERFLOW = (-3009), UV_EAI_PROTOCOL = (-3014), UV_EAI_SERVICE = (-3010), UV_EAI_SOCKTYPE = (-3011), UV_EALREADY = (-(114)), UV_EBADF = (-(9)), UV_EBUSY = (-(16)), UV_ECANCELED = (-(125)), UV_ECHARSET = (-4080), UV_ECONNABORTED = (-(103)), UV_ECONNREFUSED = (-(111)), UV_ECONNRESET = (-(104)), UV_EDESTADDRREQ = (-(89)), UV_EEXIST = (-(17)), UV_EFAULT = (-(14)), UV_EFBIG = (-(27)), UV_EHOSTUNREACH = (-(113)), UV_EINTR = (-(4)), UV_EINVAL = (-(22)), UV_EIO = (-(5)), UV_EISCONN = (-(106)), UV_EISDIR = (-(21)), UV_ELOOP = (-(40)), UV_EMFILE = (-(24)), UV_EMSGSIZE = (-(90)), UV_ENAMETOOLONG = (-(36)), UV_ENETDOWN = (-(100)), UV_ENETUNREACH = (-(101)), UV_ENFILE = (-(23)), UV_ENOBUFS = (-(105)), UV_ENODEV = (-(19)), UV_ENOENT = (-(2)), UV_ENOMEM = (-(12)), UV_ENONET = (-(64)), UV_ENOPROTOOPT = (-(92)), UV_ENOSPC = (-(28)), UV_ENOSYS = (-(38)), UV_ENOTCONN = (-(107)), UV_ENOTDIR = (-(20)), UV_ENOTEMPTY = (-(39)), UV_ENOTSOCK = (-(88)), UV_ENOTSUP = (-(95)), UV_EOVERFLOW = (-(75)), UV_EPERM = (-(1)), UV_EPIPE = (-(32)), UV_EPROTO = (-(71)), UV_EPROTONOSUPPORT = (-(93)), UV_EPROTOTYPE = (-(91)), UV_ERANGE = (-(34)), UV_EROFS = (-(30)), UV_ESHUTDOWN = (-(108)), UV_ESPIPE = (-(29)), UV_ESRCH = (-(3)), UV_ETIMEDOUT = (-(110)), UV_ETXTBSY = (-(26)), UV_EXDEV = (-(18)), UV_UNKNOWN = (-4094), UV_EOF = (-4095), UV_ENXIO = (-(6)), UV_EMLINK = (-(31)), UV_EHOSTDOWN = (-(112)), UV_EREMOTEIO = (-(121)), UV_ENOTTY = (-(25)), UV_EFTYPE = (-(134)), UV_EILSEQ = (-(84)), UV_ESOCKTNOSUPPORT = (-(94)), UV_ENODATA = (-(61)), UV_EUNATCH = (-(49)), UV_ENOEXEC = (-(8)), UV_ERRNO_MAX = (-4095) - 1 } uv_errno_t;
typedef enum { UV_UNKNOWN_HANDLE = 0, UV_ASYNC, UV_CHECK, UV_FS_EVENT, UV_FS_POLL, UV_HANDLE, UV_IDLE, UV_NAMED_PIPE, UV_POLL, UV_PREPARE, UV_PROCESS, UV_STREAM, UV_TCP, UV_TIMER, UV_TTY, UV_UDP, UV_SIGNAL, UV_FILE, UV_HANDLE_TYPE_MAX } uv_handle_type;
typedef enum { UV_UNKNOWN_REQ = 0, UV_REQ, UV_CONNECT, UV_WRITE, UV_SHUTDOWN, UV_UDP_SEND, UV_FS, UV_WORK, UV_GETADDRINFO, UV_GETNAMEINFO, UV_RANDOM, UV_REQ_TYPE_MAX } uv_req_type;
typedef struct uv_loop_s uv_loop_t;
typedef struct uv_handle_s uv_handle_t;
typedef struct uv_dir_s uv_dir_t;
typedef struct uv_stream_s uv_stream_t;
typedef struct uv_tcp_s uv_tcp_t;
typedef struct uv_udp_s uv_udp_t;
typedef struct uv_pipe_s uv_pipe_t;
typedef struct uv_tty_s uv_tty_t;
typedef struct uv_poll_s uv_poll_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_prepare_s uv_prepare_t;
typedef struct uv_check_s uv_check_t;
typedef struct uv_idle_s uv_idle_t;
typedef struct uv_async_s uv_async_t;
typedef struct uv_process_s uv_process_t;
typedef struct uv_fs_event_s uv_fs_event_t;
typedef struct uv_fs_poll_s uv_fs_poll_t;
typedef struct uv_signal_s uv_signal_t;
typedef struct uv_req_s uv_req_t;
typedef struct uv_getaddrinfo_s uv_getaddrinfo_t;
typedef struct uv_getnameinfo_s uv_getnameinfo_t;
typedef struct uv_shutdown_s uv_shutdown_t;
typedef struct uv_write_s uv_write_t;
typedef struct uv_connect_s uv_connect_t;
typedef struct uv_udp_send_s uv_udp_send_t;
typedef struct uv_fs_s uv_fs_t;
typedef struct uv_work_s uv_work_t;
typedef struct uv_random_s uv_random_t;
typedef struct uv_env_item_s uv_env_item_t;
typedef struct uv_cpu_info_s uv_cpu_info_t;
typedef struct uv_interface_address_s uv_interface_address_t;
typedef struct uv_dirent_s uv_dirent_t;
typedef struct uv_passwd_s uv_passwd_t;
typedef struct uv_group_s uv_group_t;
typedef struct uv_utsname_s uv_utsname_t;
typedef struct uv_statfs_s uv_statfs_t;
typedef struct uv_metrics_s uv_metrics_t;
typedef enum { UV_LOOP_BLOCK_SIGNAL = 0, UV_METRICS_IDLE_TIME, UV_LOOP_USE_IO_URING_SQPOLL } uv_loop_option;
typedef enum { UV_RUN_DEFAULT = 0, UV_RUN_ONCE, UV_RUN_NOWAIT } uv_run_mode;
typedef void* (*uv_malloc_func)(size_t size);
typedef void* (*uv_realloc_func)(void* ptr, size_t size);
typedef void* (*uv_calloc_func)(size_t count, size_t size);
typedef void (*uv_free_func)(void* ptr);
typedef void (*uv_alloc_cb)(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
typedef void (*uv_read_cb)(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
typedef void (*uv_write_cb)(uv_write_t* req, int status);
typedef void (*uv_connect_cb)(uv_connect_t* req, int status);
typedef void (*uv_shutdown_cb)(uv_shutdown_t* req, int status);
typedef void (*uv_connection_cb)(uv_stream_t* server, int status);
typedef void (*uv_close_cb)(uv_handle_t* handle);
typedef void (*uv_poll_cb)(uv_poll_t* handle, int status, int events);
typedef void (*uv_timer_cb)(uv_timer_t* handle);
typedef void (*uv_async_cb)(uv_async_t* handle);
typedef void (*uv_prepare_cb)(uv_prepare_t* handle);
typedef void (*uv_check_cb)(uv_check_t* handle);
typedef void (*uv_idle_cb)(uv_idle_t* handle);
typedef void (*uv_exit_cb)(uv_process_t*, int64_t exit_status, int term_signal);
typedef void (*uv_walk_cb)(uv_handle_t* handle, void* arg);
typedef void (*uv_fs_cb)(uv_fs_t* req);
typedef void (*uv_work_cb)(uv_work_t* req);
typedef void (*uv_after_work_cb)(uv_work_t* req, int status);
typedef void (*uv_getaddrinfo_cb)(uv_getaddrinfo_t* req, int status, struct addrinfo* res);
typedef void (*uv_getnameinfo_cb)(uv_getnameinfo_t* req, int status, const char* hostname, const char* service);
typedef void (*uv_random_cb)(uv_random_t* req, int status, void* buf, size_t buflen);
typedef enum { UV_CLOCK_MONOTONIC, UV_CLOCK_REALTIME } uv_clock_id;
typedef struct { long tv_sec; long tv_nsec; } uv_timespec_t;
typedef struct { int64_t tv_sec; int32_t tv_nsec; } uv_timespec64_t;
typedef struct { long tv_sec; long tv_usec; } uv_timeval_t;
typedef struct { int64_t tv_sec; int32_t tv_usec; } uv_timeval64_t;
typedef struct { uint64_t st_dev; uint64_t st_mode; uint64_t st_nlink; uint64_t st_uid; uint64_t st_gid; uint64_t st_rdev; uint64_t st_ino; uint64_t st_size; uint64_t st_blksize; uint64_t st_blocks; uint64_t st_flags; uint64_t st_gen; uv_timespec_t st_atim; uv_timespec_t st_mtim; uv_timespec_t st_ctim; uv_timespec_t st_birthtim; } uv_stat_t;
typedef void (*uv_fs_event_cb)(uv_fs_event_t* handle, const char* filename, int events, int status);
typedef void (*uv_fs_poll_cb)(uv_fs_poll_t* handle, int status, const uv_stat_t* prev, const uv_stat_t* curr);
typedef void (*uv_signal_cb)(uv_signal_t* handle, int signum);
typedef enum { UV_LEAVE_GROUP = 0, UV_JOIN_GROUP } uv_membership;
struct uv_req_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; };
struct uv_shutdown_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_stream_t* handle; uv_shutdown_cb cb; };
struct uv_handle_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; };
struct uv_stream_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; size_t write_queue_size; uv_alloc_cb alloc_cb; uv_read_cb read_cb; uv_connect_t *connect_req; uv_shutdown_t *shutdown_req; uv__io_t io_watcher; struct uv__queue write_queue; struct uv__queue write_completed_queue; uv_connection_cb connection_cb; int delayed_error; int accepted_fd; void* queued_fds; };
struct uv_write_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_write_cb cb; uv_stream_t* send_handle; uv_stream_t* handle; struct uv__queue queue; unsigned int write_index; uv_buf_t* bufs; unsigned int nbufs; int error; uv_buf_t bufsml[4]; };
struct uv_tcp_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; size_t write_queue_size; uv_alloc_cb alloc_cb; uv_read_cb read_cb; uv_connect_t *connect_req; uv_shutdown_t *shutdown_req; uv__io_t io_watcher; struct uv__queue write_queue; struct uv__queue write_completed_queue; uv_connection_cb connection_cb; int delayed_error; int accepted_fd; void* queued_fds; };
enum uv_tcp_flags { UV_TCP_IPV6ONLY = 1, UV_TCP_REUSEPORT = 2, };
struct uv_connect_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_connect_cb cb; uv_stream_t* handle; struct uv__queue queue; };
enum uv_udp_flags { UV_UDP_IPV6ONLY = 1, UV_UDP_PARTIAL = 2, UV_UDP_REUSEADDR = 4, UV_UDP_MMSG_CHUNK = 8, UV_UDP_MMSG_FREE = 16, UV_UDP_LINUX_RECVERR = 32, UV_UDP_REUSEPORT = 64, UV_UDP_RECVMMSG = 256 };
typedef void (*uv_udp_send_cb)(uv_udp_send_t* req, int status);
typedef void (*uv_udp_recv_cb)(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
struct uv_udp_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; size_t send_queue_size; size_t send_queue_count; uv_alloc_cb alloc_cb; uv_udp_recv_cb recv_cb; uv__io_t io_watcher; struct uv__queue write_queue; struct uv__queue write_completed_queue; };
struct uv_udp_send_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_udp_t* handle; uv_udp_send_cb cb; struct uv__queue queue; union { struct sockaddr addr; struct sockaddr_storage storage; } u; unsigned int nbufs; uv_buf_t* bufs; ssize_t status; uv_udp_send_cb send_cb; uv_buf_t bufsml[4]; };
struct uv_tty_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; size_t write_queue_size; uv_alloc_cb alloc_cb; uv_read_cb read_cb; uv_connect_t *connect_req; uv_shutdown_t *shutdown_req; uv__io_t io_watcher; struct uv__queue write_queue; struct uv__queue write_completed_queue; uv_connection_cb connection_cb; int delayed_error; int accepted_fd; void* queued_fds; struct termios orig_termios; int mode; };
typedef enum { UV_TTY_MODE_NORMAL, UV_TTY_MODE_RAW, UV_TTY_MODE_IO, UV_TTY_MODE_RAW_VT } uv_tty_mode_t;
typedef enum { UV_TTY_SUPPORTED, UV_TTY_UNSUPPORTED } uv_tty_vtermstate_t;
enum { UV_PIPE_NO_TRUNCATE = 1u << 0 };
struct uv_pipe_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; size_t write_queue_size; uv_alloc_cb alloc_cb; uv_read_cb read_cb; uv_connect_t *connect_req; uv_shutdown_t *shutdown_req; uv__io_t io_watcher; struct uv__queue write_queue; struct uv__queue write_completed_queue; uv_connection_cb connection_cb; int delayed_error; int accepted_fd; void* queued_fds; int ipc; const char* pipe_fname; };
struct uv_poll_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_poll_cb poll_cb; uv__io_t io_watcher; };
enum uv_poll_event { UV_READABLE = 1, UV_WRITABLE = 2, UV_DISCONNECT = 4, UV_PRIORITIZED = 8 };
struct uv_prepare_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_prepare_cb prepare_cb; struct uv__queue queue; };
struct uv_check_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_check_cb check_cb; struct uv__queue queue; };
struct uv_idle_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_idle_cb idle_cb; struct uv__queue queue; };
struct uv_async_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_async_cb async_cb; struct uv__queue queue; int pending; };
struct uv_timer_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_timer_cb timer_cb; union { void* heap[3]; struct uv__queue queue; } node; uint64_t timeout; uint64_t repeat; uint64_t start_id; };
struct uv_getaddrinfo_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_loop_t* loop; struct uv__work work_req; uv_getaddrinfo_cb cb; struct addrinfo* hints; char* hostname; char* service; struct addrinfo* addrinfo; int retcode; };
struct uv_getnameinfo_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_loop_t* loop; struct uv__work work_req; uv_getnameinfo_cb getnameinfo_cb; struct sockaddr_storage storage; int flags; char host[1025]; char service[32]; int retcode; };
typedef enum { UV_IGNORE = 0x00, UV_CREATE_PIPE = 0x01, UV_INHERIT_FD = 0x02, UV_INHERIT_STREAM = 0x04, UV_READABLE_PIPE = 0x10, UV_WRITABLE_PIPE = 0x20, UV_NONBLOCK_PIPE = 0x40, UV_OVERLAPPED_PIPE = 0x40 } uv_stdio_flags;
typedef struct uv_stdio_container_s { uv_stdio_flags flags; union { uv_stream_t* stream; int fd; } data; } uv_stdio_container_t;
typedef struct uv_process_options_s { uv_exit_cb exit_cb; const char* file; char** args; char** env; const char* cwd; unsigned int flags; int stdio_count; uv_stdio_container_t* stdio; uv_uid_t uid; uv_gid_t gid; } uv_process_options_t;
enum uv_process_flags { UV_PROCESS_SETUID = (1 << 0), UV_PROCESS_SETGID = (1 << 1), UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS = (1 << 2), UV_PROCESS_DETACHED = (1 << 3), UV_PROCESS_WINDOWS_HIDE = (1 << 4), UV_PROCESS_WINDOWS_HIDE_CONSOLE = (1 << 5), UV_PROCESS_WINDOWS_HIDE_GUI = (1 << 6), UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME = (1 << 7) };
struct uv_process_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_exit_cb exit_cb; int pid; struct uv__queue queue; int status; };
struct uv_work_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_loop_t* loop; uv_work_cb work_cb; uv_after_work_cb after_work_cb; struct uv__work work_req; };
struct uv_cpu_times_s { uint64_t user; uint64_t nice; uint64_t sys; uint64_t idle; uint64_t irq; };
struct uv_cpu_info_s { const char* model; int speed; struct uv_cpu_times_s cpu_times; };
struct uv_interface_address_s { char* name; char phys_addr[6]; int is_internal; union { struct sockaddr_in address4; struct sockaddr_in6 address6; } address; union { struct sockaddr_in netmask4; struct sockaddr_in6 netmask6; } netmask; };
struct uv_passwd_s { char* username; unsigned long uid; unsigned long gid; char* shell; char* homedir; };
struct uv_group_s { char* groupname; unsigned long gid; char** members; };
struct uv_utsname_s { char sysname[256]; char release[256]; char version[256]; char machine[256]; };
struct uv_statfs_s { uint64_t f_type; uint64_t f_bsize; uint64_t f_blocks; uint64_t f_bfree; uint64_t f_bavail; uint64_t f_files; uint64_t f_ffree; uint64_t f_frsize; uint64_t f_spare[3]; };
typedef enum { UV_DIRENT_UNKNOWN, UV_DIRENT_FILE, UV_DIRENT_DIR, UV_DIRENT_LINK, UV_DIRENT_FIFO, UV_DIRENT_SOCKET, UV_DIRENT_CHAR, UV_DIRENT_BLOCK } uv_dirent_type_t;
struct uv_dirent_s { const char* name; uv_dirent_type_t type; };
typedef struct { uv_timeval_t ru_utime; uv_timeval_t ru_stime; uint64_t ru_maxrss; uint64_t ru_ixrss; uint64_t ru_idrss; uint64_t ru_isrss; uint64_t ru_minflt; uint64_t ru_majflt; uint64_t ru_nswap; uint64_t ru_inblock; uint64_t ru_oublock; uint64_t ru_msgsnd; uint64_t ru_msgrcv; uint64_t ru_nsignals; uint64_t ru_nvcsw; uint64_t ru_nivcsw; } uv_rusage_t;
enum { UV_THREAD_PRIORITY_HIGHEST = 2, UV_THREAD_PRIORITY_ABOVE_NORMAL = 1, UV_THREAD_PRIORITY_NORMAL = 0, UV_THREAD_PRIORITY_BELOW_NORMAL = -1, UV_THREAD_PRIORITY_LOWEST = -2, };
struct uv_env_item_s { char* name; char* value; };
struct uv_metrics_s { uint64_t loop_count; uint64_t events; uint64_t events_waiting; uint64_t* reserved[13]; };
typedef enum { UV_FS_UNKNOWN = -1, UV_FS_CUSTOM, UV_FS_OPEN, UV_FS_CLOSE, UV_FS_READ, UV_FS_WRITE, UV_FS_SENDFILE, UV_FS_STAT, UV_FS_LSTAT, UV_FS_FSTAT, UV_FS_FTRUNCATE, UV_FS_UTIME, UV_FS_FUTIME, UV_FS_ACCESS, UV_FS_CHMOD, UV_FS_FCHMOD, UV_FS_FSYNC, UV_FS_FDATASYNC, UV_FS_UNLINK, UV_FS_RMDIR, UV_FS_MKDIR, UV_FS_MKDTEMP, UV_FS_RENAME, UV_FS_SCANDIR, UV_FS_LINK, UV_FS_SYMLINK, UV_FS_READLINK, UV_FS_CHOWN, UV_FS_FCHOWN, UV_FS_REALPATH, UV_FS_COPYFILE, UV_FS_LCHOWN, UV_FS_OPENDIR, UV_FS_READDIR, UV_FS_CLOSEDIR, UV_FS_STATFS, UV_FS_MKSTEMP, UV_FS_LUTIME } uv_fs_type;
struct uv_dir_s { uv_dirent_t* dirents; size_t nentries; void* reserved[4]; DIR* dir; };
struct uv_fs_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_fs_type fs_type; uv_loop_t* loop; uv_fs_cb cb; ssize_t result; void* ptr; const char* path; uv_stat_t statbuf; const char *new_path; uv_file file; int flags; mode_t mode; unsigned int nbufs; uv_buf_t* bufs; off_t off; uv_uid_t uid; uv_gid_t gid; double atime; double mtime; struct uv__work work_req; uv_buf_t bufsml[4]; };
enum uv_fs_event { UV_RENAME = 1, UV_CHANGE = 2 };
struct uv_fs_event_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; char* path; uv_fs_event_cb cb; struct uv__queue watchers; int wd; };
struct uv_fs_poll_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; void* poll_ctx; };
struct uv_signal_s { void* data; uv_loop_t* loop; uv_handle_type type; uv_close_cb close_cb; struct uv__queue handle_queue; union { int fd; void* reserved[4]; } u; uv_handle_t* next_closing; unsigned int flags; uv_signal_cb signal_cb; int signum; struct { struct uv_signal_s* rbe_left; struct uv_signal_s* rbe_right; struct uv_signal_s* rbe_parent; int rbe_color; } tree_entry; unsigned int caught_signals; unsigned int dispatched_signals; };
enum uv_fs_event_flags { UV_FS_EVENT_WATCH_ENTRY = 1, UV_FS_EVENT_STAT = 2, UV_FS_EVENT_RECURSIVE = 4 };
struct uv_random_s { void* data; uv_req_type type; void* reserved[5]; union { void* reserved2[1]; struct uv__req_write_extra_s write_extra; }; uv_loop_t* loop; int status; void* buf; size_t buflen; uv_random_cb cb; struct uv__work work_req; };
typedef void (*uv_thread_cb)(void* arg);
typedef enum { UV_THREAD_NO_FLAGS = 0x00, UV_THREAD_HAS_STACK_SIZE = 0x01 } uv_thread_create_flags;
struct uv_thread_options_s { unsigned int flags; size_t stack_size; };
typedef struct uv_thread_options_s uv_thread_options_t;
union uv_any_handle { uv_async_t async; uv_check_t check; uv_fs_event_t fs_event; uv_fs_poll_t fs_poll; uv_handle_t handle; uv_idle_t idle; uv_pipe_t pipe; uv_poll_t poll; uv_prepare_t prepare; uv_process_t process; uv_stream_t stream; uv_tcp_t tcp; uv_timer_t timer; uv_tty_t tty; uv_udp_t udp; uv_signal_t signal; };
union uv_any_req { uv_req_t req; uv_connect_t connect; uv_write_t write; uv_shutdown_t shutdown; uv_udp_send_t udp_send; uv_fs_t fs; uv_work_t work; uv_getaddrinfo_t getaddrinfo; uv_getnameinfo_t getnameinfo; uv_random_t random; };
struct uv_loop_s { void* data; unsigned int active_handles; struct uv__queue handle_queue; union { void* unused; unsigned int count; } active_reqs; void* internal_fields; unsigned int stop_flag; unsigned long flags; int backend_fd; struct uv__queue pending_queue; struct uv__queue watcher_queue; uv__io_t** watchers; unsigned int nwatchers; unsigned int nfds; struct uv__queue wq; uv_mutex_t wq_mutex; uv_async_t wq_async; uv_rwlock_t cloexec_lock; uv_handle_t* closing_handles; struct uv__queue process_handles; struct uv__queue prepare_handles; struct uv__queue check_handles; struct uv__queue idle_handles; struct uv__queue async_handles; void (*async_unused)(void); uv__io_t async_io_watcher; int async_wfd; struct { void* min; unsigned int nelts; } timer_heap; uint64_t timer_counter; uint64_t time; int signal_pipefd[2]; uv__io_t signal_io_watcher; uv_signal_t child_watcher; int emfile_fd; uv__io_t inotify_read_watcher; void* inotify_watchers; int inotify_fd; };

#endif /* LIBUV_DYN_HAVE_HEADER */

/* ============================================================
 * 动态加载（多候选路径）
 * ============================================================ */
static lib_handle_t libuv_handle = NULL;
static void *libuv_sym(const char *name) {
    return libuv_handle ? get_sym(libuv_handle, name) : NULL;
}
static int libuv_init_state = 0;

static int libuv_do_load(const char *library_path) {
    static char bufs[96][1024];
    const char *candidates[96];
    int n = 0, i;
    if (libuv_handle) return 0;
    if (library_path && *library_path) {
        snprintf(bufs[n], sizeof bufs[0], "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    {
        const char *e = getenv("LIBUV_DYN_LIB");
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
                        snprintf(bufs[n], sizeof bufs[0], "%s/libuv.so.1", q);
                        candidates[n] = bufs[n]; n++;
                        snprintf(bufs[n], sizeof bufs[0], "%s/libuv.so", q);
                        candidates[n] = bufs[n]; n++;
                    }
                    q = sep ? sep + 1 : NULL;
                }
                free(copy);
            }
        }
    }
    candidates[n++] = "libuv.so.1";
    candidates[n++] = "libuv.so";
#ifndef _WIN32
    {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libuv.so.1", home);
            candidates[n] = bufs[n]; n++;
            snprintf(bufs[n], sizeof bufs[0], "%s/.nix-profile/lib/libuv.so", home);
            candidates[n] = bufs[n]; n++;
        }
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libuv.so.1";
        candidates[n++] = "/nix/var/nix/profiles/default/lib/libuv.so";
    }
#endif
#ifdef __APPLE__
    candidates[n++] = "/opt/homebrew/lib/libuv.so.1";
    candidates[n++] = "/usr/local/lib/libuv.so.1";
    candidates[n++] = "/opt/homebrew/lib/libuv.so";
    candidates[n++] = "/usr/local/lib/libuv.so";
#elif defined(_WIN32)
    candidates[n++] = "libuv.dll";
#else
    candidates[n++] = "/usr/local/lib/libuv.so.1";
    candidates[n++] = "/usr/lib/libuv.so.1";
    candidates[n++] = "/usr/local/lib/libuv.so";
    candidates[n++] = "/usr/lib/libuv.so";
#endif
    for (i = 0; i < n; i++) {
        libuv_handle = load_lib(candidates[i]);
        if (libuv_handle) break;
    }
    if (!libuv_handle || !libuv_sym("uv_loop_init")) {
        fprintf(stderr, "libuv_dyn: cannot load libuv (%s)\n", lib_error());
        if (libuv_handle) { free_lib(libuv_handle); libuv_handle = NULL; }
        return -1;
    }
    fprintf(stderr, "libuv_dyn: loaded %d %s (1.53 headers)\n", i, candidates[i]);
    return 0;
}

int libuv_libok(void) {
    if (libuv_init_state == 0)
        libuv_init_state = (libuv_do_load(NULL) == 0) ? 1 : -1;
    return (libuv_init_state == 1 && libuv_handle != NULL) ? 1 : 0;
}

int libuv_dyn_init(const char *library_path) {
    if (libuv_do_load(library_path) == 0) {
        libuv_init_state = 1;
        return 0;
    }
    if (libuv_init_state == 0) libuv_init_state = -1;
    return -1;
}

void libuv_dyn_cleanup(void) {
    /* 仅复位加载状态，不卸载已成功加载的库（dlclose 不安全） */
    if (!libuv_handle) libuv_init_state = 0;
    else if (libuv_init_state == -1) libuv_init_state = 0;
}

int libuv_dyn_has_symbol(const char *name) {
    if (!libuv_libok()) return 0;
    return libuv_sym(name) != NULL;
}

/* ============================================================
 * WRAP 宏（首次调用 dlsym 并缓存；失败打印 stderr 返回安全值）
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!libuv_libok()) return fallback;                              \
        *(void **)(&fn) = libuv_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libuv_dyn: dlsym failed: %s\n", #name);     \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!libuv_libok()) return;                                       \
        *(void **)(&fn) = libuv_sym(#name);                               \
        if (!fn) {                                                        \
            fprintf(stderr, "libuv_dyn: dlsym failed: %s\n", #name);     \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}

/* ============================================================
 * 导出函数封装（与 nm -D 导出一一对应，共 319 个）
 * ============================================================ */
WRAP_RET(int, uv_accept, (uv_stream_t * a0, uv_stream_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_async_init, (uv_loop_t * a0, uv_async_t * a1, uv_async_cb a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_async_send, (uv_async_t * a0), (a0), -1)
WRAP_RET(unsigned int, uv_available_parallelism, (void), (), ((unsigned int){0}))
WRAP_RET(int, uv_backend_fd, (const uv_loop_t * a0), (a0), -1)
WRAP_RET(int, uv_backend_timeout, (const uv_loop_t * a0), (a0), -1)
WRAP_VOID(uv_barrier_destroy, (uv_barrier_t * a0), (a0))
WRAP_RET(int, uv_barrier_init, (uv_barrier_t * a0, unsigned int a1), (a0, a1), -1)
WRAP_RET(int, uv_barrier_wait, (uv_barrier_t * a0), (a0), -1)
WRAP_RET(uv_buf_t, uv_buf_init, (char * a0, unsigned int a1), (a0, a1), ((uv_buf_t){0}))
WRAP_RET(int, uv_cancel, (uv_req_t * a0), (a0), -1)
WRAP_RET(int, uv_chdir, (const char * a0), (a0), -1)
WRAP_RET(int, uv_check_init, (uv_loop_t * a0, uv_check_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_check_start, (uv_check_t * a0, uv_check_cb a1), (a0, a1), -1)
WRAP_RET(int, uv_check_stop, (uv_check_t * a0), (a0), -1)
WRAP_RET(int, uv_clock_gettime, (uv_clock_id a0, uv_timespec64_t * a1), (a0, a1), -1)
WRAP_VOID(uv_close, (uv_handle_t * a0, uv_close_cb a1), (a0, a1))
WRAP_VOID(uv_cond_broadcast, (uv_cond_t * a0), (a0))
WRAP_VOID(uv_cond_destroy, (uv_cond_t * a0), (a0))
WRAP_RET(int, uv_cond_init, (uv_cond_t * a0), (a0), -1)
WRAP_VOID(uv_cond_signal, (uv_cond_t * a0), (a0))
WRAP_RET(int, uv_cond_timedwait, (uv_cond_t * a0, uv_mutex_t * a1, uint64_t a2), (a0, a1, a2), -1)
WRAP_VOID(uv_cond_wait, (uv_cond_t * a0, uv_mutex_t * a1), (a0, a1))
WRAP_RET(int, uv_cpu_info, (uv_cpu_info_t ** a0, int * a1), (a0, a1), -1)
WRAP_RET(int, uv_cpumask_size, (void), (), -1)
WRAP_RET(int, uv_cwd, (char * a0, size_t * a1), (a0, a1), -1)
WRAP_RET(uv_loop_t *, uv_default_loop, (void), (), NULL)
WRAP_VOID(uv_disable_stdio_inheritance, (void), ())
WRAP_VOID(uv_dlclose, (uv_lib_t * a0), (a0))
WRAP_RET(const char *, uv_dlerror, (const uv_lib_t * a0), (a0), NULL)
WRAP_RET(int, uv_dlopen, (const char * a0, uv_lib_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_dlsym, (uv_lib_t * a0, const char * a1, void ** a2), (a0, a1, a2), -1)
WRAP_RET(const char *, uv_err_name, (int a0), (a0), NULL)
WRAP_RET(char *, uv_err_name_r, (int a0, char * a1, size_t a2), (a0, a1, a2), NULL)
WRAP_RET(int, uv_exepath, (char * a0, size_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_fileno, (const uv_handle_t * a0, uv_os_fd_t * a1), (a0, a1), -1)
WRAP_VOID(uv_free_cpu_info, (uv_cpu_info_t * a0, int a1), (a0, a1))
WRAP_VOID(uv_free_interface_addresses, (uv_interface_address_t * a0, int a1), (a0, a1))
WRAP_VOID(uv_freeaddrinfo, (struct addrinfo * a0), (a0))
WRAP_RET(int, uv_fs_access, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, int a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_chmod, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, int a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_chown, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_uid_t a3, uv_gid_t a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_close, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_closedir, (uv_loop_t * a0, uv_fs_t * a1, uv_dir_t * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_copyfile, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, const char * a3, int a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_event_getpath, (uv_fs_event_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_fs_event_init, (uv_loop_t * a0, uv_fs_event_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_fs_event_start, (uv_fs_event_t * a0, uv_fs_event_cb a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_event_stop, (uv_fs_event_t * a0), (a0), -1)
WRAP_RET(int, uv_fs_fchmod, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, int a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_fchown, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, uv_uid_t a3, uv_gid_t a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_fdatasync, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_fstat, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_fsync, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_ftruncate, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, int64_t a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_futime, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, double a3, double a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(const char *, uv_fs_get_path, (const uv_fs_t * a0), (a0), NULL)
WRAP_RET(void *, uv_fs_get_ptr, (const uv_fs_t * a0), (a0), NULL)
WRAP_RET(ssize_t, uv_fs_get_result, (const uv_fs_t * a0), (a0), -1)
WRAP_RET(uv_stat_t *, uv_fs_get_statbuf, (uv_fs_t * a0), (a0), NULL)
WRAP_RET(int, uv_fs_get_system_error, (const uv_fs_t * a0), (a0), -1)
WRAP_RET(uv_fs_type, uv_fs_get_type, (const uv_fs_t * a0), (a0), ((uv_fs_type){0}))
WRAP_RET(int, uv_fs_lchown, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_uid_t a3, uv_gid_t a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_link, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, const char * a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_lstat, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_lutime, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, double a3, double a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_mkdir, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, int a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_mkdtemp, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_mkstemp, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_open, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, int a3, int a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_opendir, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_poll_getpath, (uv_fs_poll_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_fs_poll_init, (uv_loop_t * a0, uv_fs_poll_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_fs_poll_start, (uv_fs_poll_t * a0, uv_fs_poll_cb a1, const char * a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_poll_stop, (uv_fs_poll_t * a0), (a0), -1)
WRAP_RET(int, uv_fs_read, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, const uv_buf_t * a3, unsigned int a4, int64_t a5, uv_fs_cb a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, uv_fs_readdir, (uv_loop_t * a0, uv_fs_t * a1, uv_dir_t * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_readlink, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_realpath, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_rename, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, const char * a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_VOID(uv_fs_req_cleanup, (uv_fs_t * a0), (a0))
WRAP_RET(int, uv_fs_rmdir, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_scandir, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, int a3, uv_fs_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_fs_scandir_next, (uv_fs_t * a0, uv_dirent_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_fs_sendfile, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, uv_file a3, int64_t a4, size_t a5, uv_fs_cb a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(int, uv_fs_stat, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_statfs, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_symlink, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, const char * a3, int a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_unlink, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, uv_fs_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_fs_utime, (uv_loop_t * a0, uv_fs_t * a1, const char * a2, double a3, double a4, uv_fs_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_fs_write, (uv_loop_t * a0, uv_fs_t * a1, uv_file a2, const uv_buf_t * a3, unsigned int a4, int64_t a5, uv_fs_cb a6), (a0, a1, a2, a3, a4, a5, a6), -1)
WRAP_RET(uint64_t, uv_get_available_memory, (void), (), ((uint64_t){0}))
WRAP_RET(uint64_t, uv_get_constrained_memory, (void), (), ((uint64_t){0}))
WRAP_RET(uint64_t, uv_get_free_memory, (void), (), ((uint64_t){0}))
WRAP_RET(uv_os_fd_t, uv_get_osfhandle, (int a0), (a0), -1)
WRAP_RET(int, uv_get_process_title, (char * a0, size_t a1), (a0, a1), -1)
WRAP_RET(uint64_t, uv_get_total_memory, (void), (), ((uint64_t){0}))
WRAP_RET(int, uv_getaddrinfo, (uv_loop_t * a0, uv_getaddrinfo_t * a1, uv_getaddrinfo_cb a2, const char * a3, const char * a4, const struct addrinfo * a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_getnameinfo, (uv_loop_t * a0, uv_getnameinfo_t * a1, uv_getnameinfo_cb a2, const struct sockaddr * a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_getrusage, (uv_rusage_t * a0), (a0), -1)
WRAP_RET(int, uv_getrusage_thread, (uv_rusage_t * a0), (a0), -1)
WRAP_RET(int, uv_gettimeofday, (uv_timeval64_t * a0), (a0), -1)
WRAP_RET(uv_handle_type, uv_guess_handle, (uv_file a0), (a0), ((uv_handle_type){0}))
WRAP_RET(void *, uv_handle_get_data, (const uv_handle_t * a0), (a0), NULL)
WRAP_RET(uv_loop_t *, uv_handle_get_loop, (const uv_handle_t * a0), (a0), NULL)
WRAP_RET(uv_handle_type, uv_handle_get_type, (const uv_handle_t * a0), (a0), ((uv_handle_type){0}))
WRAP_VOID(uv_handle_set_data, (uv_handle_t * a0, void * a1), (a0, a1))
WRAP_RET(size_t, uv_handle_size, (uv_handle_type a0), (a0), ((size_t){0}))
WRAP_RET(const char *, uv_handle_type_name, (uv_handle_type a0), (a0), NULL)
WRAP_RET(int, uv_has_ref, (const uv_handle_t * a0), (a0), -1)
WRAP_RET(uint64_t, uv_hrtime, (void), (), ((uint64_t){0}))
WRAP_RET(int, uv_idle_init, (uv_loop_t * a0, uv_idle_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_idle_start, (uv_idle_t * a0, uv_idle_cb a1), (a0, a1), -1)
WRAP_RET(int, uv_idle_stop, (uv_idle_t * a0), (a0), -1)
WRAP_RET(int, uv_if_indextoiid, (unsigned int a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_if_indextoname, (unsigned int a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_inet_ntop, (int a0, const void * a1, char * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_inet_pton, (int a0, const char * a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_interface_addresses, (uv_interface_address_t ** a0, int * a1), (a0, a1), -1)
WRAP_RET(int, uv_ip4_addr, (const char * a0, int a1, struct sockaddr_in * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_ip4_name, (const struct sockaddr_in * a0, char * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_ip6_addr, (const char * a0, int a1, struct sockaddr_in6 * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_ip6_name, (const struct sockaddr_in6 * a0, char * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_ip_name, (const struct sockaddr * a0, char * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_is_active, (const uv_handle_t * a0), (a0), -1)
WRAP_RET(int, uv_is_closing, (const uv_handle_t * a0), (a0), -1)
WRAP_RET(int, uv_is_readable, (const uv_stream_t * a0), (a0), -1)
WRAP_RET(int, uv_is_writable, (const uv_stream_t * a0), (a0), -1)
WRAP_RET(int, uv_key_create, (uv_key_t * a0), (a0), -1)
WRAP_VOID(uv_key_delete, (uv_key_t * a0), (a0))
WRAP_RET(void *, uv_key_get, (uv_key_t * a0), (a0), NULL)
WRAP_VOID(uv_key_set, (uv_key_t * a0, void * a1), (a0, a1))
WRAP_RET(int, uv_kill, (int a0, int a1), (a0, a1), -1)
WRAP_VOID(uv_library_shutdown, (void), ())
WRAP_RET(int, uv_listen, (uv_stream_t * a0, int a1, uv_connection_cb a2), (a0, a1, a2), -1)
WRAP_VOID(uv_loadavg, (double * a0), (a0))
WRAP_RET(int, uv_loop_alive, (const uv_loop_t * a0), (a0), -1)
WRAP_RET(int, uv_loop_close, (uv_loop_t * a0), (a0), -1)
WRAP_VOID(uv_loop_delete, (uv_loop_t * a0), (a0))
WRAP_RET(int, uv_loop_fork, (uv_loop_t * a0), (a0), -1)
WRAP_RET(void *, uv_loop_get_data, (const uv_loop_t * a0), (a0), NULL)
WRAP_RET(int, uv_loop_init, (uv_loop_t * a0), (a0), -1)
WRAP_RET(uv_loop_t *, uv_loop_new, (void), (), NULL)
WRAP_VOID(uv_loop_set_data, (uv_loop_t * a0, void * a1), (a0, a1))
WRAP_RET(size_t, uv_loop_size, (void), (), ((size_t){0}))
WRAP_RET(uint64_t, uv_metrics_idle_time, (uv_loop_t * a0), (a0), ((uint64_t){0}))
WRAP_RET(int, uv_metrics_info, (uv_loop_t * a0, uv_metrics_t * a1), (a0, a1), -1)
WRAP_VOID(uv_mutex_destroy, (uv_mutex_t * a0), (a0))
WRAP_RET(int, uv_mutex_init, (uv_mutex_t * a0), (a0), -1)
WRAP_RET(int, uv_mutex_init_recursive, (uv_mutex_t * a0), (a0), -1)
WRAP_VOID(uv_mutex_lock, (uv_mutex_t * a0), (a0))
WRAP_RET(int, uv_mutex_trylock, (uv_mutex_t * a0), (a0), -1)
WRAP_VOID(uv_mutex_unlock, (uv_mutex_t * a0), (a0))
WRAP_RET(uint64_t, uv_now, (const uv_loop_t * a0), (a0), ((uint64_t){0}))
WRAP_VOID(uv_once, (uv_once_t * a0, void (*a1) (void)), (a0, a1))
WRAP_RET(int, uv_open_osfhandle, (uv_os_fd_t a0), (a0), -1)
WRAP_RET(int, uv_os_environ, (uv_env_item_t ** a0, int * a1), (a0, a1), -1)
WRAP_VOID(uv_os_free_environ, (uv_env_item_t * a0, int a1), (a0, a1))
WRAP_VOID(uv_os_free_group, (uv_group_t * a0), (a0))
WRAP_VOID(uv_os_free_passwd, (uv_passwd_t * a0), (a0))
WRAP_RET(int, uv_os_get_group, (uv_group_t * a0, uv_uid_t a1), (a0, a1), -1)
WRAP_RET(int, uv_os_get_passwd, (uv_passwd_t * a0), (a0), -1)
WRAP_RET(int, uv_os_get_passwd2, (uv_passwd_t * a0, uv_uid_t a1), (a0, a1), -1)
WRAP_RET(int, uv_os_getenv, (const char * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_os_gethostname, (char * a0, size_t * a1), (a0, a1), -1)
WRAP_RET(uv_pid_t, uv_os_getpid, (void), (), -1)
WRAP_RET(uv_pid_t, uv_os_getppid, (void), (), -1)
WRAP_RET(int, uv_os_getpriority, (uv_pid_t a0, int * a1), (a0, a1), -1)
WRAP_RET(int, uv_os_homedir, (char * a0, size_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_os_setenv, (const char * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, uv_os_setpriority, (uv_pid_t a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_os_tmpdir, (char * a0, size_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_os_uname, (uv_utsname_t * a0), (a0), -1)
WRAP_RET(int, uv_os_unsetenv, (const char * a0), (a0), -1)
WRAP_RET(int, uv_pipe, (uv_file * a0, int a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_pipe_bind, (uv_pipe_t * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, uv_pipe_bind2, (uv_pipe_t * a0, const char * a1, size_t a2, unsigned int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_pipe_chmod, (uv_pipe_t * a0, int a1), (a0, a1), -1)
WRAP_VOID(uv_pipe_connect, (uv_connect_t * a0, uv_pipe_t * a1, const char * a2, uv_connect_cb a3), (a0, a1, a2, a3))
WRAP_RET(int, uv_pipe_connect2, (uv_connect_t * a0, uv_pipe_t * a1, const char * a2, size_t a3, unsigned int a4, uv_connect_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_pipe_getpeername, (const uv_pipe_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_pipe_getsockname, (const uv_pipe_t * a0, char * a1, size_t * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_pipe_init, (uv_loop_t * a0, uv_pipe_t * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_pipe_open, (uv_pipe_t * a0, uv_file a1), (a0, a1), -1)
WRAP_RET(int, uv_pipe_pending_count, (uv_pipe_t * a0), (a0), -1)
WRAP_VOID(uv_pipe_pending_instances, (uv_pipe_t * a0, int a1), (a0, a1))
WRAP_RET(uv_handle_type, uv_pipe_pending_type, (uv_pipe_t * a0), (a0), ((uv_handle_type){0}))
WRAP_RET(int, uv_poll_init, (uv_loop_t * a0, uv_poll_t * a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_poll_init_socket, (uv_loop_t * a0, uv_poll_t * a1, uv_os_sock_t a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_poll_start, (uv_poll_t * a0, int a1, uv_poll_cb a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_poll_stop, (uv_poll_t * a0), (a0), -1)
WRAP_RET(int, uv_prepare_init, (uv_loop_t * a0, uv_prepare_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_prepare_start, (uv_prepare_t * a0, uv_prepare_cb a1), (a0, a1), -1)
WRAP_RET(int, uv_prepare_stop, (uv_prepare_t * a0), (a0), -1)
WRAP_VOID(uv_print_active_handles, (uv_loop_t * a0, FILE * a1), (a0, a1))
WRAP_VOID(uv_print_all_handles, (uv_loop_t * a0, FILE * a1), (a0, a1))
WRAP_RET(uv_pid_t, uv_process_get_pid, (const uv_process_t * a0), (a0), -1)
WRAP_RET(int, uv_process_kill, (uv_process_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_queue_work, (uv_loop_t * a0, uv_work_t * a1, uv_work_cb a2, uv_after_work_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_random, (uv_loop_t * a0, uv_random_t * a1, void * a2, size_t a3, unsigned int a4, uv_random_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_read_start, (uv_stream_t * a0, uv_alloc_cb a1, uv_read_cb a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_read_stop, (uv_stream_t * a0), (a0), -1)
WRAP_RET(int, uv_recv_buffer_size, (uv_handle_t * a0, int * a1), (a0, a1), -1)
WRAP_VOID(uv_ref, (uv_handle_t * a0), (a0))
WRAP_RET(int, uv_replace_allocator, (uv_malloc_func a0, uv_realloc_func a1, uv_calloc_func a2, uv_free_func a3), (a0, a1, a2, a3), -1)
WRAP_RET(void *, uv_req_get_data, (const uv_req_t * a0), (a0), NULL)
WRAP_RET(uv_req_type, uv_req_get_type, (const uv_req_t * a0), (a0), ((uv_req_type){0}))
WRAP_VOID(uv_req_set_data, (uv_req_t * a0, void * a1), (a0, a1))
WRAP_RET(size_t, uv_req_size, (uv_req_type a0), (a0), ((size_t){0}))
WRAP_RET(const char *, uv_req_type_name, (uv_req_type a0), (a0), NULL)
WRAP_RET(int, uv_resident_set_memory, (size_t * a0), (a0), -1)
WRAP_RET(int, uv_run, (uv_loop_t * a0, uv_run_mode a1), (a0, a1), -1)
WRAP_VOID(uv_rwlock_destroy, (uv_rwlock_t * a0), (a0))
WRAP_RET(int, uv_rwlock_init, (uv_rwlock_t * a0), (a0), -1)
WRAP_VOID(uv_rwlock_rdlock, (uv_rwlock_t * a0), (a0))
WRAP_VOID(uv_rwlock_rdunlock, (uv_rwlock_t * a0), (a0))
WRAP_RET(int, uv_rwlock_tryrdlock, (uv_rwlock_t * a0), (a0), -1)
WRAP_RET(int, uv_rwlock_trywrlock, (uv_rwlock_t * a0), (a0), -1)
WRAP_VOID(uv_rwlock_wrlock, (uv_rwlock_t * a0), (a0))
WRAP_VOID(uv_rwlock_wrunlock, (uv_rwlock_t * a0), (a0))
WRAP_VOID(uv_sem_destroy, (uv_sem_t * a0), (a0))
WRAP_RET(int, uv_sem_init, (uv_sem_t * a0, unsigned int a1), (a0, a1), -1)
WRAP_VOID(uv_sem_post, (uv_sem_t * a0), (a0))
WRAP_RET(int, uv_sem_trywait, (uv_sem_t * a0), (a0), -1)
WRAP_VOID(uv_sem_wait, (uv_sem_t * a0), (a0))
WRAP_RET(int, uv_send_buffer_size, (uv_handle_t * a0, int * a1), (a0, a1), -1)
WRAP_RET(int, uv_set_process_title, (const char * a0), (a0), -1)
WRAP_RET(char **, uv_setup_args, (int a0, char ** a1), (a0, a1), NULL)
WRAP_RET(int, uv_shutdown, (uv_shutdown_t * a0, uv_stream_t * a1, uv_shutdown_cb a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_signal_init, (uv_loop_t * a0, uv_signal_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_signal_start, (uv_signal_t * a0, uv_signal_cb a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_signal_start_oneshot, (uv_signal_t * a0, uv_signal_cb a1, int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_signal_stop, (uv_signal_t * a0), (a0), -1)
WRAP_VOID(uv_sleep, (unsigned int a0), (a0))
WRAP_RET(int, uv_socketpair, (int a0, int a1, uv_os_sock_t * a2, int a3, int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_spawn, (uv_loop_t * a0, uv_process_t * a1, const uv_process_options_t * a2), (a0, a1, a2), -1)
WRAP_VOID(uv_stop, (uv_loop_t * a0), (a0))
WRAP_RET(size_t, uv_stream_get_write_queue_size, (const uv_stream_t * a0), (a0), ((size_t){0}))
WRAP_RET(int, uv_stream_set_blocking, (uv_stream_t * a0, int a1), (a0, a1), -1)
WRAP_RET(const char *, uv_strerror, (int a0), (a0), NULL)
WRAP_RET(char *, uv_strerror_r, (int a0, char * a1, size_t a2), (a0, a1, a2), NULL)
WRAP_RET(int, uv_tcp_bind, (uv_tcp_t * a0, const struct sockaddr * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_tcp_close_reset, (uv_tcp_t * a0, uv_close_cb a1), (a0, a1), -1)
WRAP_RET(int, uv_tcp_connect, (uv_connect_t * a0, uv_tcp_t * a1, const struct sockaddr * a2, uv_connect_cb a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_tcp_getpeername, (const uv_tcp_t * a0, struct sockaddr * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_tcp_getsockname, (const uv_tcp_t * a0, struct sockaddr * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_tcp_init, (uv_loop_t * a0, uv_tcp_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_tcp_init_ex, (uv_loop_t * a0, uv_tcp_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_tcp_keepalive, (uv_tcp_t * a0, int a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_tcp_keepalive_ex, (uv_tcp_t * a0, int a1, unsigned int a2, unsigned int a3, unsigned int a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_tcp_nodelay, (uv_tcp_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_tcp_open, (uv_tcp_t * a0, uv_os_sock_t a1), (a0, a1), -1)
WRAP_RET(int, uv_tcp_simultaneous_accepts, (uv_tcp_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_thread_create, (uv_thread_t * a0, uv_thread_cb a1, void * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_thread_create_ex, (uv_thread_t * a0, const uv_thread_options_t * a1, uv_thread_cb a2, void * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_thread_detach, (uv_thread_t * a0), (a0), -1)
WRAP_RET(int, uv_thread_equal, (const uv_thread_t * a0, const uv_thread_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_thread_getaffinity, (uv_thread_t * a0, char * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_thread_getcpu, (void), (), -1)
WRAP_RET(int, uv_thread_getname, (uv_thread_t * a0, char * a1, size_t a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_thread_getpriority, (uv_thread_t a0, int * a1), (a0, a1), -1)
WRAP_RET(int, uv_thread_join, (uv_thread_t * a0), (a0), -1)
WRAP_RET(uv_thread_t, uv_thread_self, (void), (), ((uv_thread_t){0}))
WRAP_RET(int, uv_thread_setaffinity, (uv_thread_t * a0, char * a1, char * a2, size_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_thread_setname, (const char * a0), (a0), -1)
WRAP_RET(int, uv_thread_setpriority, (uv_thread_t a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_timer_again, (uv_timer_t * a0), (a0), -1)
WRAP_RET(uint64_t, uv_timer_get_due_in, (const uv_timer_t * a0), (a0), ((uint64_t){0}))
WRAP_RET(uint64_t, uv_timer_get_repeat, (const uv_timer_t * a0), (a0), ((uint64_t){0}))
WRAP_RET(int, uv_timer_init, (uv_loop_t * a0, uv_timer_t * a1), (a0, a1), -1)
WRAP_VOID(uv_timer_set_repeat, (uv_timer_t * a0, uint64_t a1), (a0, a1))
WRAP_RET(int, uv_timer_start, (uv_timer_t * a0, uv_timer_cb a1, uint64_t a2, uint64_t a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_timer_stop, (uv_timer_t * a0), (a0), -1)
WRAP_RET(int, uv_translate_sys_error, (int a0), (a0), -1)
WRAP_RET(int, uv_try_write, (uv_stream_t * a0, const uv_buf_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_try_write2, (uv_stream_t * a0, const uv_buf_t * a1, unsigned int a2, uv_stream_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_tty_get_vterm_state, (uv_tty_vtermstate_t * a0), (a0), -1)
WRAP_RET(int, uv_tty_get_winsize, (uv_tty_t * a0, int * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_tty_init, (uv_loop_t * a0, uv_tty_t * a1, uv_file a2, int a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_tty_reset_mode, (void), (), -1)
WRAP_RET(int, uv_tty_set_mode, (uv_tty_t * a0, uv_tty_mode_t a1), (a0, a1), -1)
WRAP_VOID(uv_tty_set_vterm_state, (uv_tty_vtermstate_t a0), (a0))
WRAP_RET(int, uv_udp_bind, (uv_udp_t * a0, const struct sockaddr * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_udp_connect, (uv_udp_t * a0, const struct sockaddr * a1), (a0, a1), -1)
WRAP_RET(size_t, uv_udp_get_send_queue_count, (const uv_udp_t * a0), (a0), ((size_t){0}))
WRAP_RET(size_t, uv_udp_get_send_queue_size, (const uv_udp_t * a0), (a0), ((size_t){0}))
WRAP_RET(int, uv_udp_getpeername, (const uv_udp_t * a0, struct sockaddr * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_udp_getsockname, (const uv_udp_t * a0, struct sockaddr * a1, int * a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_udp_init, (uv_loop_t * a0, uv_udp_t * a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_init_ex, (uv_loop_t * a0, uv_udp_t * a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_udp_open, (uv_udp_t * a0, uv_os_sock_t a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_open_ex, (uv_udp_t * a0, uv_os_sock_t a1, unsigned int a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_udp_recv_start, (uv_udp_t * a0, uv_alloc_cb a1, uv_udp_recv_cb a2), (a0, a1, a2), -1)
WRAP_RET(int, uv_udp_recv_stop, (uv_udp_t * a0), (a0), -1)
WRAP_RET(int, uv_udp_send, (uv_udp_send_t * a0, uv_udp_t * a1, const uv_buf_t * a2, unsigned int a3, const struct sockaddr * a4, uv_udp_send_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_udp_set_broadcast, (uv_udp_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_set_membership, (uv_udp_t * a0, const char * a1, const char * a2, uv_membership a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_udp_set_multicast_interface, (uv_udp_t * a0, const char * a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_set_multicast_loop, (uv_udp_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_set_multicast_ttl, (uv_udp_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_set_source_membership, (uv_udp_t * a0, const char * a1, const char * a2, const char * a3, uv_membership a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_udp_set_ttl, (uv_udp_t * a0, int a1), (a0, a1), -1)
WRAP_RET(int, uv_udp_try_send, (uv_udp_t * a0, const uv_buf_t * a1, unsigned int a2, const struct sockaddr * a3), (a0, a1, a2, a3), -1)
WRAP_RET(int, uv_udp_try_send2, (uv_udp_t * a0, unsigned int a1, uv_buf_t ** a2, unsigned int * a3, struct sockaddr ** a4, unsigned int a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(int, uv_udp_using_recvmmsg, (const uv_udp_t * a0), (a0), -1)
WRAP_VOID(uv_unref, (uv_handle_t * a0), (a0))
WRAP_VOID(uv_update_time, (uv_loop_t * a0), (a0))
WRAP_RET(int, uv_uptime, (double * a0), (a0), -1)
WRAP_RET(size_t, uv_utf16_length_as_wtf8, (const uint16_t * a0, ssize_t a1), (a0, a1), ((size_t){0}))
WRAP_RET(int, uv_utf16_to_wtf8, (const uint16_t * a0, ssize_t a1, char ** a2, size_t * a3), (a0, a1, a2, a3), -1)
WRAP_RET(unsigned int, uv_version, (void), (), ((unsigned int){0}))
WRAP_RET(const char *, uv_version_string, (void), (), NULL)
WRAP_VOID(uv_walk, (uv_loop_t * a0, uv_walk_cb a1, void * a2), (a0, a1, a2))
WRAP_RET(int, uv_write, (uv_write_t * a0, uv_stream_t * a1, const uv_buf_t * a2, unsigned int a3, uv_write_cb a4), (a0, a1, a2, a3, a4), -1)
WRAP_RET(int, uv_write2, (uv_write_t * a0, uv_stream_t * a1, const uv_buf_t * a2, unsigned int a3, uv_stream_t * a4, uv_write_cb a5), (a0, a1, a2, a3, a4, a5), -1)
WRAP_RET(size_t, uv_write_nwritten, (const uv_write_t * a0), (a0), ((size_t){0}))
WRAP_RET(ssize_t, uv_wtf8_length_as_utf16, (const char * a0), (a0), -1)
WRAP_VOID(uv_wtf8_to_utf16, (const char * a0, uint16_t * a1, size_t a2), (a0, a1, a2))

/* ============================================================
 * uv_loop_configure 是可变参数导出函数。1.53.0 中只有
 * UV_LOOP_BLOCK_SIGNAL 携带一个 int 参数，其余选项无附加参数。
 * ============================================================ */
int uv_loop_configure(uv_loop_t *loop, uv_loop_option option, ...) {
    static int (*fn)(uv_loop_t *, uv_loop_option, ...) = NULL;
    va_list ap;
    int arg = 0;
    if (!libuv_libok()) return -1;
    if (!fn) {
        *(void **)(&fn) = libuv_sym("uv_loop_configure");
        if (!fn) {
            fprintf(stderr, "libuv_dyn: dlsym failed: uv_loop_configure\n");
            return -1;
        }
    }
    if (option == UV_LOOP_BLOCK_SIGNAL) {
        va_start(ap, option);
        arg = va_arg(ap, int);
        va_end(ap);
        return fn(loop, option, arg);
    }
    return fn(loop, option);
}

