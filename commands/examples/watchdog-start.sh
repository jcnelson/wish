#!/bin/sh

gsetenv SYS_STATUS "done"
for i in $(seq 1 $(nget -n)); do
   pspawn -d -g $i -f watchdog-server.sh $(nget $i)
done
