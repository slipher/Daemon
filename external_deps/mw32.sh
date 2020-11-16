d=$(dirname $(realpath $0))
rm -rf build-mw32-5/ mingw32.log
for a in zlib gmp nettle geoip curl sdl2 glew png jpeg webp freetype openal ogg vorbis speex opus opusfile lua naclsdk naclports; do
    $d/build.sh mingw32 $a
    echo $a $? >> mingw32.log
done
