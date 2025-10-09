#!/bin/bash

# Check for available serial devices and add them to docker run command
DEVICE_ARGS=""
for device in /dev/ttyUSB* /dev/ttyACM*; do
    if [ -e "$device" ]; then
        DEVICE_ARGS="$DEVICE_ARGS --device=$device"
        # Make sure the device is accessible
        sudo chmod 666 "$device" 2>/dev/null || true
    fi
done
echo "devices = $DEVICE_ARGS"

docker run -it --rm --group-add=dialout --group-add=plugdev --privileged $DEVICE_ARGS --user $(id -u):$(id -g) -v $PWD:/project -v /dev/bus/usb:/dev/bus/usb esp32_build_container:v5.5.1 bash
#docker run --rm --group-add=dialout --group-add=plugdev --privileged $DEVICE_ARGS --user $(id -u):$(id -g) -v $PWD:/project -v /dev/bus/usb:/dev/bus/usb esp32_build_container:v5.5.1 idf.py $1



