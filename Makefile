TOP           = $(shell pwd)
RECOVERY_SRC  = $(TOP)
ifeq ("$(origin O)", "command line")
  PRODUCT_OUT = $(shell cd $(O) && pwd)
else
  PRODUCT_OUT = $(TOP)/out
endif
RECOVERY_OUT  = $(PRODUCT_OUT)/recovery

CROSS_COMPILE =
CC            = ${CROSS_COMPILE}gcc
CXX           = ${CROSS_COMPILE}g++
LD            = ${CROSS_COMPILE}gcc
PREBUILD      = $(TOP)/prebuild
PREFIX        = $(PREBUILD)

INCLUDE  = -I. -I$(PREFIX)/include           \
           -I$(PREFIX)/include/directfb      \
           -I$(RECOVERY_SRC)/pos/aboot           \
           -I$(RECOVERY_SRC)/pos           \
           -I$(RECOVERY_SRC)/cos           \
           -I$(RECOVERY_SRC)/ui_ext
CFLAGS   = -L$(PREFIX)/lib                   \
           -L$(RECOVERY_SRC)/ui_ext           \
           -llite -lleck -ltextedit -m32

SOURCES  = $(RECOVERY_SRC)/recovery.c        \
           $(RECOVERY_SRC)/cos/ui.c             \
           $(RECOVERY_SRC)/cos/main.c             \
           $(RECOVERY_SRC)/cos/event.c             \
           $(RECOVERY_SRC)/pos/ui.c             \
           $(RECOVERY_SRC)/pos/main.c             \
           $(RECOVERY_SRC)/pos/event.c      \
           $(RECOVERY_SRC)/pos/aboot/aboot.c     \
           $(RECOVERY_SRC)/pos/aboot/fastboot.c  \
           $(RECOVERY_SRC)/pos/ota.c
OBJECTS  = $(SOURCES: .c=.o)
TARGET   = $(RECOVERY_OUT)/bin/recovery

LD_LIBRARY_PATH:=$(LD_LIBRARY_PATH):$(PREFIX)/lib
export LD_RUN_PATH:=$(LD_LIBRARY_PATH)

all: libtextedit.so $(TARGET)
	@mkdir -p $(RECOVERY_OUT)/etc
	@cp -a $(PREFIX)/bin/aboot $(RECOVERY_OUT)/bin/
	@cp $(PREFIX)/etc/directfbrc.$(CUSTOM_BOARD) $(RECOVERY_OUT)/etc/directfbrc
	@cp -a $(PREFIX)/lib $(PREFIX)/share $(RECOVERY_OUT)/
	@mv $(TOP)/ui_ext/*.so $(RECOVERY_OUT)/lib/
#	@cp -a $(RECOVERY_SRC)/data /recovery/share/recovery
	@cp -a $(RECOVERY_SRC)/data $(RECOVERY_OUT)/share/recovery
	@find $(RECOVERY_OUT) -name  *.a -exec rm -rf {} \;
	@find $(RECOVERY_OUT) -name *.la -exec rm -rf {} \;
	@rm -rf $(RECOVERY_OUT)/share/man     \
		$(RECOVERY_OUT)/lib/pkgconfig \
		$(RECOVERY_OUT)/share/aclocal
	@cd $(RECOVERY_OUT); $(PREFIX)/bin/busybox tar -zcvf $(PRODUCT_OUT)/recovery.tar.gz * > /dev/null

$(TARGET): $(OBJECTS)
	@rm -rf $(RECOVERY_OUT)
	@mkdir -p $(RECOVERY_OUT)/bin
	$(LD) $(INCLUDE) $(CFLAGS) -Wall -o $@ $^

%.o: %.c
	$(CC) $(INCLUDE) $(CFLAGS) -Wall -o $@ -c $<

libtextedit.so:
	@cd $(RECOVERY_SRC)/ui_ext && $(MAKE)
	@cd $(RECOVERY_SRC)

clean:
	rm -rf $(PRODUCT_OUT) ui_ext/*.so
