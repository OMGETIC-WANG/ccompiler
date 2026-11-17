int add(int left, int right) {
  return left + right;
}

int first(int values[]) {
  return values[0];
}

int invoke(int (*function)(int, int), int left, int right) {
  return function(left, right);
}

int first_vararg(int ignored, ...) {
  __builtin_va_list arguments;
  __builtin_va_start(arguments, ignored);
  int value = __builtin_va_arg(arguments, int);
  __builtin_va_end(arguments);
  return value;
}

int main(void) {
  int (*function_pointer)(int, int) = add;
  int values[2] = {7, 8};

  if (function_pointer(2, 3) != 5) {
    return 1;
  }
  if (invoke(function_pointer, 4, 5) != 9) {
    return 2;
  }
  if (first(values) != 7) {
    return 3;
  }
  if (first_vararg(1, 11) != 11) {
    return 4;
  }

  return 0;
}
