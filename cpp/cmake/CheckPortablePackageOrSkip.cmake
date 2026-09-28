# Как CheckPortablePackage.cmake, но без ошибки, если lite-папка ещё не собрана.
# cmake -DPACKAGE_DIR=<папка> -P cpp/cmake/CheckPortablePackageOrSkip.cmake

if (NOT PACKAGE_DIR)
    get_filename_component(_cpp_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(PACKAGE_DIR "${_cpp_root}/portable-lite")
endif()

if (NOT EXISTS "${PACKAGE_DIR}/TLing.exe")
    message(STATUS
        "SKIP: нет портативной lite-папки (${PACKAGE_DIR}). "
        "Соберите cpp/package_win32.ps1 без -IncludeModels.")
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckPortablePackage.cmake")
