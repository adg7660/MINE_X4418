obj-y	+= $(MACH)/
obj-y	+= lib/
obj-y	+= $(patsubst $(srctree)/$(src)/%.c, %.o, $(wildcard $(srctree)/$(src)/*.c)) \
			$(patsubst $(srctree)/$(src)/%.S, %.o, $(wildcard $(srctree)/$(src)/*.S))
obj-y	+= entry.o
obj-y	+= context_switch.o
obj-y	+= backtrace.o