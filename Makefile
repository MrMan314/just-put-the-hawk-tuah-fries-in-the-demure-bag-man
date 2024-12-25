CFLAGS=-I include/ -o $@

proxy: main.c include/*.c
	gcc $^ ${CFLAGS}
proxy.exe: main.c include/*.c
	x86_64-w64-mingw32-gcc $^ -lws2_32 -static ${CFLAGS}
clean:
	rm -f proxy *.o *.exe
run: proxy
	./proxy
windows_run: proxy.exe
	./proxy.exe
