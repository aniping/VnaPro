if(NOT DEFINED LIBRARY_FILE OR NOT EXISTS "${LIBRARY_FILE}")
    message(FATAL_ERROR "Shared library does not exist: ${LIBRARY_FILE}")
endif()

if(NOT HOST_SYSTEM STREQUAL "Windows" AND (NOT DEFINED NM_TOOL OR NM_TOOL STREQUAL ""))
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

set(_export_tool_name "nm")
if(HOST_SYSTEM STREQUAL "Windows")
    if(NOT DEFINED OBJDUMP_TOOL OR OBJDUMP_TOOL STREQUAL "")
        find_program(OBJDUMP_TOOL objdump)
        if(NOT OBJDUMP_TOOL)
            message(FATAL_ERROR "objdump tool was not found")
        endif()
    endif()
    set(_export_tool_name "objdump")
    execute_process(
        COMMAND "${OBJDUMP_TOOL}" -p "${LIBRARY_FILE}"
        RESULT_VARIABLE _nm_result
        OUTPUT_VARIABLE _nm_output
        ERROR_VARIABLE _nm_error
    )
elseif(HOST_SYSTEM STREQUAL "Darwin")
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
    message(FATAL_ERROR "${_export_tool_name} failed (${_nm_result}): ${_nm_error}")
endif()

string(REPLACE "\r\n" "\n" _nm_output "${_nm_output}")
string(REPLACE "\r" "\n" _nm_output "${_nm_output}")
string(REPLACE "\n" ";" _raw_symbols "${_nm_output}")

set(_actual_symbols)
set(_unexpected_symbols)
set(_reading_pe_names OFF)
if(HOST_SYSTEM STREQUAL "Windows")
    if(NOT _nm_output MATCHES "Export Address Table[ \t]+([0-9a-fA-F]+)")
        message(FATAL_ERROR "PE export address table was not found")
    endif()
    math(EXPR _pe_address_count "0x${CMAKE_MATCH_1}")
    list(LENGTH _expected_symbols _expected_count)
    if(NOT _pe_address_count EQUAL _expected_count)
        message(FATAL_ERROR "Expected ${_expected_count} PE export addresses, found ${_pe_address_count}")
    endif()
endif()
foreach(_symbol IN LISTS _raw_symbols)
    if(_symbol STREQUAL "")
        continue()
    endif()

    set(_normalized "${_symbol}")
    if(HOST_SYSTEM STREQUAL "Windows")
        if(_symbol STREQUAL "[Ordinal/Name Pointer] Table")
            set(_reading_pe_names ON)
            continue()
        endif()
        if(NOT _reading_pe_names)
            continue()
        endif()
        if(_symbol MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+([^ \t]+)[ \t]*$")
            set(_normalized "${CMAKE_MATCH_1}")
        else()
            set(_reading_pe_names OFF)
            continue()
        endif()
    elseif(HOST_SYSTEM STREQUAL "Darwin")
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
list(LENGTH _expected_symbols _expected_count)
if(NOT _actual_count EQUAL _expected_count)
    message(FATAL_ERROR "Expected ${_expected_count} named native exports, found ${_actual_count}")
endif()
message(STATUS "Verified ${_actual_count} native exports in ${LIBRARY_FILE}")
