d=$(dirname $(realpath $0))
rm -rf build-mw64-5/ mingw64.log
for a in zlib gmp nettle geoip curl sdl2 glew png jpeg webp freetype openal ogg vorbis speex opus opusfile lua naclsdk naclports; do
    $d/build.sh mingw64 $a
    echo $a $? >> mingw64.log
done
