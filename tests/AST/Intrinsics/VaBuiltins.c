int function(void) {
  va_list ap;
  int last;
  __builtin_va_start(ap, last);
  int value = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return value;
}
