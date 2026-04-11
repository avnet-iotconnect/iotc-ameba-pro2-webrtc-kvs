# IoTConnect CMake Configuration

message(STATUS "Configuring IoTConnect...")

set(IOTCONNECT_ROOT "${prj_root}/src/iotconnect")
set(IOTCL_ROOT      "${repo_root}/libraries/iotc-c-lib")

# ---- iotc-c-lib submodule -----------------------------------------------
# NOTE: iotc-c-lib's CMakeLists.txt requires CMake >= 3.22.
# If cmake fails here, upgrade your CMake installation.
# Suppress the cJSON "CMake < 3.5" deprecation noise on CMake 4.x
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
add_subdirectory(${IOTCL_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/iotc-c-lib)
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

# Link iotc-c-lib into the app executable.
# application.cmake populates 'libs' then passes it to target_link_libraries,
# so appending here (before that include) is sufficient.
list(APPEND libs iotc-c-lib)

# ---- Application-layer IoTConnect sources --------------------------------
set(IOTCONNECT_SOURCES
    "${IOTCONNECT_ROOT}/iotconnect.c"
    "${IOTCONNECT_ROOT}/iotconnect_mqtt.c"
    "${IOTCONNECT_ROOT}/iotconnect_certs.c"
    "${IOTCONNECT_ROOT}/iotconnect_discovery.c"
    "${IOTCONNECT_ROOT}/iotconnect_kvs_creds.c"
)

list(APPEND app_sources ${IOTCONNECT_SOURCES})

list(APPEND app_inc_path
    "${IOTCONNECT_ROOT}"
)

message(STATUS "IoTConnect configured")
message(STATUS "  Ameba layer : ${IOTCONNECT_ROOT}")
message(STATUS "  iotc-c-lib  : ${IOTCL_ROOT}")
