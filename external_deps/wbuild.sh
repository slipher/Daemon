if [ "${#}" -lt "1" ]; then
    echo "usage: ${0} <platform>"
fi
d=$(dirname $(realpath $0))
rm -rf build-$1-5/ $1.log
for a in zlib gmp nettle geoip curl sdl2 glew png jpeg webp freetype openal ogg vorbis speex opus opusfile lua naclsdk naclports; do
    $d/build.sh $1 $a
    echo $a $? >> $1.log
done
