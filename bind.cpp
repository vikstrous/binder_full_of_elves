#include <fstream>

using namespace std;

int main(void) {
  fstream from("hello", fstream::in | fstream::binary);
  fstream to("evil", fstream::in | fstream::out | fstream::binary);
  return 0;
}
