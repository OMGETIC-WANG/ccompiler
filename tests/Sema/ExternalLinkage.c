extern int shared;
int shared;

static int internal_value;
extern int internal_value;

static int internal_function(void) {
  return 1;
}
extern int internal_function(void);

int main(void) {
  extern int shared;
  return shared + internal_function();
}
