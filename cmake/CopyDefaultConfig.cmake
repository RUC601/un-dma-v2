# A rebuild must not reset the device settings beside an existing executable.
if(NOT EXISTS "${OUTPUT_CONFIG}")
    configure_file("${SOURCE_CONFIG}" "${OUTPUT_CONFIG}" COPYONLY)
endif()
