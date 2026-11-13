static int counter;

static int helper(void) {
  static int local;
  const int value = 2;
  return value;
}

const int * const pointer;
