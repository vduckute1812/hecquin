# Ad-hoc code-signing helper for Apple platforms.
#
# macOS 14+ will quarantine (and occasionally delete) freshly-linked Mach-O
# binaries that carry no code signature — especially after the first run.
# Adding an empty "ad-hoc" signature (`codesign --sign -`) is enough to make
# Gatekeeper and AMFI treat the binary as intentional and leave it alone.
#
# Usage:
#   hecquin_adhoc_codesign(target_name [target_name ...])

function(hecquin_adhoc_codesign)
    if (NOT APPLE)
        return()
    endif ()
    find_program(CODESIGN_EXECUTABLE codesign)
    if (NOT CODESIGN_EXECUTABLE)
        message(WARNING "codesign not found — binaries will run unsigned and may be quarantined by macOS.")
        return()
    endif ()
    foreach (tgt IN LISTS ARGN)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CODESIGN_EXECUTABLE} --force --sign - $<TARGET_FILE:${tgt}>
            COMMENT "Ad-hoc signing ${tgt}"
            VERBATIM)
    endforeach ()
endfunction()
