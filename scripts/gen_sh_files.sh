#!/bin/bash
# scripts/gen_sh_files.sh
# Generates FreeBSD/ravynOS BSD sh source files using host tools

SH_DIR="sh"
HOST_CC="clang"

echo "[XIU] Generating FreeBSD/ravynOS BSD sh source files..."

cd $SH_DIR
sh mktokens
$HOST_CC -o mksyntax mksyntax.c
./mksyntax
$HOST_CC -o mknodes mknodes.c
./mknodes nodetypes nodes.c.pat
sh mkbuiltins .
rm -f mksyntax mknodes
cd -

echo "[XIU] BSD sh generation complete."
