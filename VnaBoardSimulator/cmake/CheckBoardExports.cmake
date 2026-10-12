set(expected_symbols
    DrvDataFrameDownload DrvGetSweepData DrvHwInit DrvSetFreqList DrvSetIfbwList
    DrvSetPowerList DrvSetRfPowerOn DrvSetWorkPorts DrvStartSweep
)

if(NOT EXISTS "${LIBRARY_FILE}")
    message(FATAL_ERROR "TDC1VN10 library does not exist: ${LIBRARY_FILE}")
endif()

if(HOST_SYSTEM STREQUAL "Windows")
    execute_process(COMMAND "${OBJDUMP_TOOL}" -p "${LIBRARY_FILE}"
        OUTPUT_VARIABLE exports ERROR_VARIABLE export_error RESULT_VARIABLE result
    )
elseif(HOST_SYSTEM STREQUAL "Darwin")
    execute_process(COMMAND "${NM_TOOL}" -gUj "${LIBRARY_FILE}"
        OUTPUT_VARIABLE exports ERROR_VARIABLE export_error RESULT_VARIABLE result
    )
else()
    # 使用 POSIX 格式，让名称固定在首列，避免把十六进制地址末位和类型字母误识别成符号。
    execute_process(COMMAND "${NM_TOOL}" -D --defined-only -P "${LIBRARY_FILE}"
        OUTPUT_VARIABLE exports ERROR_VARIABLE export_error RESULT_VARIABLE result
    )
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect TDC1VN10 export symbols: ${export_error}")
endif()

string(REPLACE "\r\n" "\n" exports "${exports}")
string(REPLACE "\n" ";" symbol_lines "${exports}")
set(actual_symbols)
set(reading_pe_names OFF)
foreach(line IN LISTS symbol_lines)
    if(HOST_SYSTEM STREQUAL "Windows")
        if(line STREQUAL "[Ordinal/Name Pointer] Table")
            set(reading_pe_names ON)
        elseif(reading_pe_names)
            if(line MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+([^ \t]+)[ \t]*$")
                list(APPEND actual_symbols "${CMAKE_MATCH_1}")
            else()
                set(reading_pe_names OFF)
            endif()
        endif()
    elseif(HOST_SYSTEM STREQUAL "Darwin")
        if(NOT line STREQUAL "")
            string(REGEX REPLACE "^_" "" symbol "${line}")
            list(APPEND actual_symbols "${symbol}")
        endif()
    elseif(line MATCHES "^([^ \t]+)[ \t]+[A-Za-z][ \t]+")
        list(APPEND actual_symbols "${CMAKE_MATCH_1}")
    endif()
endforeach()

if(HOST_SYSTEM STREQUAL "Windows")
    # 地址表计数还需核对，防止额外序号导出没有名称而漏过名称白名单。
    if(NOT exports MATCHES "Export Address Table[ \t]+([0-9a-fA-F]+)")
        message(FATAL_ERROR "TDC1VN10 export address table not found")
    endif()
    math(EXPR address_count "0x${CMAKE_MATCH_1}")
    list(LENGTH expected_symbols expected_count)
    if(NOT address_count EQUAL expected_count)
        message(FATAL_ERROR "TDC1VN10 export address count differs: ${address_count}")
    endif()
endif()

list(SORT expected_symbols)
list(SORT actual_symbols)
if(NOT "${actual_symbols}" STREQUAL "${expected_symbols}")
    message(FATAL_ERROR "TDC1VN10 symbols differ: actual=${actual_symbols}, expected=${expected_symbols}")
endif()
