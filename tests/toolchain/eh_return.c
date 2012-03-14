/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This test is guarding against regressions wrt
    __builtin_eh_return (offset, handler)
    especially for pnacl
    The behavior of this builtin is not seem to be well defined.
    It does not even exist for arm, so we empirically determine
    values for STACK_REMAINDER which make the builtin
    behave like a tail-call for testing.
 */
#include "native_client/tests/toolchain/eh_helper.h"
#include "native_client/tests/toolchain/utils.h"


void* dummy0_cfa = 0;
void* return_address = 0;

/* STACK_REMAINDER is to compensate for the
 *  return address slot which is still on the stack on x86.
 */
#if TARGET_FULLARCH == arm
  #define STACK_REMAINDER 0L
#elif defined(__i386__) || TARGET_FULLARCH == x86-32
  #define STACK_REMAINDER -4L
#elif defined(__x86_64__) || TARGET_FULLARCH == x86-64
  #define STACK_REMAINDER -8L
#else
#error "unknown arch"
#endif

/* prevent inlining, especially into main */
void dummy3() __attribute__((noinline));
void dummy2() __attribute__((noinline));
void dummy1() __attribute__((noinline));
void dummy0() __attribute__((noinline));


void dummy3() {
  void* cfa = (void*) __builtin_dwarf_cfa();
  printf("cfa: %p\n", cfa);
  ASSERT(dummy0_cfa == cfa, "ERROR: cfa mismatch");

  next_step(5);
  /* back to main */
  __builtin_eh_return (STACK_REMAINDER, return_address);
}

void dummy2() {
  void* cfa = (void*) __builtin_dwarf_cfa();
  printf("cfa: %p\n", cfa);
  ASSERT(dummy0_cfa == cfa, "ERROR: cfa mismatch");

  next_step(4);
  __builtin_eh_return (STACK_REMAINDER, dummy3);
}

void dummy1() {
  void* cfa = (void*) __builtin_dwarf_cfa();
  printf("cfa: %p\n", cfa);
  ASSERT(dummy0_cfa == cfa, "ERROR: cfa mismatch");

  next_step(3);
  __builtin_eh_return (STACK_REMAINDER, dummy2);
}

void dummy0() {

  /* Save the return address for later use. */
  return_address = (void*) __builtin_return_address (0);
  printf("ra: %p\n", return_address);
  /* NOTE: __builtin_dwarf_cfa() return a pointer to the "current stack frame"
   *  CFA="canonical frame address".
   *  We pick STACK_REMAINDER so that stack is completely cleaned up and
   *  it looks like the function was never called.
   *  Hence all the CFA's should be the same.
   */
  void* cfa = (void*) __builtin_dwarf_cfa();
  printf("cfa: %p\n", cfa);
  dummy0_cfa = cfa;
  next_step(2);
  __builtin_eh_return (STACK_REMAINDER, dummy1);
}

int main(int argc, char* argv[]) {
  if (argc == 666) {
    goto this_is_reachable;
  }
  next_step(1);
  dummy0();

this_is_reachable:
  next_step(6);
  /* would prefer to use "return 55;" here but the eh_return calls appear to
   * clobber the register state in some unexpected way
   */
  exit(55);
}
