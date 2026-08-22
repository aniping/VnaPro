function(vna_prepare_third_party package archive archive_hash sentinel sentinel_hash)
    set(package_directory "${PROJECT_SOURCE_DIR}/third-part/${package}")
    set(archive_path "${PROJECT_SOURCE_DIR}/third-part/archives/${archive}")
    set(sentinel_path "${package_directory}/${sentinel}")

    # Never overwrite an existing dependency tree: it may contain local work.
    # The sentinel hash also rejects stale submodule or partial-copy contents.
    if(EXISTS "${sentinel_path}")
        file(SHA256 "${sentinel_path}" actual_sentinel_hash)
        if(actual_sentinel_hash STREQUAL sentinel_hash)
            return()
        endif()
        message(FATAL_ERROR
            "third-part/${package} does not match the vendored snapshot. "
            "Remove that directory and configure again.")
    endif()
    if(EXISTS "${package_directory}")
        message(FATAL_ERROR
            "third-part/${package} is incomplete. Remove that directory and "
            "configure again.")
    endif()
    if(NOT EXISTS "${archive_path}")
        message(FATAL_ERROR "Missing vendored dependency archive: ${archive_path}")
    endif()

    file(SHA256 "${archive_path}" actual_archive_hash)
    if(NOT actual_archive_hash STREQUAL archive_hash)
        message(FATAL_ERROR "Vendored dependency archive checksum failed: ${archive}")
    endif()

    # Only a missing tree is restored, from a checksum-pinned offline archive.
    message(STATUS "Extracting vendored dependency ${package}")
    file(ARCHIVE_EXTRACT INPUT "${archive_path}"
        DESTINATION "${PROJECT_SOURCE_DIR}/third-part")
    if(NOT EXISTS "${sentinel_path}")
        message(FATAL_ERROR "Archive ${archive} did not create ${sentinel_path}")
    endif()
    file(SHA256 "${sentinel_path}" actual_sentinel_hash)
    if(NOT actual_sentinel_hash STREQUAL sentinel_hash)
        message(FATAL_ERROR "Extracted dependency checksum failed: ${package}")
    endif()
endfunction()

vna_prepare_third_party(
    googletest googletest-1.17.0.tar.xz
    98463668a2c0333472f8d6ef834fae861f4970ef9e4860aff6b2936eb3b72f43
    CMakeLists.txt cdb27ec80782d56c49ec8a50a5b5819a42ec9354ab16ed43bb727e6ea8c5fbf3)
vna_prepare_third_party(
    cpp-httplib cpp-httplib-0.51.0-vna1.tar.xz
    edea744ba00da1c3f14aea9fde895eed02edbef37af26bacdf3a09cafe21b00e
    httplib.h 8cd04b8627f6d65bdb25846b4f1a96b009bedeeff557736412bfc77b8bdc7875)
vna_prepare_third_party(
    nlohmann-json nlohmann-json-3.12.0.tar.xz
    c67a10f5ac8fa59449dfba8e651bdb3dfc8fbd4da6f3119822a13a3af4e538ae
    CMakeLists.txt 90d570b423d9c8cce5609cb07f43052713ddbf768cc94bbef26fba3d982239a1)
vna_prepare_third_party(
    scpi-parser scpi-parser-2.3.tar.xz
    70293372e51fc064e12fcabe9cbef7311c3556a444c800ea4fe051bf697e6d53
    libscpi/src/parser.c 98b9a05c6a56283eb5e478e0d8edb3d8150d7fe8b145665765a231f29b2cccb3)
vna_prepare_third_party(
    spdlog spdlog-1.17.0.tar.xz
    266939b62e6afbe10f871ae40784e28ae81d90209315861913ff3a6baac065ea
    CMakeLists.txt 9f75430b4fd65eceb9fa9fb62cda6c58ff5c96b1521bf308f57b0c99a22244cc)
