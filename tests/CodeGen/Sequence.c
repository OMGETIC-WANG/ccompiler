int global_sequence = (1, 2, 3);

enum Mode {
  First = (4, 5),
  Second = 6
};

void bump(int *value) {
  (*value)++;
}

int main(void) {
  int value = 0;
  int result = (bump(&value), bump(&value), value);
  if (result != 2) {
    return 1;
  }

  (bump(&value), bump(&value));
  if (value != 4) {
    return 2;
  }

  int values[2] = {7, 8};
  int *pointer = (value, values + 1);
  if (*pointer != 8) {
    return 3;
  }

  global_sequence = 1, global_sequence = 2, global_sequence = 3;
  if (global_sequence != 3) {
    return 4;
  }

  switch (5) {
  case (4, 5):
    break;
  default:
    return 5;
  }

  switch (First) {
  case 5:
    break;
  default:
    return 6;
  }

  return 0;
}
