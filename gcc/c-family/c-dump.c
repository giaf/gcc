/* Tree-dumping functionality for C-family languages.
   Copyright (C) 2002-2015 Free Software Foundation, Inc.
   Written by Mark Mitchell <mark@codesourcery.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "c-common.h"
#include "alias.h"
#include "tree-dump.h"

/* Dump any C-specific tree codes and attributes of common codes.  */

bool
c_dump_tree (void *dump_info, tree t)
{
  enum tree_code code;
  dump_info_p di = (dump_info_p) dump_info;

  /* Figure out what kind of node this is.  */
  code = TREE_CODE (t);

  switch (code)
    {
    case FIELD_DECL:
      if (DECL_C_BIT_FIELD (t))
	dump_string (di, "bitfield");
      break;

    default:
      break;
    }

  return false;
}
