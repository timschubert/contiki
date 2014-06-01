#/bin/bash

#set -x
set -e

wpan_prefix=babe::
wpan_usbtty=/dev/ttyUSB3
host_prefix=baba::
host_nwcard=wlan0
button_ipv6=323:4501:3283:343

read -n 1 -p "please disconnect all usb devices, then press any key "
read -n 1 -p "connect 'red-button' "
read -n 1 -p "connect 'border-router' "

assert() { $1 &> /dev/null || { echo $2; exit 1; } }
assert "ifconfig $host_nwcard" "$mypc_nwcard not found"
assert "ls $wpan_usbtty"       "$wpan_usbtty not found"

STEP() { printf "%-50s" "$1"; }

STEP "we need sudo right"
sudo echo ok.

STEP "starting tunslip6 serial/ipv6 bridge"
(cd ../../../tools
 sudo ./tunslip6 ${wpan_prefix}1/64 -s $wpan_usbtty
) &> /dev/null &
echo ok.

STEP "waiting for red-button http access"
while ! curl -sg "http://[$wpan_prefix$button_ipv6]/" > /dev/null ; do
	sleep 1
done
echo ok.

STEP "configuring host global ipv6 address"
#addr=$(ifconfig wlan0 | grep fe80:: | sed 's/.*fe80:://; s:/.*::')
addr=b00b:beef
sudo ifconfig wlan0 add $host_prefix$addr/128
echo ok.

STEP "configuring red-button destination"
curl -sg "http://[$wpan_prefix$button_ipv6]/set_destination?$host_prefix$addr"\
| grep -q dest_address=
echo ok.

STEP "starting notification web-server"
(while true; do
	nc -6 -l 8000 | grep button_state || break
done
) &
echo ok.

echo
echo "now, hit the red button !"
echo
echo "[press any key to terminate demo]"
echo
read -n 1

STEP "deconfiguring global ipv6 & stopping processes"
sudo ifconfig wlan0 del $host_prefix$addr/128
sudo killall tunslip6
killall nc
echo ok.
