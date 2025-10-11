ACLOCAL_AMFLAGS = -I m4

# Build processmonitor

root_sbindir           = "/usr/sbin"
root_sbin_PROGRAMS     = processmonitor
processmonitor_SOURCES   = src/process-monitor.cpp src/system-mngr-bus-connection.cpp

processmonitor_CPPFLAGS  = -I$(top_srcdir)/
processmonitor_CPPFLAGS += -std=c++11 -Wall -Wno-unused-parameter
processmonitor_LDFLAGS    = -lsystemd -ljsoncpp -lqc_faultmgr_report
