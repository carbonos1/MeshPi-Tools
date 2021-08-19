#!/bin/bash

# do the Sudo Dance
sudo apt update && sudo apt upgrade -y

# Installl Dependancies and download Git Repos for the Drivers we are using
sudo apt install git dnsmasq hostapd bc build-essential dkms raspberrypi-kernel-headers


# Drivers for the Volans adapters, this shold build for the current Kernel installed
# Note, this version of the installer seems to link into WPA_Supplicant
git clone https://github.com/cilynx/rtl88x2bu
cd rtl88x2bu/
sed -i 's/I386_PC = y/I386_PC = n/' Makefile
sed -i 's/ARM_RPI = n/ARM_RPI = y/' Makefile
VER=$(sed -n 's/\PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf)
sudo rsync -rvhP ./ /usr/src/rtl88x2bu-${VER}
sudo dkms add -m rtl88x2bu -v ${VER}
sudo dkms build -m rtl88x2bu -v ${VER} # Takes ~3-minutes on a 3B+
sudo dkms install -m rtl88x2bu -v ${VER}

# Install Drivers for the Archer T4U
# TODO: Find out where you got your original drivers, but it should just be
# make
# make install


sudo apt install -y olsrd batctl

# Note: Change the IP Addresses here before running the script on the destination pi. We may need to disable this for some other Pis.

#sudo tee -a /etc/dhcpcd.conf <<EOF
#interface wlan1
#    static ip_address=192.168.4.4/24
#    nohook wpa_supplicant
#EOF


# This is another Method of Assigning Static IP's, may be a bit easier than the other way we have been doing it.

echo 'denyinterfaces wlan1' | sudo tee --append /etc/dhcpcd.conf