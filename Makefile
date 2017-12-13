obj-m += chardevice.o

insmod:
	insmod chardevice.ko

rmmod:
	rmmod chardevice

local-clean:
	rm -rf chardevice.mod.c chardevice.o modules.order chardevice.ko chardevice.mod.o Module.symvers
