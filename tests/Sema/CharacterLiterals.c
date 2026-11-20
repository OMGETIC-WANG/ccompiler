int hex = '\x42';
char newline = '\n';

int main(void) {
  return hex == 66 && newline == 10 ? 0 : 1;
}
