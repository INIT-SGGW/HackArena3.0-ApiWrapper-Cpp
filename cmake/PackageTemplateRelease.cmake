if(NOT DEFINED REPO_ROOT OR NOT DEFINED STAGING_DIR OR NOT DEFINED OUTPUT_ZIP OR NOT DEFINED WRAPPER_VERSION)
    message(FATAL_ERROR "Missing required variables for template packaging.")
endif()

set(TEMPLATE_ROOT "${REPO_ROOT}/template")
set(MANIFEST_SOURCE "${TEMPLATE_ROOT}/system/manifest.toml")
set(MANIFEST_STAGING "${STAGING_DIR}/system/manifest.toml")

if(NOT EXISTS "${TEMPLATE_ROOT}")
    message(FATAL_ERROR "Missing template/ directory.")
endif()

if(NOT EXISTS "${MANIFEST_SOURCE}")
    message(FATAL_ERROR "Missing template/system/manifest.toml.")
endif()

file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${STAGING_DIR}")
file(COPY "${TEMPLATE_ROOT}/" DESTINATION "${STAGING_DIR}")

file(READ "${MANIFEST_SOURCE}" MANIFEST_CONTENT)
string(REGEX REPLACE
    "template_version[ \t]*=[ \t]*\"[^\"]*\""
    "template_version = \"${WRAPPER_VERSION}\""
    MANIFEST_RENDERED
    "${MANIFEST_CONTENT}"
)
if(MANIFEST_CONTENT STREQUAL MANIFEST_RENDERED)
    message(FATAL_ERROR "Cannot update template_version in template/system/manifest.toml.")
endif()
file(WRITE "${MANIFEST_STAGING}" "${MANIFEST_RENDERED}")

get_filename_component(OUTPUT_DIR "${OUTPUT_ZIP}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT_ZIP}" --format=zip .
    WORKING_DIRECTORY "${STAGING_DIR}"
    RESULT_VARIABLE TAR_RESULT
)
if(NOT TAR_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to create template archive: ${OUTPUT_ZIP}")
endif()
