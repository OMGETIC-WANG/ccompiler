int target = 7;
int *global_pointer = (int *)5;
long global_address = (long)&target;
int *global_roundtrip = (int *)(long)&target;

int main(void) {
  int local = 3;
  int *null_pointer = 0;
  long address = (long)&local;
  int *restored = (int *)address;

  return (long)restored == address && (long)global_pointer == 5 &&
         global_address == (long)&target &&
         (long)global_roundtrip == (long)&target &&
         (long)null_pointer == 0;
}
