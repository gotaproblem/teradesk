#!/bin/sh

ARANYM="${HOME}/.aranym"
echo "ARANYM=$ARANYM" >> $GITHUB_ENV

(
mkdir "$ARANYM"
cd "$ARANYM"

curl -L --get https://mikro.atari.org/tho-otto.de/mag-hdd.tar.bz2 --output mag-hdd.tar.bz2
#curl -L --get https://mikro.atari.org/tho-otto.de/aranym-1.0.2-trusty-x86_64-a9de1ec.tar.xz --output aranym.tar.xz
curl -L --get https://mikro.atari.org/tho-otto.de/aranym-1.1.0-xenial-x86_64-4ebb186.tar.xz --output aranym.tar.xz

tar xvf mag-hdd.tar.bz2
test -f config-hdd || exit 1

tar xvf aranym.tar.xz

image=hdd.img
offset=0

echo "drive c: file=\"$PWD/$image\" MTOOLS_SKIP_CHECK=1 MTOOLS_LOWER_CASE=1 MTOOLS_NO_VFAT=1 offset=$offset" > ~/.mtoolsrc

)

