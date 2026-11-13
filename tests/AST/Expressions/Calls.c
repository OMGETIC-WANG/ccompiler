int identity(int value) {
  return value;
}

int call = identity(1);
int nested_call = identity(identity(2));
