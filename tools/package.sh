#!/bin/sh
# package.sh VERSION -- build release packages into dist/
# Linux binary is linked statically so it runs on any x86_64 distro.
set -e
v=${1:-1.0}
root=`pwd`
d=$root/dist
mkdir -p $d
cc -O2 -std=c89 -o $d/pli-linux-x86_64 pli_all.c -lm -static
# Windows zips: pli.exe, runtime header, manual, examples
for a in i686:win32 x86_64:win64; do
  arch=${a%%:*}; tag=${a##*:}
  w=$d/pli-$v-$tag
  rm -rf $w; mkdir -p $w
  cp $d/pli-$arch.exe $w/pli.exe
  cp src/plirt.h README.md $w/
  [ -d $d/tcc-$tag ] && cp -r $d/tcc-$tag $w/tcc
  cp -r examples $w/
  [ -f docs/PLI-User-Guide.docx ] && cp docs/PLI-User-Guide.docx $w/
  (cd $d && rm -f pli-$v-$tag.zip &&
   zip -qr pli-$v-$tag.zip pli-$v-$tag)
  rm -rf $w
done
# Linux tarball with binary
t=$d/pli-$v-linux-x86_64
rm -rf $t; mkdir -p $t/bin $t/share/pli
cp $d/pli-linux-x86_64 $t/bin/pli
cp src/plirt.h $t/share/pli/
cp -r examples $t/share/pli/
cp README.md $t/
(cd $d && tar czf pli-$v-linux-x86_64.tar.gz pli-$v-linux-x86_64)
rm -rf $t
# source tarball and rpm
s=$d/src/pli-$v
rm -rf $d/src; mkdir -p $s
cp -r Makefile README.md pli.spec pli_all.c src tools \
   tests examples $s/
(cd $d/src && tar czf $d/pli-$v-src.tar.gz pli-$v)
if which rpmbuild >/dev/null 2>&1; then
  r=$d/rpmbuild; rm -rf $r; mkdir -p $r/SOURCES $r/SPECS
  cp $d/pli-$v-src.tar.gz $r/SOURCES/pli-$v.tar.gz
  cp pli.spec $r/SPECS/
  rpmbuild -bb --nodeps --define "_topdir $r" \
    --define "debug_package %{nil}" $r/SPECS/pli.spec >/dev/null
  cp $r/RPMS/*/*.rpm $d/
fi
# deb
if which dpkg-deb >/dev/null 2>&1; then
  b=$d/deb/pli_${v}_amd64
  rm -rf $d/deb; mkdir -p $b/DEBIAN $b/usr/bin $b/usr/share/pli
  cp $d/pli-linux-x86_64 $b/usr/bin/pli
  cp src/plirt.h $b/usr/share/pli/
  cp -r examples $b/usr/share/pli/
  cat > $b/DEBIAN/control <<EOT
Package: pli
Version: $v
Architecture: amd64
Maintainer: pli project
Description: PL/I to C89 translator
 Translates PL/I to portable C89; needs a C compiler.
EOT
  dpkg-deb --build $b $d/pli_${v}_amd64.deb >/dev/null
fi
rm -rf $d/src $d/rpmbuild $d/deb
ls -l $d
