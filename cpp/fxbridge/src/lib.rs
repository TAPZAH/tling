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

/// ISO 639-1 код языка по тексту (UTF-8) или пустая строка, если язык не
/// определён, не поддерживается приложением или уверенность слишком низкая
/// (тогда ядро использует определение по письму). `allowlist` — языки через
/// запятую, среди которых выбирать; пустая строка снимает ограничение.
/// Строку освобождает fxt_string_free.
#[no_mangle]
pub extern "C" fn fxt_detect_language(
    text: *const c_char,
    allowlist: *const c_char,
) -> *mut c_char {
    let outcome = catch_unwind(|| {
        let Some(text) = path_from_ptr(text) else {
            return String::new();
        };
        if text.trim().is_empty() {
            return String::new();
        }
        let allowed: Vec<whatlang::Lang> = path_from_ptr(allowlist)
            .map(|list| {
                list.split(',')
                    .filter_map(|code| lang_from_iso(code.trim()))
                    .collect()
            })
            .unwrap_or_default();
        // 1) Полный набор языков: уверенное определение используем как есть.
        //    Так турецкий текст распознаётся даже без установленной модели, и
        //    приложение предлагает её скачать.
        if let Some(info) = whatlang::detect(&text) {
            if info.confidence() >= 0.20 {
                let code = iso639_1(info.lang());
                if !code.is_empty() {
                    return code.to_string();
                }
            }
        }
        // 2) Среди доступных языков: короткие фразы вроде «Guten Morgen»
        //    полный набор путает (nb), а список установленных моделей — нет.
        if !allowed.is_empty() {
            if let Some(info) =
                whatlang::Detector::with_allowlist(allowed).detect(&text)
            {
                if info.confidence() >= 0.10 {
                    let code = iso639_1(info.lang());
                    if !code.is_empty() {
                        return code.to_string();
                    }
                }
            }
        }
        String::new()
    });
    match outcome {
        Ok(code) => CString::new(code).unwrap_or_default().into_raw(),
        Err(_) => CString::default().into_raw(),
    }
}

/// ISO 639-1 -> whatlang::Lang для языков, которые поддерживает TLing.
fn lang_from_iso(code: &str) -> Option<whatlang::Lang> {
    use whatlang::Lang;
    Some(match code {
        "af" => Lang::Afr,
        "ar" => Lang::Ara,
        "az" => Lang::Aze,
        "be" => Lang::Bel,
        "ben" => Lang::Ben,
        "bn" => Lang::Ben,
        "bg" => Lang::Bul,
        "ca" => Lang::Cat,
        "cs" => Lang::Ces,
        "zh" => Lang::Cmn,
        "da" => Lang::Dan,
        "de" => Lang::Deu,
        "el" => Lang::Ell,
        "en" => Lang::Eng,
        "eo" => Lang::Epo,
        "et" => Lang::Est,
        "fi" => Lang::Fin,
        "fr" => Lang::Fra,
        "gu" => Lang::Guj,
        "he" => Lang::Heb,
        "hi" => Lang::Hin,
        "hr" => Lang::Hrv,
        "hu" => Lang::Hun,
        "id" => Lang::Ind,
        "it" => Lang::Ita,
        "ja" => Lang::Jpn,
        "kn" => Lang::Kan,
        "ka" => Lang::Kat,
        "ko" => Lang::Kor,
        "lv" => Lang::Lav,
        "lt" => Lang::Lit,
        "ml" => Lang::Mal,
        "mk" => Lang::Mkd,
        "nl" => Lang::Nld,
        "nb" => Lang::Nob,
        "fa" => Lang::Pes,
        "pl" => Lang::Pol,
        "pt" => Lang::Por,
        "pb" => Lang::Por,
        "ro" => Lang::Ron,
        "ru" => Lang::Rus,
        "sk" => Lang::Slk,
        "sl" => Lang::Slv,
        "sr" => Lang::Srp,
        "sv" => Lang::Swe,
        "ta" => Lang::Tam,
        "te" => Lang::Tel,
        "tl" => Lang::Tgl,
        "th" => Lang::Tha,
        "tr" => Lang::Tur,
        "uk" => Lang::Ukr,
        "ur" => Lang::Urd,
        "vi" => Lang::Vie,
        _ => return None,
    })
}

/// whatlang::Lang -> ISO 639-1 для языков, которые поддерживает TLing.
fn iso639_1(lang: whatlang::Lang) -> &'static str {
    use whatlang::Lang;
    match lang {
        Lang::Afr => "af",
        Lang::Ara => "ar",
        Lang::Aze => "az",
        Lang::Bel => "be",
        Lang::Ben => "bn",
        Lang::Bul => "bg",
        Lang::Cat => "ca",
        Lang::Ces => "cs",
        Lang::Cmn => "zh",
        Lang::Dan => "da",
        Lang::Deu => "de",
        Lang::Ell => "el",
        Lang::Eng => "en",
        Lang::Epo => "eo",
        Lang::Est => "et",
        Lang::Fin => "fi",
        Lang::Fra => "fr",
        Lang::Guj => "gu",
        Lang::Heb => "he",
        Lang::Hin => "hi",
        Lang::Hrv => "hr",
        Lang::Hun => "hu",
        Lang::Ind => "id",
        Lang::Ita => "it",
        Lang::Jpn => "ja",
        Lang::Kan => "kn",
        Lang::Kat => "ka",
        Lang::Kor => "ko",
        Lang::Lav => "lv",
        Lang::Lit => "lt",
        Lang::Mal => "ml",
        Lang::Mkd => "mk",
        Lang::Nld => "nl",
        Lang::Nob => "nb",
        Lang::Pes => "fa",
        Lang::Pol => "pl",
        Lang::Por => "pt",
        Lang::Ron => "ro",
        Lang::Rus => "ru",
        Lang::Slk => "sk",
        Lang::Slv => "sl",
        Lang::Srp => "sr",
        Lang::Swe => "sv",
        Lang::Tam => "ta",
        Lang::Tel => "te",
        Lang::Tgl => "tl",
        Lang::Tha => "th",
        Lang::Tur => "tr",
        Lang::Ukr => "uk",
        Lang::Urd => "ur",
        Lang::Vie => "vi",
        _ => "",
    }
}


#[cfg(test)]
mod tests {
    use super::{iso639_1, lang_from_iso};

    #[test]
    fn maps_supported_iso_codes() {
        for code in ["tr", "ru", "en", "de", "fr", "zh", "ja", "ko", "ar", "eo"] {
            assert!(
                lang_from_iso(code).is_some(),
                "код {code} должен поддерживаться"
            );
        }
        assert!(lang_from_iso("xx").is_none());
    }

    #[test]
    fn detects_common_languages() {
        let cases = [
            ("Merhaba, dünya! Nasılsın?", "tr"),
            ("Привет, мир", "ru"),
            ("Bonjour le monde", "fr"),
            ("Saluton mondo, kiel vi fartas?", "eo"),
            ("こんにちは、世界", "ja"),
        ];
        for (text, expected) in cases {
            let info = whatlang::detect(text).expect("язык определён");
            assert_eq!(iso639_1(info.lang()), expected, "текст: {text}");
        }
    }

    #[test]
    fn allowlist_narrows_detection() {
        let detector = whatlang::Detector::with_allowlist(vec![
            whatlang::Lang::Deu,
            whatlang::Lang::Eng,
            whatlang::Lang::Rus,
        ]);
        let info = detector.detect("Guten Morgen").expect("язык определён");
        assert_eq!(iso639_1(info.lang()), "de");
    }
}
