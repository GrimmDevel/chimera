#!/bin/bash
# scripts/gen_dash_files.sh
# Generates dash source files using host tools

DASH_SRC="dash/src"
HOST_CC="clang"

echo "[CHIMERA] Generating dash source files..."

# 0. mktokens -> token.h
cd $DASH_SRC && sh mktokens
cd -

# 1. mksyntax -> syntax.c, syntax.h
$HOST_CC -o $DASH_SRC/mksyntax $DASH_SRC/mksyntax.c
cd $DASH_SRC && ./mksyntax
cd -

# 2. mknodes -> nodes.c, nodes.h
$HOST_CC -o $DASH_SRC/mknodes $DASH_SRC/mknodes.c
cd $DASH_SRC && ./mknodes nodetypes nodes.c.pat
cd -

# 3. builtins.c, builtins.h
grep -v '^[[:space:]]*\*' $DASH_SRC/builtins.def.in | grep -v '^[[:space:]]*/' | grep -v '^[[:space:]]*$' > $DASH_SRC/builtins.def
cd $DASH_SRC && sh mkbuiltins builtins.def
cd -

# 4. mkinit -> init.c
$HOST_CC -o $DASH_SRC/mkinit $DASH_SRC/mkinit.c
cd $DASH_SRC
# Collect all C files for mkinit, excluding tools and already generated files that shouldn't be parsed
INIT_FILES=$(ls *.c | grep -v mkinit.c | grep -v mknodes.c | grep -v mksignames.c | grep -v mksyntax.c | grep -v init.c)
./mkinit $INIT_FILES
cd -

# 5. signames.c
$HOST_CC -o $DASH_SRC/mksignames $DASH_SRC/mksignames.c
cd $DASH_SRC && ./mksignames
cd -

echo "[CHIMERA] Dash generation complete."
