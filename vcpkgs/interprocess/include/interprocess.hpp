#ifndef INTERPROCESS_FFI_HPP
#define INTERPROCESS_FFI_HPP

#include "interprocess.h"

#include <cstddef>
#include <string>

namespace ipc {

inline std::string last_error() {
    const char *message = ipc_last_error_message();
    return message == NULL ? std::string() : std::string(message);
}

class Stream {
public:
    Stream() : handle_(NULL) {}
    explicit Stream(ipc_stream *handle) : handle_(handle) {}

    ~Stream() { close(); }

    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;

    Stream(Stream &&other) noexcept : handle_(other.handle_) { other.handle_ = NULL; }

    Stream &operator=(Stream &&other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = NULL;
        }
        return *this;
    }

    explicit operator bool() const { return handle_ != NULL; }

    ipc_stream *native_handle() const { return handle_; }

    size_t read(void *buffer, size_t size) {
        return ipc_stream_read(handle_, buffer, size);
    }

    bool write(const void *buffer, size_t size) {
        return ipc_stream_write(handle_, buffer, size) == 0;
    }

    void close() {
        if (handle_ != NULL) {
            ipc_stream_close(handle_);
            handle_ = NULL;
        }
    }

private:
    ipc_stream *handle_;
};

class Listener {
public:
    Listener() : handle_(NULL) {}
    explicit Listener(ipc_listener *handle) : handle_(handle) {}

    ~Listener() { close(); }

    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;

    Listener(Listener &&other) noexcept : handle_(other.handle_) { other.handle_ = NULL; }

    Listener &operator=(Listener &&other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = NULL;
        }
        return *this;
    }

    explicit operator bool() const { return handle_ != NULL; }

    ipc_listener *native_handle() const { return handle_; }

    Stream accept() {
        ipc_stream *stream = NULL;
        if (ipc_local_socket_accept(handle_, &stream) != 0) {
            return Stream();
        }
        return Stream(stream);
    }

    void close() {
        if (handle_ != NULL) {
            ipc_listener_close(handle_);
            handle_ = NULL;
        }
    }

private:
    ipc_listener *handle_;
};

inline Listener listen(const char *name) {
    ipc_listener *listener = NULL;
    if (ipc_local_socket_listen(name, &listener) != 0) {
        return Listener();
    }
    return Listener(listener);
}

inline Stream connect(const char *name) {
    ipc_stream *stream = NULL;
    if (ipc_local_socket_connect(name, &stream) != 0) {
        return Stream();
    }
    return Stream(stream);
}

inline bool unnamed_pipe_pair(Stream &sender, Stream &receiver) {
    sender.close();
    receiver.close();
    ipc_stream *new_sender = NULL;
    ipc_stream *new_receiver = NULL;
    if (ipc_unnamed_pipe_pair(&new_sender, &new_receiver) != 0) {
        return false;
    }
    sender = Stream(new_sender);
    receiver = Stream(new_receiver);
    return true;
}

} /* namespace ipc */

#endif /* INTERPROCESS_FFI_HPP */
