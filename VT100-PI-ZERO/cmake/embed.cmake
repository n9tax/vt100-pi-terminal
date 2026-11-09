# Turn a file into a C byte array: cmake -DIN=... -DOUT=... -DNAME=... -P embed.cmake
# Used to compile web/index.html into the binary, so the web view needs no
# files installed beside it.
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}"
    "/* Generated from ${IN} by cmake/embed.cmake. Do not edit. */\n"
    "const unsigned char ${NAME}[] = {${bytes}0x00};\n"
    "const unsigned int ${NAME}_len = sizeof ${NAME} - 1;\n")
