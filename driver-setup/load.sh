#!/usr/bin/env bash
devices=$(ls /sys/bus/usb/drivers/smsc95xx | grep "^[1-9]")
for i in $devices; do
        echo Unlinking device: "$i"
        echo "$i" | sudo tee /sys/bus/usb/drivers/smsc95xx/unbind > /dev/null
done
if [[ -n $(lsmod | grep microchip_t1s) ]]
then
        echo Unloading microchip_t1s driver...
        sudo rmmod microchip_t1s
fi
echo Loading microchip_t1s driver...
sudo insmod microchip_t1s.ko
for i in $devices; do
        echo Linking device: "$i"
                echo "$i" | sudo tee /sys/bus/usb/drivers/smsc95xx/bind > /dev/null
done
