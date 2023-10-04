ACLOCAL_AMFLAGS = -I m4

# Build libbootreason.so
lib_LTLIBRARIES        = libbootreason.la

libbootreason_la_SOURCES   = reboot-reason.cpp

libbootreason_la_CPPFLAGS  = -I$(top_srcdir)/inc
libbootreason_la_CPPFLAGS += -std=c++11 -Wall -Wno-unused-parameter
libbootreason_la_CPPFLAGS += $(AM_CPPFLAGS) -Wno-deprecated-declarations

# Build Utility
root_sbindir            = "/usr/sbin"
root_sbin_PROGRAMS      = reboot-reason-test

reboot_reason_test_SOURCES   = reboot-reason-test.cpp

reboot_reason_test_CPPFLAGS  = -I$(top_srcdir)/
reboot_reason_test_CPPFLAGS += -std=c++11 -Wall -Wno-unused-parameter

reboot_reason_test_LDADD     = libbootreason.la
