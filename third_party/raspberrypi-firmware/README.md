# Raspberry Pi Firmware

This directory vendors the Raspberry Pi boot and WLAN firmware files that BMX
stages onto the FAT boot partition for Pi 3, Pi 4 and Pi 5 builds.

Boot firmware source repository: https://github.com/raspberrypi/firmware

Boot firmware source commit: `dd28ccd67f16e60f5739dc779060be218b28eb1e`

WLAN firmware source repository: https://github.com/RPi-Distro/firmware-nonfree

WLAN firmware source commit: `c9d3ae6584ab79d19a4f94ccf701e888f9f87a53`

Pi 3 staging (`boot/pi3`: `bootcode.bin`, `start.elf`, `fixup.dat`,
`bcm2710-rpi-3-b.dtb`, `bcm2710-rpi-3-b-plus.dtb`, `bcm2710-rpi-cm3.dtb`,
`bcm2710-rpi-zero-2.dtb`) uses the firmware default ARM stub; no Circle
armstub is staged. VideoCore IV path: firmware dispmanx + GLES2 + crt-pi.

Pi 4 AArch32 fallback staging uses `armstub7-rpi4.bin`; that file comes from
the Circle armstub build output in `third_party/circle-stdlib-pi4/libs/circle/boot`.
The default AArch64 stage omits it and uses the separately built
`armstub8-rpi4.bin` instead.
