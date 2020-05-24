#!smake -k
# --------------------------------------------------------------------
#        Copyright (C) 1997,1998 Nintendo. (Originated by SGI)
#
#        $RCSfile: Makefile,v $
#        $Revision: 1.9 $
#        $Date: 1999/04/02 10:10:22 $
# --------------------------------------------------------------------
#
# Makefile for game template
#
#

include $(ROOT)/usr/include/make/PRdefs

FINAL = YES
ifeq ($(FINAL), YES)
OPTIMIZER       = -O2 -std=gnu90 -mno-shared -mhard-float -fno-stack-protector -fno-common -fno-PIC -mno-abicalls -fno-strict-aliasing -fno-inline-functions -ffreestanding -fwrapv -Werror
LCDEFS          = -D_FINALROM -DNDEBUG -DF3DEX_GBI_2
ASFLAGS         = -mabi=32
N64LIB          = -lultra_rom
else
OPTIMIZER       = -g -std=gnu90 -mno-shared -mhard-float -fno-stack-protector -fno-common -fno-PIC -mno-abicalls -fno-strict-aliasing -fno-inline-functions -ffreestanding -fwrapv -Werror
LCDEFS          = -DDEBUG -DF3DEX_GBI_2
ASFLAGS         = -mabi=32
N64LIB          = -lultra_d
endif

APP =		bin/gb.out

TARGETS =	bin/gb.n64

TEXHFILES =	RGBA16foliageMM.h

HFILES =	$(TEXHFILES) boot.h game.h controller.h font.h font_ext.h \
		letters_img.h static.h \
		src/test/z80_test.h    \
		src/z80.h              \
		src/memory_map.h

CODEFILES   =	boot.c game.c controller.c font.c dram_stack.c \
       src/test/z80_test.c                   \
       src/test/z80_tests_0.c                \
       src/test/z80_tests_1.c                \
       src/test/z80_tests_2.c                \
       src/test/z80_tests_3.c                \
       src/test/z80_tests_4_7.c              \
       src/test/z80_tests_8_9.c              \
       src/test/z80_tests_A_B.c              \
       src/test/z80_tests_C.c                \
       src/test/z80_tests_D.c                \
       src/test/z80_tests_E.c                \
       src/test/z80_tests_F.c                \
       src/test/z80_tests_prefix_cb.c        \
       src/test/register_test.c              \
       src/test/interrupt_test.c             \
       src/z80.c                             \
       src/rom.c                             \
       src/memory_map.c                      \
       src/gameboy.c                         \
       src/graphics.c                        \
       memory.c

S_FILES = asm/cpu.s

CODEOBJECTS =	$(CODEFILES:.c=.o) $(S_FILES:.s=.o)

DATAFILES   =	gfxinit.c \
		rsp_cfb.c \
		texture.c \
		cfb.c \
		zbuffer.c

DATAOBJECTS =	$(DATAFILES:.c=.o)

CODESEGMENT =	codesegment.o

OBJECTS =	$(CODESEGMENT) $(DATAOBJECTS)

LCDEFS +=	$(HW_FLAGS)
LCINCS =	-I. -I/usr/include/n64/PR -I/usr/include/n64
LCOPTS =	-G 0
LDFLAGS =	$(MKDEPOPT)  -L/usr/lib/n64 $(N64LIB) -lkmc

LDIRT  =	$(APP)

default:	$(TARGETS)

include $(COMMONRULES)

$(CODESEGMENT):	$(CODEOBJECTS)
		$(LD) -o $(CODESEGMENT) -r $(CODEOBJECTS) $(LDFLAGS)

ifeq ($(FINAL), YES)
$(TARGETS) $(APP):      spec $(OBJECTS)
	$(MAKEROM) -s 9 -r $(TARGETS) -e $(APP) spec
	makemask $(TARGETS)
else
$(TARGETS) $(APP):      spec $(OBJECTS)
	$(MAKEROM) -r $(TARGETS) -e $(APP) spec
endif

font.o:		./letters_img.h

# for exeGCC CELF
ifeq ($(GCC_CELF), ON)
ifneq ($(FINAL), YES)
CELFDIR = .
include $(CELFRULES)
$(CODEOBJECTS) $(DATAOBJECTS) :	$(CELFINFOFILES)
endif
endif
