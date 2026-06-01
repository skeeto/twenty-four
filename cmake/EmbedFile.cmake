# Embed a binary file as a C++ byte array header.
#
# Invoke in script mode:
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DSYMBOL=<name> -P EmbedFile.cmake
#
# Produces a header declaring:
#   inline constexpr unsigned char <SYMBOL>[]   = { ... };
#   inline constexpr size_t        <SYMBOL>_len = <N>;
# inside namespace tf, so the asset is compiled directly into the binary/wasm.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR num_bytes "${hex_length} / 2")

# "ab" -> "0xab,"
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

file(WRITE "${OUTPUT}"
"// Generated from ${INPUT} -- do not edit.
#pragma once
#include <cstddef>

namespace tf {
inline constexpr unsigned char ${SYMBOL}[] = {${bytes}};
inline constexpr size_t ${SYMBOL}_len = ${num_bytes};
}  // namespace tf
")
