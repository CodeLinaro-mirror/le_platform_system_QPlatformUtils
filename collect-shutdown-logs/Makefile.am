ACLOCAL_AMFLAGS = -I m4

#build collect-logs bin

root_sbindir = "/usr/sbin"
root_sbin_PROGRAMS = collect-logs

collect_logs_SOURCES = collect-logs.cpp

collect_logs_SOURCES_CPPFLAGS   = -I$(top_srcdir)
collect_logs_SOURCES_CPPFLAGS  += -std=c++17 -Wall -Wno-unused-parameter
collect_logs_LDFLAGS = -lz
