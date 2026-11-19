int main(void) {
  int value = 0;

  switch (1) {
  case 1:
    value += 1;
  case 2:
    value += 2;
  case 3:
    value += 4;
    break;
  case 4:
    value = 100;
  }
  if (value != 7) {
    return 1;
  }

  switch (2) {
  case 2:
    value += 8;
    break;
  case 3:
    value += 16;
  }
  if (value != 15) {
    return 2;
  }

  return 0;
}
