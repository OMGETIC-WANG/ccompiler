int *global_ptr = 0;

int as_int(double value) { return (int)value; }

int main(void) {
  char narrow = 1;
  narrow += 2.5;

  int value = 2;
  if ((value && 2) && !2) {
    return 91;
  }
  if (global_ptr) {
    return 92;
  }

  value += 0.0;
  switch (narrow) {
  case 3:
    value += 1;
    break;
  default:
    return 93;
  }

  return value + (narrow < 4 ? 1 : 0) + as_int(2.9);
}
