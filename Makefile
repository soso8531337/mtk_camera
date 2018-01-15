CROSS=mipsel-openwrt-linux-
CC=mipsel-openwrt-linux-gcc
STRIP=$(CROSS)strip
TARGET=mtkCamera
USBLIB=/home/zhangwei/exercise/linux_c/vstfun/aoa/nxp/libusb
#CFLAGS= -fPIC -g -Wall -Iinclude -D_FILE_OFFSET_BITS=64 -I$(USBLIB)/include -L$(USBLIB)/lib 
#CFLAGS= -fPIC -g -Wall -O2 -Iinclude -D_FILE_OFFSET_BITS=64 -I$(USBLIB)/include -L$(USBLIB)/lib 
CFLAGS= -fPIC -O2 -Iinclude -DDEBUG_ENABLE1 -D_FILE_OFFSET_BITS=64 -I$(USBLIB)/include -L$(USBLIB)/lib -I./jpeg/usr/include/ -L./jpeg/usr/lib/ 
ALL: clean $(TARGET)
obj:=$(patsubst %.c, %.o, $(wildcard *.c))
mtkCamera: $(obj)
	$(CC) -o $(TARGET)  $(obj) $(LDFLAGS) -lusb-1.0 -lpthread -ljpeg $(CFLAGS) 
%.o:%.c
	$(CC) -c -o $@ $^ $(CFLAGS) 
clean: 
	rm -f *.o mtkCamera 
