#include <stdio.h>
#include <stdlib.h>

static void bf_host_fail(const char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}

char *bf_host_read_file(char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    bf_host_fail("brainfuck: cannot open program file");
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    bf_host_fail("brainfuck: cannot seek program file");
  }

  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    bf_host_fail("brainfuck: cannot determine program size");
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    bf_host_fail("brainfuck: cannot rewind program file");
  }

  char *data = malloc((size_t)size + 1);
  if (!data) {
    fclose(file);
    bf_host_fail("brainfuck: out of memory");
  }

  size_t bytes_read = fread(data, 1, (size_t)size, file);
  if (bytes_read != (size_t)size) {
    free(data);
    fclose(file);
    bf_host_fail("brainfuck: cannot read program file");
  }

  data[size] = '\0';
  fclose(file);
  return data;
}

void bf_host_release(char *data) { free(data); }

int bf_host_getchar(void) {
  int c = getchar();
  return c == EOF ? 0 : (c & 0xff);
}

void bf_host_putchar(int c) {
  fputc(c & 0xff, stdout);
  fflush(stdout);
}

void bf_host_error(int error_code) {
  if (error_code == 0) {
    fputs("usage: brainfuck PROGRAM.bf\n", stderr);
  } else if (error_code == 2) {
    fputs("brainfuck: unmatched bracket\n", stderr);
  } else if (error_code == 3) {
    fputs("brainfuck: data pointer out of bounds\n", stderr);
  }
}
