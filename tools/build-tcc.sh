#!/bin/sh
# build-tcc.sh -- cross-build TinyCC for Windows into dist/tcc-win32
# and dist/tcc-win64, to be bundled next to pli.exe (see package.sh).
# Needs MinGW-w64, git and Wine (libtcc1.a is made by running the new
# tcc.exe). TinyCC is LGPL; its COPYING file goes into the package.
# Tested with TinyCC commit 43c7708.
set -e
root=`pwd`
w=${TMPDIR:-/tmp}/tcc-build
rm -rf $w; mkdir -p $w
git clone -q --depth 1 https://github.com/TinyCC/tinycc.git $w/src
# configure builds c2str for the target; make a native one instead
gcc -DC2STR $w/src/conftest.c -o $w/c2str.native
for a in x86_64:win64:x86_64 i686:win32:i386; do
  arch=${a%%:*}; r=${a#*:}; tag=${r%%:*}; cpu=${r#*:}
  b=$w/b-$tag
  cp -r $w/src $b
  printf '#!/bin/sh\nexport WINEDEBUG=-all\n' > $w/wine-$tag.sh
  printf 'exec wine %s/tcc.exe "$@"\n' $b >> $w/wine-$tag.sh
  chmod +x $w/wine-$tag.sh
  (cd $b &&
   ./configure --targetos=WIN32 --cpu=$cpu \
     --cross-prefix=$arch-w64-mingw32- --enable-static >/dev/null &&
   cp $w/c2str.native c2str.exe && touch c2str.exe &&
   make tcc.exe >/dev/null &&
   make -C lib XTCC=$w/wine-$tag.sh >/dev/null)
  d=$root/dist/tcc-$tag
  rm -rf $d; mkdir -p $d/include $d/lib
  cp $b/tcc.exe $d/
  cp -r $b/include/* $b/win32/include/* $d/include/
  cp $b/win32/lib/*.def $d/lib/
  cp $b/libtcc1.a $b/bcheck.o $b/bt-*.o $b/runmain.o $d/lib/
  cp $b/COPYING $d/
done
ls dist
