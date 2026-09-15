/* Generate SP parameters and compiler metadata: setparams SP CLASS. */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#define VERSION "3.3.1"
#define FILENAME "npbparams.h"
#define DESC_LINE "/* CLASS = %c */\n"
#define DEFFILE "../config/make.def"
#define DEFAULT_MESSAGE "(none)"
#define LL 400
#define MAXL 46

static FILE *deffile;
static char read_info(void);
static void write_info(char class);
static void write_sp_info(FILE *fp, char class);
static void write_compiler_info(FILE *fp);
static void check_line(char *line, char *label, char *val);
static void put_def_string(FILE *fp, char *name, char *val);

/* Validate SP's class and preserve an existing header when its class matches. */
int main(int argc, char *argv[])
{
  if (argc != 3) {
    printf("Usage: %s benchmark-name class\n", argv[0]);
    return 1;
  }
  if (strcmp(argv[1], "sp") && strcmp(argv[1], "SP")) {
    printf("setparams: Error: unknown benchmark type %s\n", argv[1]);
    return 1;
  }
  char class = *argv[2];
  if (class != 'S' && class != 'W' && class != 'A' && class != 'B' &&
      class != 'C' && class != 'D' && class != 'E') {
    printf("setparams: Unknown benchmark class %c\n", class);
    printf("setparams: Allowed classes are \"S\", \"W\", and \"A\" through \"E\"\n");
    return 1;
  }
  if (class != read_info()) write_info(class);
  return 0;
}

/* Read the generated class marker; a missing or unreadable marker is unknown. */
static char read_info(void)
{
  char class = 'X';
  FILE *fp = fopen(FILENAME, "r");
  if (fp == NULL) return class;
  if (fscanf(fp, DESC_LINE, &class) != 1) {
    printf("setparams: Error parsing config file %s. Ignoring previous settings\n",
           FILENAME);
    class = 'X';
  }
  fclose(fp);
  return class;
}

/* Keep the existing SP header format and optional CONVERTDOUBLE definition. */
static void write_info(char class)
{
  FILE *fp = fopen(FILENAME, "w");
  if (fp == NULL) {
    printf("setparams: Can't open file %s for writing\n", FILENAME);
    exit(1);
  }
  fprintf(fp, DESC_LINE, class);
  fputs("/*\n"
        "   This file is generated automatically by the setparams utility.\n"
        "   It sets the number of processors and the class of the NPB\n"
        "   in this directory. Do not modify it by hand.   \n"
        "*/\n", fp);
  write_sp_info(fp, class);
#ifdef CONVERTDOUBLE
  fprintf(fp, "\n#define CONVERTDOUBLE  true\n");
#else
  fprintf(fp, "\n#define CONVERTDOUBLE  false\n");
#endif
  write_compiler_info(fp);
  fclose(fp);
}

/* Emit SP's original problem size, iteration count and timestep for each class. */
static void write_sp_info(FILE *fp, char class)
{
  int problem_size, niter;
  char *dt;
  if      (class == 'S') { problem_size = 12;  dt = "0.015";   niter = 100; }
  else if (class == 'W') { problem_size = 36;  dt = "0.0015";  niter = 400; }
  else if (class == 'A') { problem_size = 64;  dt = "0.0015";  niter = 400; }
  else if (class == 'B') { problem_size = 102; dt = "0.001";   niter = 400; }
  else if (class == 'C') { problem_size = 162; dt = "0.00067"; niter = 400; }
  else if (class == 'D') { problem_size = 408; dt = "0.00030"; niter = 500; }
  else if (class == 'E') { problem_size = 1020; dt = "0.0001"; niter = 500; }
  else {
    printf("setparams: Internal error: invalid class %c\n", class);
    exit(1);
  }
  fprintf(fp, "#define PROBLEM_SIZE   %d\n", problem_size);
  fprintf(fp, "#define NITER_DEFAULT  %d\n", niter);
  fprintf(fp, "#define DT_DEFAULT     %s\n", dt);
}

/* Read C build settings from make.def and emit the original CS1..CS7 strings. */
static void write_compiler_info(FILE *fp)
{
  char line[LL], compiletime[LL], randfile[LL];
  char cc[LL], cflags[LL], clink[LL], clinkflags[LL], c_lib[LL], c_inc[LL];
  time_t t;
  deffile = fopen(DEFFILE, "r");
  if (deffile == NULL) {
    printf("setparams: File %s doesn't exist.\n", DEFFILE);
    exit(1);
  }
  strcpy(randfile, DEFAULT_MESSAGE);
  strcpy(cc, DEFAULT_MESSAGE);
  strcpy(cflags, DEFAULT_MESSAGE);
  strcpy(clink, DEFAULT_MESSAGE);
  strcpy(clinkflags, DEFAULT_MESSAGE);
  strcpy(c_lib, DEFAULT_MESSAGE);
  strcpy(c_inc, DEFAULT_MESSAGE);
  while (fgets(line, LL, deffile) != NULL) {
    if (*line == '#') continue;
    check_line(line, "RAND", randfile);
    check_line(line, "CC", cc);
    check_line(line, "CFLAGS", cflags);
    check_line(line, "CLINK", clink);
    check_line(line, "CLINKFLAGS", clinkflags);
    check_line(line, "C_LIB", c_lib);
    check_line(line, "C_INC", c_inc);
  }
  fclose(deffile);
  time(&t);
  strftime(compiletime, sizeof(compiletime), "%d %b %Y", localtime(&t));
  put_def_string(fp, "COMPILETIME", compiletime);
  put_def_string(fp, "NPBVERSION", VERSION);
  put_def_string(fp, "CS1", cc);
  put_def_string(fp, "CS2", clink);
  put_def_string(fp, "CS3", c_lib);
  put_def_string(fp, "CS4", c_inc);
  put_def_string(fp, "CS5", cflags);
  put_def_string(fp, "CS6", clinkflags);
  put_def_string(fp, "CS7", randfile);
}

/* Parse a make.def assignment, retaining the existing continuation handling. */
static void check_line(char *line, char *label, char *val)
{
  char *original_line;
  int n;
  original_line = line;
  /* compare beginning of line and label */
  while (*label != '\0' && *line == *label) {
    line++; label++;
  }
  /* if *label is not EOS, we must have had a mismatch */
  if (*label != '\0') return;
  /* if *line is not a space, actual label is longer than test label */
  if (!isspace(*line) && *line != '=') return ;
  /* skip over white space */
  while (isspace(*line)) line++;
  /* next char should be '=' */
  if (*line != '=') return;
  /* skip over white space */
  while (isspace(*++line));
  /* if EOS, nothing was specified */
  if (*line == '\0') return;
  /* finally we've come to the value */
  strcpy(val, line);
  /* chop off the newline at the end */
  n = strlen(val)-1;
  if (n >= 0 && val[n] == '\n')
    val[n--] = '\0';
  if (n >= 0 && val[n] == '\r')
    val[n--] = '\0';
  /* treat continuation */
  while (val[n] == '\\' && fgets(original_line, LL, deffile)) {
     line = original_line;
     while (isspace(*line)) line++;
     if (isspace(*original_line)) val[n++] = ' ';
     while (*line && *line != '\n' && *line != '\r' && n < LL-1)
       val[n++] = *line++;
     val[n] = '\0';
     n--;
  }
}

/* Escape quotes using the original compiler-metadata length limit. */
static int fix_string_quote(char *val, char *newval, int maxl)
{
  int len;
  int i, j;
  len = strlen(val);
  i = j = 0;
  while (i < len && j < maxl) {
    if (val[i] == '"')
      newval[j++] = '\\';
    if (j < maxl)
      newval[j++] = val[i++];
  }
  newval[j] = '\0';
  return j;
}

/* Emit a quoted definition, shortening long values to the original 46 columns. */
static void put_def_string(FILE *fp, char *name, char *val0)
{
  int len;
  char val[MAXL+3];
  len = fix_string_quote(val0, val, MAXL+2);
  if (len > MAXL) {
    val[MAXL] = '\0';
    val[MAXL-1] = '.';
    val[MAXL-2] = '.';
    val[MAXL-3] = '.';
    len = MAXL;
  }
  fprintf(fp, "#define %s \"%s\"\n", name, val);
}
