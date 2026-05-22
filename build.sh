#!/bin/bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..
rsync -a --delete static/ build/static/
