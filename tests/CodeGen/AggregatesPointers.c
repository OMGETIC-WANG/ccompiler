struct Pair {
  int first;
  int second;
};

union Number {
  int integer;
  double real;
};

struct Pair global_pair = {.first = 1, .second = 2};
int global_values[3] = {1, 2, 3};

int main(void) {
  struct Pair positional = {3, 4};
  struct Pair designated = {.second = 6, .first = 5};
  struct Pair *pointer = &designated;

  if (global_pair.first != 1 || global_pair.second != 2) {
    return 1;
  }
  if (positional.first != 3 || positional.second != 4) {
    return 2;
  }
  if (pointer->first != 5 || pointer->second != 6) {
    return 3;
  }

  union Number number;
  number.integer = 7;
  if (number.integer != 7) {
    return 4;
  }

  int local_values[3] = {8, 9, 10};
  if (global_values[0] != 1 || global_values[2] != 3) {
    return 5;
  }

  int *cursor = local_values;
  if (*cursor != 8) {
    return 6;
  }
  cursor++;
  if (*cursor != 9 || cursor[1] != 10) {
    return 7;
  }
  if (*(cursor + 1) != 10 || *(cursor - 1) != 8) {
    return 8;
  }

  return 0;
}
