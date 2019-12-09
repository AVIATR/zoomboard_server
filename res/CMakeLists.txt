file(GLOB RES_FILES_TO_CONFIGURE RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.json.in")
message(STATUS, "Configuring resource files with stream folder: ${WWW_DIR}")
foreach(file ${RES_FILES_TO_CONFIGURE})
    string(REGEX REPLACE "\\.[^.]*$" "" outfile ${file}) #remove the *.in extension
    configure_file(
        ${file}
        ${outfile}
        @ONLY
    )
    message(STATUS "Configured file ${CMAKE_CURRENT_BINARY_DIR}/${outfile}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${outfile}" DESTINATION .)
endforeach()
