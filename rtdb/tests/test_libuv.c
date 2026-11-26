/* libuv_dyn 测试：只链 -ldl，不链 libuv
 * 类型块由 ../libuv_dyn.c 机械复制，保证 ABI 完全一致。
 * 额外用 uv_loop_size()/uv_handle_size()/uv_req_size() 校验布局。
 */
#include "test_common.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <termios.h>

/* ------------------------- 类型（自动复制） ------------------------- */
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

/* ------------------------- 测试用常量 ------------------------- */
#define UV_EINVAL (-22)
#define UV_ENOENT (-2)

/* ------------------------- 封装函数声明 ------------------------- */
int libuv_dyn_init(const char *library_path);
int libuv_libok(void);
void libuv_dyn_cleanup(void);

unsigned int uv_version(void);
const char *uv_version_string(void);
const char *uv_strerror(int);
const char *uv_err_name(int);
uint64_t uv_hrtime(void);
unsigned int uv_available_parallelism(void);
size_t uv_loop_size(void);
size_t uv_handle_size(uv_handle_type);
size_t uv_req_size(uv_req_type);

int uv_loop_init(uv_loop_t *);
int uv_loop_close(uv_loop_t *);
int uv_run(uv_loop_t *, uv_run_mode);
int uv_timer_init(uv_loop_t *, uv_timer_t *);
int uv_timer_start(uv_timer_t *, uv_timer_cb, uint64_t, uint64_t);
int uv_timer_stop(uv_timer_t *);
void uv_close(uv_handle_t *, uv_close_cb);
uv_buf_t uv_buf_init(char *, unsigned int);

static int timer_hits, close_hits;

static void on_close(uv_handle_t *h) {
    (void)h;
    close_hits++;
}

static void on_timer(uv_timer_t *t) {
    timer_hits++;
    uv_timer_stop(t);
    uv_close((uv_handle_t *)t, on_close);
}

int main(int argc, char **argv) {
    int failures = 0;
    uv_loop_t loop;
    uv_timer_t timer;

    if (libuv_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("libuv 不可加载\n");
    failures += CHECK(libuv_libok() != 0);

    printf("libuv %s (0x%06x), cpus=%u\n", uv_version_string(), uv_version(),
           uv_available_parallelism());
    failures += CHECK(uv_version() >= 0x010000u);
    failures += CHECK(uv_version_string() != NULL);
    failures += CHECK(uv_available_parallelism() >= 1);
    failures += CHECK(uv_hrtime() > 0);

    /* 1. 结构体布局与运行时一致（uv.h 公开按栈分配结构的关键校验） */
    printf("sizes: loop=%zu/%zu timer=%zu/%zu write=%zu/%zu fs=%zu/%zu\n",
           sizeof(uv_loop_t), uv_loop_size(),
           sizeof(uv_timer_t), uv_handle_size(UV_TIMER),
           sizeof(uv_write_t), uv_req_size(UV_WRITE),
           sizeof(uv_fs_t), uv_req_size(UV_FS));
    failures += CHECK(uv_loop_size() == sizeof(uv_loop_t));
#define CHECK_HANDLE_SIZE(e, T) failures += CHECK(uv_handle_size(e) == sizeof(T))
    CHECK_HANDLE_SIZE(UV_HANDLE, uv_handle_t);
    CHECK_HANDLE_SIZE(UV_STREAM, uv_stream_t);
    CHECK_HANDLE_SIZE(UV_TCP, uv_tcp_t);
    CHECK_HANDLE_SIZE(UV_UDP, uv_udp_t);
    CHECK_HANDLE_SIZE(UV_NAMED_PIPE, uv_pipe_t);
    CHECK_HANDLE_SIZE(UV_TTY, uv_tty_t);
    CHECK_HANDLE_SIZE(UV_TIMER, uv_timer_t);
    CHECK_HANDLE_SIZE(UV_ASYNC, uv_async_t);
    CHECK_HANDLE_SIZE(UV_PREPARE, uv_prepare_t);
    CHECK_HANDLE_SIZE(UV_CHECK, uv_check_t);
    CHECK_HANDLE_SIZE(UV_IDLE, uv_idle_t);
    CHECK_HANDLE_SIZE(UV_POLL, uv_poll_t);
    CHECK_HANDLE_SIZE(UV_SIGNAL, uv_signal_t);
    CHECK_HANDLE_SIZE(UV_PROCESS, uv_process_t);
    CHECK_HANDLE_SIZE(UV_FS_EVENT, uv_fs_event_t);
    CHECK_HANDLE_SIZE(UV_FS_POLL, uv_fs_poll_t);
#undef CHECK_HANDLE_SIZE
#define CHECK_REQ_SIZE(e, T) failures += CHECK(uv_req_size(e) == sizeof(T))
    CHECK_REQ_SIZE(UV_REQ, uv_req_t);
    CHECK_REQ_SIZE(UV_CONNECT, uv_connect_t);
    CHECK_REQ_SIZE(UV_WRITE, uv_write_t);
    CHECK_REQ_SIZE(UV_SHUTDOWN, uv_shutdown_t);
    CHECK_REQ_SIZE(UV_UDP_SEND, uv_udp_send_t);
    CHECK_REQ_SIZE(UV_FS, uv_fs_t);
    CHECK_REQ_SIZE(UV_WORK, uv_work_t);
    CHECK_REQ_SIZE(UV_GETADDRINFO, uv_getaddrinfo_t);
    CHECK_REQ_SIZE(UV_GETNAMEINFO, uv_getnameinfo_t);
    CHECK_REQ_SIZE(UV_RANDOM, uv_random_t);
#undef CHECK_REQ_SIZE

    /* 2. loop + timer，回调里 stop + close */
    failures += CHECK(uv_loop_init(&loop) == 0);
    failures += CHECK(uv_timer_init(&loop, &timer) == 0);
    failures += CHECK(uv_timer_start(&timer, NULL, 0, 0) == UV_EINVAL);
    failures += CHECK(uv_timer_start(&timer, on_timer, 1, 0) == 0);
    failures += CHECK(uv_run(&loop, UV_RUN_DEFAULT) == 0);
    failures += CHECK(timer_hits == 1);
    failures += CHECK(uv_run(&loop, UV_RUN_DEFAULT) == 0);
    failures += CHECK(close_hits == 1);
    failures += CHECK(uv_loop_close(&loop) == 0);

    /* 3. 工具函数 */
    failures += CHECK(uv_strerror(UV_EINVAL) != NULL);
    failures += CHECK(strcmp(uv_err_name(UV_ENOENT), "ENOENT") == 0);
    {
        char raw[4] = {'a', 'b', 'c', 0};
        uv_buf_t b = uv_buf_init(raw, 3);
        failures += CHECK(b.base == raw && b.len == 3);
    }

    /* 4. 错误路径：已关闭的 handle 再启动 */
    failures += CHECK(uv_timer_start(&timer, on_timer, 0, 0) == UV_EINVAL);

    libuv_dyn_cleanup();
    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
