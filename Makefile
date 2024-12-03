# Custom config file?  Otherwise use defaults.
-include config.mk
Q          ?= @
TOOLPREFIX ?= x86_64-jos-elf-
QEMU 	   ?= qemu-system-x86_64
QEMUSMP	   ?= 128
QEMUMEM    ?= 48G
QMP_PORT   = 9889
QEMUSRC    ?= ../mtrace
MTRACE	   ?= $(QEMU)
HW	   ?= qemu
EXCEPTIONS ?= y
RUN	   ?= $(empty)
PYTHON     ?= python2
O  	   = o.$(HW)

ifeq ($(HW),linux)
PLATFORM   := native
else
PLATFORM   := xv6
endif

ifeq ($(HW),codex)
CODEXINC = -Icodexinc
else
CODEXINC =
endif

ifdef USE_CLANG
CC  = $(TOOLPREFIX)clang
CXX = $(TOOLPREFIX)clang++
CXXFLAGS = -Wno-delete-non-virtual-dtor -Wno-gnu-designator -Wno-tautological-compare -Wno-unused-private-field -fno-pie
CFLAGS   = -no-integrated-as
ASFLAGS  =
else
CC  = /usr/local/gcc-4.8.5/bin/gcc
CXX = /usr/local/gcc-4.8.5/bin/g++
# CC  ?= $(TOOLPREFIX)gcc
# CXX ?= $(TOOLPREFIX)g++
CXXFLAGS = -Wno-delete-non-virtual-dtor -fno-pie
CFLAGS   =
ASFLAGS  = -Wa,--divide
endif

LD = ld
NM = nm
OBJCOPY = objcopy
STRIP = strip

ifeq ($(PLATFORM),xv6)
INCLUDES  = --sysroot=$(O)/sysroot \
	-iquote include -iquote$(O)/include \
	-iquote libutil/include \
	-Istdinc $(CODEXINC) -I$(QEMUSRC) \
	-include param.h -include libutil/include/compiler.h -I/root/include/
COMFLAGS  = -static -DXV6_HW=$(HW) -DXV6 \
	    -fno-builtin -fno-strict-aliasing -fno-omit-frame-pointer -fms-extensions \
	    -mno-red-zone
COMFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector) -I$(shell $(CC) -print-file-name=include)
LDFLAGS   = -m elf_x86_64
else
INCLUDES := -include param.h -iquote libutil/include -I$(QEMUSRC)
COMFLAGS := -pthread -Wno-unused-result
LDFLAGS := -pthread
# No mere mortal can call ld correctly on a real machine, so use gcc's
# link driver instead.
LD = /usr/local/gcc-4.8.5/bin/gcc
# LD = $(TOOLPREFIX)gcc
endif
COMFLAGS += -g -MD -MP -O3 -Wall -Werror -DHW_$(HW) $(INCLUDES)
CFLAGS   := $(COMFLAGS) -std=c99 $(CFLAGS)
CXXFLAGS := $(COMFLAGS) -std=c++0x -Wno-sign-compare $(CXXFLAGS)
ASFLAGS  := $(ASFLAGS) -Iinclude -I$(O)/include -m64 -gdwarf-2 -MD -MP -DHW_$(HW) -include param.h

ALL :=
all:

define SYSCALLGEN
	@echo "  GEN    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(PYTHON) tools/syscalls.py $(1) kernel/*.cc > $@.tmp
	$(Q)cmp -s $@.tmp $@ || mv $@.tmp $@
endef

ifeq ($(PLATFORM),xv6)
include net/Makefrag
include kernel/Makefrag
endif
include lib/Makefrag
include libutil/Makefrag
include bin/Makefrag
include tools/Makefrag
include metis/Makefrag
-include user/Makefrag.$(HW)

$(O)/%.o: %.c $(O)/sysroot
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(O)/%.o: %.cc $(O)/sysroot
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

$(O)/%.o: $(O)/%.cc $(O)/sysroot
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

$(O)/%.o: %.S
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

$(O)/%.o: $(O)/%.S
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

# Construct an alternate "system include root" by copying headers from
# the host that are part of C++'s freestanding implementation.  These
# headers are distributed across several directories, so we reproduce
# that directory tree here and let GCC use its standard (large)
# include path, but re-rooted at this new directory.
$(O)/sysroot: include/host_hdrs.hh
	rm -rf $@.tmp $@
	mkdir -p $@.tmp
	tar c $$($(CXX) -E -H -std=c++0x $< -o /dev/null 2>&1 \
		| awk '/^[.]/ {print $$2}') | tar xC $@.tmp
	mv $@.tmp $@

xv6memfs.img: bootblock kernelmemfs
	dd if=/dev/zero of=xv6memfs.img count=10000
	dd if=bootblock of=xv6memfs.img conv=notrunc
	dd if=kernelmemfs of=xv6memfs.img seek=1 conv=notrunc

FSEXTRA += README

$(O)/fs.img: $(O)/tools/mkfs $(FSEXTRA) $(UPROGS)
	@echo "  MKFS   $@"
	$(Q)$(O)/tools/mkfs $@ $(FSEXTRA) $(UPROGS)

.PRECIOUS: $(O)/%.o
.PHONY: clean qemu gdb rsync codex

##
## qemu
##
QEMUOPTS = -smp $(QEMUSMP) -m ${QEMUMEM} -serial mon:stdio -nographic \
	-machine q35,kernel-irqchip=split \
	-netdev user,id=ethernet.0,hostfwd=tcp::2323-:23,hostfwd=tcp::8080-:80 \
	-device e1000,netdev=ethernet.0 \
	$(if $(RUN),-append "\$$ $(RUN)",) \
	-qmp tcp:127.0.0.1:${QMP_PORT},server,nowait \
	# -numa node -numa node \

QEMUKVMFLAGS = --enable-kvm

qemu: $(KERN)
	$(QEMU) $(QEMUOPTS) $(QEMUKVMFLAGS) -kernel $(KERN)
gdb: $(KERN)
	$(QEMU) $(QEMUOPTS) $(QEMUKVMFLAGS) -kernel $(KERN) -s

codex: $(KERN)

##
## mtrace
##
mscan.syms: $(KERN)
	$(NM) -C -S $< > $@

mscan.kern: $(KERN)
	cp $< $@

MTRACEOPTS = -rtc clock=vm -mtrace-enable -mtrace-file mtrace.out \
	     -mtrace-quantum 100 -mtrace-calls
mtrace.out: mscan.kern mscan.syms
	$(Q)rm -f mtrace.out
	$(MTRACE) $(QEMUOPTS) $(MTRACEOPTS) -kernel mscan.kern -s
.PHONY: mtrace.out

mscan.out: $(QEMUSRC)/mtrace-tools/mscan mtrace.out
	$(QEMUSRC)/mtrace-tools/mscan > $@ || (rm -f $@; exit 2)

mscan.sorted: mscan.out $(QEMUSRC)/mtrace-tools/sersec-sort
	$(QEMUSRC)/mtrace-tools/sersec-sort < $< > $@

rsync: $(KERN)
	rsync -avP $(KERN) amsterdam.csail.mit.edu:/tftpboot/$(HW)/kernel.xv6

ifneq ($(HW),tom)
IPMIOPTS = -A MD5 -U ADMIN
endif
reboot-xv6: setup-xv6
	ssh amsterdam.csail.mit.edu \
	ipmitool -I lanplus $(IPMIOPTS) -H $(HW)adm.csail.mit.edu -f/home/am6/mpdev/.ipmipassword power reset

setup-xv6:
	ssh amsterdam.csail.mit.edu \
	sed -i .bak "'s/^default /#&/;/^# *default xv6/s/^# *//'" /tftpboot/$(HW)/pxelinux.cfg

reboot-linux: setup-linux
	ssh amsterdam.csail.mit.edu \
	ipmitool -I lanplus $(IPMIOPTS) -H $(HW)adm.csail.mit.edu -f/home/am6/mpdev/.ipmipassword power reset

setup-linux:
	ssh amsterdam.csail.mit.edu \
	sed -i .bak "'s/^default /#&/;/^# *default localboot/s/^# *//'" /tftpboot/$(HW)/pxelinux.cfg

bench:
	/bin/echo -ne "xv6\\nbench\\nexit\\n" | nc $(HW).csail.mit.edu 23

clean:
	rm -fr $(O)

all:	$(ALL)

