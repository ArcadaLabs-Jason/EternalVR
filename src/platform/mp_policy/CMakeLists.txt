# The command-line arguments the layer refuses are the launcher's list (launcher/data/refused-args.txt), built
# in here, so the launcher and the layer refuse the same text. Each record becomes one RefusedArgument.
set(EVR_REFUSED_ARGS_FILE "${PROJECT_SOURCE_DIR}/launcher/data/refused-args.txt")
file(READ "${EVR_REFUSED_ARGS_FILE}" refused_text)
# One list element per line: ';', '[' and ']' would change how CMake splits the list, so they are read as
# placeholders first (and refused in a record).
string(REPLACE ";" "<semicolon>" refused_text "${refused_text}")
string(REPLACE "[" "<bracket>" refused_text "${refused_text}")
string(REPLACE "]" "<bracket>" refused_text "${refused_text}")
string(REPLACE "\n" ";" refused_lines "${refused_text}")
set(EVR_REFUSED_ARGUMENTS "")
set(refused_count 0)
foreach(line IN LISTS refused_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    if(NOT line MATCHES "^([^|]+)[|]([^|]+)$")
        message(FATAL_ERROR "${EVR_REFUSED_ARGS_FILE}: not 'pattern | reason': ${line}")
    endif()
    string(STRIP "${CMAKE_MATCH_1}" pattern)
    string(STRIP "${CMAKE_MATCH_2}" reason)
    # Patterns are matched against normalised text (mp_policy.hpp): ASCII, lower case, no quotes, '/' for '\', no
    # '//'. A space would need the launcher's and the layer's whitespace rules to agree on it, so none is allowed.
    string(TOLOWER "${pattern}" lower)
    if(NOT pattern STREQUAL lower OR pattern MATCHES "[^!-~]|[\"\\\\]|//|<semicolon>|<bracket>")
        message(FATAL_ERROR "${EVR_REFUSED_ARGS_FILE}: pattern '${pattern}' is not written as matched (printable "
                            "ASCII in lower case, no spaces, quotes, backslashes, brackets, semicolons or '//')")
    endif()
    if(reason MATCHES "[\"\\\\]|<semicolon>|<bracket>")
        message(FATAL_ERROR "${EVR_REFUSED_ARGS_FILE}: reason '${reason}' has a quote, a backslash, a bracket or a "
                            "semicolon")
    endif()
    string(APPEND EVR_REFUSED_ARGUMENTS "    RefusedArgument{\"${pattern}\", \"${reason}\"},\n")
    math(EXPR refused_count "${refused_count} + 1")
endforeach()
if(refused_count EQUAL 0)
    message(FATAL_ERROR "${EVR_REFUSED_ARGS_FILE} has no patterns")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EVR_REFUSED_ARGS_FILE}")
configure_file(refused_arguments.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/refused_arguments.cpp" @ONLY)

evr_add_module(evr_mp_policy
    SOURCES
        mp_policy.cpp
        saved_cvars.cpp
        trip_listeners.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/refused_arguments.cpp")
