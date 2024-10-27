proxy: main.c
	gcc main.c -o proxy -g
proxy.exe: main.c
	x86_64-w64-mingw32-gcc main.c -lws2_32 -static -o proxy.exe
clean:
	rm -f proxy *.o *.exe
run: proxy
	./proxy
windows_run: proxy.exe
	./proxy.exe
