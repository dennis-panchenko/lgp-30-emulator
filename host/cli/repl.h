#ifndef LGP30_HOST_REPL_H
#define LGP30_HOST_REPL_H

/* argv[0] is the program name (for usage messages); argv[1], if present,
 * is an optional program file to auto-load before the prompt starts. */
int repl_main(int argc, char **argv);

#endif
