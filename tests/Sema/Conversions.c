int *global_ptr = 0;

int main(void) {
  char narrow = 1;
  narrow += 2.5;

  int value = 2;
  if (value && 2) {
    value = !0;
  }

  switch (narrow) {
  case 257:
    return value < 3;
  }
  return global_ptr ? value : 0;
}
