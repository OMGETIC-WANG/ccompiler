int calls = 0;

struct Pair {
  int first;
};

void bump(void) {
  calls += 10;
  return;
}

int value_call(void) {
  calls += 1;
  return 7;
}

int main(void) {
  int value = 1;
  struct Pair pair = {3};

  (void)value;
  (void)pair;
  (void)value_call();
  (void)(value = 4);
  (void)bump();
  int ignored = ((void)value, 9);

  if (calls != 11) {
    return 1;
  }
  if (value != 4 || ignored != 9) {
    return 2;
  }
  return 0;
}
