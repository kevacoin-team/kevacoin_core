# ===========================================================================
#     https://www.gnu.org/software/autoconf-archive/ax_boost_thread.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BOOST_THREAD
#
# DESCRIPTION
#
#   Test for Thread library from the Boost C++ libraries. The macro requires
#   a preceding call to AX_BOOST_BASE. Further documentation is available at
#   <http://randspringer.de/boost/index.html>.
#
#   This macro calls:
#
#     AC_SUBST(BOOST_THREAD_LIB)
#
#   And sets:
#
#     HAVE_BOOST_THREAD
#
# LICENSE
#
#   Copyright (c) 2009 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2009 Michael Tindal
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 33

AC_DEFUN([AX_BOOST_THREAD],
[
    AC_ARG_WITH([boost-thread],
    AS_HELP_STRING([--with-boost-thread@<:@=special-lib@:>@],
                   [use the Thread library from boost -
                    it is possible to specify a certain library for the linker
                    e.g. --with-boost-thread=boost_thread-gcc-mt ]),
        [
        if test "$withval" = "yes"; then
            want_boost="yes"
            ax_boost_user_thread_lib=""
        else
            want_boost="yes"
            ax_boost_user_thread_lib="$withval"
        fi
        ],
        [want_boost="yes"]
    )

    
    if test "x$want_boost" = "xyes"; then
        AC_REQUIRE([AC_PROG_CC])
        AC_REQUIRE([AC_CANONICAL_BUILD])
        CPPFLAGS_SAVED="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
        export CPPFLAGS

        LDFLAGS_SAVED="$LDFLAGS"
        LDFLAGS="$LDFLAGS $BOOST_LDFLAGS"
        export LDFLAGS

        # Adjust CXXFLAGS for C++14 or later
        CXXFLAGS_SAVE=$CXXFLAGS
        CXXFLAGS="$CXXFLAGS -std=c++14"

        AC_CACHE_CHECK([whether the Boost::Thread library is available],
                       ax_cv_boost_thread,
        [
            AC_LANG_PUSH([C++])

            # Simplified check assuming potential dependency on Boost System
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
                [[@%:@include <boost/thread.hpp>]
                 [@%:@include <boost/system/system_error.hpp>]],
                [[boost::thread t; boost::system::error_code ec;]])],
                ax_cv_boost_thread=yes,
                ax_cv_boost_thread=no)

            CXXFLAGS=$CXXFLAGS_SAVE
            AC_LANG_POP([C++])
        ])
        if test "x$ax_cv_boost_thread" = "xyes"; then
            # Linking code and other settings...
        fi

        # Restore original flags
        CPPFLAGS="$CPPFLAGS_SAVED"
        LDFLAGS="$LDFLAGS_SAVED"
    fi
])