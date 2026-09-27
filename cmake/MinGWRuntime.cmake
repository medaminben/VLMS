# MinGW/GCC Windows builds: link C/C++ runtimes into vlms.exe so customers
# do not need libgcc/libstdc++/winpthread DLLs beside the app (MSVC runtime not used).

if(NOT MINGW OR NOT TARGET vlms)
  return()
endif()

target_link_options(vlms PRIVATE
  -static-libgcc
  -static-libstdc++
  "-Wl,-Bstatic"
  "-lwinpthread"
  "-Wl,-Bdynamic"
)

message(STATUS "VLMS MinGW: static libgcc/libstdc++/winpthread linked into vlms.exe")
