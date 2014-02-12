#!/bin/sh

#gpids=$(seq 1 $(nget -n))
gpids="1 2"
#keydir="/etc/pki/tls/private/"
keydir="/tmp/"

for i in $gpids; do
  host=$(nget $i)

  pspawn -g $i -i /home/jude/csr.dat -c "openssl req -new -key $keydir/localhost.key -out /tmp/out.csr" $host
done

for i in $gpids; do
  if [[ $(pjoin $i) != 0 ]]; then
    echo "Key generation failed on $(nget $i)"
  fi
done
