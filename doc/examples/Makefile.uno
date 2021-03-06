#Project Settings
# Configuration Settings
DEFINES = -DSERIAL_DEBUG -DSER_DEVICE_TYPE=SLAVE -DDEVICE_ID=1 -DMAX485_PIN=2
# available macro variables:
# - BIG_ENDIAN

#Generic settings!
# The arduino directories
ARDUINO_DIR=/usr/share/arduino
ARDUINO_LIB_PATH  = $(ARDUINO_DIR)/libraries
ARDUINO_CORE_PATH = $(ARDUINO_DIR)/hardware/arduino/cores/arduino

# The chip type:
# atmega328p > Uno
# atmega2560 > Mega
# atmega168 > Pro
MCU=atmega328p

# Project name
TARGET=OpenCRTLClient

# Used libraries. BUG: Now only one is allowed (more will make compile errors)
ARDUINO_LIBS=NewSoftSerial

# Arduino CPU Speed
F_CPU=16000000

# Port where we can find the arduino (Normaly ttyACM* or ttyUSB*)
ARDUINO_PORT=/dev/ttyACM1

#Flash Settings
# What programmer to use:
# - mega, uno = stk500v2
AVRDUDE_ARD_PROGRAMMER = arduino

# This can be a bitch.. newer devices have faster baudrate
# DON'T SET ANY BITRATE FOR THE NEW MEGA'S OR UNO'S
# - pro mini = 57600
# more to follow
#AVRDUDE_ARD_BAUDRATE   = 115200

OBJDIR = bin

# The actual make file
include Arduino.mk
