# РџСЂРѕРІРµСЂСЏРµС‚ СЃРѕСЃС‚Р°РІ lite-РїРѕСЂС‚Р°С‚РёРІРЅРѕР№ РїР°РїРєРё (Р±РµР· dumpbin).
# cmake -DPACKAGE_DIR=<РїР°РїРєР°> -P cpp/cmake/CheckPortablePackage.cmake

if (NOT PACKAGE_DIR)
    message(FATAL_ERROR "Р—Р°РґР°Р№С‚Рµ PACKAGE_DIR вЂ” РєР°С‚Р°Р»РѕРі portable-СЃР±РѕСЂРєРё")
endif()

if (NOT IS_DIRECTORY "${PACKAGE_DIR}")
    message(FATAL_ERROR "РќРµС‚ РєР°С‚Р°Р»РѕРіР° РїРѕСЃС‚Р°РІРєРё: ${PACKAGE_DIR}")
endif()

set(_required_files
    TLing.exe
    ctranslate2.dll
    openblas.dll
    libprotobuf.dll
    abseil_dll.dll
    vcruntime140.dll
    vcruntime140_1.dll
    msvcp140.dll
    msvcp140_atomic_wait.dll
    vcomp140.dll
    assets/app.ico
    assets/icon-light.png
    assets/icon-dark.png
    licenses/THIRD_PARTY.md
    README.txt
)

set(_missing "")
foreach (_file IN LISTS _required_files)
    if (NOT EXISTS "${PACKAGE_DIR}/${_file}")
        list(APPEND _missing "${_file}")
    endif()
endforeach()

if (NOT IS_DIRECTORY "${PACKAGE_DIR}/data")
    list(APPEND _missing "data/")
endif()

if (_missing)
    message(FATAL_ERROR
        "Р’ РїРѕСЃС‚Р°РІРєРµ РЅРµС‚ РѕР±СЏР·Р°С‚РµР»СЊРЅС‹С… С„Р°Р№Р»РѕРІ: ${_missing} (РєР°С‚Р°Р»РѕРі: ${PACKAGE_DIR})")
endif()

foreach (_forbidden IN ITEMS
    CMakeCache.txt
    CMakeFiles
    vcpkg_installed
    ALL_BUILD.vcxproj
)
    if (EXISTS "${PACKAGE_DIR}/${_forbidden}")
        message(FATAL_ERROR
            "Р’ РїРѕСЃС‚Р°РІРєРµ РЅРµ РґРѕР»Р¶РЅРѕ Р±С‹С‚СЊ РєСЌС€Р° СЃР±РѕСЂРєРё: ${PACKAGE_DIR}/${_forbidden}")
    endif()
endforeach()

file(GLOB_RECURSE _pdbs "${PACKAGE_DIR}/*.pdb")
if (_pdbs)
    message(FATAL_ERROR
        "Р’ РїРѕСЃС‚Р°РІРєРµ РЅРµ РґРѕР»Р¶РЅРѕ Р±С‹С‚СЊ .pdb (РѕС‚Р»Р°РґРѕС‡РЅС‹Рµ СЃРёРјРІРѕР»С‹): ${_pdbs}")
endif()

message(STATUS "РџРѕСЂС‚Р°С‚РёРІРЅР°СЏ РїР°РїРєР° РІС‹РіР»СЏРґРёС‚ С†РµР»РѕР№: ${PACKAGE_DIR}")
