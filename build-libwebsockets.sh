build_libwebsockets() (
  configure_libwebsockets "$@"
  make_libwebsockets
)

make_libwebsockets() {
  (set -x; make ${njobs:+-j} ${njobs:+$njobs} ${builddir:+-C} ${builddir:+$builddir} "$@")
}

cmake_run() {
  { cd $builddir && cmake "$@" -LA; } | {
    IFS=":="
    while read -r LINE; do
      read -r NAME TYPE VALUE <<<"$LINE"

      case $TYPE in
        STRING | PATH | FILEPATH | BOOL)
          echo "$NAME:$TYPE=$VALUE"
          ;;
        *)
          echo "$LINE" 1>&2
          ;;
      esac
    done > cmake-vars.txt
  }
}

configure_libwebsockets() {
  : ${sourcedir:=libwebsockets}
  : ${builddir:=libwebsockets/build/$($CC -dumpmachine)}
  : ${prefix:=/opt/libwebsockets-$(cd "$sourcedir" && git branch -a |sed -n '/^\*/ { s|^[* ]*||; s|-[a-z]*$||; s|\s||g; p }')}
  : ${njobs:=10}
  : ${relsrcdir:=$(realpath --relative-to $builddir $sourcedir)}
  : ${PLUGINS:=OFF}
  : ${DISKCACHE:=ON}
	: ${CC:=gcc}
	: ${SHARED:=OFF}

	if [ -n "$OPENSSL_PREFIX" -a -d "$OPENSSL_PREFIX" ]; then
	  if [ -z "$OPENSSL_LIBDIR" ]; then
	  	OPENSSL_LIBDIR="$OPENSSL_PREFIX/lib"
	  fi
	  if [ -z "$OPENSSL_INCLUDEDIR" ]; then
	  	OPENSSL_INCLUDEDIR="$OPENSSL_PREFIX/include"
	  fi
	else
		archlibdir=/usr/lib/$(${CC-gcc} -dumpmachine)
		if [ -n "$archlibdir" -a -d "$archlibdir" ]; then
			if [ -e "$archlibdir/libssl.so" ]; then
		   OPENSSL_LIBDIR="$archlibdir"
		   OPENSSL_INCLUDEDIR=/usr/include
			fi
		fi
	fi

	if [ -d "$OPENSSL_LIBDIR" ]; then
		LINK_FLAGS="${LINK_FLAGS:+$LINK_FLAGS }-Wl,-rpath=$OPENSSL_LIBDIR"
	fi

  mkdir -p $builddir
  set --  $relsrcdir ${TOOLCHAIN+"-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$TOOLCHAIN"} \
	-DCMAKE_VERBOSE_MAKEFILE:BOOL=${VERBOSE-OFF} \
  ${prefix:+-DCMAKE_INSTALL_PREFIX:PATH="$prefix"} \
  -DCOMPILER_IS_CLANG:BOOL=OFF \
  ${CC:+-DCMAKE_C_COMPILER:STRING=${CC}} \
	-DCMAKE_BUILD_TYPE:STRING=${TYPE-RelWithDebInfo} \
	-DCMAKE_{SHARED,MODULE}_LINKER_FLAGS="$LINK_FLAGS" \
	-DLWS_WITH_SHARED:BOOL=${SHARED-OFF} \
	-DLWS_WITH_STATIC:BOOL=${STATIC-ON} \
	-DLWS_STATIC_PIC:BOOL=${PIC-ON} \
	-DDISABLE_WERROR:BOOL=ON \
	-DLWS_HAVE_LIBCAP:BOOL=FALSE \
	-DLWS_ROLE_RAW_PROXY:BOOL=ON \
	-DLWS_UNIX_SOCK:BOOL=ON \
	-DLWS_WITH_DISKCACHE:BOOL="$DISKCACHE" \
	-DLWS_WITH_ACCESS_LOG:BOOL=ON \
	-DLWS_WITH_CGI:BOOL=OFF \
	-DLWS_WITH_DIR:BOOL=ON \
	-DLWS_WITH_EVLIB_PLUGINS:BOOL=OFF \
	-DLWS_WITH_EXTERNAL_POLL:BOOL=ON \
	-DLWS_WITH_FILE_OPS:BOOL=ON \
	-DLWS_WITH_FSMOUNT:BOOL=OFF \
	-DLWS_WITH_GENCRYPTO:BOOL=ON \
	-DLWS_WITH_NETLINK:BOOL=OFF \
	-DLWS_WITH_HTTP2:BOOL="${HTTP2:-ON}" \
	-DLWS_WITH_HTTP_BROTLI:BOOL=ON \
	-DLWS_WITH_HTTP_PROXY:BOOL=ON \
	-DLWS_WITH_HTTP_STREAM_COMPRESSION:BOOL=ON \
	-DLWS_WITH_LEJP:BOOL=ON \
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
	-DLWS_HAVE_HMAC_CTX_new:STRING=1 \
	-DLWS_HAVE_EVP_MD_CTX_free:STRING=1 \
	-DLWS_OPENSSL_INCLUDE_DIRS:PATH="${OPENSSL_INCLUDEDIR}" \
  -DLWS_OPENSSL_LIBRARIES:PATH="${OPENSSL_LIBDIR}/libcrypto.so;${OPENSSL_LIBDIR}/libssl.so" \
  "$@"
  (echo -e "Command: cd $builddir && cmake $@" | sed 's,\s\+-, \n\t-,g') >&2
CFLAGS="-I$PWD/libwebsockets/lib/plat/unix${CFLAGS:+ $CFLAGS}" cmake_run "$@" 2>&1 | tee cmake.log
}
