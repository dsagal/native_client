/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This file contains common parts of x86-32 and x86-64 internals (inline
 * functions and defines).
 */

#ifndef NATIVE_CLIENT_SRC_TRUSTED_VALIDATOR_RAGEL_VALIDATOR_INTERNAL_H_
#define NATIVE_CLIENT_SRC_TRUSTED_VALIDATOR_RAGEL_VALIDATOR_INTERNAL_H_

#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/shared/utils/types.h"
#include "native_client/src/trusted/validator_ragel/unreviewed/decoding.h"
#include "native_client/src/trusted/validator_ragel/validator.h"

/* Maximum set of R-DFA allowable CPUID features.  */
extern const NaClCPUFeaturesX86 kValidatorCPUIDFeatures;

/* Macroses to suppport CPUID handling.  */
#define SET_CPU_FEATURE(F) \
  if (!(F##_Allowed)) { \
    instruction_info_collected |= UNRECOGNIZED_INSTRUCTION; \
  } \
  if (!(F)) { \
    instruction_info_collected |= CPUID_UNSUPPORTED_INSTRUCTION; \
  }
#define CPUFeature_3DNOW    cpu_features->data[NaClCPUFeatureX86_3DNOW]
/*
 * AMD documentation claims it's always available if CPUFeature_LM is present,
 * But Intel documentation does not even mention it!
 * Keep it as 3DNow! instruction.
 */
#define CPUFeature_3DPRFTCH CPUFeature_3DNOW || CPUFeature_PRE
#define CPUFeature_AES      cpu_features->data[NaClCPUFeatureX86_AES]
#define CPUFeature_AESAVX   CPUFeature_AES && CPUFeature_AVX
#define CPUFeature_AVX      cpu_features->data[NaClCPUFeatureX86_AVX]
#define CPUFeature_BMI1     cpu_features->data[NaClCPUFeatureX86_BMI1]
#define CPUFeature_CLFLUSH  cpu_features->data[NaClCPUFeatureX86_CLFLUSH]
#define CPUFeature_CLMUL    cpu_features->data[NaClCPUFeatureX86_CLMUL]
#define CPUFeature_CLMULAVX CPUFeature_CLMUL && CPUFeature_AVX
#define CPUFeature_CMOV     cpu_features->data[NaClCPUFeatureX86_CMOV]
#define CPUFeature_CMOVx87  CPUFeature_CMOV && CPUFeature_x87
#define CPUFeature_CX16     cpu_features->data[NaClCPUFeatureX86_CX16]
#define CPUFeature_CX8      cpu_features->data[NaClCPUFeatureX86_CX8]
#define CPUFeature_E3DNOW   cpu_features->data[NaClCPUFeatureX86_E3DNOW]
#define CPUFeature_EMMX     cpu_features->data[NaClCPUFeatureX86_EMMX]
#define CPUFeature_EMMXSSE  CPUFeature_EMMX || CPUFeature_SSE
#define CPUFeature_F16C     cpu_features->data[NaClCPUFeatureX86_F16C]
#define CPUFeature_FMA      cpu_features->data[NaClCPUFeatureX86_FMA]
#define CPUFeature_FMA4     cpu_features->data[NaClCPUFeatureX86_FMA4]
#define CPUFeature_FXSR     cpu_features->data[NaClCPUFeatureX86_FXSR]
#define CPUFeature_LAHF     cpu_features->data[NaClCPUFeatureX86_LAHF]
#define CPUFeature_LM       cpu_features->data[NaClCPUFeatureX86_LM]
#define CPUFeature_LWP      cpu_features->data[NaClCPUFeatureX86_LWP]
/*
 * We allow lzcnt unconditionally
 * See http://code.google.com/p/nativeclient/issues/detail?id=2869
 */
#define CPUFeature_LZCNT    TRUE
#define CPUFeature_MMX      cpu_features->data[NaClCPUFeatureX86_MMX]
#define CPUFeature_MON      cpu_features->data[NaClCPUFeatureX86_MON]
#define CPUFeature_MOVBE    cpu_features->data[NaClCPUFeatureX86_MOVBE]
#define CPUFeature_OSXSAVE  cpu_features->data[NaClCPUFeatureX86_OSXSAVE]
#define CPUFeature_POPCNT   cpu_features->data[NaClCPUFeatureX86_POPCNT]
#define CPUFeature_PRE      cpu_features->data[NaClCPUFeatureX86_PRE]
#define CPUFeature_SSE      cpu_features->data[NaClCPUFeatureX86_SSE]
#define CPUFeature_SSE2     cpu_features->data[NaClCPUFeatureX86_SSE2]
#define CPUFeature_SSE3     cpu_features->data[NaClCPUFeatureX86_SSE3]
#define CPUFeature_SSE41    cpu_features->data[NaClCPUFeatureX86_SSE41]
#define CPUFeature_SSE42    cpu_features->data[NaClCPUFeatureX86_SSE42]
#define CPUFeature_SSE4A    cpu_features->data[NaClCPUFeatureX86_SSE4A]
#define CPUFeature_SSSE3    cpu_features->data[NaClCPUFeatureX86_SSSE3]
#define CPUFeature_TBM      cpu_features->data[NaClCPUFeatureX86_TBM]
#define CPUFeature_TSC      cpu_features->data[NaClCPUFeatureX86_TSC]
/*
 * We allow tzcnt unconditionally
 * See http://code.google.com/p/nativeclient/issues/detail?id=2869
 */
#define CPUFeature_TZCNT    TRUE
#define CPUFeature_x87      cpu_features->data[NaClCPUFeatureX86_x87]
#define CPUFeature_XOP      cpu_features->data[NaClCPUFeatureX86_XOP]

#define CPUFeature_3DNOW_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_3DNOW]
/*
 * AMD documentation claims it's always available if CPUFeature_LM is present,
 * But Intel documentation does not even mention it!
 * Keep it as 3DNow! instruction.
 */
#define CPUFeature_3DPRFTCH_Allowed \
  CPUFeature_3DNOW_Allowed || CPUFeature_PRE_Allowed
#define CPUFeature_AES_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_AES]
#define CPUFeature_AESAVX_Allowed \
  CPUFeature_AES_Allowed && CPUFeature_AVX_Allowed
#define CPUFeature_AVX_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_AVX]
#define CPUFeature_BMI1_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_BMI1]
#define CPUFeature_CLFLUSH_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_CLFLUSH]
#define CPUFeature_CLMUL_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_CLMUL]
#define CPUFeature_CLMULAVX_Allowed \
  CPUFeature_CLMUL_Allowed && CPUFeature_AVX_Allowed
#define CPUFeature_CMOV_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_CMOV]
#define CPUFeature_CMOVx87_Allowed \
  CPUFeature_CMOV_Allowed && CPUFeature_x87_Allowed
#define CPUFeature_CX16_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_CX16]
#define CPUFeature_CX8_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_CX8]
#define CPUFeature_E3DNOW_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_E3DNOW]
#define CPUFeature_EMMX_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_EMMX]
#define CPUFeature_EMMXSSE_Allowed \
  CPUFeature_EMMX_Allowed || CPUFeature_SSE_Allowed
#define CPUFeature_F16C_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_F16C]
#define CPUFeature_FMA_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_FMA]
#define CPUFeature_FMA4_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_FMA4]
#define CPUFeature_FXSR_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_FXSR]
#define CPUFeature_LAHF_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_LAHF]
#define CPUFeature_LM_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_LM]
#define CPUFeature_LWP_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_LWP]
/*
 * We allow lzcnt unconditionally
 * See http://code.google.com/p/nativeclient/issues/detail?id=2869
 */
#define CPUFeature_LZCNT_Allowed TRUE
#define CPUFeature_MMX_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_MMX]
#define CPUFeature_MON_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_MON]
#define CPUFeature_MOVBE_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_MOVBE]
#define CPUFeature_OSXSAVE_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_OSXSAVE]
#define CPUFeature_POPCNT_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_POPCNT]
#define CPUFeature_PRE_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_PRE]
#define CPUFeature_SSE_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSE]
#define CPUFeature_SSE2_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSE2]
#define CPUFeature_SSE3_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSE3]
#define CPUFeature_SSE41_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSE41]
#define CPUFeature_SSE42_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSE42]
#define CPUFeature_SSE4A_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSE4A]
#define CPUFeature_SSSE3_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_SSSE3]
#define CPUFeature_TBM_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_TBM]
#define CPUFeature_TSC_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_TSC]
/*
 * We allow tzcnt unconditionally
 * See http://code.google.com/p/nativeclient/issues/detail?id=2869
 */
#define CPUFeature_TZCNT_Allowed TRUE
#define CPUFeature_x87_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_x87]
#define CPUFeature_XOP_Allowed \
  kValidatorCPUIDFeatures.data[NaClCPUFeatureX86_XOP]

/* Remember some information about instruction for further processing.  */
#define GET_REX_PREFIX() rex_prefix
#define SET_REX_PREFIX(P) rex_prefix = (P)
#define GET_VEX_PREFIX2() vex_prefix2
#define SET_VEX_PREFIX2(P) vex_prefix2 = (P)
#define GET_VEX_PREFIX3() vex_prefix3
#define SET_VEX_PREFIX3(P) vex_prefix3 = (P)
#define SET_MODRM_BASE(N) base = (N)
#define SET_MODRM_INDEX(N) index = (N)

/* Ignore this information for now.  */
#define SET_DATA16_PREFIX(S)
#define SET_REPZ_PREFIX(S)
#define SET_REPNZ_PREFIX(S)
#define SET_MODRM_SCALE(S)
#define SET_DISP_PTR(P)
#define SET_IMM_PTR(P)
#define SET_IMM2_PTR(P)

/*
 * Collect information about anyfields (offsets and immediates).
 * Note: we use += below instead of |=. This means two immediate fields will
 * be treated as one.  It's not important for safety.
 */
#define SET_DISP_TYPE(T) SET_DISP_TYPE_##T
#define SET_DISP_TYPE_DISPNONE
#define SET_DISP_TYPE_DISP8 (instruction_info_collected += DISPLACEMENT_8BIT)
#define SET_DISP_TYPE_DISP32 (instruction_info_collected += DISPLACEMENT_32BIT)
#define SET_IMM_TYPE(T) SET_IMM_TYPE_##T
/* imm2 field is a flag, not accumulator, like with other immediates  */
#define SET_IMM_TYPE_IMM2 (instruction_info_collected |= IMMEDIATE_2BIT)
#define SET_IMM_TYPE_IMM8 (instruction_info_collected += IMMEDIATE_8BIT)
#define SET_IMM_TYPE_IMM16 (instruction_info_collected += IMMEDIATE_16BIT)
#define SET_IMM_TYPE_IMM32 (instruction_info_collected += IMMEDIATE_32BIT)
#define SET_IMM_TYPE_IMM64 (instruction_info_collected += IMMEDIATE_64BIT)
#define SET_IMM2_TYPE(T) SET_IMM2_TYPE_##T
#define SET_IMM2_TYPE_IMM8 \
    (instruction_info_collected += SECOND_IMMEDIATE_8BIT)
#define SET_IMM2_TYPE_IMM16 \
    (instruction_info_collected += SECOND_IMMEDIATE_16BIT)

/* Mark the destination of a jump instruction and make an early validity check:
 * to jump outside given code region, the target address must be aligned.
 *
 * Returns TRUE iff the jump passes the early validity check.
 */
static FORCEINLINE int MarkJumpTarget(size_t jump_dest,
                                      bitmap_word *jump_dests,
                                      size_t size) {
  if ((jump_dest & kBundleMask) == 0) {
    return TRUE;
  }
  if (jump_dest >= size) {
    return FALSE;
  }
  BitmapSetBit(jump_dests, jump_dest);
  return TRUE;
}

/*
 * Mark the given address as valid jump target address.
 */
static FORCEINLINE void MarkValidJumpTarget(size_t address,
                                            bitmap_word *valid_targets) {
  BitmapSetBit(valid_targets, address);
}

/*
 * Mark the given address as invalid jump target address (that is: unmark it).
 */
static FORCEINLINE void UnmarkValidJumpTarget(size_t address,
                                              bitmap_word *valid_targets) {
  BitmapClearBit(valid_targets, address);
}

/*
 * Mark the given addresses as invalid jump target addresses (that is: unmark
 * them).
 */
static FORCEINLINE void UnmarkValidJumpTargets(size_t address,
                                               size_t bytes,
                                               bitmap_word *valid_targets) {
  BitmapClearBits(valid_targets, address, bytes);
}

static INLINE Bool ProcessInvalidJumpTargets(
    const uint8_t *data,
    size_t size,
    bitmap_word *valid_targets,
    bitmap_word *jump_dests,
    ValidationCallbackFunc user_callback,
    void *callback_data) {
  size_t elements = (size + NACL_HOST_WORDSIZE - 1) / NACL_HOST_WORDSIZE;
  size_t i, j;
  Bool result = TRUE;

  for (i = 0; i < elements ; i++) {
    bitmap_word jump_dest_mask = jump_dests[i];
    bitmap_word valid_target_mask = valid_targets[i];
    if ((jump_dest_mask & ~valid_target_mask) != 0) {
      for (j = i * NACL_HOST_WORDSIZE; j < (i + 1) * NACL_HOST_WORDSIZE; j++)
        if (BitmapIsBitSet(jump_dests, j) &&
            !BitmapIsBitSet(valid_targets, j)) {
          result &= user_callback(data + j,
                                  data + j,
                                  BAD_JUMP_TARGET,
                                  callback_data);
        }
    }
  }

  return result;
}


/*
 * Process rel8_operand.  Note: rip points to the beginning of the next
 * instruction here and x86 encoding guarantees rel8 field is the last one
 * in a current instruction.
 */
static FORCEINLINE void Rel8Operand(const uint8_t *rip,
                                    const uint8_t* codeblock_start,
                                    bitmap_word *jump_dests,
                                    size_t jumpdests_size,
                                    uint32_t *instruction_info_collected) {
  int8_t offset = (uint8_t) (rip[-1]);
  size_t jump_dest = offset + (rip - codeblock_start);

  if (MarkJumpTarget(jump_dest, jump_dests, jumpdests_size))
    *instruction_info_collected |= RELATIVE_8BIT;
  else
    *instruction_info_collected |= RELATIVE_8BIT | DIRECT_JUMP_OUT_OF_RANGE;
}

/*
 * Process rel32_operand.  Note: rip points to the beginning of the next
 * instruction here and x86 encoding guarantees rel32 field is the last one
 * in a current instruction.
 */
static FORCEINLINE void Rel32Operand(const uint8_t *rip,
                                     const uint8_t* codeblock_start,
                                     bitmap_word *jump_dests,
                                     size_t jumpdests_size,
                                     uint32_t *instruction_info_collected) {
  int32_t offset = (rip[-4] + 256U * (rip[-3] + 256U * (
                                       rip[-2] + 256U * ((uint32_t) rip[-1]))));
  size_t jump_dest = offset + (rip - codeblock_start);

  if (MarkJumpTarget(jump_dest, jump_dests, jumpdests_size))
    *instruction_info_collected |= RELATIVE_32BIT;
  else
    *instruction_info_collected |= RELATIVE_32BIT | DIRECT_JUMP_OUT_OF_RANGE;
}

#endif  /* NATIVE_CLIENT_SRC_TRUSTED_VALIDATOR_RAGEL_VALIDATOR_INTERNAL_H_ */
