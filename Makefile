all: bind

stub: stub.asm
	yasm -f bin -o stub stub.asm

bind: bind.cpp
	g++ bind.cpp -o bind -g

test: bind
	gcc evil.c -o evil
	gcc hello.c -o hello
	./bind
	./hello

funcksections: fucksections.cpp
	g++ -o fucksections fucksections.cpp

test1: bind
	gcc hello.c -o hello
	readelf hello -a

test2: bind
	./bind
	readelf hello -a
