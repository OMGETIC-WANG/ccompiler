char *bf_host_read_file(char *path);
void bf_host_release(char *data);
int bf_host_getchar(void);
void bf_host_putchar(int c);
void bf_host_error(int error_code);

int validate_brackets(char *code) {
  int instruction_pointer = 0;
  int depth = 0;
  int current = 0;

  while ((current = (int)code[instruction_pointer]) != 0) {
    if (current == 91) {
      depth = depth + 1;
    }

    if (current == 93) {
      depth = depth - 1;
      if (depth < 0) {
        return 0;
      }
    }

    instruction_pointer = instruction_pointer + 1;
  }

  return depth == 0;
}

int run_brainfuck(char *code) {
  int cells[30000];
  int data_pointer = 0;
  int instruction_pointer = 0;
  int depth = 0;
  int current = 0;
  int init_index;

  for (init_index = 0; init_index < 30000; init_index = init_index + 1) {
    cells[init_index] = 0;
  }

  while ((current = (int)code[instruction_pointer]) != 0) {
    switch (current) {
    case 43: // '+'
      cells[data_pointer] = cells[data_pointer] + 1;
      if (cells[data_pointer] > 255) {
        cells[data_pointer] = cells[data_pointer] - 256;
      }
      break;

    case 45: // '-'
      cells[data_pointer] = cells[data_pointer] - 1;
      if (cells[data_pointer] < 0) {
        cells[data_pointer] = cells[data_pointer] + 256;
      }
      break;

    case 62: // '>'
      data_pointer = data_pointer + 1;
      if (data_pointer >= 30000) {
        bf_host_error(3);
        return 1;
      }
      break;

    case 60: // '<'
      data_pointer = data_pointer - 1;
      if (data_pointer < 0) {
        bf_host_error(3);
        return 1;
      }
      break;

    case 46: // '.'
      bf_host_putchar(cells[data_pointer]);
      break;

    case 44: // ','
      cells[data_pointer] = bf_host_getchar();
      break;

    case 91: // '['
      if (cells[data_pointer] == 0) {
        depth = 1;
        while (depth > 0) {
          instruction_pointer = instruction_pointer + 1;
          current = (int)code[instruction_pointer];
          if (current == 91) {
            depth = depth + 1;
          }
          if (current == 93) {
            depth = depth - 1;
          }
        }
      }
      break;

    case 93: // ']'
      if (cells[data_pointer] != 0) {
        depth = 1;
        while (depth > 0) {
          instruction_pointer = instruction_pointer - 1;
          current = (int)code[instruction_pointer];
          if (current == 93) {
            depth = depth + 1;
          }
          if (current == 91) {
            depth = depth - 1;
          }
        }
        instruction_pointer = instruction_pointer - 1;
      }
      break;
    }

    instruction_pointer = instruction_pointer + 1;
  }

  return 0;
}

int main(int argc, char **argv) {
  char *program;
  int exit_code;

  if (argc != 2) {
    bf_host_error(0);
    return 2;
  }

  program = bf_host_read_file(argv[1]);
  if (validate_brackets(program) == 0) {
    bf_host_error(2);
    bf_host_release(program);
    return 1;
  }
  exit_code = run_brainfuck(program);
  bf_host_release(program);
  return exit_code;
}
