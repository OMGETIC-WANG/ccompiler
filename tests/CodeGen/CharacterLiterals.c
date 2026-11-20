int main(void) {
  if ('\n' != 10) {
    return 1;
  }
  if ('\x42' != 66) {
    return 2;
  }
  if ('\101' != 65) {
    return 3;
  }
  if ('A' + 1 != 66) {
    return 4;
  }
  if ('\'' != 39) {
    return 5;
  }
  if ('\\' != 92) {
    return 6;
  }
  if ('\0' != 0) {
    return 7;
  }
  if ('\xFF' != 255) {
    return 8;
  }
  if ('\377' != 255) {
    return 9;
  }
  if ('\a' != 7) {
    return 10;
  }
  if ('\b' != 8) {
    return 11;
  }
  if ('\f' != 12) {
    return 12;
  }
  if ('\r' != 13) {
    return 13;
  }
  if ('\t' != 9) {
    return 14;
  }
  if ('\v' != 11) {
    return 15;
  }
  if ('\?' != 63) {
    return 16;
  }
  if ('\"' != 34) {
    return 17;
  }

  char tab = '\t';
  switch ('B') {
  case 'A' + 1:
    break;
  default:
    return 18;
  }
  return tab == 9 ? 0 : 19;
}
