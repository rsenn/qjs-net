macro(find_libwebsockets)

  if(NOT LIBWEBSOCKETS_FOUND)
    unset(LIBWEBSOCKETS_INCLUDE_DIR CACHE)
    unset(LIBWEBSOCKETS_LIBRARY_DIR CACHE)
    unset(LIBWEBSOCKETS_LIBRARIES CACHE)

    find_package(LIBWEBSOCKETS NAMES libwebsockets)

    if(LIBWEBSOCKETS_FOUND)

      message("LIBWEBSOCKETS_CONFIG: ${LIBWEBSOCKETS_CONFIG}")
      include(${LIBWEBSOCKETS_CONFIG})

      dirname(LIBWEBSOCKETS_DIR "${LIBWEBSOCKETS_CONFIG}")
      message("LIBWEBSOCKETS_DIR: ${LIBWEBSOCKETS_DIR}")

      include(${LIBWEBSOCKETS_DIR}/LibwebsocketsTargets.cmake)

      get_target_property(pkgcfg_lib_LIBWEBSOCKETS_websockets websockets
                          INTERFACE_LINK_LIBRARIES)
      get_target_property(LIBWEBSOCKETS_INCLUDE_DIR websockets
                          INTERFACE_INCLUDE_DIRECTORIES)

      #set_target_properties(  websockets  PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")

      string(REGEX REPLACE "/include.*" "/lib" LIBWEBSOCKETS_LIBRARY_DIR
                           "${LIBWEBSOCKETS_INCLUDE_DIR}")

      #     string(REGEX REPLACE " *" "" pkgcfg_lib_LIBWEBSOCKETS_websockets "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")
      set(LIBWEBSOCKETS_LIBRARIES ${LIBWEBSOCKETS_LIBRARIES}
                                  ${pkgcfg_lib_LIBWEBSOCKETS_websockets})
      list(FILTER LIBWEBSOCKETS_LIBRARIES EXCLUDE REGEX websockets_shared)
      message("LIBWEBSOCKETS_INCLUDE_DIR: ${LIBWEBSOCKETS_INCLUDE_DIR}")
      message("LIBWEBSOCKETS_LIBRARY_DIR: ${LIBWEBSOCKETS_LIBRARY_DIR}")

    else(LIBWEBSOCKETS_FOUND)

      if(NOT PKG_CONFIG_FOUND)
        include(FindPkgConfig)
      endif(NOT PKG_CONFIG_FOUND)

      if(LIBWEBSOCKETS_ROOT_DIR)
        list(PREPEND CMAKE_PREFIX_PATH "${LIBWEBSOCKETS_ROOT_DIR}")
        list(PREPEND CMAKE_MODULE_PATH
             "${LIBWEBSOCKETS_ROOT_DIR}/lib/cmake/libwebsockets")
      endif(LIBWEBSOCKETS_ROOT_DIR)
      set(LIBWEBSOCKETS_ROOT_DIR "${LIBWEBSOCKETS_ROOT_DIR}"
                                 "libwebsockets installation prefix")

      pkg_check_modules(LIBWEBSOCKETS libwebsockets)
    endif(LIBWEBSOCKETS_FOUND)

    if(NOT DEFINED OPENSSL_FOUND)
      include(FindOpenSSL)
    endif(NOT DEFINED OPENSSL_FOUND)

    message(
      "pkgcfg_lib_LIBWEBSOCKETS_websockets: ${pkgcfg_lib_LIBWEBSOCKETS_websockets}"
    )

    if(pkgcfg_lib_LIBWEBSOCKETS_websockets
       AND EXISTS "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")
      set(LIBWEBSOCKETS_LIBRARIES "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")
    endif(pkgcfg_lib_LIBWEBSOCKETS_websockets
          AND EXISTS "${pkgcfg_lib_LIBWEBSOCKETS_websockets}")

    if(NOT LIBWEBSOCKETS_LIBRARIES)
      if(LIBWEBSOCKETS_LINK_LIBRARIES)
        set(LIBWEBSOCKETS_LIBRARIES "${LIBWEBSOCKETS_LINK_LIBRARIES}")
      endif(LIBWEBSOCKETS_LINK_LIBRARIES)
    endif(NOT LIBWEBSOCKETS_LIBRARIES)
    message("LIBWEBSOCKETS_LIBRARIES: ${LIBWEBSOCKETS_LIBRARIES}")

    if(LIBWEBSOCKETS_LIBRARIES AND "${LIBWEBSOCKETS_LIBRARIES}" MATCHES ".*/.*")
      if(NOT LIBWEBSOCKETS_LIBRARY_DIR)
        get_filename_component(LIBWEBSOCKETS_LIBRARY_DIR
                               "${LIBWEBSOCKETS_LIBRARIES}" DIRECTORY CACHE)
        #string(REGEX REPLACE "/lib.*/.*" "/lib" LIBWEBSOCKETS_LIBRARY_DIR "${LIBWEBSOCKETS_LIBRARIES}")
      endif(NOT LIBWEBSOCKETS_LIBRARY_DIR)
    endif(LIBWEBSOCKETS_LIBRARIES AND "${LIBWEBSOCKETS_LIBRARIES}" MATCHES
                                      ".*/.*")
    if(LIBWEBSOCKETS_LIBRARY_DIR)
      if(NOT LIBWEBSOCKETS_INCLUDE_DIR)
        string(REGEX REPLACE "/lib[^/]*$" "/include" LIBWEBSOCKETS_INCLUDE_DIR
                             "${LIBWEBSOCKETS_LIBRARY_DIR}")
      endif(NOT LIBWEBSOCKETS_INCLUDE_DIR)
    endif(LIBWEBSOCKETS_LIBRARY_DIR)

    if(LIBWEBSOCKETS_LIBRARY_DIR)
      rpath_append(CMAKE_INSTALL_RPATH "${LIBWEBSOCKETS_LIBRARY_DIR}")
    endif(LIBWEBSOCKETS_LIBRARY_DIR)

    #set(LIBWEBSOCKETS_LIBRARY "${LIBWEBSOCKETS_LIBRARY}" CACHE FILEPATH "libwebsockets library")
    set(LIBWEBSOCKETS_LIBRARIES "${LIBWEBSOCKETS_LIBRARIES}"
        CACHE FILEPATH "libwebsockets libraries")
    set(LIBWEBSOCKETS_LIBRARY_DIR "${LIBWEBSOCKETS_LIBRARY_DIR}"
        CACHE PATH "libwebsockets library directory")
    set(LIBWEBSOCKETS_INCLUDE_DIR "${LIBWEBSOCKETS_INCLUDE_DIR}"
        CACHE PATH "libwebsockets include directory")

    set(LIBWEBSOCKETS_FOUND TRUE)

  endif(NOT LIBWEBSOCKETS_FOUND)

endmacro(find_libwebsockets)
