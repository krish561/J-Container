#!/bin/bash
# setup.sh - Downloads Alpine Linux for J-Container

if [ -d "./rootfs" ]; then
    echo "RootFS already exists."
    exit 0
fi

echo "Downloading Alpine Linux RootFS..."
mkdir -p rootfs
cd rootfs
wget https://dl-cdn.alpinelinux.org/alpine/v3.18/releases/x86_64/alpine-minirootfs-3.18.4-x86_64.tar.gz
tar -xvf alpine-minirootfs-3.18.4-x86_64.tar.gz
rm alpine-minirootfs-3.18.4-x86_64.tar.gz
cd ..
echo "RootFS ready!"
