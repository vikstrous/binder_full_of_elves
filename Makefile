all: bind

stub: stub.asm
	yasm -f bin -o stub stub.asm

bind: bind.cpp
	g++ bind.cpp -o bind -g

test: bind
	gcc hello.c -o hello
	./bind stub hello > hello_bound
	chmod +x hello_bound
	./hello_bound

funcksections: fucksections.cpp
	g++ -o fucksections fucksections.cpp

test1: bind
	gcc hello.c -o hello
	readelf hello -a

test2: bind
	./bind stub hello > hello_bound
	readelf hello_bound -a
