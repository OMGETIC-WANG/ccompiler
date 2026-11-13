struct S {
  int field;
};

struct S object;
struct S *pointer;
int member = object.field;
int pointer_member = pointer->field;
