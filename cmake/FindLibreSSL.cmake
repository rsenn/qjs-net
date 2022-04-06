macro(find_libressl)
  message(STATUS "Finding LibreSSL library...")

  if(LIBRESSL_ROOT_DIR)
    # Disable re-rooting paths in find_path/find_library. This assumes LIBRESSL_ROOT_DIR is an absolute path.
    set(_EXTRA_FIND_ARGS "NO_DEFAULT_PATH")
  endif()

  find_path(LIBRESSL_INCLUDE_DIR NAMES libressl/ssl.h PATH_SUFFIXES include HINTS ${LIBRESSL_ROOT_DIR} ${_EXTRA_FIND_ARGS})

  # based on https://github.com/ARMlibre/libressl/issues/298
  if(LIBRESSL_INCLUDE_DIR AND EXISTS "${LIBRESSL_INCLUDE_DIR}/libressl/version.h")
    file(STRINGS "${LIBRESSL_INCLUDE_DIR}/libressl/version.h" VERSION_STRING_LINE REGEX "^#define LIBRESSL_VERSION_STRING[ \\t\\n\\r]+\"[^\"]*\"$")
    file(STRINGS "${LIBRESSL_INCLUDE_DIR}/libressl/version.h" VERSION_MAJOR_LINE REGEX "^#define LIBRESSL_VERSION_MAJOR[ \\t\\n\\r]+[0-9]+$")
    file(STRINGS "${LIBRESSL_INCLUDE_DIR}/libressl/version.h" VERSION_MINOR_LINE REGEX "^#define LIBRESSL_VERSION_MINOR[ \\t\\n\\r]+[0-9]+$")
    file(STRINGS "${LIBRESSL_INCLUDE_DIR}/libressl/version.h" VERSION_PATCH_LINE REGEX "^#define LIBRESSL_VERSION_PATCH[ \\t\\n\\r]+[0-9]+$")

    string(REGEX REPLACE "^#define LIBRESSL_VERSION_STRING[ \\t\\n\\r]+\"([^\"]*)\"$" "\\1" LIBRESSL_VERSION "${VERSION_STRING_LINE}")
    string(REGEX REPLACE "^#define LIBRESSL_VERSION_MAJOR[ \\t\\n\\r]+([0-9]+)$" "\\1" LIBRESSL_VERSION_MAJOR "${VERSION_MAJOR_LINE}")
    string(REGEX REPLACE "^#define LIBRESSL_VERSION_MINOR[ \\t\\n\\r]+([0-9]+)$" "\\1" LIBRESSL_VERSION_MINOR "${VERSION_MINOR_LINE}")
    string(REGEX REPLACE "^#define LIBRESSL_VERSION_PATCH[ \\t\\n\\r]+([0-9]+)$" "\\1" LIBRESSL_VERSION_PATCH "${VERSION_PATCH_LINE}")
  endif()

  if(LIBRESSL_USE_STATIC_LIBS)
    set(_LIBRESSL_CRYPTO_LIB_NAME libcrypto.a)
    set(_LIBRESSL_SSL_LIB_NAME libssl.a)
    set(_LIBRESSL_TLS_LIB_NAME libtls.a)
  else()
    set(_LIBRESSL_CRYPTO_LIB_NAME libcrypto)
    set(_LIBRESSL_SSL_LIB_NAME libressl)
    set(_LIBRESSL_TLS_LIB_NAME libtls)
  endif()

  find_library(LIBRESSL_CRYPTO_LIBRARY NAMES ${_LIBRESSL_CRYPTO_LIB_NAME} PATH_SUFFIXES lib HINTS ${LIBRESSL_ROOT_DIR} ${_EXTRA_FIND_ARGS})
  find_library(LIBRESSL_SSL_LIBRARY NAMES ${_LIBRESSL_SSL_LIB_NAME} PATH_SUFFIXES lib HINTS ${LIBRESSL_ROOT_DIR} ${_EXTRA_FIND_ARGS})
  find_library(LIBRESSL_TLS_LIBRARY NAMES ${_LIBRESSL_TLS_LIB_NAME} PATH_SUFFIXES lib HINTS ${LIBRESSL_ROOT_DIR} ${_EXTRA_FIND_ARGS})

  set(LIBRESSL_LIBRARIES ${LIBRESSL_TLS_LIBRARY}^${LIBRESSL_SSL_LIBRARY} ${LIBRESSL_CRYPTO_LIBRARY})

  if(LIBRESSL_INCLUDE_DIR AND LIBRESSL_SSL_LIBRARY)
    set(LIBRESSL_FOUND TRUE)
  endif(LIBRESSL_INCLUDE_DIR AND LIBRESSL_SSL_LIBRARY)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(libreSSL FOUND_VAR LIBRESSL_FOUND REQUIRED_VARS LIBRESSL_INCLUDE_DIR LIBRESSL_SSL_LIBRARY LIBRESSL_CRYPTO_LIBRARY LIBRESSL_TLS_LIBRARY LIBRESSL_LIBRARIES
                                                                                    LIBRESSL_VERSION VERSION_VAR LIBRESSL_VERSION)

  if(LIBRESSL_FOUND)
    if(NOT TARGET libcrypto)
      add_library(libcrypto UNKNOWN IMPORTED)
      set_target_properties(libcrypto PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBRESSL_INCLUDE_DIR}" IMPORTED_LINK_INTERFACE_LANGUAGES "C" IMPORTED_LOCATION "${LIBRESSL_CRYPTO_LIBRARY}")
    endif()

    if(NOT TARGET libssl)
      add_library(libssl UNKNOWN IMPORTED)
      set_target_properties(libssl PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBRESSL_INCLUDE_DIR}" INTERFACE_LINK_LIBRARIES libtls IMPORTED_LINK_INTERFACE_LANGUAGES "C" IMPORTED_LOCATION
                                                                                                                                                                            "${LIBRESSL_SSL_LIBRARY}")
    endif()

    if(NOT TARGET libtls)
      add_library(libtls UNKNOWN IMPORTED)
      set_target_properties(libtls PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBRESSL_INCLUDE_DIR}" INTERFACE_LINK_LIBRARIES libcrypto IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                                              IMPORTED_LOCATION "${LIBRESSL_TLS_LIBRARY}")
    endif()

    get_target_property(LIBRESSL_LIBRARY_LOCATION libressl IMPORTED_LOCATION)

    if(EXISTS "${LIBRESSL_ROOT_DIR}/lib")
      set(LIBRARY_DIR "${LIBRESSL_ROOT_DIR}/lib")
    endif(EXISTS "${LIBRESSL_ROOT_DIR}/lib")

    if(NOT LIBRARY_DIR)
      string(REGEX REPLACE "/[^/]*$" "" LIBRARY_DIR "${LIBRESSL_SSL_LIBRARY}")
    endif(NOT LIBRARY_DIR)

    if(EXISTS "${LIBRARY_DIR}")
      rpath_append(CMAKE_INSTALL_RPATH "${LIBRARY_PATH}")
    endif(EXISTS "${LIBRARY_DIR}")

    set(LIBRESSL_LIBRARY_DIR "${LIBRARY_DIR}" CACHE PATH "LibreSSL library directory")
    set(LIBRESSL_INCLUDE_DIR "${LIBRESSL_INCLUDE_DIR}" CACHE PATH "LibreSSL include directory")

  else(LIBRESSL_FOUND)
    unset(LIBRESSL_LIBRARIES CACHE)
    unset(LIBRESSL_SSL_LIBRARY CACHE)
    unset(LIBRESSL_CRYPTO_LIBRARY CACHE)
    unset(LIBRESSL_TLS_LIBRARY CACHE)
  endif(LIBRESSL_FOUND)

endmacro(find_libressl)
