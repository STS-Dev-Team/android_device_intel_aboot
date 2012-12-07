CFLAGS = -m32 -shared -I../prebuild/include -I../prebuild/include/directfb

OBJS = textedit.c

all: libtextedit.so

libtextedit.so: $(OBJS)
	gcc $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -rf *.so
