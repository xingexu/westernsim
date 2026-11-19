#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#define WESTERNSIM_GLSL "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#include <glad/gl.h>
#define WESTERNSIM_GLSL "#version 330 core\n"
#endif
