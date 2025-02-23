/* IR-agnostic target query functions relating to optabs
   Copyright (C) 2001-2015 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_OPTABS_QUERY_H
#define GCC_OPTABS_QUERY_H

#include "insn-opinit.h"

/* Return the insn used to implement mode MODE of OP, or CODE_FOR_nothing
   if the target does not have such an insn.  */

inline enum insn_code
optab_handler (optab op, machine_mode mode)
{
  unsigned scode = (op << 16) | mode;
  gcc_assert (op > LAST_CONV_OPTAB);
  return raw_optab_handler (scode);
}

/* Return the insn used to perform conversion OP from mode FROM_MODE
   to mode TO_MODE; return CODE_FOR_nothing if the target does not have
   such an insn.  */

inline enum insn_code
convert_optab_handler (convert_optab op, machine_mode to_mode,
		       machine_mode from_mode)
{
  unsigned scode = (op << 16) | (from_mode << 8) | to_mode;
  gcc_assert (op > unknown_optab && op <= LAST_CONV_OPTAB);
  return raw_optab_handler (scode);
}

/* Return the insn used to implement mode MODE of OP, or CODE_FOR_nothing
   if the target does not have such an insn.  */

inline enum insn_code
direct_optab_handler (direct_optab op, machine_mode mode)
{
  return optab_handler (op, mode);
}

/* Return true if UNOPTAB is for a trapping-on-overflow operation.  */

inline bool
trapv_unoptab_p (optab unoptab)
{
  return (unoptab == negv_optab
	  || unoptab == absv_optab);
}

/* Return true if BINOPTAB is for a trapping-on-overflow operation.  */

inline bool
trapv_binoptab_p (optab binoptab)
{
  return (binoptab == addv_optab
	  || binoptab == subv_optab
	  || binoptab == smulv_optab);
}

/* Return insn code for a conditional operator with a comparison in
   mode CMODE, unsigned if UNS is true, resulting in a value of mode VMODE.  */

inline enum insn_code
get_vcond_icode (machine_mode vmode, machine_mode cmode, bool uns)
{
  enum insn_code icode = CODE_FOR_nothing;
  if (uns)
    icode = convert_optab_handler (vcondu_optab, vmode, cmode);
  else
    icode = convert_optab_handler (vcond_optab, vmode, cmode);
  return icode;
}

/* Enumerates the possible extraction_insn operations.  */
enum extraction_pattern { EP_insv, EP_extv, EP_extzv };

/* Describes an instruction that inserts or extracts a bitfield.  */
struct extraction_insn
{
  /* The code of the instruction.  */
  enum insn_code icode;

  /* The mode that the structure operand should have.  This is byte_mode
     when using the legacy insv, extv and extzv patterns to access memory.  */
  machine_mode struct_mode;

  /* The mode of the field to be inserted or extracted, and by extension
     the mode of the insertion or extraction itself.  */
  machine_mode field_mode;

  /* The mode of the field's bit position.  This is only important
     when the position is variable rather than constant.  */
  machine_mode pos_mode;
};

bool get_best_reg_extraction_insn (extraction_insn *,
				   enum extraction_pattern,
				   unsigned HOST_WIDE_INT, machine_mode);
bool get_best_mem_extraction_insn (extraction_insn *,
				   enum extraction_pattern,
				   HOST_WIDE_INT, HOST_WIDE_INT, machine_mode);

enum insn_code can_extend_p (machine_mode, machine_mode, int);
enum insn_code can_float_p (machine_mode, machine_mode, int);
enum insn_code can_fix_p (machine_mode, machine_mode, int, bool *);
bool can_conditionally_move_p (machine_mode mode);
bool can_vec_perm_p (machine_mode, bool, const unsigned char *);
enum insn_code widening_optab_handler (optab, machine_mode, machine_mode);
/* Find a widening optab even if it doesn't widen as much as we want.  */
#define find_widening_optab_handler(A,B,C,D) \
  find_widening_optab_handler_and_mode (A, B, C, D, NULL)
enum insn_code find_widening_optab_handler_and_mode (optab, machine_mode,
						     machine_mode, int,
						     machine_mode *);
int can_mult_highpart_p (machine_mode, bool);
bool can_vec_mask_load_store_p (machine_mode, bool);
bool can_compare_and_swap_p (machine_mode, bool);
bool can_atomic_exchange_p (machine_mode, bool);
bool lshift_cheap_p (bool);

#endif
