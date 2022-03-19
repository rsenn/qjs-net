macro(find_libwebsockets)

  if(NOT LIBWEBSOCKETS_FOUND)
    unset(LIBWEBSOCKETS_INCLUDE_DIRS CACHE)
    unset(LIBWEBSOCKETS_LIBRARY_DIR CACHE)
    unset(LIBWEBSOCKETS_LIBRARIES CACHE)
    if(NOT PKG_CONFIG_FOUND)
      include(FindPkgConfig)

    endif(NOT PKG_CONFIG_FOUND)

    pkg_check_modules(LIBWEBSOCKETS libwebsockets)
    pkg_search_module(OPENSSL openssl)

    #if(NOT OPENSSL_SSL_LIBRARY AND NOT OPENSSL_CRYPTO_LIBRARY)
    if(pkgcfg_lib_OPENSSL_ssl)
      set(OPENSSL_SSL_LIBRARY "${pkgcfg_lib_OPENSSL_ssl}")
    endif(pkgcfg_lib_OPENSSL_ssl)
    if(pkgcfg_lib_OPENSSL_crypto)
      set(OPENSSL_CRYPTO_LIBRARY "${pkgcfg_lib_OPENSSL_crypto}")
    endif(pkgcfg_lib_OPENSSL_crypto)
    #endif(NOT OPENSSL_SSL_LIBRARY AND NOT OPENSSL_CRYPTO_LIBRARY)

    set(OPENSSL_SSL_LIBRARY "${OPENSSL_SSL_LIBRARY}"
        CACHE PATH "OpenSSL ssl library")
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_CRYPTO_LIBRARY}"
        CACHE PATH "OpenSSL crypto library")

    if(OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
      set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}")
    endif(OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)

    if(OPENSSL_CRYPTO_LIBRARY)
      set(LIB_EAY_DEBUG "${OPENSSL_CRYPTO_LIBRARY}")
      set(LIB_EAY_RELEASE "${OPENSSL_CRYPTO_LIBRARY}")
    endif(OPENSSL_CRYPTO_LIBRARY)
    if(OPENSSL_SSL_LIBRARY)
      set(SSL_EAY_DEBUG "${OPENSSL_SSL_LIBRARY}")
      set(SSL_EAY_RELEASE "${OPENSSL_SSL_LIBRARY}")
    endif(OPENSSL_SSL_LIBRARY)

    if(pkgcfg_lib_LIBWEBSOCKETS_websockets
       AND EXISTS "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")
      set(LIBWEBSOCKETS_LIBRARIES "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")
    endif(pkgcfg_lib_LIBWEBSOCKETS_websockets
          AND EXISTS "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")
    #set(LIBWEBSOCKETS_LIBRARIES "${LIBWEBSOCKETS_LIBRARIES}" CACHE FILEPATH "libwebsockets library")

    if(LIBWEBSOCKETS_LIBRARIES AND "${LIBWEBSOCKETS_LIBRARIES}" MATCHES ".*/.*")
      if(NOT LIBWEBSOCKETS_LIBRARY_DIR)
        string(REGEX REPLACE "/lib.*/.*" "/lib" LIBWEBSOCKETS_LIBRARY_DIR
                             "${LIBWEBSOCKETS_LIBRARIES}")
      endif(NOT LIBWEBSOCKETS_LIBRARY_DIR)
      if(NOT LIBWEBSOCKETS_INCLUDE_DIRS)
        string(REGEX REPLACE "/lib/.*$" "/include" LIBWEBSOCKETS_INCLUDE_DIRS
                             "${LIBWEBSOCKETS_LIBRARIES}")
      endif(NOT LIBWEBSOCKETS_INCLUDE_DIRS)
    endif(LIBWEBSOCKETS_LIBRARIES AND "${LIBWEBSOCKETS_LIBRARIES}" MATCHES
                                      ".*/.*")

    if(CMAKE_INSTALL_RPATH)
      set(CMAKE_INSTALL_RPATH
          "${LIBWEBSOCKETS_LIBRARY_DIR}:${CMAKE_INSTALL_RPATH}"
          CACHE PATH "Install runtime path")
    else(CMAKE_INSTALL_RPATH)
      set(CMAKE_INSTALL_RPATH "${LIBWEBSOCKETS_LIBRARY_DIR}"
          CACHE PATH "Install runtime path")
    endif(CMAKE_INSTALL_RPATH)

    set(LIBWEBSOCKETS_LIBRARY "${LIBWEBSOCKETS_LIBRARY}"
        CACHE FILEPATH "libwebsockets library")
    set(LIBWEBSOCKETS_LIBRARY_DIR "${LIBWEBSOCKETS_LIBRARY_DIR}"
        CACHE PATH "libwebsockets library directory")
    set(LIBWEBSOCKETS_INCLUDE_DIRS
        "${LIBWEBSOCKETS_INCLUDE_DIRS};${OPENSSL_INCLUDE_DIRS}"
        CACHE PATH "libwebsockets include directory")

    set(LIBWEBSOCKETS_FOUND TRUE)

  endif(NOT LIBWEBSOCKETS_FOUND)

endmacro(find_libwebsockets)
