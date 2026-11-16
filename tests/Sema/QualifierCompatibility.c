int function(const volatile int value);

int function(int value) {
  volatile int local = 1;
  local = local + value;
  return local;
}
