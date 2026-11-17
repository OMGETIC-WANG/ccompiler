struct Pair {
  char first;
  long second;
};

union Value {
  int integer;
  double real;
};

int main(void) {
  struct Pair positional = {1, 2};
  struct Pair designated = {.first = 3, .second = 4};
  struct Pair *pointer = &designated;
  union Value value;
  value.integer = 5;
  return positional.first + pointer->second + value.integer;
}
