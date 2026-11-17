long double global = 1.0L;

long double promote(long double left, double right) {
  return left + right;
}

int main(void) {
  long double value = 1.0L;
  value += 2;
  double narrowed = (double)value;
  long double widened = narrowed;
  return (int)value;
}
