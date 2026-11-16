struct Record {
  int value;
};

int main(void) {
  const struct Record record = {.value = 1};
  record.value = 2;
  return record.value;
}
