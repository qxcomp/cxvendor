use std::cell::RefCell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::io::{self, Read, Write};
use std::ptr;

use interprocess::local_socket::traits::{Listener as _, Stream as LocalSocketStreamTrait};
use interprocess::local_socket::{
    GenericNamespaced, Listener, ListenerOptions, Name, Stream as LocalSocketStream, ToNsName,
};
use interprocess::unnamed_pipe::{pipe, Recver as PipeRecver, Sender as PipeSender};

pub const IPCPP_ERROR_SIZE: usize = usize::MAX;

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = RefCell::new(None);
}

fn clear_error() {
    LAST_ERROR.with(|slot| {
        slot.borrow_mut().take();
    });
}

fn set_error(message: &str) {
    let message = CString::new(message)
        .unwrap_or_else(|_| CString::new("unknown error").expect("fallback CString"));
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = Some(message);
    });
}

fn set_io_error(context: &str, error: &io::Error) {
    set_error(&format!("{}: {}", context, error));
}

#[no_mangle]
pub extern "C" fn ipc_last_error_message() -> *const c_char {
    LAST_ERROR.with(|slot| match slot.borrow().as_ref() {
        Some(message) => message.as_ptr(),
        None => ptr::null(),
    })
}

pub struct ipc_listener {
    inner: Listener,
}

enum OwnedStream {
    LocalSocket(LocalSocketStream),
    PipeSender(PipeSender),
    PipeRecver(PipeRecver),
}

impl Read for OwnedStream {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        match self {
            OwnedStream::LocalSocket(stream) => stream.read(buffer),
            OwnedStream::PipeRecver(receiver) => receiver.read(buffer),
            OwnedStream::PipeSender(_) => Err(io::Error::new(
                io::ErrorKind::Unsupported,
                "this pipe end is write-only",
            )),
        }
    }
}

impl Write for OwnedStream {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        match self {
            OwnedStream::LocalSocket(stream) => stream.write(buffer),
            OwnedStream::PipeSender(sender) => sender.write(buffer),
            OwnedStream::PipeRecver(_) => Err(io::Error::new(
                io::ErrorKind::Unsupported,
                "this pipe end is read-only",
            )),
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        match self {
            OwnedStream::LocalSocket(stream) => stream.flush(),
            OwnedStream::PipeSender(sender) => sender.flush(),
            OwnedStream::PipeRecver(_) => Ok(()),
        }
    }
}

pub struct ipc_stream {
    inner: OwnedStream,
}

unsafe fn name_from_ptr(name: *const c_char) -> Result<Name<'static>, ()> {
    if name.is_null() {
        set_error("local socket name is null");
        return Err(());
    }
    let name = CStr::from_ptr(name);
    let name = name.to_ns_name::<GenericNamespaced>().map_err(|error| {
        set_io_error("invalid local socket name", &error);
    })?;
    Ok(name.into_owned())
}

#[no_mangle]
pub unsafe extern "C" fn ipc_local_socket_listen(
    name: *const c_char,
    out_listener: *mut *mut ipc_listener,
) -> i32 {
    clear_error();
    if out_listener.is_null() {
        set_error("out_listener is null");
        return -1;
    }
    let name = match name_from_ptr(name) {
        Ok(name) => name,
        Err(()) => return -1,
    };
    match ListenerOptions::new().name(name).create_sync() {
        Ok(listener) => {
            *out_listener = Box::into_raw(Box::new(ipc_listener { inner: listener }));
            0
        }
        Err(error) => {
            set_io_error("failed to create local socket listener", &error);
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ipc_local_socket_accept(
    listener: *mut ipc_listener,
    out_stream: *mut *mut ipc_stream,
) -> i32 {
    clear_error();
    if listener.is_null() {
        set_error("listener is null");
        return -1;
    }
    if out_stream.is_null() {
        set_error("out_stream is null");
        return -1;
    }
    match (*listener).inner.accept() {
        Ok(stream) => {
            *out_stream = Box::into_raw(Box::new(ipc_stream {
                inner: OwnedStream::LocalSocket(stream),
            }));
            0
        }
        Err(error) => {
            set_io_error("failed to accept local socket connection", &error);
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ipc_local_socket_connect(
    name: *const c_char,
    out_stream: *mut *mut ipc_stream,
) -> i32 {
    clear_error();
    if out_stream.is_null() {
        set_error("out_stream is null");
        return -1;
    }
    let name = match name_from_ptr(name) {
        Ok(name) => name,
        Err(()) => return -1,
    };
    match LocalSocketStreamTrait::connect(name) {
        Ok(stream) => {
            *out_stream = Box::into_raw(Box::new(ipc_stream {
                inner: OwnedStream::LocalSocket(stream),
            }));
            0
        }
        Err(error) => {
            set_io_error("failed to connect to local socket", &error);
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ipc_unnamed_pipe_pair(
    out_sender: *mut *mut ipc_stream,
    out_receiver: *mut *mut ipc_stream,
) -> i32 {
    clear_error();
    if out_sender.is_null() {
        set_error("out_sender is null");
        return -1;
    }
    if out_receiver.is_null() {
        set_error("out_receiver is null");
        return -1;
    }
    match pipe() {
        Ok((sender, receiver)) => {
            *out_sender = Box::into_raw(Box::new(ipc_stream {
                inner: OwnedStream::PipeSender(sender),
            }));
            *out_receiver = Box::into_raw(Box::new(ipc_stream {
                inner: OwnedStream::PipeRecver(receiver),
            }));
            0
        }
        Err(error) => {
            set_io_error("failed to create unnamed pipe", &error);
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ipc_stream_read(
    stream: *mut ipc_stream,
    buffer: *mut c_void,
    size: usize,
) -> usize {
    clear_error();
    if stream.is_null() {
        set_error("stream is null");
        return IPCPP_ERROR_SIZE;
    }
    if buffer.is_null() && size != 0 {
        set_error("buffer is null");
        return IPCPP_ERROR_SIZE;
    }
    let slice: &mut [u8] = if size == 0 {
        &mut []
    } else {
        std::slice::from_raw_parts_mut(buffer as *mut u8, size)
    };
    match (*stream).inner.read(slice) {
        Ok(read) => read,
        Err(error) => {
            set_io_error("failed to read from stream", &error);
            IPCPP_ERROR_SIZE
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ipc_stream_write(
    stream: *mut ipc_stream,
    buffer: *const c_void,
    size: usize,
) -> i32 {
    clear_error();
    if stream.is_null() {
        set_error("stream is null");
        return -1;
    }
    if buffer.is_null() && size != 0 {
        set_error("buffer is null");
        return -1;
    }
    let slice: &[u8] = if size == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(buffer as *const u8, size)
    };
    match (*stream).inner.write_all(slice) {
        Ok(()) => 0,
        Err(error) => {
            set_io_error("failed to write to stream", &error);
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ipc_stream_close(stream: *mut ipc_stream) -> i32 {
    clear_error();
    if stream.is_null() {
        set_error("stream is null");
        return -1;
    }
    drop(Box::from_raw(stream));
    0
}

#[no_mangle]
pub unsafe extern "C" fn ipc_listener_close(listener: *mut ipc_listener) -> i32 {
    clear_error();
    if listener.is_null() {
        set_error("listener is null");
        return -1;
    }
    drop(Box::from_raw(listener));
    0
}
