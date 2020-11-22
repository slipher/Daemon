if [ "${#}" -ne "1" ]; then
    echo "usage: ${0} <platform>"
    exit
fi
d=$(dirname $(realpath $0))
$d/build.sh $1 clean || exit
rm -f $1.log
for a in zlib gmp nettle geoip curl sdl2 glew png jpeg webp freetype openal ogg vorbis speex opus opusfile lua naclsdk naclports gendef; do
    $d/build.sh $1 $a
    echo $a $? >> $1.log
done
