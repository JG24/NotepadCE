# Regenerates build_info.h with the current YYYYMMDDHHMM timestamp.
# Invoked both at configure time and as a pre-build custom target, so the
# build number reflects the moment the binary was last compiled.
#
# The file is only rewritten when the value actually changed — within the
# same minute the content is identical, so dependent translation units
# don't needlessly recompile.
#
# Expects -DOUTPUT=<path to build_info.h>.

string(TIMESTAMP BUILD_NUMBER "%Y%m%d%H%M")
set(CONTENT "#pragma once\n#define BUILD_NUMBER L\"${BUILD_NUMBER}\"\n")

set(EXISTING "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" EXISTING)
endif()

if(NOT "${EXISTING}" STREQUAL "${CONTENT}")
    file(WRITE "${OUTPUT}" "${CONTENT}")
endif()
