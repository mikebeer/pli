#!/bin/sh
# linelen.sh -- fail if any source line is longer than 71 columns
# usage: sh tools/linelen.sh file...
max=71
bad=0
for f in "$@"; do
  awk -v m=$max -v f="$f" 'length($0) > m \
    { printf "%s:%d: %d columns\n", f, NR, length($0); b=1 } \
    END { exit b }' "$f" || bad=1
done
exit $bad
