int factorial(int value) {
  if (value == 0) {
    return 1;
  }
  return value * factorial(value - 1);
}

int main(void) {
  return factorial(5);
}
