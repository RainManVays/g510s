#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([libg15render],[1.3],[mirabeaj@gmail.com])
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR([src/libg15render.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

# Checks for programs.
AC_PROG_CC
LT_INIT

# Checks for --enable args
AC_MSG_CHECKING(whether to enable FreeType2 support)
AC_ARG_ENABLE(ttf, [  --enable-ttf		enable FreeType2 support])
AS_IF([[[ "$enable_ttf" = "yes" ]]],[
	AC_DEFINE(TTF_SUPPORT, [1], [Define to 1 to enable FreeType2 support])
	PKG_CHECK_MODULES([FREETYPE2],[freetype2])
	CFLAGS="$CFLAGS $FREETYPE2_CFLAGS"
	ttf_support="yes"
],[
	ttf_support="no"
])
AC_MSG_RESULT($ttf_support)

# Checks for libraries.
AC_CHECK_LIB([g15], [writePixmapToLCD], ,AC_MSG_ERROR(["libg15 not found. please install it"]))
AC_CHECK_LIB([m], [ceil], ,AC_MSG_ERROR(["libm not found.  Please install it"]))

AC_DEFINE_UNQUOTED([G15FONT_DIR],"${prefix}/share/g15tools/fonts",[Location of G15 fonts])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

AC_CHECK_HEADERS([string.h ft2build.h])

AC_CHECK_FUNCS([memset])

AC_CONFIG_FILES([
Makefile
src/Makefile
])

AC_OUTPUT
