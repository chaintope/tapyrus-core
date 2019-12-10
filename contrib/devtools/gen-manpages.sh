#!/usr/bin/env bash

export LC_ALL=C
TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
BUILDDIR=${BUILDDIR:-$TOPDIR}

BINDIR=${BINDIR:-$BUILDDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

TAPYRUSD=${TAPYRUSD:-$BINDIR/tapyrusd}
TAPYRUSCLI=${TAPYRUSCLI:-$BINDIR/tapyrus-cli}
TAPYRUSTX=${TAPYRUSTX:-$BINDIR/tapyrus-tx}
TAPYRUSGENESIS=${TAPYRUSGENESIS:-$BINDIR/tapyrus-genesis}

[ ! -x $TAPYRUSD ] && echo "$TAPYRUSD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
BTCVER=($($TAPYRUSCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for tapyrusd if --version-string is not set,
# but has different outcomes for tapyrus-qt and tapyrus-cli.
echo "[COPYRIGHT]" > footer.h2m
$TAPYRUSD --version | sed -n '1!p' >> footer.h2m

for cmd in $TAPYRUSD $TAPYRUSCLI $TAPYRUSTX $TAPYRUSGENESIS; do
  cmdname="${cmd##*/}"
  /Users/navia/Documents/Tools/help2man-1.47.11/help2man -N --version-string=${BTCVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${BTCVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
