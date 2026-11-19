#define APPLY(function, value) function(value)
#define TWICE(value) ((value) + (value))
#define PASTE(left, right) left##right
#define STRINGIFY(value) #value
#define KEEP(...) __VA_ARGS__

int PASTE(answer, _value) = APPLY(TWICE, 3);
char label[] = STRINGIFY(single);
int values[] = {KEEP(1, 2, 3)};
