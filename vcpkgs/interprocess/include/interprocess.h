#ifndef INTERPROCESS_FFI_H
#define INTERPROCESS_FFI_H

#include <stddef.h>

#if defined(_WIN32) && defined(IPCPP_SHARED)
#  define IPCPP_API __declspec(dllimport)
#else
#  define IPCPP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ipc_listener ipc_listener;
typedef struct ipc_stream ipc_stream;

#define IPCPP_ERROR_SIZE ((size_t)-1)

/* All name parameters are NUL-terminated portable local socket names. */
IPCPP_API int ipc_local_socket_listen(const char *name, ipc_listener **out_listener);
IPCPP_API int ipc_local_socket_accept(ipc_listener *listener, ipc_stream **out_stream);
IPCPP_API int ipc_local_socket_connect(const char *name, ipc_stream **out_stream);

IPCPP_API int ipc_unnamed_pipe_pair(ipc_stream **out_sender, ipc_stream **out_receiver);

/* Returns the number of bytes read, or IPCPP_ERROR_SIZE on failure. */
IPCPP_API size_t ipc_stream_read(ipc_stream *stream, void *buffer, size_t size);

/* Returns 0 on success, or -1 on failure. */
IPCPP_API int ipc_stream_write(ipc_stream *stream, const void *buffer, size_t size);

/* Closes and invalidates the handle. Do not reuse it afterwards. */
IPCPP_API int ipc_stream_close(ipc_stream *stream);
IPCPP_API int ipc_listener_close(ipc_listener *listener);

/*
 * Returns a thread-local error message, or NULL if no error is stored.
 * The pointer is valid until the next FFI call on the same thread.
 */
IPCPP_API const char *ipc_last_error_message(void);

#ifdef __cplusplus
}
#endif

#endif /* INTERPROCESS_FFI_H */
