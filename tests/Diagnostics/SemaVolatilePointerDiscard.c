int main(void) {
  volatile int *source;
  int *target = source;
  return target != source;
}
