int choose(int value) {
  switch (value) {
  case 1:
  case 2:
    return 2;
  case 3:
    value = 4;
    break;
  default:
    return 0;
  }
}
