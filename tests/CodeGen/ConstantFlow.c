enum Mode {
  First = 1 + 1,
  Second
};

int global = 2 * 3;
int enum_global = First;

int main(void) {
  enum Mode local = First;
  switch (local) {
    case First:
      return global;
    default:
      return 0;
  }
}
