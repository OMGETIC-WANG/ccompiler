typedef int *integer_pointer;

volatile int object;
volatile int values[2];
integer_pointer restrict typedef_restrict;

int *volatile volatile_pointer;
const volatile int *restrict restrict_pointer;
int *__restrict__ gnu_restrict;
int *__restrict gnu_restrict_compat;
int *restrict standard_restrict;

struct Holder {
  volatile int field;
  int *__restrict__ pointer;
};

int apply(int *__restrict__ first, volatile int second);
