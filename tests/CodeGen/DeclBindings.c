int global = 10;

int pick(int value) {
  {
    int value = 20;
    return value;
  }
}

int main(void) {
  int value = 30;
  return pick(value) + value + global;
}
