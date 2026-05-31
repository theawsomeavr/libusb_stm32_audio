CC        := arm-none-eabi-gcc
OBJSIZE   := arm-none-eabi-size
OBJCOPY   := arm-none-eabi-objcopy

OUT_ELF   := main.elf
OUT_BIN   := main.bin
OBJ_DIR   := obj

OPT       := -O2 -g

CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

INCLUDES  := -Ideps/system \
             -Ideps/CMSIS  \
             -Ilibusb_stm32/inc

CFLAGS    := $(OPT) $(CPU_FLAGS) -Werror=unused-result -DSTM32F411xE \
             -std=c11 $(INCLUDES)
ASFLAGS   := -x assembler-with-cpp $(INCLUDES)
LDFLAGS   := $(CPU_FLAGS) \
             -specs=nano.specs -specs=nosys.specs \
             -Tdeps/system/STM32F411CEUx_FLASH.ld \
             -Wl,--gc-sections

C_SRCS    := libusb_stm32/src/usbd_core.c            \
             libusb_stm32/src/usbd_stm32f429_otgfs.c  \
             ./src/main.c

ASM_SRCS  := deps/system/startup_stm32f411xe.s

C_OBJS    := $(patsubst %,$(OBJ_DIR)/%.o,$(basename $(notdir $(C_SRCS))))
ASM_OBJS  := $(patsubst %,$(OBJ_DIR)/%.o,$(basename $(notdir $(ASM_SRCS))))
OBJS      := $(C_OBJS) $(ASM_OBJS)

VPATH     := libusb_stm32/src ./src deps/system

all: $(OUT_BIN)

$(OUT_ELF): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(OBJSIZE) $(OUT_ELF)

$(OUT_BIN): $(OUT_ELF)
	$(OBJCOPY) -O binary -S $(OUT_ELF) $(OUT_BIN)

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(OBJ_DIR)/%.o: %.s | $(OBJ_DIR)
	$(CC) $(ASFLAGS) -o $@ -c $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(OUT_ELF) $(OUT_BIN)

flash: $(OUT_BIN)
	st-flash --reset write $(OUT_BIN) 0x08000000

gdb:
	arm-none-eabi-gdb -nh -nx -ex 'target remote :3333' $(OUT_ELF)

probe:
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

.PHONY: all clean
