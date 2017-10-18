# Selyp
A Lisp interpreter implemented in C.

Based on http://www.buildyourownlisp.com.

## Dependencies

* https://github.com/orangeduck/mpc

## Build
```
$ cmake .
```
or
```
$ cc -std=99 -Wall selyp.c mpc.c -leddit -o selyp
```
## Run
```
$ ./selyp
```