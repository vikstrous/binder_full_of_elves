all:
	g++ bind.cpp -o bind

test:
	gcc evil.c -o evil
	gcc hello.c -o hello
	./bind
