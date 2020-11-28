#!/bin/bash
d=$(dirname $0)
$d/build.sh macosx64 clean || exit
$d/build.sh macosx64 nasm gmp nettle geoip sdl2 glew png jpeg webp freetype openal ogg vorbis speex opus opusfile lua naclsdk naclports install package
