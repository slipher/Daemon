#!/bin/bash
if [ "${#}" -ne "0" ]; then
    echo "usage: ${0}"
    exit
fi
d=$(dirname $0)
$d/build.sh macosx64 clean || exit
rm -f macosx64.log
for a in nasm gmp nettle geoip sdl2 glew png jpeg webp freetype openal ogg vorbis speex opus opusfile lua naclsdk naclports; do
    $d/build.sh macosx64 $a
    echo $a $? >> macosx64.log
done
