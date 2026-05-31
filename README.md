# USB soundcard example for the STM32f411 blackpill
This project is a proof of concept of an USB audio soundcard (sink only)
implemented minimally for the STM32f411 mcu using only CMSIS and the very
versatile and lightweight [libusb_stm32](https://github.com/dmitrystu/libusb_stm32) USB stack
by dmitrystu with minor modifications to fix USB isochronous endpoint handling
and to increase the USB RX FIFO size.

The device reports itself as a USB audio device with a sampling rate of 32 kHz
and uses timer's 2 PWM outputs on pins *A0* and *A1* for the left and right
channels respectively. When receiving audio packets, PC13 toggles to indicate
USB activity.

The also well developed [LUFA](https://github.com/abcminiuser/lufa) USB library
was used as a reference to this project as well as providing the device configuration
descriptor on its LowLevel/AudioOutput example

## Build and flash
Simply run **make** followed by **make flash**, assuming you are using an st-link with st-flash
