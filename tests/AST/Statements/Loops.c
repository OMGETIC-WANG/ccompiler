void loops(void) {
  int a = 0;
  while (a < 2) {
    a++;
  }
  do {
    a--;
  } while (a);
  for (a = 0; a < 3; a++) {
    continue;
  }
  for (;;) {
    break;
  }
  for (int index = 0; index < 2; index++) {
  }
}
