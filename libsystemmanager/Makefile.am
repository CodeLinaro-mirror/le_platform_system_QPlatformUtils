ACLOCAL_AMFLAGS = -I m4

# Build libsystemmanager.so
lib_LTLIBRARIES        = libsystemmanager.la

libsystemmanager_la_SOURCES   = system_manager_watchdog.cpp

libsystemmanager_la_CPPFLAGS  = -I$(top_srcdir)
libsystemmanager_la_CPPFLAGS += -std=c++11 -Wall -Wno-unused-parameter
libsystemmanager_la_CPPFLAGS += $(AM_CPPFLAGS) -Wno-deprecated-declarations

libsystemmanager_la_LDFLAGS   = -lpthread -lsystemd

# Export header
libsystemmanager_includedir      = $(includedir)
libsystemmanager_include_HEADERS = $(top_srcdir)/system_manager_watchdog.h
