int global_sequence = (1, 2, 3);

int main(void) {
  int value = 0;
  int result = (value = 1, value);
  int values[2] = {result, 2};
  int *pointer = (result, values);
  return (pointer[0], result);
}
