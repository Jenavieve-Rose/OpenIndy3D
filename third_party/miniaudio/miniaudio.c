/*
 * miniaudio's implementation, compiled once (PORT-PLAN.md P4.1). The port uses only its playback device; the
 * decoders, encoders, engine, node graph and resource manager are compiled out, and Core Audio is the only backend.
 * Those switches are the miniaudio target's public compile definitions (CMakeLists.txt), so every file that includes
 * miniaudio.h sees the same structures. Version and source: README.port.md in this folder.
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
