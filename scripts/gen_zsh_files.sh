#!/bin/bash
# scripts/gen_zsh_files.sh
# Generates Zsh source and prototype files for Chimera OS

WORKSPACE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZSH_SRC="$WORKSPACE/zsh/Src"

echo "[CHIMERA] Generating Zsh headers and prototypes..."

cd "$ZSH_SRC"

# 1. Generate signames.c from sys/signal.h
awk -f signames1.awk "$WORKSPACE/usr/libsystem/include/sys/signal.h" > sigtmp.c
clang -E -I"$WORKSPACE/usr/libsystem/include" sigtmp.c > sigtmp.out
awk -f signames2.awk sigtmp.out > signames.c
rm -f sigtmp.c sigtmp.out

# 2. Generate prototypes for root Src
for f in *.c; do
    base=$(basename "$f" .c)
    awk -f makepro.awk "$f" Src > "$base.syms"
    (echo '/* Generated automatically */'; sed -n '/^E/{s/^E//;p;}' < "$base.syms") > "$base.epro"
    (echo '/* Generated automatically */'; sed -n '/^L/{s/^L//;p;}' < "$base.syms") > "$base.pro"
    rm -f "$base.syms"
done

# Patch math.epro for isinf/isnan
sed -i '' 's/extern int isinf/#ifndef HAVE_ISINF\nextern int isinf/' "$ZSH_SRC/math.epro"
sed -i '' 's/extern int isnan _((double x));/extern int isnan _((double x));\n#endif/' "$ZSH_SRC/math.epro"

# 3. Generate prototypes for Zle
if [ -d "Zle" ]; then
    cd Zle
    for f in *.c; do
        base=$(basename "$f" .c)
        awk -f ../makepro.awk "$f" Zle > "$base.syms"
        (echo '/* Generated automatically */'; sed -n '/^E/{s/^E//;p;}' < "$base.syms") > "$base.epro"
        (echo '/* Generated automatically */'; sed -n '/^L/{s/^L//;p;}' < "$base.syms") > "$base.pro"
        rm -f "$base.syms"
    done
    cd ..
fi

# 4. Generate prototypes for Builtins
if [ -d "Builtins" ]; then
    cd Builtins
    for f in *.c; do
        base=$(basename "$f" .c)
        awk -f ../makepro.awk "$f" Builtins > "$base.syms"
        (echo '/* Generated automatically */'; sed -n '/^E/{s/^E//;p;}' < "$base.syms") > "$base.epro"
        (echo '/* Generated automatically */'; sed -n '/^L/{s/^L//;p;}' < "$base.syms") > "$base.pro"
        rm -f "$base.syms"
    done
    cd ..
fi

cd "$WORKSPACE"
echo "[CHIMERA] Zsh generation complete."
