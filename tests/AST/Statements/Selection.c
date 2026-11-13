int select(int value) {
  if (value) {
    return 1;
  } else if (value > 1) {
    return 2;
  } else {
    value = 3;
  }
  return value;
}
