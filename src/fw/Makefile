# (Windows: msys make has HOME=/home/<user>, so the xPack is also looked for in /c/Users/<user>/opt)
# Ф0 bench firmware: doomgeneric for rv32 (xPack riscv-none-elf-gcc), run by ../sim/iss.
#   make [FB=direct|copy] [LAYOUT=sdram|sram] [ARCH=rv32im] [OPT=-O2] [MB=6: zone MiB] [ARGS="-timedemo demo1"]
# FB direct: Doom draws into the SRAM frame (fbdirect.py); copy: upstream, 64 KB memcpy per frame.
# LAYOUT sdram: program + zone in SDRAM; sram: program + zone in the 4 MB SRAM (zone must be small).
# The doom objects are built once per ARCH / OPT in build-$(ARCH)$(OPT)/; LAYOUT / ARGS only relink.
XPACK   ?= $(firstword $(wildcard $(HOME)/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin /c/Users/*/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin) $(HOME)/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin)
CROSS   := $(XPACK)/riscv-none-elf-
ifneq ($(filter MINGW% MSYS%,$(shell uname -s)),)
# msys make passes no TMP / LOCALAPPDATA / USERPROFILE to Windows programs: the gcc fell back to C:/Windows
# for its temporary files, the python of the Python install manager installed a runtime into the current dir
export TMP := $(shell cygpath -w /tmp)
export TEMP := $(TMP)
export LOCALAPPDATA := $(shell cygpath -w -F 0x1c)
export USERPROFILE := $(shell cygpath -w -F 0x28)
endif
PYTHON ?= python3
ARCH    ?= rv32im
OPT     ?= -O2
LAYOUT  ?= sdram
FB      ?= direct
MB      ?= 6
ARGS    ?= -timedemo demo1
HWARGS  ?=
NAME    := doom-$(FB)-$(LAYOUT)-$(MB)

DG      := ../../upstream/doomgeneric/doomgeneric
B       := build-$(ARCH)$(subst -,,$(OPT))
# upstream Makefile SRC_DOOM without the xlib platform and w_file_stdc (w_file_kg.c instead)
DOOM    := dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net f_finale \
           f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio \
           m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy \
           p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch \
           p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump \
           st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone i_input i_video \
           doomgeneric
OBJ     := $(addprefix $(B)/,$(addsuffix .o,$(filter-out i_video,$(DOOM)))) $(B)/w_file_kg.o $(B)/i_video_$(FB).o
ABI     := -march=$(ARCH) -mabi=ilp32
DEFS    := -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
CFLAGS  := $(ABI) $(OPT) -g -ffunction-sections -fdata-sections -w $(DEFS) -I$(DG) -I.
LDFLAGS := $(ABI) -nostartfiles --specs=nano.specs -u _printf_float -Wl,--gc-sections

ifeq ($(LAYOUT),sram)
ORG := 0x00000000
LEN := 0x3f0000
else
ORG := 0x10000000
LEN := 0x0c00000
endif

vpath %.c $(DG)

all: $(B)/$(NAME).bin

$(B):
	mkdir -p $@
$(B)/%.o: %.c | $(B)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/i_video_copy.o: $(DG)/i_video.c | $(B)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/i_video_direct.c: $(DG)/i_video.c fbdirect.py | $(B)
	$(PYTHON) fbdirect.py $< $@
$(B)/i_video_direct.o: $(B)/i_video_direct.c mmio.h
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/w_file_kg.o: w_file_kg.c mmio.h | $(B)
	$(CROSS)gcc $(CFLAGS) -I$(DG) -c $< -o $@

OPTS := $(B)/opts-$(NAME)-$(subst $(eval) ,_,$(ARGS))
$(OPTS): | $(B)
	rm -f $(B)/opts-$(NAME)-*; touch $@
$(B)/link-$(LAYOUT).ld: link.ld.in Makefile | $(B)
	sed -e 's/@ORG@/$(ORG)/' -e 's/@LEN@/$(LEN)/' -e 's/@REGION@/$(LAYOUT)/' $< > $@
$(B)/$(NAME).elf: $(OBJ) kg_main.c syscalls.c crt0.S mmio.h $(B)/link-$(LAYOUT).ld $(OPTS)
	$(CROSS)gcc $(CFLAGS) '-DMB="$(MB)"' '-DARGS="$(ARGS)"' -c kg_main.c -o $(B)/kg_main.o
	$(CROSS)gcc $(CFLAGS) -c syscalls.c -o $(B)/syscalls.o
	$(CROSS)gcc $(ABI) -c crt0.S -o $(B)/crt0.o
	$(CROSS)gcc $(LDFLAGS) -T $(B)/link-$(LAYOUT).ld -Wl,-Map,$(B)/$(NAME).map -o $@ \
	  $(B)/crt0.o $(OBJ) $(B)/kg_main.o $(B)/syscalls.o -lm
	$(CROSS)size $@
$(B)/$(NAME).bin: $(B)/$(NAME).elf
	$(CROSS)objcopy -O binary $< $@

# the board firmware (roms/doom.bin of the .kg): kg_hw.c + crt0_hw.S, LAYOUT sdram, FB direct, and
# the sound (Ф2: FEATURE_SOUND, i_kgsound.c / i_kgmusic.c, the OPL music of chocolate-doom 2.3.0 with
# opl_kg.c and compat/): its own build dir build-hw-$(ARCH)$(OPT)/
CHOC    := ../../upstream/chocolate-doom
BH      := build-hw-$(ARCH)$(subst -,,$(OPT))
HWDEFS  := $(DEFS) -DFEATURE_SOUND
HWCFLAGS := -march=$(ARCH)_zicsr -mabi=ilp32 $(OPT) -std=gnu11 -g -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -ffunction-sections -fdata-sections -w $(HWDEFS) -I. -I$(DG) -Icompat -I$(CHOC)/opl -I$(CHOC)/src
HWOBJ   := $(addprefix $(BH)/,$(addsuffix .o,$(filter-out i_video,$(DOOM)))) $(BH)/w_file_kg.o $(BH)/i_video_direct.o \
           $(BH)/opl.o $(BH)/opl_queue.o $(BH)/i_oplmusic.o $(BH)/midifile.o $(BH)/mus2mid.o $(BH)/sdl_compat.o \
           $(BH)/kg_irq.o $(BH)/opl_kg.o $(BH)/i_kgsound.o $(BH)/i_kgmusic.o $(BH)/trap.o $(BH)/kg_save.o
$(BH):
	mkdir -p $@
$(BH)/%.o: $(DG)/%.c | $(BH)
	$(CROSS)gcc $(HWCFLAGS) -c $< -o $@
$(BH)/%.o: $(CHOC)/opl/%.c | $(BH)
	$(CROSS)gcc $(HWCFLAGS) -c $< -o $@
# copies in the build dir: their quoted includes then find the doomgeneric headers first (the
# chocolate-doom ones of the same names differ), midifile.h from $(CHOC)/src
$(BH)/choc_%.c: $(CHOC)/src/%.c | $(BH)
	cp $< $@
$(BH)/i_oplmusic.o $(BH)/midifile.o: $(BH)/%.o: $(BH)/choc_%.c compat/kg_choc_sound.h compat/SDL.h
	$(CROSS)gcc $(HWCFLAGS) -include compat/kg_choc_sound.h -include compat/SDL.h -c $< -o $@
$(BH)/sdl_compat.o: compat/sdl_compat.c compat/SDL.h | $(BH)
	$(CROSS)gcc $(HWCFLAGS) -c $< -o $@
$(BH)/i_video_direct.o: $(B)/i_video_direct.c mmio.h | $(BH)
	$(CROSS)gcc $(HWCFLAGS) -c $< -o $@
$(BH)/w_file_kg.o $(BH)/kg_irq.o $(BH)/opl_kg.o $(BH)/i_kgsound.o $(BH)/i_kgmusic.o $(BH)/kg_save.o: $(BH)/%.o: %.c mmio.h kg_irq.h kg_save.h | $(BH)
	$(CROSS)gcc $(HWCFLAGS) -c $< -o $@
$(BH)/trap.o: trap.S | $(BH)
	$(CROSS)gcc -march=$(ARCH)_zicsr -mabi=ilp32 -c $< -o $@

hw: $(BH)/doom-hw.bin
.PHONY: $(BH)/doom-hw.elf
$(BH)/doom-hw.elf: $(HWOBJ) kg_hw.c font8.h syscalls.c crt0_hw.S mmio.h $(B)/link-$(LAYOUT).ld
	$(CROSS)gcc $(HWCFLAGS) '-DHWARGS="$(HWARGS)"' -c kg_hw.c -o $(BH)/kg_hw.o
	$(CROSS)gcc $(HWCFLAGS) -c syscalls.c -o $(BH)/syscalls.o
	$(CROSS)gcc $(ABI) -c crt0_hw.S -o $(BH)/crt0_hw.o
	$(CROSS)gcc $(LDFLAGS) -Wl,--wrap=realloc -T $(B)/link-$(LAYOUT).ld -Wl,-Map,$(BH)/doom-hw.map -o $@ \
	  $(BH)/crt0_hw.o $(HWOBJ) $(BH)/kg_hw.o $(BH)/syscalls.o -lm
	$(CROSS)size $@
$(BH)/doom-hw.bin: $(BH)/doom-hw.elf
	$(CROSS)objcopy -O binary $< $@

# Heretic (chocolate-doom 2.3.0 src/heretic + the common src/ files), the Ф0 bench build:
#   make htic [ARCH=..] [OPT=..] [MB=..] [ARGS=..] -> build-htic-$(ARCH)$(OPT)/heretic-$(MB).bin (LAYOUT sdram)
# The platform is htic/kg_htic.c (main, timer, video into the SRAM frame); htic/kg_htic.h, force-included,
# replaces src/doomfeatures.h (no multiplayer / dehacked / WAD merge).
HT      := build-htic-$(ARCH)$(subst -,,$(OPT))
CHOC_COMMON := d_event d_iwad d_loop d_mode i_sound i_system m_argv m_bbox m_cheat m_config m_controls \
           m_fixed m_misc memio sha1 tables v_video w_checksum w_file w_main w_wad z_zone
HERETIC := am_map ct_chat d_main d_net f_finale g_game info in_lude m_random mn_menu p_ceilng p_doors p_enemy \
           p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch \
           p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_things sb_bar sounds s_sound
HTOBJ   := $(addprefix $(HT)/,$(addsuffix .o,$(CHOC_COMMON) $(addprefix h_,$(HERETIC)))) $(HT)/w_file_kg.o
HTCFLAGS := $(ABI) $(OPT) -std=gnu11 -g -ffunction-sections -fdata-sections -w -D_DEFAULT_SOURCE \
           -include htic/kg_htic.h -Ihtic -I. -Icompat -I$(CHOC)/src
$(HT):
	mkdir -p $@
$(HT)/%.o: $(CHOC)/src/%.c htic/kg_htic.h | $(HT)
	$(CROSS)gcc $(HTCFLAGS) -c $< -o $@
$(HT)/h_%.o: $(CHOC)/src/heretic/%.c htic/kg_htic.h | $(HT)
	$(CROSS)gcc $(HTCFLAGS) -c $< -o $@
# src/i_sound.c binds the variables of the sound modules always, the table of m_config.c has them
# only with FEATURE_SOUND
$(HT)/m_config.o: HTCFLAGS += -DFEATURE_SOUND
$(HT)/w_file_kg.o: w_file_kg.c mmio.h | $(HT)
	$(CROSS)gcc $(HTCFLAGS) -c $< -o $@
HTOPTS := $(HT)/opts-$(MB)-$(subst $(eval) ,_,$(ARGS))
$(HTOPTS): | $(HT)
	rm -f $(HT)/opts-*; touch $@
htic: $(HT)/heretic-$(MB).bin
$(HT)/heretic-$(MB).elf: $(HTOBJ) htic/kg_htic.c syscalls.c crt0.S mmio.h $(B)/link-sdram.ld $(HTOPTS)
	$(CROSS)gcc $(HTCFLAGS) '-DMB="$(MB)"' '-DARGS="$(ARGS)"' -c htic/kg_htic.c -o $(HT)/kg_htic.o
	$(CROSS)gcc $(HTCFLAGS) -c syscalls.c -o $(HT)/syscalls.o
	$(CROSS)gcc $(ABI) -c crt0.S -o $(HT)/crt0.o
	$(CROSS)gcc $(LDFLAGS) -T $(B)/link-sdram.ld -Wl,-Map,$(HT)/heretic-$(MB).map -o $@ \
	  $(HT)/crt0.o $(HTOBJ) $(HT)/kg_htic.o $(HT)/syscalls.o -lm
	$(CROSS)size $@
$(HT)/heretic-$(MB).bin: $(HT)/heretic-$(MB).elf
	$(CROSS)objcopy -O binary $< $@

# Ф1: the board firmware of Heretic (roms/heretic.bin of the .kg): htic/kg_htic_hw.c + kg_htic.c (KG_HW),
# the sound (FEATURE_SOUND: i_sound.c of chocolate with i_kgsound.c / i_kgmusic.c as its SDL modules,
# i_oplmusic.c + opl_kg.c), the saves (kg_save.c, KG_HERETIC), LAYOUT sdram: build-htic-hw-$(ARCH)$(OPT)/
HH      := build-htic-hw-$(ARCH)$(subst -,,$(OPT))
HHCFLAGS := -march=$(ARCH)_zicsr -mabi=ilp32 $(OPT) -std=gnu11 -g -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -ffunction-sections -fdata-sections -w -D_DEFAULT_SOURCE \
           -DFEATURE_SOUND -DKG_HW -DKG_HERETIC -include htic/kg_htic.h -Ihtic -I. -Icompat -I$(CHOC)/src -I$(CHOC)/opl
HHOBJ   := $(addprefix $(HH)/,$(addsuffix .o,$(CHOC_COMMON) i_oplmusic midifile mus2mid opl opl_queue \
           $(addprefix h_,$(HERETIC)))) \
           $(addprefix $(HH)/,w_file_kg.o kg_irq.o opl_kg.o i_kgsound.o i_kgmusic.o kg_save.o sdl_compat.o trap.o \
           kg_htic.o)
$(HH):
	mkdir -p $@
$(HH)/%.o: $(CHOC)/src/%.c htic/kg_htic.h | $(HH)
	$(CROSS)gcc $(HHCFLAGS) -c $< -o $@
$(HH)/%.o: $(CHOC)/opl/%.c | $(HH)
	$(CROSS)gcc $(HHCFLAGS) -c $< -o $@
$(HH)/h_%.o: $(CHOC)/src/heretic/%.c htic/kg_htic.h | $(HH)
	$(CROSS)gcc $(HHCFLAGS) -c $< -o $@
$(HH)/%.o: %.c mmio.h kg_irq.h kg_save.h | $(HH)
	$(CROSS)gcc $(HHCFLAGS) -c $< -o $@
$(HH)/sdl_compat.o: compat/sdl_compat.c compat/SDL.h | $(HH)
	$(CROSS)gcc $(HHCFLAGS) -c $< -o $@
$(HH)/kg_htic.o: htic/kg_htic.c mmio.h | $(HH)
	$(CROSS)gcc $(HHCFLAGS) -c $< -o $@
$(HH)/trap.o: trap.S | $(HH)
	$(CROSS)gcc -march=$(ARCH)_zicsr -mabi=ilp32 -c $< -o $@

htic-hw: $(HH)/heretic-hw.bin
.PHONY: $(HH)/heretic-hw.elf
$(HH)/heretic-hw.elf: $(HHOBJ) htic/kg_htic_hw.c font8.h syscalls.c crt0_hw.S mmio.h $(B)/link-sdram.ld
	$(CROSS)gcc $(HHCFLAGS) '-DHWARGS="$(HWARGS)"' -c htic/kg_htic_hw.c -o $(HH)/kg_htic_hw.o
	$(CROSS)gcc $(HHCFLAGS) -c syscalls.c -o $(HH)/syscalls.o
	$(CROSS)gcc $(ABI) -c crt0_hw.S -o $(HH)/crt0_hw.o
	$(CROSS)gcc $(LDFLAGS) -Wl,--wrap=realloc -T $(B)/link-sdram.ld -Wl,-Map,$(HH)/heretic-hw.map -o $@ \
	  $(HH)/crt0_hw.o $(HHOBJ) $(HH)/kg_htic_hw.o $(HH)/syscalls.o -lm
	$(CROSS)size $@
$(HH)/heretic-hw.bin: $(HH)/heretic-hw.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -rf build-*
.PHONY: all clean hw htic htic-hw
