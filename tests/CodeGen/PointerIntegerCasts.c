int target = 7;
int *global_pointer = (int *)5;
long global_address = (long)&target;
int *global_roundtrip = (int *)(long)&target;
void *global_negative = (void *)-1;
void *global_wide = (void *)4294967295U;

int identity(int value) { return value; }

int main(void) {
  int local = 3;
  long address = (long)&local;
  int *restored = (int *)address;
  int *null_pointer = 0;

  if ((long)restored != address) {
    return 1;
  }
  if ((long)null_pointer != 0) {
    return 2;
  }
  if ((long)global_pointer != 5) {
    return 3;
  }
  if (global_address != (long)&target) {
    return 4;
  }
  if ((long)global_roundtrip != (long)&target) {
    return 5;
  }

  long function_address = (long)&identity;
  int (*function_pointer)(int) = (int (*)(int))function_address;
  if (function_pointer(9) != 9) {
    return 6;
  }

  long negative = (long)(void *)-1;
  if (negative != -1) {
    return 7;
  }
  unsigned int narrow = (unsigned int)(void *)5;
  if (narrow != 5) {
    return 8;
  }
  void *wide = (void *)4294967295U;
  if ((unsigned long)wide != 4294967295UL) {
    return 9;
  }
  if ((long)global_negative != -1) {
    return 10;
  }
  if ((unsigned long)global_wide != 4294967295UL) {
    return 11;
  }

  return 0;
}
