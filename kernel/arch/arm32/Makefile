obj-y	+= $(MACH)/
obj-y	+= lib/
obj-y	+= $(patsubst $(srctree)/$(src)/%.c, %.o, $(wildcard $(srctree)/$(src)/*.c)) \
			$(patsubst $(srctree)/$(src)/%.S, %.o, $(wildcard $(srctree)/$(src)/*.S))