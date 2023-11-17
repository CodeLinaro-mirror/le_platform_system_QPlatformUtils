ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS  = $(GLIB_CFLAGS) -include glib.h -Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat

# Build crash-collect bin utility
root_sbindir           = "/sbin"
root_sbin_PROGRAMS     = crashcollect
crashcollect_CC = @CC@
crashcollect_SOURCES  = crash-collect.c
crashcollect_LDADD = $(GLIB_LIBS)

crashcollect_CPPFLAGS  = -I$(top_srcdir)
crashcollect_CPPFLAGS += -std=c++11 -Wall -Wno-unused-parameter
crashcollect_CPPFLAGS += $(AM_CPPFLAGS) -Wno-deprecated-declarations
