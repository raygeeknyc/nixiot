#!/bin/bash
#
# Copyright 2011,2017 Google Inc. All Rights Reserved.
# Author: raymond@insanegiantrobots.com (Raymond Blum)
# Note: /dev/tty/ACM0 is the Bluetooth serial port you have connected to the Nixie board.
# Mine is at 38400 local speed
getnum() {
  word=$(echo "${1}" | sed -r -e 's/^[^0-9]*([0-9]+).*$/\1/g')
  word="${3}${3}${3}${3}${word}"
  len=$(echo $word | wc -c)
  len=$(( len - ${2} ))
  echo $word | cut -c$len-
}


formattube_num() {
  echo " " > /dev/ttyACM0
  echo "${1}${2}${3}00000000000000000000900000000600" > /dev/ttyACM0
  echo "${1}${2}${3}00000000000000000000900000000600" > /dev/ttyACM0
}
formattube_pct() {
  echo " " > /dev/ttyACM0
  echo "XX${1}${2}${3}00000000000000000000900000000600" > /dev/ttyACM0
  echo "XX${1}${2}${3}00000000000000000000900000000600" > /dev/ttyACM0
}
blanktube() {
  formattube_num "XXXX" "N" "N"
}


while true;do
  all=$(rpcget http://0.ddash.grstatus.gtape-prod.ea.borg.google.com/ | grep -A 2 allSummary | tail -1)
  fine=$(rpcget http://0.ddash.grstatus.gtape-prod.ea.borg.google.com/ | grep -A 3 allSummary | tail -1)
  pct=$(rpcget http://0.ddash.grstatus.gtape-prod.ea.borg.google.com/ | grep -A 4 allSummary | tail -1)
  all=$(getnum "${all}" "4" "0")
  fine=$(getnum "${fine}" "4" "0")
  pct=$(getnum "${pct}" "2" "X")
  if [[ "${all}" -eq "0000" ]];then
    echo "blanking for 5m"
    blanktube
    sleep 300
  else
    echo "all: ${all}"
    formattube_num "${all}" "N" "Y"
    sleep 20
    echo "fine: ${fine}"
    formattube_num "${fine}" "Y" "N"
    sleep 20
    echo "pct: ${pct}"
    if [[ ${pct} -lt 67 ]];then
      formattube_pct "${pct}" "T" "T"
    else
      formattube_pct "${pct}" "Y" "Y"
    fi
    sleep 185
  fi
  sleep 10
done
