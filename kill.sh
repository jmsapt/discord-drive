#!/bin/bash

sudo nbd-client -d /dev/nbd0
fuser -k 10809/tcp
