# Copyright (c) 2018 The Bitcoin developers

find_program(BREW brew)

function(find_brew_prefix VAR NAME)
    if(NOT BREW)
        return()
    endif()

    if(DEFINED ${VAR})
        return()
    endif()

    execute_process(
            COMMAND ${BREW} --prefix ${NAME}
            OUTPUT_VARIABLE PREFIX
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(${VAR} ${PREFIX} PARENT_SCOPE)
endfunction()

# Like find_brew_prefix, but BASE_NAME may be installed under any
# version-suffixed formula name (e.g. berkeley-db@4, berkeley-db@5,
# berkeley-db@18) as well as the plain/unversioned name. Homebrew has
# renamed and dropped versioned formulas before (berkeley-db@4 was removed
# from homebrew-core entirely) -- rather than hardcoding one exact formula
# name and breaking again the next time that happens, this asks Homebrew
# what's actually installed and accepts any matching formula, preferring a
# version-suffixed one (oldest first, for the closest match to whatever
# baseline version this project was designed against) over the plain/latest
# formula.
function(find_brew_prefix_any_version VAR BASE_NAME)
    if(NOT BREW)
        return()
    endif()

    if(DEFINED ${VAR})
        return()
    endif()

    execute_process(
            COMMAND ${BREW} list --formula
            OUTPUT_VARIABLE _installed_formulae
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REPLACE "\n" ";" _installed_formulae "${_installed_formulae}")

    set(_versioned_matches)
    set(_unversioned_match)
    foreach(_formula IN LISTS _installed_formulae)
        if(_formula STREQUAL "${BASE_NAME}")
            set(_unversioned_match "${_formula}")
        elseif(_formula MATCHES "^${BASE_NAME}@[0-9.]+$")
            list(APPEND _versioned_matches "${_formula}")
        endif()
    endforeach()
    list(SORT _versioned_matches COMPARE NATURAL) # numeric-aware: @4 before @18, not alphabetical
    if(_unversioned_match)
        list(APPEND _versioned_matches "${_unversioned_match}")
    endif()

    foreach(_formula IN LISTS _versioned_matches)
        execute_process(
                COMMAND ${BREW} --prefix ${_formula}
                OUTPUT_VARIABLE PREFIX
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(PREFIX)
            set(${VAR} ${PREFIX} PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()
