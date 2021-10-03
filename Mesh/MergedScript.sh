#!/bin/bash

# File for Setting up Mesh Networking, Should support a whole load of stuff

# Check Script is Run as Root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

# Take down the Interface Beforehand
ip link set wlan1 down

# Set up Ad-Hoc mode
iwconfig wlan1 mode ad-hoc
iwconfig wlan1 channel 6
iwconfig wlan1 essid 'TOC-TestMesh'

# Set up Static IP Address
ifconfig wlan1 $2 netmask 255.255.255.0 


# You have to manually enable arp or manually add arp entries, Google this when you get the chance, but for safekeeping its in comments here

#touch /etc/ethers
#echo 169.254.1.1 bb:bb:bb:bb:etcetc

# then run
# arp -f 
# arp -a

# Determine The Routing Protocol to use
case $1 in
olsr | OLSR | o )
    olsrd -i wlan1 -f ./olsrd.conf
    ;;
batman | BATMAN | batman-adv | BATMAN-adv )
    # Remove the IP address from the earlier command
    ifconfig wlan1 del $2
    # Set up IBSS to urn underneath
    iw wlan1 set type ibss
    # Set Larger MTU for BATMAN
    ifconfig wlan1 mtu 1532
    ifconfig wlan1 up
    iw wlan1 ibss join 'TOC-TestMesh' 2437
    # Get BATMAN
    modprobe batman-adv
    batctl if add wlan1
    ip link set up dev bat0
    ifconfig bat0 192.168.4.3/24
    ifconfig bat0 up
    ;;
IBSS | ibss ) 
    iw wlan1 set type ibss
    ip link set wlan1 up
    iw wlan1 ibss join 'TOC-TestMesh' 2437
;;
*) 
    echo "No Option Specified, no routing protocol will be used"
    ;;
esac

# Bring up the interface with the set changes
ip link set wlan1 up



