solution "minnet"
location("build")
configurations { "Debug", "Release" }
platforms { "native", "x64", "x32" }

  project "minnet"
  language "C"
  kind "SharedLib"

  files { 
    "lib/*.c",
    "src/*.c"
  }

  defines { "JS_SHARED_LIBRARY" }

  includedirs { 
      "lib",
      "/opt/openssl-3.0.2-x86_64/include",
      "/opt/libwebsockets-x86_64/include",
      "/usr/local/include/quickjs"
   }

  libdirs { 
      "/opt/openssl-3.0.2-x86_64/lib",
      "/opt/libwebsockets-x86_64/lib",
      "/usr/local/lib/x86_64-linux-gnu"
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

  linkoptions { 
      "-Wl,-soname,net.so",
      "-Wl,-rpath=/opt/openssl-3.0.2-x86_64/lib:/opt/libwebsockets-x86_64/lib:/usr/local/lib/x86_64-linux-gnu"
  }

  targetprefix ""
  targetname "net"
  targetdir(".")

  configuration "Debug"
      defines { "JS_SHARED_LIBRARY", "_DEBUG", "DEBUG_OUTPUT" }
      flags { "Symbols" }

  configuration "Release"
      defines { "JS_SHARED_LIBRARY", "NDEBUG" }
      flags { "Optimize" }
