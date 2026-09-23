if(NOT DEFINED LIBRARY_FILE OR NOT EXISTS "${LIBRARY_FILE}")
    message(FATAL_ERROR "Shared library does not exist: ${LIBRARY_FILE}")
endif()

if(NOT DEFINED NM_TOOL OR NM_TOOL STREQUAL "")
    find_program(NM_TOOL nm)
    if(NOT NM_TOOL)
        message(FATAL_ERROR "nm tool was not found")
    endif()
endif()

set(_expected_symbols
    DrvHwInit
    DrvGetSweepData
    DrvStartSweep
    DrvActivateRxLink
    DrvSetRfPowerOn
    DrvSetFastSweepMode
    DrvGetIDN
    DrvCheckSweepPortPower
    DrvDataFrameDownload
)

if(HOST_SYSTEM STREQUAL "Darwin")
    execute_process(
        COMMAND "${NM_TOOL}" -gUj "${LIBRARY_FILE}"
        RESULT_VARIABLE _nm_result
        OUTPUT_VARIABLE _nm_output
        ERROR_VARIABLE _nm_error
    )
else()
    execute_process(
        COMMAND "${NM_TOOL}" -D --defined-only -P "${LIBRARY_FILE}"
        RESULT_VARIABLE _nm_result
        OUTPUT_VARIABLE _nm_output
        ERROR_VARIABLE _nm_error
    )
endif()

if(NOT _nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed (${_nm_result}): ${_nm_error}")
endif()

string(REPLACE "\r\n" "\n" _nm_output "${_nm_output}")
string(REPLACE "\r" "\n" _nm_output "${_nm_output}")
string(REPLACE "\n" ";" _raw_symbols "${_nm_output}")

set(_actual_symbols)
set(_unexpected_symbols)
foreach(_symbol IN LISTS _raw_symbols)
    if(_symbol STREQUAL "")
        continue()
    endif()

    set(_normalized "${_symbol}")
    if(HOST_SYSTEM STREQUAL "Darwin")
        if(_normalized MATCHES "^_")
            string(SUBSTRING "${_normalized}" 1 -1 _normalized)
        endif()
    else()
        string(REGEX REPLACE " .*$" "" _normalized "${_normalized}")
    endif()

    if(HOST_SYSTEM STREQUAL "Darwin" AND _normalized STREQUAL "_mh_dylib_header")
        continue()
    endif()

    list(APPEND _actual_symbols "${_normalized}")
    list(FIND _expected_symbols "${_normalized}" _expected_index)
    if(_expected_index EQUAL -1)
        list(APPEND _unexpected_symbols "${_normalized}")
    endif()
endforeach()

set(_missing_symbols)
foreach(_expected IN LISTS _expected_symbols)
    list(FIND _actual_symbols "${_expected}" _actual_index)
    if(_actual_index EQUAL -1)
        list(APPEND _missing_symbols "${_expected}")
    endif()
endforeach()

if(_missing_symbols OR _unexpected_symbols)
    message(FATAL_ERROR
        "Native export allowlist mismatch. "
        "Missing=[${_missing_symbols}] Unexpected=[${_unexpected_symbols}]"
    )
endif()

list(LENGTH _actual_symbols _actual_count)
message(STATUS "Verified ${_actual_count} native exports in ${LIBRARY_FILE}")
