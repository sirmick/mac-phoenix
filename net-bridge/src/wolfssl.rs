//! Thin safe wrapper around vendored wolfSSL.
//!
//! Replaces the OpenSSL+legacy-provider mess for the guest-facing TLS
//! acceptor (and upstream client). wolfSSL was built (see
//! tools/build-wolfssl.sh) with `--enable-sslv3 --enable-oldtls
//! --enable-arc4 --enable-rc2 --enable-des3 --enable-md5` so all the
//! cipher suites a classic-Mac browser actually negotiates are available.
//!
//! Only the surface tls_listener.rs uses is bound; widen as needed.

use std::ffi::{c_char, c_int, c_long, c_uchar, c_void, CStr, CString};
use std::io::{self, Read, Write};
use std::ptr;
use std::sync::{Arc, Mutex, Once};

// -------- FFI surface ----------------------------------------------------

#[repr(C)]
pub struct WOLFSSL_CTX {
    _private: [u8; 0],
}
#[repr(C)]
pub struct WOLFSSL {
    _private: [u8; 0],
}
#[repr(C)]
pub struct WOLFSSL_METHOD {
    _private: [u8; 0],
}

// Constants (from wolfssl/ssl.h + wolfssl/internal.h)
const WOLFSSL_SSLV3: c_int = 0;
const WOLFSSL_FILETYPE_PEM: c_int = 1;
const SSL_VERIFY_NONE: c_int = 0;
const SSL_VERIFY_PEER: c_int = 1;
const WOLFSSL_SUCCESS: c_int = 1;
const WOLFSSL_FATAL_ERROR: c_int = -1;
const WOLFSSL_CBIO_ERR_WANT_READ: c_int = -2;
const WOLFSSL_CBIO_ERR_WANT_WRITE: c_int = -2;
const WOLFSSL_CBIO_ERR_GENERAL: c_int = -1;
// Error codes from wolfSSL_get_error
const SSL_ERROR_WANT_READ: c_int = 2;
const SSL_ERROR_WANT_WRITE: c_int = 3;
const SSL_ERROR_ZERO_RETURN: c_int = 6;

type CallbackIORecv = extern "C" fn(*mut WOLFSSL, *mut c_char, c_int, *mut c_void) -> c_int;
type CallbackIOSend = extern "C" fn(*mut WOLFSSL, *mut c_char, c_int, *mut c_void) -> c_int;

#[link(name = "wolfssl", kind = "static")]
extern "C" {
    fn wolfSSL_Init() -> c_int;
    fn wolfSSL_Cleanup() -> c_int;

    fn wolfSSLv23_server_method() -> *mut WOLFSSL_METHOD;
    fn wolfSSLv23_client_method() -> *mut WOLFSSL_METHOD;

    fn wolfSSL_CTX_new(method: *mut WOLFSSL_METHOD) -> *mut WOLFSSL_CTX;
    fn wolfSSL_CTX_free(ctx: *mut WOLFSSL_CTX);
    fn wolfSSL_CTX_SetMinVersion(ctx: *mut WOLFSSL_CTX, version: c_int) -> c_int;
    fn wolfSSL_CTX_set_cipher_list(ctx: *mut WOLFSSL_CTX, list: *const c_char) -> c_int;
    fn wolfSSL_CTX_use_certificate_buffer(
        ctx: *mut WOLFSSL_CTX,
        cert: *const c_uchar,
        sz: c_long,
        format: c_int,
    ) -> c_int;
    fn wolfSSL_CTX_use_PrivateKey_buffer(
        ctx: *mut WOLFSSL_CTX,
        key: *const c_uchar,
        sz: c_long,
        format: c_int,
    ) -> c_int;
    fn wolfSSL_CTX_set_verify(
        ctx: *mut WOLFSSL_CTX,
        mode: c_int,
        cb: Option<extern "C" fn(c_int, *mut c_void) -> c_int>,
    );
    fn wolfSSL_CTX_load_system_CA_certs(ctx: *mut WOLFSSL_CTX) -> c_int;

    fn wolfSSL_CTX_SetIORecv(ctx: *mut WOLFSSL_CTX, cb: CallbackIORecv);
    fn wolfSSL_CTX_SetIOSend(ctx: *mut WOLFSSL_CTX, cb: CallbackIOSend);
    fn wolfSSL_SetIOReadCtx(ssl: *mut WOLFSSL, ptr: *mut c_void);
    fn wolfSSL_SetIOWriteCtx(ssl: *mut WOLFSSL, ptr: *mut c_void);

    fn wolfSSL_new(ctx: *mut WOLFSSL_CTX) -> *mut WOLFSSL;
    fn wolfSSL_free(ssl: *mut WOLFSSL);
    fn wolfSSL_accept(ssl: *mut WOLFSSL) -> c_int;
    fn wolfSSL_connect(ssl: *mut WOLFSSL) -> c_int;
    fn wolfSSL_read(ssl: *mut WOLFSSL, buf: *mut c_void, sz: c_int) -> c_int;
    fn wolfSSL_write(ssl: *mut WOLFSSL, buf: *const c_void, sz: c_int) -> c_int;
    fn wolfSSL_shutdown(ssl: *mut WOLFSSL) -> c_int;

    fn wolfSSL_get_error(ssl: *mut WOLFSSL, ret: c_int) -> c_int;
    fn wolfSSL_ERR_error_string(err: c_long, buf: *mut c_char) -> *mut c_char;
    fn wolfSSL_get_version(ssl: *const WOLFSSL) -> *const c_char;
    fn wolfSSL_get_cipher_name(ssl: *mut WOLFSSL) -> *const c_char;
    fn wolfSSL_lib_version() -> *const c_char;

    fn wolfSSL_UseSNI(
        ssl: *mut WOLFSSL,
        sni_type: c_uchar,
        data: *const c_void,
        size: u16,
    ) -> c_int;
    // SNI type 0 = host_name (RFC 6066)
}
const WOLFSSL_SNI_HOST_NAME: c_uchar = 0;

// -------- One-time global init ------------------------------------------

static INIT: Once = Once::new();

pub fn init() {
    INIT.call_once(|| {
        unsafe { wolfSSL_Init() };
        let v = unsafe { CStr::from_ptr(wolfSSL_lib_version()) }
            .to_string_lossy()
            .into_owned();
        log::info!("TLS: wolfSSL initialised (version {})", v);
    });
}

// -------- Errors ---------------------------------------------------------

#[derive(Debug)]
pub enum WolfError {
    /// wolfSSL returned a fatal error from a handshake / I/O call. The
    /// inner code is from wolfSSL_get_error().
    Ssl { code: c_int, detail: String },
    /// FFI returned a NULL pointer when constructing a CTX or SSL.
    NullPointer(&'static str),
    /// Wrapping I/O reported an error other than WouldBlock.
    Io(io::Error),
}

impl std::fmt::Display for WolfError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            WolfError::Ssl { code, detail } => {
                write!(f, "wolfSSL error {}: {}", code, detail)
            }
            WolfError::NullPointer(s) => write!(f, "wolfSSL null pointer: {}", s),
            WolfError::Io(e) => write!(f, "wolfSSL I/O error: {}", e),
        }
    }
}

impl std::error::Error for WolfError {}

impl From<io::Error> for WolfError {
    fn from(e: io::Error) -> Self {
        WolfError::Io(e)
    }
}

fn err_string(code: c_int) -> String {
    let mut buf = vec![0u8; 80];
    unsafe {
        wolfSSL_ERR_error_string(code as c_long, buf.as_mut_ptr() as *mut c_char);
    }
    let nul = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    buf.truncate(nul);
    String::from_utf8_lossy(&buf).into_owned()
}

// -------- Server context (acceptor) -------------------------------------

/// SSL_CTX wrapper. `Send`+`Sync` only because wolfSSL's per-CTX state is
/// internally synchronised and we use it from a single thread anyway.
pub struct ServerCtx {
    ctx: *mut WOLFSSL_CTX,
}

unsafe impl Send for ServerCtx {}
unsafe impl Sync for ServerCtx {}

impl ServerCtx {
    /// Build an SSLv3-floor / TLS1.2-ceiling acceptor with the legacy
    /// cipher list, using the supplied PEM cert + key.
    pub fn new(cert_pem: &[u8], key_pem: &[u8], cipher_list: &str) -> Result<Self, WolfError> {
        init();
        let ctx = unsafe { wolfSSL_CTX_new(wolfSSLv23_server_method()) };
        if ctx.is_null() {
            return Err(WolfError::NullPointer("CTX_new(server)"));
        }
        let me = ServerCtx { ctx };

        // SSLv3 floor — overrides the default TLS1.2 minimum.
        ssl_check(
            unsafe { wolfSSL_CTX_SetMinVersion(ctx, WOLFSSL_SSLV3) },
            "SetMinVersion(SSLV3)",
        )?;

        let cipher_c = CString::new(cipher_list).unwrap();
        ssl_check(
            unsafe { wolfSSL_CTX_set_cipher_list(ctx, cipher_c.as_ptr()) },
            "set_cipher_list",
        )?;

        ssl_check(
            unsafe {
                wolfSSL_CTX_use_certificate_buffer(
                    ctx,
                    cert_pem.as_ptr(),
                    cert_pem.len() as c_long,
                    WOLFSSL_FILETYPE_PEM,
                )
            },
            "use_certificate_buffer",
        )?;

        ssl_check(
            unsafe {
                wolfSSL_CTX_use_PrivateKey_buffer(
                    ctx,
                    key_pem.as_ptr(),
                    key_pem.len() as c_long,
                    WOLFSSL_FILETYPE_PEM,
                )
            },
            "use_PrivateKey_buffer",
        )?;

        // Acceptor doesn't ask the client for a cert.
        unsafe { wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, None) };

        // Wire the custom-IO callbacks. The actual fn pointers live in
        // CustomIo; we set per-SSL pointers in `accept_with_io`.
        unsafe {
            wolfSSL_CTX_SetIORecv(ctx, custom_io_recv);
            wolfSSL_CTX_SetIOSend(ctx, custom_io_send);
        }

        Ok(me)
    }

    pub fn raw(&self) -> *mut WOLFSSL_CTX {
        self.ctx
    }
}

impl Drop for ServerCtx {
    fn drop(&mut self) {
        unsafe { wolfSSL_CTX_free(self.ctx) };
    }
}

// -------- Client context (upstream) -------------------------------------

pub struct ClientCtx {
    ctx: *mut WOLFSSL_CTX,
}

unsafe impl Send for ClientCtx {}
unsafe impl Sync for ClientCtx {}

impl ClientCtx {
    /// Modern TLS-client context that validates against the system trust
    /// store. Used for the upstream hop where we talk to the real server.
    pub fn new() -> Result<Self, WolfError> {
        init();
        let ctx = unsafe { wolfSSL_CTX_new(wolfSSLv23_client_method()) };
        if ctx.is_null() {
            return Err(WolfError::NullPointer("CTX_new(client)"));
        }
        let me = ClientCtx { ctx };
        unsafe {
            wolfSSL_CTX_load_system_CA_certs(ctx);
            wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, None);
            wolfSSL_CTX_SetIORecv(ctx, custom_io_recv);
            wolfSSL_CTX_SetIOSend(ctx, custom_io_send);
        }
        Ok(me)
    }

    pub fn raw(&self) -> *mut WOLFSSL_CTX {
        self.ctx
    }
}

impl Drop for ClientCtx {
    fn drop(&mut self) {
        unsafe { wolfSSL_CTX_free(self.ctx) };
    }
}

// -------- Custom I/O bridge --------------------------------------------
//
// wolfSSL calls into recv/send callbacks during handshake and read/write.
// We hand each WOLFSSL* a Box<CustomIo<T>> raw pointer and dispatch into
// `T: Read + Write`. Errors that look like "no data right now" are
// translated to WOLFSSL_CBIO_ERR_WANT_READ/WRITE so wolfSSL retries
// (matches the old SslStream<GuestIo> behaviour).

struct CustomIo<IO: Read + Write> {
    io: IO,
    last_io_err: Option<io::Error>,
}

extern "C" fn custom_io_recv(
    _ssl: *mut WOLFSSL,
    buf: *mut c_char,
    sz: c_int,
    ctx: *mut c_void,
) -> c_int {
    if ctx.is_null() {
        log::error!("wolfSSL recv cb: NULL ctx");
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
    // SAFETY: pointer was set via SetIOReadCtx with our CustomIo box ptr.
    let io = unsafe { &mut *(ctx as *mut CustomIo<DynIo>) };
    let slice = unsafe { std::slice::from_raw_parts_mut(buf as *mut u8, sz as usize) };
    let result = io.io.read(slice);
    log::trace!("wolfSSL recv cb: requested={} result={:?}", sz, result.as_ref().map(|n| *n));
    match result {
        Ok(0) => 0, // EOF
        Ok(n) => n as c_int,
        Err(e) => {
            if e.kind() == io::ErrorKind::WouldBlock {
                WOLFSSL_CBIO_ERR_WANT_READ
            } else {
                log::warn!("wolfSSL recv cb: io error: {}", e);
                io.last_io_err = Some(e);
                WOLFSSL_CBIO_ERR_GENERAL
            }
        }
    }
}

extern "C" fn custom_io_send(
    _ssl: *mut WOLFSSL,
    buf: *mut c_char,
    sz: c_int,
    ctx: *mut c_void,
) -> c_int {
    if ctx.is_null() {
        log::error!("wolfSSL send cb: NULL ctx");
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
    let io = unsafe { &mut *(ctx as *mut CustomIo<DynIo>) };
    let slice = unsafe { std::slice::from_raw_parts(buf as *const u8, sz as usize) };
    let result = io.io.write(slice);
    log::trace!("wolfSSL send cb: bytes={} result={:?}", sz, result.as_ref().map(|n| *n));
    match result {
        Ok(n) => n as c_int,
        Err(e) => {
            if e.kind() == io::ErrorKind::WouldBlock {
                WOLFSSL_CBIO_ERR_WANT_WRITE
            } else {
                log::warn!("wolfSSL send cb: io error: {}", e);
                io.last_io_err = Some(e);
                WOLFSSL_CBIO_ERR_GENERAL
            }
        }
    }
}

/// Boxed dyn Read+Write — single concrete type so the C callback pointer
/// signatures match across all I/O backings.
type DynIo = Box<dyn ReadWrite + Send>;

pub trait ReadWrite: Read + Write {}
impl<T: Read + Write> ReadWrite for T {}

// -------- TLS stream (server-side accept + I/O) --------------------------

/// One in-flight wolfSSL session over a Rust I/O type.
pub struct WolfStream {
    ssl: *mut WOLFSSL,
    // Box held alive by the WOLFSSL via the io ctx pointer; when WolfStream
    // drops we free the SSL first (which stops calling into io_ctx) then
    // drop io_ctx.
    io_ctx: *mut CustomIo<DynIo>,
    handshake_done: bool,
}

unsafe impl Send for WolfStream {}

impl WolfStream {
    fn new_with_io(ctx: *mut WOLFSSL_CTX, io: DynIo) -> Result<Self, WolfError> {
        let ssl = unsafe { wolfSSL_new(ctx) };
        if ssl.is_null() {
            return Err(WolfError::NullPointer("SSL_new"));
        }
        let io_ctx = Box::into_raw(Box::new(CustomIo {
            io,
            last_io_err: None,
        }));
        unsafe {
            wolfSSL_SetIOReadCtx(ssl, io_ctx as *mut c_void);
            wolfSSL_SetIOWriteCtx(ssl, io_ctx as *mut c_void);
        }
        Ok(WolfStream {
            ssl,
            io_ctx,
            handshake_done: false,
        })
    }

    /// Set the SNI hint sent in our ClientHello (upstream client).
    pub fn set_sni(&mut self, host: &str) -> Result<(), WolfError> {
        let bytes = host.as_bytes();
        ssl_check(
            unsafe {
                wolfSSL_UseSNI(
                    self.ssl,
                    WOLFSSL_SNI_HOST_NAME,
                    bytes.as_ptr() as *const c_void,
                    bytes.len() as u16,
                )
            },
            "UseSNI",
        )
    }

    /// Drive the handshake. Returns Ok(true) on completion, Ok(false) if
    /// it would block (caller should pump I/O and call again), Err on
    /// fatal failure.
    pub fn do_handshake_accept(&mut self) -> Result<bool, WolfError> {
        self.do_handshake_step(true)
    }

    pub fn do_handshake_connect(&mut self) -> Result<bool, WolfError> {
        self.do_handshake_step(false)
    }

    fn do_handshake_step(&mut self, accept: bool) -> Result<bool, WolfError> {
        if self.handshake_done {
            return Ok(true);
        }
        let r = unsafe {
            if accept {
                wolfSSL_accept(self.ssl)
            } else {
                wolfSSL_connect(self.ssl)
            }
        };
        if r == WOLFSSL_SUCCESS {
            self.handshake_done = true;
            return Ok(true);
        }
        let err = unsafe { wolfSSL_get_error(self.ssl, r) };
        match err {
            SSL_ERROR_WANT_READ | SSL_ERROR_WANT_WRITE => Ok(false),
            _ => {
                // Surface the inner io::Error if the IO callback stashed one.
                let io = unsafe { &mut *self.io_ctx };
                if let Some(e) = io.last_io_err.take() {
                    return Err(WolfError::Io(e));
                }
                Err(WolfError::Ssl {
                    code: err,
                    detail: err_string(err as c_int),
                })
            }
        }
    }

    pub fn current_cipher_name(&mut self) -> Option<String> {
        let p = unsafe { wolfSSL_get_cipher_name(self.ssl) };
        if p.is_null() {
            return None;
        }
        Some(unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
    }

    pub fn version(&self) -> Option<String> {
        let p = unsafe { wolfSSL_get_version(self.ssl) };
        if p.is_null() {
            return None;
        }
        Some(unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
    }
}

impl Drop for WolfStream {
    fn drop(&mut self) {
        // Best-effort close-notify, then free the SSL object before the IO
        // ctx is reclaimed (otherwise wolfSSL might call into freed memory).
        unsafe {
            wolfSSL_shutdown(self.ssl);
            wolfSSL_free(self.ssl);
            // Reclaim the boxed CustomIo.
            drop(Box::from_raw(self.io_ctx));
        }
    }
}

impl Read for WolfStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let r = unsafe {
            wolfSSL_read(
                self.ssl,
                buf.as_mut_ptr() as *mut c_void,
                buf.len() as c_int,
            )
        };
        if r > 0 {
            return Ok(r as usize);
        }
        let err = unsafe { wolfSSL_get_error(self.ssl, r) };
        match err {
            SSL_ERROR_WANT_READ | SSL_ERROR_WANT_WRITE => {
                Err(io::Error::from(io::ErrorKind::WouldBlock))
            }
            SSL_ERROR_ZERO_RETURN => Ok(0),
            _ => {
                let io = unsafe { &mut *self.io_ctx };
                if let Some(e) = io.last_io_err.take() {
                    return Err(e);
                }
                Err(io::Error::new(
                    io::ErrorKind::Other,
                    format!("wolfSSL_read err={} ({})", err, err_string(err as c_int)),
                ))
            }
        }
    }
}

impl Write for WolfStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let r = unsafe {
            wolfSSL_write(
                self.ssl,
                buf.as_ptr() as *const c_void,
                buf.len() as c_int,
            )
        };
        if r > 0 {
            return Ok(r as usize);
        }
        let err = unsafe { wolfSSL_get_error(self.ssl, r) };
        match err {
            SSL_ERROR_WANT_READ | SSL_ERROR_WANT_WRITE => {
                Err(io::Error::from(io::ErrorKind::WouldBlock))
            }
            _ => {
                let io = unsafe { &mut *self.io_ctx };
                if let Some(e) = io.last_io_err.take() {
                    return Err(e);
                }
                Err(io::Error::new(
                    io::ErrorKind::Other,
                    format!("wolfSSL_write err={} ({})", err, err_string(err as c_int)),
                ))
            }
        }
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

// -------- Convenience constructors --------------------------------------

impl ServerCtx {
    pub fn accept_with_io<IO: ReadWrite + Send + 'static>(
        &self,
        io: IO,
    ) -> Result<WolfStream, WolfError> {
        WolfStream::new_with_io(self.ctx, Box::new(io))
    }
}

impl ClientCtx {
    pub fn connect_with_io<IO: ReadWrite + Send + 'static>(
        &self,
        io: IO,
    ) -> Result<WolfStream, WolfError> {
        WolfStream::new_with_io(self.ctx, Box::new(io))
    }
}

// -------- Helpers --------------------------------------------------------

fn ssl_check(rv: c_int, what: &'static str) -> Result<(), WolfError> {
    if rv == WOLFSSL_SUCCESS {
        Ok(())
    } else {
        Err(WolfError::Ssl {
            code: rv,
            detail: format!("{} returned {}", what, rv),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn init_smoke() {
        init();
        // Idempotent.
        init();
    }
}
