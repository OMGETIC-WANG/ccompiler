int calls = 0;

void bump(void) { calls += 1; }

int main(void) {
  int value = 1;
  (void)value;
  (void)bump();
  (void)(value = 4);
  return value;
}
