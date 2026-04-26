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

---

# ESP32 delegation validation (current path)

Build and flash:

```bash
./run-lab.sh build
./run-lab.sh flash
```

Run four-node delegation with serial crash capture:

```bash
./run-lab.sh delegation --serial-monitor \
  --label multi-peer-run10 \
  --hold-seconds 60 \
  --low-load 200 \
  --victim node-34A9F0 \
  --mqtt-verbose \
  --min-nodes 4 \
  --nodes-timeout 180 \
  --nodes-settle-seconds 1
```

Serial logs are written to:

```text
serial_logs/session_YYYYMMDD-HHMMSS/
```

Look for hard failures with:

```bash
rg -n "stack overflow|Backtrace|panic|Guru|abort|rst:|Rebooting|watchdog" serial_logs/session_* -a -S
```

Expected after the latest stack-budget fix:

- no stack overflow or reboot
- victim reaches `ACTIVE`
- `deleg_dispatched` and `deleg_returned` increase
- `deleg_inflight_total` stays bounded by active channels times 4
- `deleg_dispatch_err` stays 0
