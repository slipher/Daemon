set(SAIGOSDK_BASENAME saigosdk-${DEPS_SYSTEM}-${DEPS_ARCH}_${SAIGOSDK_VERSION})
set(SAIGOSDK_ROOT ${EXTERNAL_DEPS_DIR}/${SAIGOSDK_BASENAME})
set(SAIGOSDK_ROOT "${SAIGOSDK_ROOT}" PARENT_SCOPE)

function(download_saigosdk)
    if (NOT EXISTS ${SAIGOSDK_ROOT})
        set(DEPS_FILENAME "${SAIGOSDK_BASENAME}.tar.xz")
        set(REMOTE "https://github.com/DaemonEngine/saigo-nacl-sdk/releases/download/v${SAIGOSDK_VERSION}/${DEPS_FILENAME}")
        message(STATUS "Downloading NaCl SDK from '${REMOTE}'")

        file(DOWNLOAD "${REMOTE}" "${OBJ_DIR}/${DEPS_FILENAME}"
            SHOW_PROGRESS
            STATUS DOWNLOAD_RESULT
            LOG DOWNLOAD_LOG
        )

        list(GET DOWNLOAD_RESULT 0 DOWNLOAD_STATUS)
        list(GET DOWNLOAD_RESULT 1 DOWNLOAD_STRING)

        if (NOT DOWNLOAD_STATUS EQUAL 0)
            message(FATAL_ERROR "Failed to download '${REMOTE}': This may be a network error, or your system may be an unsupported host platform.
                Status code: ${DOWNLOAD_STATUS}
                Error string: ${DOWNLOAD_STRING}
                Download log: ${DOWNLOAD_LOG}"
            )
        endif()

        message(STATUS "SDK download completed successfully")

        # Extract the downloaded archive
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf ${OBJ_DIR}/${DEPS_FILENAME}
            WORKING_DIRECTORY ${EXTERNAL_DEPS_DIR}
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if (NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not extract ${DEPS_FILENAME}")
        endif()
    endif()
endfunction()
