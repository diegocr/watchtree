CC	=	m68k-amigaos-gcc
AMIGA_CFLAGS	=	-m68060 -m68881 -msmall-code -fbaserel
CFLAGS	=	$(AMIGA_CFLAGS) -O3 -I. -W -Wall -Winline -DBUILD_SYSTEM=\"`uname -s`\"
LIBS	=	-Wl,-Map,$@.map,--cref

watchtree: watchtree.c fnmatch.c
	$(CC) $(CFLAGS) -s -o $@ $? $(LIBS)
