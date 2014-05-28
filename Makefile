CC=gcc
CXX=g++
TEST_STUB=stubs/stub_exit_43
TEST_METHOD=2

all: bind

# build the binder

bind: bind.cpp
	$(CXX) bind.cpp -o bind -g

clean:
	rm -f bind test/remove_sections test/hello test/hello_bound

# build the stubs

stub_exit_43: stub_exit_43.asm
	yasm -f bin -o stub_exit_43 stub_exit_43.asm

stub_act_normal: stub_act_normal.asm
	yasm -f bin -o stub_act_normal stub_act_normal.asm

stubs: stub_exit_43 stub_act_normal

# used for testing

remove_sections: test/remove_sections.cpp
	$(CXX) -o test/remove_sections test/remove_sections.cpp

test: bind
	$(CC) test/hello.c -o test/hello
	./bind $(TEST_STUB) test/hello 1 > test/hello_bound
	chmod +x test/hello_bound
	./test/hello_bound

testt: bind
	$(CC) test/hello.c -o test/hello
	./bind $(TEST_STUB) test/hello 2 > test/hello_bound
	chmod +x test/hello_bound
	./test/hello_bound

testtt: bind
	$(CC) test/hello.c -o test/hello
	./bind $(TEST_STUB) test/hello 3 > test/hello_bound
	chmod +x test/hello_bound
	./test/hello_bound

test1: bind
	$(CC) test/hello.c -o test/hello
	readelf test/hello -a

test2: bind
	./bind $(TEST_STUB) test/hello $(TEST_METHOD) > test/hello_bound
	readelf test/hello_bound -a
