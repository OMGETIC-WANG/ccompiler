int main() {
  int value = 0;
again:
  value = value + 1;
  if (value < 3) {
    goto again;
  }
  return value;
}
