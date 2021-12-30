build_libwebsockets() {
 (: ${CC=cc}
  : ${host=`$CC -dumpmachine`}
  : ${sourcedir=libwebsockets}
  : ${builddir=libwebsockets/build/$host}

  relsrcdir=`realpath --relative-to $builddir $sourcedir`

  : ${PLUGINS=OFF}
  : ${DISKCACHE=OFF}
  export CFLAGS="-I$PWD/libwebsockets/lib/plat/unix"

  mkdir -p $builddir
  echo "builddir: $builddir" 1>&2
  (cd $builddir
  cmake $relsrcdir \
    ${TOOLCHAIN+"-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$TOOLCHAIN"} \
    -DCMAKE_VERBOSE_MAKEFILE:BOOL=${VERBOSE-OFF} \
    -DCOMPILER_IS_CLANG:BOOL=OFF \
    -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo \
    -DLWS_WITH_SHARED:BOOL=OFF \
    -DLWS_WITH_STATIC:BOOL=ON \
    -DLWS_STATIC_PIC:BOOL=ON \
    -DDISABLE_WERROR:BOOL=ON \
    -DLWS_HAVE_LIBCAP:BOOL=FALSE \
    -DLWS_ROLE_RAW_PROXY:BOOL=ON \
    -DLWS_UNIX_SOCK:BOOL=ON \
    -DLWS_WITH_DISKCACHE:BOOL="$DISKCACHE" \
    -DLWS_WITH_ACCESS_LOG:BOOL=ON \
    -DLWS_WITH_CGI:BOOL=OFF \
    -DLWS_WITH_DIR:BOOL=OFF \
    -DLWS_WITH_EVLIB_PLUGINS:BOOL=OFF \
    -DLWS_WITH_EXTERNAL_POLL:BOOL=ON \
    -DLWS_WITH_FILE_OPS:BOOL=ON \
    -DLWS_WITH_FSMOUNT:BOOL=OFF \
    -DLWS_WITH_NETLINK:BOOL=OFF \
    -DLWS_WITH_HTTP2:BOOL=ON \
    -DLWS_WITH_HTTP_BROTLI:BOOL=ON \
    -DLWS_WITH_HTTP_PROXY:BOOL=ON \
    -DLWS_WITH_HTTP_STREAM_COMPRESSION:BOOL=ON \
    -DLWS_WITH_LEJP:BOOL=OFF \
    -DLWS_WITH_LEJP_CONF:BOOL=OFF \
    -DLWS_WITH_LIBUV:BOOL=OFF \
    -DLWS_WITH_MINIMAL_EXAMPLES:BOOL=OFF \
    -DLWS_WITH_NO_LOGS:BOOL=OFF \
    -DLWS_WITHOUT_EXTENSIONS:BOOL=OFF \
    -DLWS_WITHOUT_TESTAPPS:BOOL=ON \
    -DLWS_WITH_PLUGINS_API:BOOL=$PLUGINS \
    -DLWS_WITH_PLUGINS:BOOL=$PLUGINS \
    -DLWS_WITH_PLUGINS_BUILTIN:BOOL=$PLUGINS \
    -DLWS_WITH_RANGES:BOOL=ON \
    -DLWS_WITH_SERVER:BOOL=ON \
    -DLWS_WITH_SOCKS5:BOOL=ON \
    -DLWS_WITH_SYS_ASYNC_DNS:BOOL=ON \
    -DLWS_WITH_THREADPOOL:BOOL=ON \
    -DLWS_WITH_UNIX_SOCK:BOOL=ON \
    -DLWS_WITH_ZIP_FOPS:BOOL=ON \
    -DLWS_WITH_ZLIB:BOOL=ON \
    "$@" && make) 2>&1 | tee cmake.log
    )
}