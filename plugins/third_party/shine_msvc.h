/* Force-included when shine is built with MSVC (see plugins/CMakeLists.txt): shine marks two variables with GCC's
   __attribute__((unused)), which MSVC doesn't know. Not part of shine. */
#pragma once
#define __attribute__(x)
