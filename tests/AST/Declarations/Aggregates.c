struct Record {
  int id;
  char *name;
  unsigned long tags[2];
};

union Number {
  int integer;
  double real;
};

enum Color : unsigned int {
  Red = 1,
  Green,
  Blue = 4
};
