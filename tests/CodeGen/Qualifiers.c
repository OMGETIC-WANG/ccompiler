volatile int counter;

int sum(int *restrict first, int *restrict second) {
  return *first + *second;
}

int main(void) {
  extern int putchar(int);
  int first = 2;
  int second = 3;
  counter = sum(&first, &second);
  if (counter != 5) {
    return 1;
  }
  putchar(65);
  return counter - 5;
}
