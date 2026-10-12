#!/bin/sh
# run.sh -- translate, compile and run every tests/*.pli, compare
# the output with tests/NAME.out (stdin from tests/NAME.in if present)
# usage: sh tests/run.sh      (set PLI=./pli, CC=cc as needed)
PLI=${PLI:-./pli}
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c89 -pedantic -Wall -Wno-unused-function \
 -Wno-unused-variable -Wno-unused-label}
T=${TMPDIR:-/tmp}/plitest.$$
mkdir -p $T || exit 1
cp src/plirt.h $T/
here=`pwd`
pass=0
fail=0
for f in tests/*.pli; do
  n=`basename $f .pli`
  if ! $PLI $f -o $T/$n.c 2>$T/$n.err; then
    echo "FAIL $n (translate): `cat $T/$n.err`"
    fail=`expr $fail + 1`
    continue
  fi
  if ! $CC $CFLAGS -I$T -o $T/$n $T/$n.c -lm 2>$T/$n.cerr; then
    echo "FAIL $n (C compile)"
    head -5 $T/$n.cerr
    fail=`expr $fail + 1`
    continue
  fi
  if [ -s $T/$n.cerr ]; then
    echo "WARN $n: C compiler warnings"
    head -5 $T/$n.cerr
  fi
  in=/dev/null
  [ -f tests/$n.in ] && in=$here/tests/$n.in
  ( cd $T && timeout 10 ./$n < $in > $n.act 2>&1 )
  if cmp -s $T/$n.act tests/$n.out; then
    pass=`expr $pass + 1`
  else
    echo "FAIL $n (output differs)"
    diff $T/$n.act tests/$n.out | head -10
    fail=`expr $fail + 1`
  fi
done
# multi-file programs: tests/multi/NAME/*.pli and NAME/expected.out
for d in tests/multi/*/; do
  n=`basename $d`
  if ! $PLI $d*.pli -o $T/$n.c 2>$T/$n.err; then
    echo "FAIL $n (translate): `cat $T/$n.err`"
    fail=`expr $fail + 1`
    continue
  fi
  if ! $CC $CFLAGS -I$T -o $T/$n $T/$n.c -lm 2>$T/$n.cerr; then
    echo "FAIL $n (C compile)"
    head -5 $T/$n.cerr
    fail=`expr $fail + 1`
    continue
  fi
  ( cd $T && timeout 10 ./$n < /dev/null > $n.act 2>&1 )
  if cmp -s $T/$n.act ${d}expected.out; then
    pass=`expr $pass + 1`
  else
    echo "FAIL $n (output differs)"
    diff $T/$n.act ${d}expected.out | head -10
    fail=`expr $fail + 1`
  fi
done
# separately compiled modules and C: tests/modules/NAME/*.pli *.c
for d in tests/modules/*/; do
  n=`basename $d`
  mkdir -p $T/m_$n && cp $d*.pli $d*.c $T/m_$n/ 2>/dev/null
  files=`cd $T/m_$n && ls *.pli *.c 2>/dev/null`
  if ! ( cd $T/m_$n && $here/${PLI#./} -M -c $files -cc "$CC" \
         -o $T/$n ) 2>$T/$n.err; then
    echo "FAIL $n (build): `head -5 $T/$n.err`"
    fail=`expr $fail + 1`
    continue
  fi
  ( cd $T && timeout 10 ./$n < /dev/null > $n.act 2>&1 )
  if cmp -s $T/$n.act ${d}expected.out; then
    pass=`expr $pass + 1`
  else
    echo "FAIL $n (output differs)"
    diff $T/$n.act ${d}expected.out | head -10
    fail=`expr $fail + 1`
  fi
done
echo "passed $pass, failed $fail"
rm -rf $T
[ $fail = 0 ]
