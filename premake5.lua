newoption { trigger = "output-dir", value = "PATH", description = "Output directory", default = "." }
newoption { trigger = "build-dir", value = "PATH", description = "Build directory", default = "build" }
newoption { trigger = "with-quickjs", value = "PATH", description = "QuickJS prefix", default = "/usr/local" }
newoption { trigger = "with-openssl", value = "PATH", description = "OpenSSL prefix", default = "/opt/libressl-3.5.1" } 
newoption { trigger = "with-libwebsockets", value = "PATH", description = "libwebsockets prefix", default = "/opt/libwebsockets" }
 
OUTDIR = _OPTIONS["output-dir"]

QUICKJS_PREFIX = _OPTIONS["with-quickjs"]
QUICKJS_INCLUDE_DIR = QUICKJS_PREFIX .. "/include/quickjs"
QUICKJS_LIBRARY_DIR = QUICKJS_PREFIX .. "/lib"

OPENSSL_PREFIX = _OPTIONS["with-openssl"]
OPENSSL_INCLUDE_DIR = OPENSSL_PREFIX .. "/include"
OPENSSL_LIBRARY_DIR = OPENSSL_PREFIX .. "/lib"

LWS_PREFIX = _OPTIONS["with-libwebsockets"]
LWS_INCLUDE_DIR = LWS_PREFIX .. "/include"
LWS_LIBRARY_DIR = LWS_PREFIX .. "/lib"

BUILD_DIR = _OPTIONS["build-dir"]
OUTPUT_DIR = _OPTIONS["output-dir"] or BUILD_DIR .. "/bin"

location(BUILD_DIR)

workspace "qjs-net"
  configurations {"Debug", "Release"}

project "qjs-net"
  language "C"
  kind "SharedLib"

  files {
      "lib/*.c",
      "src/*.c"
  }

  defines {"JS_SHARED_LIBRARY"}
  functionlevellinking "on"
  omitframepointer "on"
  pic "On"
  visibility "Hidden"

  includedirs {"lib"}

  externalincludedirs {
     OPENSSL_INCLUDE_DIR,
     LWS_INCLUDE_DIR,
     QUICKJS_INCLUDE_DIR
  }

  libdirs {
     OPENSSL_LIBRARY_DIR,
     LWS_LIBRARY_DIR,
     QUICKJS_LIBRARY_DIR
  }

  links {
      "websockets",
      "brotlienc",
      "brotlidec",
      "brotlicommon",
      "ssl",
      "crypto",
      "z",
      "quickjs"
  }

  --linkoptions { "-Wl,-soname,net.so" }

  runpathdirs {
    ":" .. OPENSSL_LIBRARY_DIR .. ":" .. LWS_LIBRARY_DIR .. ":" .. QUICKJS_LIBRARY_DIR
  }

  targetprefix ""
  targetname "net"

  filter "configurations:Debug"
    defines {"JS_SHARED_LIBRARY", "_DEBUG", "DEBUG_OUTPUT"}
    symbols "On"
    warnings "Default"

  filter "configurations:Release"
    defines {"JS_SHARED_LIBRARY", "NDEBUG"}
    optimize "On"
    warnings "Off"
