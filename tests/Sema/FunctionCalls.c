int add(int left, int right) {
  return left + right;
}

int first(int values[]) {
  return values[0];
}

int variadic(int count, ...) {
  return count;
}

int main(void) {
  int (*function_pointer)(int, int) = add;
  int values[2] = {1, 2};
  return function_pointer(1, 2) + first(values) + variadic(3, 4);
}
