cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bitbot VERSION 0.1.0)

# esp_audio_codec 2.5 links with an unquoted "-L <its dir>", which breaks any checkout path that
# contains a space. Its libraries are linked by full path anyway, so drop that one flag.
set(codec __idf_espressif__esp_audio_codec)
if(TARGET ${codec})
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(libs ${codec} ${property})
        if(libs)
            list(FILTER libs EXCLUDE REGEX "-L ")
            set_target_properties(${codec} PROPERTIES ${property} "${libs}")
        endif()
    endforeach()
endif()
