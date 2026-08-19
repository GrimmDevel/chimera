#!/bin/sh
# Generate GNU Autotools build system files

aclocal \
&& autoheader \
&& automake --add-missing \
&& autoconf
