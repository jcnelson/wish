#!/bin/sh

while true; do
   sleep 5
   val=$(ggetenv SYS_STATUS)

   if [[ $val != "done" ]]; then
      /etc/init.d/project $val
      gsetenv RC_$(hostname) $?
      psync $(seq 1 $(nget -n))
      gsetenv SYS_STATUS "done"
   fi
done

