ACLOCAL_AMFLAGS = -I m4

# Build log-collector
root_sbindir        = "/usr/sbin"
root_sbin_PROGRAMS  = log-collector

log_collector_SOURCES    = src/log-collector.cpp
log_collector_SOURCES   += src/socket-connection.cpp
log_collector_SOURCES   += src/ssh-connection.cpp

# Flags 
log_collector_SOURCES_CPPFLAGS   = -I$(top_srcdir)/inc
log_collector_SOURCES_CPPFLAGS  += -std=c++11 -Wall -Wno-unused-parameter

# Dependencies
log_collector_LDFLAGS    = -lssh2 -ljsoncpp
