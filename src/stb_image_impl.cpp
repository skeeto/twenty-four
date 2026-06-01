// Single translation unit that compiles the stb_image implementation.
// We only decode our own embedded PNG window icon, so trim to PNG support.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
