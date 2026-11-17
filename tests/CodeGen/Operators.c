int main(void) {
  int left = 10;
  int right = 3;
  int value = 0;

  if (left + right != 13) {
    return 1;
  }
  if (left - right != 7) {
    return 2;
  }
  if (left * right != 30) {
    return 3;
  }
  if (left / right != 3 || left % right != 1) {
    return 4;
  }
  if (!(left == 10) || !(left != right) || !(right < left) ||
      !(left > right) || !(right <= left) || !(left >= right)) {
    return 5;
  }
  if ((6 & 3) != 2 || (4 | 1) != 5 || (6 ^ 3) != 5) {
    return 6;
  }
  if ((1 << 4) != 16 || (16 >> 3) != 2) {
    return 7;
  }

  int side_effect = 0;
  if (0 && (side_effect = 1)) {
    return 8;
  }
  if (side_effect != 0) {
    return 9;
  }
  if (!(1 || (side_effect = 2))) {
    return 10;
  }
  if (side_effect != 0) {
    return 11;
  }

  value = 5;
  value += 3;
  value -= 2;
  value *= 4;
  value /= 3;
  value %= 5;
  value <<= 2;
  value >>= 1;
  value |= 1;
  value ^= 3;
  value &= 6;
  if (value != 4) {
    return 12;
  }

  int counter = 1;
  if (counter++ != 1 || counter != 2) {
    return 13;
  }
  if (++counter != 3) {
    return 14;
  }
  if (counter-- != 3 || counter != 2) {
    return 15;
  }
  if (--counter != 1) {
    return 16;
  }
  if (+counter != 1 || -counter != -1 || ~0 != -1) {
    return 17;
  }
  if (!(1 ? 2 : 3) || (counter ? 4 : 5) != 4) {
    return 18;
  }
  if ((int)2.9 != 2 || (double)3 != 3.0) {
    return 20;
  }

  return 0;
}
