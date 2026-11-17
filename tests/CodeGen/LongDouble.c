struct Wide {
  long double value;
};

long double global_wide = 2.0L;

static long double add_wide(long double left, long double right) {
  return left + right;
}

static long double first_vararg(int count, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, count);
  long double value = __builtin_va_arg(args, long double);
  __builtin_va_end(args);
  return value;
}

int main(void) {
  struct Wide record = {1.5L};
  long double array[2] = {record.value, global_wide};
  long double value = add_wide(array[0], array[1]);
  value += 0.5L;

  double narrowed = (double)value;
  float narrowed_float = (float)value;
  long double widened = narrowed;
  long double widened_float = narrowed_float;
  int signed_value = (int)value;
  unsigned int unsigned_value = (unsigned int)value;
  long double selected = value > 3.0L ? value : 0.0L;
  long double negative = -selected;
  ++value;
  --value;

  if (!(value == 4.0L)) {
    return 1;
  }
  if (!(widened == 4.0L) || !(widened_float == 4.0L)) {
    return 2;
  }
  if (signed_value != 4 || unsigned_value != 4) {
    return 3;
  }
  if (!(first_vararg(1, 5.0L) == 5.0L)) {
    return 4;
  }
  if (sizeof(long double) != 16) {
    return 5;
  }
  if (!selected || !(negative < 0.0L)) {
    return 6;
  }
  return 0;
}
