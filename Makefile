CC=clang
CXX=clang++
TEST_STUB=stub_exit_43
TEST_METHOD=2

all: bind

stub: stub.asm
	yasm -f bin -o stub stub.asm

stub_act_normal: stub_act_normal.asm
	yasm -f bin -o stub_act_normal stub_act_normal.asm

stubs: stub stub_act_normal

bind: bind.cpp
	$(CXX) bind.cpp -o bind -g

test: bind
	$(CC) hello.c -o hello
	./bind $(TEST_STUB) hello $(TEST_METHOD) > hello_bound
	chmod +x hello_bound
	./hello_bound

funcksections: fucksections.cpp
	$(CXX) -o fucksections fucksections.cpp

test1: bind
	$(CC) hello.c -o hello
	readelf hello -a

test2: bind
	./bind $(TEST_STUB) hello $(TEST_METHOD) > hello_bound
	readelf hello_bound -a
