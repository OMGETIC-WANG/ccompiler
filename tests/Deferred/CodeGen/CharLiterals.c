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
  char tab = '\t';
  return tab == 9 ? 0 : 5;
}
