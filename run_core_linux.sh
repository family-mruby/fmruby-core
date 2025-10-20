#!/bin/bash

docker run --rm --user $(id -u):$(id -g) \
    -v $PWD:/project \
    -v /tmp:/tmp \
    esp32_build_container:v5.5.1 \
    /project/build/fmruby-core.elf
