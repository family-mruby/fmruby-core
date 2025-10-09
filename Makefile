# Family mruby ESP-IDF build wrapper
DOCKER_CMD = docker run --rm --user $(shell id -u):$(shell id -g) -v $(PWD):/project esp32_build_container:v5.5.1
DOCKER_CMD_PRIVILEGED = docker run --rm --group-add=dialout --group-add=plugdev --privileged $(DEVICE_ARGS) --user $(shell id -u):$(shell id -g) -v $(PWD):/project -v /dev/bus/usb:/dev/bus/usb esp32_build_container:v5.5.1

# Linuxターゲットビルド（開発・テスト用）
linux-build:
	$(DOCKER_CMD) idf.py --preview set-target linux
	$(DOCKER_CMD) idf.py --preview build
	@echo "Linux build complete. Run with: ./build/fmruby-graphics-audio.elf"

# ESP32ターゲットビルド
esp32-build:
	$(DOCKER_CMD) idf.py set-target esp32s3
	$(DOCKER_CMD) idf.py build

# ESP32フラッシュ
flash:
	$(DOCKER_CMD_PRIVILEGED) idf.py flash

# 設定メニュー
menuconfig:
	$(DOCKER_CMD) idf.py menuconfig

# クリーンビルド
clean:
	$(DOCKER_CMD) idf.py fullclean

# モニター
monitor:
	$(DOCKER_CMD_PRIVILEGED) idf.py monitor

# Linux版実行
run-linux: linux-build
	./build/fmruby-graphics-audio.elf

.PHONY: linux-build esp32-build flash menuconfig clean monitor run-linux