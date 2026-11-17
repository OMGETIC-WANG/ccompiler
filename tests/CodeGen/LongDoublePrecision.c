long double precise_global = 1.0000000000000000001L;
long double folded_global = 1.0L + 0x1p-63L;

int main(void) {
  unsigned long long integer = 9007199254740993ULL;
  long double from_integer = integer;
  long double precise = 1.0000000000000000001L;
  long double folded = 1.0L + 0x1p-63L;
  double narrowed = (double)folded;

  if (!(precise > 1.0L) || !(precise_global > 1.0L)) {
    return 1;
  }
  if (!(folded > 1.0L) || !(folded_global > 1.0L)) {
    return 2;
  }
  if (narrowed != 1.0) {
    return 3;
  }
  if (!(from_integer > 9007199254740992ULL)) {
    return 4;
  }
  if (folded - 1.0L != 0x1p-63L) {
    return 5;
  }
  return 0;
}
