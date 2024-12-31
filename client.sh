#!/bin/bash 


sudo nbd-client -d /dev/nbd0
sudo nbd-client 127.0.0.1 10809 -b 4096 /dev/nbd0
