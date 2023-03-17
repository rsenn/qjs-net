newoption { trigger = "output-dir", value = "PATH", description = "Output directory" }
newoption { trigger = "build-dir", value = "PATH", description = "Build directory", default = "build" }
newoption { trigger = "with-quickjs", value = "PATH", description = "QuickJS prefix", default = "/usr/local" }
newoption { trigger = "with-openssl", value = "PATH", description = "OpenSSL prefix" } 
newoption { trigger = "with-libwebsockets", value = "PATH", description = "libwebsockets prefix" }
 
OUTDIR = _OPTIONS["output-dir"] or "."

QUICKJS_PREFIX = _OPTIONS["with-quickjs"] or "/usr/local"
QUICKJS_INCLUDE_DIR = QUICKJS_PREFIX .. "/include/quickjs"
QUICKJS_LIBRARY_DIR = QUICKJS_PREFIX .. "/lib"

OPENSSL_PREFIX = _OPTIONS["with-openssl"] or "/usr"
OPENSSL_INCLUDE_DIR = OPENSSL_PREFIX .. "/include"
OPENSSL_LIBRARY_DIR = OPENSSL_PREFIX .. "/lib"

LWS_PREFIX = _OPTIONS["with-libwebsockets"] or "/usr"
LWS_INCLUDE_DIR = LWS_PREFIX .. "/include"
LWS_LIBRARY_DIR = LWS_PREFIX .. "/lib"

BUILD_DIR = _OPTIONS["build-dir"]
OUTPUT_DIR = _OPTIONS["output-dir"] or BUILD_DIR .. "/bin"

location(BUILD_DIR)

workspace "minnet"
  configurations {"Debug", "Release"}

project "minnet"
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
     OPENSSL_LIBRARY_DIR,
     LWS_LIBRARY_DIR,
     QUICKJS_LIBRARY_DIR
  }

  targetprefix ""
  targetname "net"
  --targetdir(OUTDIR)


  filter "configurations:Debug"
    defines {"JS_SHARED_LIBRARY", "_DEBUG", "DEBUG_OUTPUT"}
    symbols "On"
    warnings "Default"

    --objdir "obj/Debug"
    targetdir(OUTPUT_DIR .. "/Debug")

  filter "configurations:Release"
    defines {"JS_SHARED_LIBRARY", "NDEBUG"}
    optimize "On"
    warnings "Off"

    --objdir "obj/Release"
    targetdir(OUTPUT_DIR .. "/Release")
