proxy: main.c
	gcc main.c -o proxy
clean:
	rm -f proxy *.o
run: proxy
	./proxy