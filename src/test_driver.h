/* test_driver.h — `lls test` native test runner (extracted from main.c, W4) */
#ifndef LS_TEST_DRIVER_H
#define LS_TEST_DRIVER_H

/* ls test <files...> [--filter <pat>] [--memcheck]
   argv layout is identical to main()'s (argv[1] == "test"). Returns the
   process exit code: non-zero if any file failed. */
int test_driver_run(int argc, char *argv[]);

#endif /* LS_TEST_DRIVER_H */
