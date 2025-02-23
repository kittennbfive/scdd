# scdd (scope data dumper)

## What is this?
This is a small and simple Linux only tool for retrieving (and converting) raw channel data from Rigol MSO5000 series (and possibly other) oscilloscopes connected via USB. Output is either formated (ASCII) or raw  float.

## Licence and disclaimer
This tool is licenced under AGPLv3+ and comes WITHOUT ANY WARRANTY! Use at your own risk.

## Why?
As for my own [scsd](https://github.com/kittennbfive/scsd) (scope screen dumper) i wanted/needed a way to access the raw channel data from the scope from Linux and without closed source (and often bloated) software. So i made this tool.

## How to compile
As simple as `gcc -Wall -Wextra -Werror -o scdd main.c`. No dependencies except `libc`.

## How to use
Set up your scope, capture the data and set the scope to STOP mode (mandatory, checked by code). Then run `scdd [optional arguments]` to retrieve data.
### Arguments
#### --device
Usually this will be `/dev/usbtmc0` (used by default) unless you have several USBTMC capable devices connected to your computer.
#### --channel
Channel to retrieve data from, 1-4, by default CH1 is used.
#### --filename
Filename for output, if not provided a sane default will be used. Specify `PIPE` to use stdout (for piping into another tool).
#### --raw-float
Spit out raw float (using `fwrite()`) instead of formatted values (ASCII).

## Limitations?
It's slow... The Linux USBTMC driver has a hardcoded buffer size of 4096 bytes which can't be changed (except by recompiling the driver of course). I considered bypassing the USBTMC driver by using libusb, but i vaguely recall (from previous experiments for sc**s**d) that this does not work better/reliably. I even had the scope totally freeze once (power-cycling required)...  
  
Using `$ time ./scdd --filename PIPE --raw-float  > /dev/null` and on my system i get about 48s for 10Mpts; 2m02 for 25Mpts and 4m04 for 50Mpts. YMMV.
