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
# version-suffixed formula name up to @MAX_MAJOR (e.g. berkeley-db@4,
# berkeley-db@5) instead of one hardcoded exact name. Homebrew has renamed
# and dropped versioned formulas before (berkeley-db@4 was removed from
# homebrew-core entirely) -- rather than guessing one name and breaking
# again the next time that happens, this asks Homebrew what's actually
# installed and accepts any matching @N formula, preferring the newest
# allowed major version.
#
# The plain/unversioned formula name is deliberately never considered:
# callers that care about a specific major-version ceiling generally do so
# because a newer major version means a different (possibly incompatible
# license, e.g. Homebrew's plain berkeley-db is presently 18.1.x under
# AGPL-3.0-only, vs. the Sleepycat-licensed @4/@5) -- and the plain formula
# tracks whatever Homebrew currently ships, with no version ceiling of its
# own. Only an explicit berkeley-db@N match, with N checked against
# MAX_MAJOR, is ever accepted.
function(find_brew_prefix_any_version VAR BASE_NAME MAX_MAJOR)
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

    set(_candidates)
    foreach(_formula IN LISTS _installed_formulae)
        if(_formula MATCHES "^${BASE_NAME}@([0-9]+)$" AND CMAKE_MATCH_1 LESS_EQUAL MAX_MAJOR)
            list(APPEND _candidates "${_formula}")
        endif()
    endforeach()
    if(NOT _candidates)
        return()
    endif()
    list(SORT _candidates COMPARE NATURAL ORDER DESCENDING) # newest allowed major first
    list(GET _candidates 0 _best)
    list(LENGTH _candidates _num_candidates)
    if(_num_candidates GREATER 1)
        message(STATUS "find_brew_prefix_any_version: multiple ${BASE_NAME}@<=${MAX_MAJOR} formulae installed (${_candidates}) -- using ${_best}")
    endif()

    execute_process(
            COMMAND ${BREW} --prefix ${_best}
            OUTPUT_VARIABLE PREFIX
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(PREFIX)
        set(${VAR} ${PREFIX} PARENT_SCOPE)
    endif()
endfunction()
