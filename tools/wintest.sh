#!/bin/sh
# wintest.sh ARCH -- cross-build tests with MinGW and run them in Wine
# ARCH is i686 or x86_64; needs dist/pli-ARCH.exe (see Makefile: win)
a=${1:-x86_64}
root=`pwd`
T=${TMPDIR:-/tmp}/win-$a
export WINEDEBUG=-all
rm -rf $T; mkdir -p $T
cp src/plirt.h $T/
cp tests/*.inc tests/*.in $T/ 2>/dev/null
pass=0; fail=0
for f in tests/*.pli; do
  n=`basename $f .pli`
  cp $f $T/
  in=/dev/null; [ -f tests/$n.in ] && in=$T/$n.in
  ( cd $T &&
    wine $root/dist/pli-$a.exe $n.pli -o $n.c >/dev/null 2>$n.err &&
    $a-w64-mingw32-gcc -std=c89 -w -o $n.exe $n.c -lm 2>>$n.err &&
    timeout 60 wine ./$n.exe < $in 2>&1 | tr -d '\r' > $n.act )
  if cmp -s $T/$n.act tests/$n.out; then pass=`expr $pass + 1`
  else fail=`expr $fail + 1`; echo "FAIL $a $n"; fi
done
echo "$a: passed $pass, failed $fail"
