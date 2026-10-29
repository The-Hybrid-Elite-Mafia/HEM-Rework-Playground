/* The free audio decoders the engine reads files with, compiled once (see README.md). */
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "dr_libs/dr_wav.h"
#include "dr_libs/dr_flac.h"
#include "dr_libs/dr_mp3.h"
#include "stb/stb_vorbis.c"
