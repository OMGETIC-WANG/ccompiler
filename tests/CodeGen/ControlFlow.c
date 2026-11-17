int main(void) {
  int sum = 0;
  int index = 0;

  while (index < 4) {
    index++;
    if (index == 2) {
      continue;
    }
    sum += index;
  }
  if (sum != 8) {
    return 1;
  }

  do {
    sum++;
  } while (sum < 10);
  if (sum != 10) {
    return 2;
  }

  for (int local = 0; local < 4; local++) {
    if (local == 2) {
      continue;
    }
    if (local == 3) {
      break;
    }
    sum += local;
  }
  if (sum != 11) {
    return 3;
  }

  for (;;) {
    sum += 2;
    break;
  }
  if (sum != 13) {
    return 4;
  }

  int selected = 0;
  switch (2) {
  case 1:
    selected = 1;
    break;
  case 2:
  case 3:
    selected = 5;
    break;
  default:
    selected = 9;
    break;
  }
  if (selected != 5) {
    return 5;
  }

  goto target;
  sum = 99;
target:
  if (sum != 13) {
    return 6;
  }

  int nested = 0;
  for (index = 0; index < 2; index++) {
    int inner = 0;
    while (inner < 2) {
      nested++;
      inner++;
    }
  }
  if (nested != 4) {
    return 7;
  }

  return 0;
}
