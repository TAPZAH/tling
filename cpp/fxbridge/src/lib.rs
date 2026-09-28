//! C-ABI мост между C++-ядром TLing и нативным Rust-движком
//! fxtranslate (Firefox Translations).
//!
//! Гарантия потоков: Engine крейта привязан к создавшему потоку, поэтому
//! fxt_engine_create / fxt_translate / fxt_engine_destroy должны вызываться
//! из одного и того же потока (в ядре это единственный рабочий поток
//! TranslationService — как и в Python FirefoxEngine).

use std::cell::RefCell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::panic::catch_unwind;
use std::path::Path;
use std::ptr;

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn set_error(message: impl Into<String>) {
    let text = message.into();
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = CString::new(text).ok();
    });
}

/// Последняя ошибка на текущем потоке (UTF-8) или пустая строка.
#[no_mangle]
pub extern "C" fn fxt_last_error() -> *mut c_char {
    match LAST_ERROR.with(|slot| slot.borrow().clone()) {
        Some(value) => value.into_raw(),
        None => CString::default().into_raw(),
    }
}

fn path_from_ptr(pointer: *const c_char) -> Option<String> {
    if pointer.is_null() {
        return None;
    }
    // Ненулевой указатель обязан вести на валидную C-строку UTF-8.
    let slice = unsafe { CStr::from_ptr(pointer) };
    Some(slice.to_string_lossy().into_owned())
}

struct CreateArgs {
    model: String,
    src_vocab: String,
    trg_vocab: String,
    shortlist: Option<String>,
}

fn log_path(kind: &str, path: &str) {
    if std::env::var_os("FXBRIDGE_LOG").is_some() {
        eprintln!("[fxbridge] {kind}: {path}");
    }
}

fn build_engine(args: CreateArgs) -> Result<fxtranslate::engine::Engine, String> {
    log_path("model", &args.model);
    log_path("src_vocab", &args.src_vocab);
    log_path("trg_vocab", &args.trg_vocab);
    if let Some(path) = &args.shortlist {
        log_path("shortlist", path);
    }
    let engine = fxtranslate::engine::Engine::load(
        Path::new(&args.model),
        Path::new(&args.src_vocab),
        Path::new(&args.trg_vocab),
    )
    .map_err(|error| {
        set_error(format!("Engine::load: {error}"));
        error
    })?;
    Ok(match args.shortlist {
        Some(path) => match std::fs::read(Path::new(&path)) {
            Ok(bytes) => engine.with_shortlist_bytes(&bytes),
            Err(error) => {
                let message = format!("lex.bin: {error}");
                set_error(message.clone());
                return Err(message);
            }
        },
        None => engine,
    })
}

/// Создаёт движок по путям model.bin / словарей / опционального lex.bin.
/// Возвращает handle или null при ошибке.
#[no_mangle]
pub extern "C" fn fxt_engine_create(
    model_path: *const c_char,
    src_vocab_path: *const c_char,
    trg_vocab_path: *const c_char,
    shortlist_path: *const c_char,
) -> *mut c_void {
    let result = catch_unwind(|| {
        let (Some(model), Some(src_vocab), Some(trg_vocab)) = (
            path_from_ptr(model_path),
            path_from_ptr(src_vocab_path),
            path_from_ptr(trg_vocab_path),
        ) else {
            set_error("null-указатель в аргументах fxt_engine_create");
            return ptr::null_mut();
        };
        let args = CreateArgs {
            model,
            src_vocab,
            trg_vocab,
            shortlist: path_from_ptr(shortlist_path),
        };
        match build_engine(args) {
            Ok(engine) => Box::into_raw(Box::new(engine)) as *mut c_void,
            Err(error) => {
                set_error(format!("создание движка не удалось: {error}"));
                ptr::null_mut()
            }
        }
    });
    result.unwrap_or_else(|_| {
        set_error("паника Rust внутри fxt_engine_create");
        ptr::null_mut()
    })
}

fn translate_impl(handle: *mut c_void, text: *const c_char, long: bool) -> *mut c_char {
    let outcome = catch_unwind(|| unsafe {
        if handle.is_null() || text.is_null() {
            return ptr::null_mut();
        }
        let input = CStr::from_ptr(text).to_string_lossy().into_owned();
        let engine = &*(handle as *const fxtranslate::engine::Engine);
        let translated = if long {
            engine.translate_long(&input)
        } else {
            engine.translate(&input)
        };
        match CString::new(translated) {
            Ok(value) => value.into_raw(),
            Err(_) => ptr::null_mut(),
        }
    });
    outcome.unwrap_or(ptr::null_mut())
}

/// Перевод одного отрезка (вход длиннее контекста обрезается).
#[no_mangle]
pub extern "C" fn fxt_translate(
    handle: *mut c_void,
    text: *const c_char,
) -> *mut c_char {
    translate_impl(handle, text, false)
}

/// Перевод произвольной длины с сегментацией предложений (ICU4X).
#[no_mangle]
pub extern "C" fn fxt_translate_long(
    handle: *mut c_void,
    text: *const c_char,
) -> *mut c_char {
    translate_impl(handle, text, true)
}

/// Освобождает строку, возвращённую fxt_translate / fxt_backend.
#[no_mangle]
pub extern "C" fn fxt_string_free(string: *mut c_char) {
    if !string.is_null() {
        unsafe {
            drop(CString::from_raw(string));
        }
    }
}

/// Имя активного SIMD-бэкенда int8 GEMM (для диагностики).
#[no_mangle]
pub extern "C" fn fxt_backend() -> *mut c_char {
    let outcome = catch_unwind(|| {
        match CString::new(fxtranslate::gemm::backend()) {
            Ok(value) => value.into_raw(),
            Err(_) => ptr::null_mut(),
        }
    });
    outcome.unwrap_or(ptr::null_mut())
}

/// Уничтожает движок. Handle после вызова невалиден.
#[no_mangle]
pub extern "C" fn fxt_engine_destroy(handle: *mut c_void) {
    if handle.is_null() {
        return;
    }
    let _ = catch_unwind(|| unsafe {
        drop(Box::from_raw(handle as *mut fxtranslate::engine::Engine));
    });
}

