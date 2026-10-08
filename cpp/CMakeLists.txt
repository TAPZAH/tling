cmake_minimum_required(VERSION 3.20)
project(offline_translator_core LANGUAGES CXX)

if (WIN32)
    enable_language(RC)
endif()

if (POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json CONFIG REQUIRED)
option(ENABLE_CTRANSLATE2 "РџРѕРґРєР»СЋС‡РёС‚СЊ СЃРѕР±СЂР°РЅРЅС‹Р№ CTranslate2" OFF)
set(CTranslate2_ROOT "" CACHE PATH "РљРѕСЂРµРЅСЊ РёСЃС…РѕРґРЅРёРєРѕРІ Рё СЃР±РѕСЂРєРё CTranslate2")
set(CTranslate2_BUILD_DIR "" CACHE PATH "РљР°С‚Р°Р»РѕРі CMake-СЃР±РѕСЂРєРё CTranslate2")

if (ENABLE_CTRANSLATE2)
    if (NOT CTranslate2_ROOT)
        message(FATAL_ERROR "Р”Р»СЏ ENABLE_CTRANSLATE2 СѓРєР°Р¶РёС‚Рµ CTranslate2_ROOT")
    endif()
    if (NOT CTranslate2_BUILD_DIR)
        set(CTranslate2_BUILD_DIR
            "${CTranslate2_ROOT}/build-openblas-dnnl/ctranslate2")
    endif()
    # find_package РёРЅР°С‡Рµ РјРѕР¶РµС‚ РІР·СЏС‚СЊ Р·Р°РєСЌС€РёСЂРѕРІР°РЅРЅС‹Р№ CTranslate2_DIR
    # РѕС‚ РґСЂСѓРіРѕР№ СЃР±РѕСЂРєРё (РЅР°РїСЂРёРјРµСЂ C:/deps/CTranslate2/build Р±РµР· OpenBLAS).
    set(CTranslate2_DIR "${CTranslate2_BUILD_DIR}" CACHE PATH
        "РљР°С‚Р°Р»РѕРі CTranslate2Config.cmake" FORCE)
    find_package(CTranslate2 CONFIG REQUIRED
        PATHS "${CTranslate2_BUILD_DIR}"
        NO_DEFAULT_PATH
    )
    get_target_property(_ctranslate2_dll
        CTranslate2::ctranslate2 IMPORTED_LOCATION_RELEASE)
    message(STATUS "CTranslate2 DLL: ${_ctranslate2_dll}")
    # oneDNN СЂСЏРґРѕРј СЃ СЌС‚РѕР№ СЃР±РѕСЂРєРѕР№ CTranslate2 РґР°С‘С‚ СЌС„С„РµРєС‚РёРІРЅС‹Р№ int8 РґР»СЏ NLLB.
    if (NOT DNNL_DLL)
        get_filename_component(_ct2_install_root
            "${_ctranslate2_dll}" DIRECTORY)
        find_file(DNNL_DLL NAMES dnnl.dll
            PATHS "${_ct2_install_root}"
                  "${CTranslate2_ROOT}/build-openblas-dnnl/install/bin"
                  "C:/deps/oneDNN/install/bin"
            NO_DEFAULT_PATH)
    endif()
    if (DNNL_DLL)
        message(STATUS "DNNL DLL: ${DNNL_DLL}")
    endif()
    set_property(TARGET CTranslate2::ctranslate2
        PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release
    )
    find_path(SENTENCEPIECE_INCLUDE_DIR sentencepiece_processor.h REQUIRED)
    get_filename_component(SENTENCEPIECE_ROOT
        "${SENTENCEPIECE_INCLUDE_DIR}" DIRECTORY
    )
    find_library(SENTENCEPIECE_RELEASE_LIBRARY
        NAMES sentencepiece
        PATHS "${SENTENCEPIECE_ROOT}/lib"
        NO_DEFAULT_PATH
        REQUIRED
    )
    find_library(SENTENCEPIECE_DEBUG_LIBRARY
        NAMES sentencepiece
        PATHS "${SENTENCEPIECE_ROOT}/debug/lib"
        NO_DEFAULT_PATH
        REQUIRED
    )
    add_library(sentencepiece_compat STATIC IMPORTED GLOBAL)
    set_target_properties(sentencepiece_compat PROPERTIES
        IMPORTED_LOCATION_RELEASE "${SENTENCEPIECE_RELEASE_LIBRARY}"
        IMPORTED_LOCATION_DEBUG "${SENTENCEPIECE_DEBUG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SENTENCEPIECE_INCLUDE_DIR}"
    )
    find_package(absl CONFIG REQUIRED)
    find_package(protobuf CONFIG REQUIRED)
endif()

add_library(offline_translator_core
    src/app_language.cpp
    src/app_log.cpp
    src/app_settings.cpp
    src/app_update.cpp
    src/argos_engine.cpp
    src/argos_model_manager.cpp
    src/autostart.cpp
    src/clipboard.cpp
    src/clipboard_history.cpp
    src/compression.cpp
    src/file_transfer.cpp
    src/firefox_engine.cpp
    src/firefox_model_manager.cpp
    src/fs_utils.cpp
    src/hotkey.cpp
    src/language_store.cpp
    src/language_detect.cpp
    src/marian_engine.cpp
    src/marian_model_manager.cpp
    src/nllb_model_manager.cpp
    src/nllb_language.cpp
    src/nllb_engine.cpp
    src/online_engine.cpp
    src/selection.cpp
    src/text_split.cpp
    src/translation_application.cpp
    src/route_planner.cpp
    src/translation_service.cpp
    src/window_policy.cpp
    src/zip_archive.cpp
)

find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
target_link_libraries(offline_translator_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE ZLIB::ZLIB zstd::libzstd
)
# DLL РґР»СЏ РґРµРїР»РѕСЏ: zlib РёР· РёРјРїРѕСЂС‚РЅРѕР№ С†РµР»Рё, zstd вЂ” РёРЅС‚РµСЂС„РµР№СЃРЅР°СЏ, РёС‰РµРј С„Р°Р№Р»РѕРј.
set(_manifest_bin "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
get_target_property(_zlib_dll ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
if (NOT _zlib_dll)
    get_target_property(_zlib_dll ZLIB::ZLIB IMPORTED_LOCATION)
endif()
find_file(ZSTD_DLL NAMES zstd.dll libzstd.dll PATHS "${_manifest_bin}" NO_DEFAULT_PATH)
find_file(ZLIB_RUNTIME_DLL NAMES z.dll zlib1.dll PATHS "${_manifest_bin}" NO_DEFAULT_PATH)
if (NOT ZLIB_RUNTIME_DLL)
    set(ZLIB_RUNTIME_DLL "${_zlib_dll}")
endif()
message(STATUS "zlib DLL: ${ZLIB_RUNTIME_DLL}; zstd DLL: ${ZSTD_DLL}")

if (ENABLE_CTRANSLATE2)
    target_sources(offline_translator_core
        PRIVATE
            src/ctranslate2_engine.cpp
            src/sentencepiece_tokenizer.cpp
    )
    target_include_directories(offline_translator_core
        PRIVATE
            ${SENTENCEPIECE_INCLUDE_DIR}
    )

    add_executable(nllb_smoke
        tools/nllb_smoke.cpp
    )
    add_executable(argos_smoke
        tools/argos_smoke.cpp
    )
    add_executable(marian_smoke
        tools/marian_smoke.cpp
    )
    add_executable(translator_cli
        tools/translator_cli.cpp
    )
    add_executable(firefox_smoke
        tools/firefox_smoke.cpp
    )
    if (WIN32)
        add_executable(TLing WIN32
            app/win32_main.cpp
            app/tling.rc
        )
    endif()
    target_include_directories(nllb_smoke
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${SENTENCEPIECE_INCLUDE_DIR}
    )
    target_include_directories(argos_smoke
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${SENTENCEPIECE_INCLUDE_DIR}
    )
    target_include_directories(marian_smoke
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${SENTENCEPIECE_INCLUDE_DIR}
    )
    target_include_directories(translator_cli
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_include_directories(firefox_smoke
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(firefox_smoke
        PRIVATE
            offline_translator_core
    )
    if (WIN32)
        target_include_directories(TLing
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
    endif()
    target_link_libraries(nllb_smoke
        PRIVATE
            offline_translator_core
            CTranslate2::ctranslate2
            sentencepiece_compat
            protobuf::libprotobuf
            absl::base
            absl::strings
            absl::raw_hash_set
            absl::synchronization
            absl::log
            absl::flags
    )
    target_link_libraries(argos_smoke
        PRIVATE
            offline_translator_core
            CTranslate2::ctranslate2
            sentencepiece_compat
            protobuf::libprotobuf
            absl::base
            absl::strings
            absl::raw_hash_set
            absl::synchronization
            absl::log
            absl::flags
    )
    target_link_libraries(marian_smoke
        PRIVATE
            offline_translator_core
            CTranslate2::ctranslate2
            sentencepiece_compat
            protobuf::libprotobuf
            absl::base
            absl::strings
            absl::raw_hash_set
            absl::synchronization
            absl::log
            absl::flags
    )
    target_link_libraries(translator_cli
        PRIVATE
            offline_translator_core
            CTranslate2::ctranslate2
            sentencepiece_compat
            protobuf::libprotobuf
            absl::base
            absl::strings
            absl::raw_hash_set
            absl::synchronization
            absl::log
            absl::flags
    )
    if (WIN32)
        target_link_libraries(TLing
            PRIVATE
                offline_translator_core
                CTranslate2::ctranslate2
                sentencepiece_compat
                protobuf::libprotobuf
                absl::base
                absl::strings
                absl::raw_hash_set
                absl::synchronization
                absl::log
                absl::flags
                user32
                shell32
                gdi32
                comctl32
                dwmapi
        )
        set(_app_ico "${CMAKE_CURRENT_SOURCE_DIR}/../assets/app.ico")
        if (EXISTS "${_app_ico}")
            add_custom_command(TARGET TLing POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_FILE_DIR:TLing>/assets"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_app_ico}"
                    "$<TARGET_FILE_DIR:TLing>/assets/app.ico"
                COMMENT "РљРѕРїРёСЂРѕРІР°РЅРёРµ assets/app.ico СЂСЏРґРѕРј СЃ GUI"
            )
        endif()
        set(_fxbridge_dll "${CMAKE_CURRENT_SOURCE_DIR}/fxbridge/target/release/fxbridge.dll")
        # РЎР±РѕСЂРєР° РјРѕСЃС‚Р° Firefox: cargo, РµСЃР»Рё СѓСЃС‚Р°РЅРѕРІР»РµРЅ; РёРЅР°С‡Рµ РіРѕС‚РѕРІС‹Р№ DLL.
        find_program(CARGO_EXECUTABLE cargo)
        if (CARGO_EXECUTABLE)
            add_custom_target(fxbridge_dll
                COMMAND "${CARGO_EXECUTABLE}" build --release
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fxbridge"
                BYPRODUCTS "${_fxbridge_dll}"
                COMMENT "cargo build --release (fxbridge)"
                VERBATIM
            )
            foreach (_bridge_target IN ITEMS
                TLing
                firefox_smoke
            )
                add_dependencies(${_bridge_target} fxbridge_dll)
                add_custom_command(TARGET ${_bridge_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_fxbridge_dll}"
                        "$<TARGET_FILE_DIR:${_bridge_target}>/fxbridge.dll"
                    COMMENT "РљРѕРїРёСЂРѕРІР°РЅРёРµ fxbridge.dll СЂСЏРґРѕРј СЃ ${_bridge_target}"
                )
            endforeach()
        elseif (EXISTS "${_fxbridge_dll}")
            message(STATUS "cargo РЅРµ РЅР°Р№РґРµРЅ вЂ” РёСЃРїРѕР»СЊР·СѓРµС‚СЃСЏ РіРѕС‚РѕРІС‹Р№ fxbridge.dll")
            foreach (_bridge_target IN ITEMS
                TLing
                firefox_smoke
            )
                add_custom_command(TARGET ${_bridge_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_fxbridge_dll}"
                        "$<TARGET_FILE_DIR:${_bridge_target}>/fxbridge.dll"
                    COMMENT "РљРѕРїРёСЂРѕРІР°РЅРёРµ fxbridge.dll СЂСЏРґРѕРј СЃ ${_bridge_target}"
                )
            endforeach()
        else()
            message(WARNING
                "РќРё cargo, РЅРё РіРѕС‚РѕРІРѕРіРѕ fxbridge.dll РЅРµС‚ вЂ” РґРІРёР¶РѕРє Firefox "
                "РЅРµ Р·Р°СЂР°Р±РѕС‚Р°РµС‚. РЈСЃС‚Р°РЅРѕРІРёС‚Рµ Rust РёР»Рё СЃРѕР±РµСЂРёС‚Рµ cpp/fxbridge.")
        endif()
        foreach (_icon_png IN ITEMS icon.png icon-light.png icon-dark.png)
            set(_selection_png "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${_icon_png}")
            if (EXISTS "${_selection_png}")
                add_custom_command(TARGET TLing POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E make_directory
                        "$<TARGET_FILE_DIR:TLing>/assets"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_selection_png}"
                        "$<TARGET_FILE_DIR:TLing>/assets/${_icon_png}"
                    COMMENT "РљРѕРїРёСЂРѕРІР°РЅРёРµ assets/${_icon_png} СЂСЏРґРѕРј СЃ GUI"
                )
            endif()
        endforeach()
    endif()
endif()

target_include_directories(offline_translator_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(OT_ZLIB_DLL_PATH "${_zlib_dll}" CACHE INTERNAL "zlib runtime dll")
set(OT_ZSTD_DLL_PATH "${ZSTD_DLL}" CACHE INTERNAL "zstd runtime dll")

target_link_libraries(offline_translator_core
    PUBLIC
        nlohmann_json::nlohmann_json
)

if (WIN32)
    target_link_libraries(offline_translator_core PUBLIC winhttp advapi32 user32)
endif()

if (ENABLE_CTRANSLATE2)
    target_link_libraries(offline_translator_core
        PUBLIC
            CTranslate2::ctranslate2
            sentencepiece_compat
    )
    if (WIN32)
        find_file(OpenBLAS_DLL
            NAMES openblas.dll
            PATHS
                "C:/vcpkg/installed/x64-windows/bin"
                "$ENV{VCPKG_ROOT}/installed/x64-windows/bin"
                "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
            NO_DEFAULT_PATH
        )
        if (NOT OpenBLAS_DLL)
            find_file(OpenBLAS_DLL NAMES openblas.dll)
        endif()
        if (NOT OpenBLAS_DLL)
            message(FATAL_ERROR
                "РќРµ РЅР°Р№РґРµРЅ openblas.dll. РЈРєР°Р¶РёС‚Рµ OpenBLAS_DLL РёР»Рё СѓСЃС‚Р°РЅРѕРІРёС‚Рµ OpenBLAS РІ vcpkg.")
        endif()
        set(_vcpkg_bin_search
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
            "C:/vcpkg/installed/x64-windows/bin"
            "$ENV{VCPKG_ROOT}/installed/x64-windows/bin"
        )
        find_file(ABSEIL_DLL
            NAMES abseil_dll.dll
            PATHS ${_vcpkg_bin_search}
            NO_DEFAULT_PATH
        )
        find_file(PROTOBUF_DLL
            NAMES libprotobuf.dll
            PATHS ${_vcpkg_bin_search}
            NO_DEFAULT_PATH
        )
        if (NOT ABSEIL_DLL OR NOT PROTOBUF_DLL)
            message(FATAL_ERROR
                "РќРµ РЅР°Р№РґРµРЅС‹ abseil_dll.dll Рё/РёР»Рё libprotobuf.dll РІ vcpkg bin.")
        endif()
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWin32Runtime.cmake")
    endif()
endif()

if (MSVC)
    target_compile_options(offline_translator_core PRIVATE /W4 /EHsc /permissive- /utf-8)
    if (ENABLE_CTRANSLATE2)
        target_compile_options(nllb_smoke PRIVATE /EHsc /permissive- /utf-8)
        target_compile_options(argos_smoke PRIVATE /EHsc /permissive- /utf-8)
        target_compile_options(marian_smoke PRIVATE /EHsc /permissive- /utf-8)
        target_compile_options(translator_cli PRIVATE /EHsc /permissive- /utf-8)
        if (TARGET TLing)
            target_compile_options(TLing PRIVATE /EHsc /permissive- /utf-8)
        endif()
    endif()
else()
    target_compile_options(offline_translator_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(core_tests
    tests/core_tests.cpp
)
add_executable(integration_tests
    tests/integration_tests.cpp
)

target_link_libraries(core_tests PRIVATE offline_translator_core ZLIB::ZLIB zstd::libzstd)
target_link_libraries(integration_tests PRIVATE offline_translator_core)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Тесты определения языка используют Rust-мост, поэтому fxbridge.dll нужен
# и рядом с core_tests.
if (WIN32 AND EXISTS "${_fxbridge_dll}")
    add_custom_command(TARGET core_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_fxbridge_dll}"
            "$<TARGET_FILE_DIR:core_tests>/fxbridge.dll"
        COMMENT "Копирование fxbridge.dll рядом с core_tests"
    )
endif()
if (MSVC)
    target_compile_options(core_tests PRIVATE /EHsc /permissive- /utf-8)
    target_compile_options(integration_tests PRIVATE /EHsc /permissive- /utf-8)
endif()
if (ENABLE_CTRANSLATE2)
    target_compile_definitions(integration_tests PRIVATE ENABLE_CTRANSLATE2)
    target_link_libraries(integration_tests PRIVATE
        CTranslate2::ctranslate2
        sentencepiece_compat
        protobuf::libprotobuf
        absl::base
        absl::strings
        absl::raw_hash_set
        absl::synchronization
        absl::log
        absl::flags
    )
endif()

if (ENABLE_CTRANSLATE2 AND WIN32)
    foreach (_runtime_target IN ITEMS
        nllb_smoke
        argos_smoke
        marian_smoke
        translator_cli
        firefox_smoke
        TLing
        core_tests
        integration_tests
    )
        if (TARGET "${_runtime_target}")
            offline_translator_deploy_win32_runtime("${_runtime_target}")
        endif()
    endforeach()
endif()

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
add_test(NAME integration_tests COMMAND integration_tests)
set_tests_properties(integration_tests PROPERTIES TIMEOUT 180)
add_test(
    NAME portable_package_check
    COMMAND "${CMAKE_COMMAND}"
        -DPACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/portable-lite
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPortablePackageOrSkip.cmake"
)

if (ENABLE_CTRANSLATE2 AND WIN32 AND TARGET nllb_smoke)
    add_test(
        NAME check_runtime_dlls
        COMMAND "${CMAKE_COMMAND}"
            -DEXE_DIR=$<TARGET_FILE_DIR:nllb_smoke>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckRuntimeDlls.cmake"
    )
    set(NLLB_MODELS_ROOT
        "$ENV{USERPROFILE}/.local/share/offline-translator/nllb-200")
    set(ARGOS_PACKAGES_ROOT
        "$ENV{USERPROFILE}/.local/share/argos-translate/packages")
    if (EXISTS "${NLLB_MODELS_ROOT}/nllb-200-distilled-600M/model.bin")
        add_test(
            NAME nllb_cli_smoke
            COMMAND nllb_smoke "${NLLB_MODELS_ROOT}"
        )
        set_tests_properties(nllb_cli_smoke PROPERTIES TIMEOUT 30)
    endif()
    if (EXISTS "${ARGOS_PACKAGES_ROOT}")
        file(GLOB _argos_en_ru "${ARGOS_PACKAGES_ROOT}/translate-en_ru-*")
        if (_argos_en_ru)
            add_test(
                NAME argos_cli_smoke
                COMMAND argos_smoke "${ARGOS_PACKAGES_ROOT}"
            )
            set_tests_properties(argos_cli_smoke PROPERTIES TIMEOUT 30)
        endif()
    endif()
    set(MARIAN_MODELS_ROOT
        "$ENV{USERPROFILE}/.local/share/offline-translator/marian-models")
    if (EXISTS "${MARIAN_MODELS_ROOT}/en-ru/model.bin")
        add_test(
            NAME marian_cli_smoke
            COMMAND marian_smoke "${MARIAN_MODELS_ROOT}"
        )
        set_tests_properties(marian_cli_smoke PROPERTIES TIMEOUT 60)
    endif()
    set(FIREFOX_MODELS_ROOT
        "$ENV{USERPROFILE}/.local/share/offline-translator/firefox-models")
    if (TARGET firefox_smoke AND EXISTS "${FIREFOX_MODELS_ROOT}/base/en-ru/model.bin")
        add_test(
            NAME firefox_cli_smoke
            COMMAND firefox_smoke "${FIREFOX_MODELS_ROOT}"
        )
        set_tests_properties(firefox_cli_smoke PROPERTIES TIMEOUT 60)
    endif()
    if (TARGET TLing)
        add_test(
            NAME win32_gui_smoke
            COMMAND TLing --smoke-start
        )
        set_tests_properties(win32_gui_smoke PROPERTIES TIMEOUT 15)
        add_test(
            NAME win32_popup_smoke
            COMMAND TLing --smoke-popup
        )
        set_tests_properties(win32_popup_smoke PROPERTIES TIMEOUT 15)
    endif()
endif()
