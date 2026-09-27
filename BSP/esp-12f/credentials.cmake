# Read the user's local text files without exposing secrets in CMake output/cache.
set(_credentials_dir "${CMAKE_CURRENT_LIST_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_credentials_dir}/wifi.txt" "${_credentials_dir}/onenet.txt")

function(esp12f_read_field file key output allow_empty)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "ESP-12F requires ${file}; see BSP/esp-12f/README.md")
    endif()
    file(READ "${file}" contents)
    # Ignore a possible UTF-8 BOM. Values are the rest of one line.
    string(ASCII 239 187 191 bom)
    string(REPLACE "${bom}" "" contents "${contents}")
    string(REGEX MATCH "(^|[\r\n])${key}[ \t]*(:|：|=)[ \t]*([^\r\n]*)" matched "${contents}")
    if(NOT matched)
        message(FATAL_ERROR "Missing ${key} field in ${file}")
    endif()
    set(value "${CMAKE_MATCH_3}")
    # Both name:plain and name:"quoted" occur in local configuration files.
    if(value MATCHES "^\"(.*)\"[ \t]*$")
        set(value "${CMAKE_MATCH_1}")
    endif()
    if(NOT allow_empty AND value STREQUAL "")
        message(FATAL_ERROR "Empty ${key} field in ${file}")
    endif()
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${output} "${value}" PARENT_SCOPE)
endfunction()

esp12f_read_field("${_credentials_dir}/wifi.txt" "name" ONENET_WIFI_SSID FALSE)
esp12f_read_field("${_credentials_dir}/wifi.txt" "password" ONENET_WIFI_PASSWORD TRUE)
esp12f_read_field("${_credentials_dir}/onenet.txt" "产品id" ONENET_PRODUCT_ID FALSE)
esp12f_read_field("${_credentials_dir}/onenet.txt" "设备名称" ONENET_DEVICE_NAME FALSE)
esp12f_read_field("${_credentials_dir}/onenet.txt" "token" ONENET_TOKEN FALSE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${_credentials_dir}/onenet_credentials.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/onenet_credentials.h" @ONLY)
