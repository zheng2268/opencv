# --- V4L ---
if(NOT HAVE_AWCSI)
  set(CMAKE_REQUIRED_QUIET TRUE) # for check_include_file
  check_include_file(AWIspApi.h HAVE_CAMAWCSI)
  if(HAVE_CAMAWCSI)
    set(HAVE_AWCSI TRUE)
    set(defs)
    if(HAVE_CAMAWCSI)
      list(APPEND defs "HAVE_CAMAWCSI")
    endif()
    ocv_add_external_target(awcsi "" "" "${defs}")
  endif()
endif()
