#! /usr/bin/python

import sys
import os
import time
file = open(sys.argv[1])

lines = file.readlines()

i = 0

for line in lines:
	i = i+1
	print "Command " + str(i) + ": " + line,
	os.system(line)
	time.sleep(1)
