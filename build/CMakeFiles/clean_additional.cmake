# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles/obs-virtual-mic_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/obs-virtual-mic_autogen.dir/ParseCache.txt"
  "obs-virtual-mic_autogen"
  )
endif()
