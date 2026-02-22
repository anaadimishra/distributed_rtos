# Build ESP8266 firmware
docker build --no-cache --platform linux/amd64 -f Dockerfile.esp8266 -t esp8266-sdk .

docker run --rm -it \
  --platform linux/amd64 \
  -v $PWD:/project \
  -w /project/firmware \
  esp8266-sdk \
  bash

cd /opt/ESP8266_RTOS_SDK
source export.sh
cd /project/firmware
make clean
make


# Flashing
python3 ESP8266_RTOS_SDK/components/esptool_py/esptool/esptool.py \
  --chip esp8266 \
  --port /dev/tty.SLAB_USBtoUART \
  --baud 115200 \
  write_flash -z \
  0x0 firmware/build/bootloader/bootloader.bin \
  0x10000 firmware/build/distributed_rtos.bin \
  0x8000 firmware/build/partitions_singleapp.bin


#Start mqtt server manually
mosquitto -p 1883 -v

conda activate rtos-dash
python app.py


# Later

make docker-build
make docker-shell
make flash
make dashboard

