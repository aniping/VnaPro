# 第三方依赖仅从仓库内的压缩包解压，不使用网络或外部源码覆盖。
include_guard(GLOBAL)

function(vnapro_extract_local_archive archive_name archive_root expected_hash output_source)
    set(archive_file "${CMAKE_SOURCE_DIR}/third-part/${archive_name}.tar.gz")
    if(NOT EXISTS "${archive_file}")
        message(FATAL_ERROR
            "Missing third-party archive: ${archive_file}. Restore the archive from the repository; downloading is disabled.")
    endif()

    file(SHA256 "${archive_file}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "Third-party archive SHA256 mismatch: ${archive_file}. Expected ${expected_hash}, got ${actual_hash}. Restore the original archive from the repository.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${archive_file}")

    set(source_parent "${CMAKE_SOURCE_DIR}/third-part")
    set(destination "${source_parent}/${archive_name}")
    set(stamp_file "${destination}/.vnapro-archive-sha256")

    # 同一依赖的解压串行执行；不同构建目录共享只读源码。
    file(MAKE_DIRECTORY "${source_parent}")
    file(LOCK "${source_parent}/.${archive_name}.lock"
        GUARD FUNCTION TIMEOUT 60 RESULT_VARIABLE lock_result)
    if(NOT lock_result STREQUAL "0")
        message(FATAL_ERROR "Cannot lock third-party extraction for ${archive_name}: ${lock_result}")
    endif()

    if(NOT EXISTS "${destination}")
        # 先在独立目录完成解压，再发布整套源码，避免编译读取未完成的目录。
        string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef staging_suffix)
        set(staging_directory "${source_parent}/.extract-${archive_name}-${staging_suffix}")
        set(staging_source_directory "${staging_directory}/${archive_root}")
        file(MAKE_DIRECTORY "${staging_directory}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xzf "${archive_file}"
            WORKING_DIRECTORY "${staging_directory}"
            RESULT_VARIABLE extract_result
            OUTPUT_QUIET ERROR_VARIABLE extract_error)
        if(NOT extract_result STREQUAL "0" OR
            NOT EXISTS "${staging_source_directory}/CMakeLists.txt")
            file(REMOVE_RECURSE "${staging_directory}")
            message(FATAL_ERROR
                "Cannot extract third-party archive ${archive_file}: ${extract_error}. Expected source root ${archive_root}.")
        endif()
        file(WRITE "${staging_source_directory}/.vnapro-archive-sha256" "${expected_hash}\n")

        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rename "${staging_source_directory}" "${destination}"
            RESULT_VARIABLE publish_result
            OUTPUT_QUIET ERROR_VARIABLE publish_error)
        file(REMOVE_RECURSE "${staging_directory}")
        if(NOT publish_result STREQUAL "0")
            # 跨平台同时配置时，另一进程可能已经发布相同版本；下面统一校验。
            if(NOT EXISTS "${stamp_file}")
                message(FATAL_ERROR "Cannot publish third-party source ${destination}: ${publish_error}")
            endif()
        endif()
    endif()

    if(NOT EXISTS "${stamp_file}" OR NOT EXISTS "${destination}/CMakeLists.txt")
        message(FATAL_ERROR
            "Incomplete third-party source directory: ${destination}. Remove this generated directory and configure again.")
    endif()
    file(READ "${stamp_file}" source_hash)
    string(STRIP "${source_hash}" source_hash)
    if(NOT source_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "Third-party source checksum marker mismatch: ${destination}. Remove this generated directory and configure again.")
    endif()

    message(STATUS "Third-party source: ${destination}")
    set("${output_source}" "${destination}" PARENT_SCOPE)
endfunction()
