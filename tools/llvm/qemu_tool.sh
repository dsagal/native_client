#!/bin/bash
#
# Copyright 2009 The Native Client Authors.  All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.
# Copyright 2009, Google Inc.

set -o nounset
set -o errexit

#@ Various commands to emulate arm code using qemu
#@
#@ Note: this script is not meant to be run as
#@     tools/llvm/qemu_tool.sh
#@ but rather as:
#@     toolchain/linux_arm-trusted/qemu-arm

# From a qemu build based on qemu-0.10.1.tar.gz
readonly SDK_ROOT=$(dirname $0)
readonly QEMU=${SDK_ROOT}/qemu-arm
readonly QEMU_JAIL=${SDK_ROOT}/arm-2009q3/arm-none-linux-gnueabi/libc
# NOTE: some useful debugging options for qemu:
#       env vars:
#                  QEMU_STRACE=1
#       args:
#                  -strace
#                  -d out_asm,in_asm,op,int,exec,cpu
#       c.f.  cpu_log_items in qemu-XXX/exec.c
readonly QEMU_ARGS="-cpu cortex-a8"
readonly QEMU_ARGS_DEBUG="-d in_asm,int,exec,cpu"
readonly QEMU_ARGS_DEBUG_SR="-d in_asm,int,exec,cpu,service_runtime"

######################################################################
# Helpers
######################################################################

Banner() {
  echo "######################################################################"
  echo $*
  echo "######################################################################"
}

Usage() {
  egrep "^#@" $0 | cut --bytes=3-
}


CheckPrerequisites () {
  if [[ ! -d ${QEMU_JAIL} ]] ; then
    echo "ERROR:  no proper root-jail directory found"
    exit -1
  fi
}


Hints() {
  echo
  echo "traces can be found in /tmp/qemu.log"
  echo "for faster execution disable sel_ldr validation"
  echo
}

######################################################################

#@
#@ help
#@
#@   print help for all modes
help () {
  Usage
}

#@
#@ run
#@
#@   run stuff
run() {
  CheckPrerequisites
  exec ${QEMU} -L ${QEMU_JAIL} ${QEMU_ARGS} "$@"
}

#@
#@ run_debug
#@
#@   run stuff but also generate trace in /tmp
run_debug() {
  Hints
  CheckPrerequisites
  exec ${QEMU} -L ${QEMU_JAIL} ${QEMU_ARGS} ${QEMU_ARGS_DEBUG} "$@"
}

#@
#@ run_debug_service_runtime
#@
#@   run stuff but also generate trace in /tmp even for service_runtime
run_debug_service_runtime() {
  Hints
  CheckPrerequisites
  exec ${QEMU} -L ${QEMU_JAIL} ${QEMU_ARGS} ${QEMU_ARGS_DEBUG_SR} "$@"
}

######################################################################
if [[ "$0" == *run_under_qemu_arm ]] ; then
  run "$@"
elif [[ $# -eq 0 ]] ; then
  echo "you must specify a mode on the commandline:"
  echo
  Usage
  exit -1
elif [[ "$(type -t $1)" != "function" ]]; then
  echo "ERROR: unknown function '$1'." >&2
  echo "For help, try:"
  echo "    $0 help"
  exit 1
else
  "$@"
fi
