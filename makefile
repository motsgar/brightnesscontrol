all: brightness.c
	gcc -o brightness brightness.c -lm

install: all
	cp brightness /bin/backlightBrightness