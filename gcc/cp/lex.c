/* Separate lexical analyzer for GNU C++.
   Copyright (C) 1987-2015 Free Software Foundation, Inc.
   Hacked by Michael Tiemann (tiemann@cygnus.com)

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


/* This file is the lexical analyzer for GNU C++.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "timevar.h"
#include "tm_p.h"
#include "stringpool.h"
#include "alias.h"
#include "flags.h"
#include "c-family/c-pragma.h"
#include "c-family/c-objc.h"

static int interface_strcmp (const char *);
static void init_cp_pragma (void);

static tree parse_strconst_pragma (const char *, int);
static void handle_pragma_vtable (cpp_reader *);
static void handle_pragma_unit (cpp_reader *);
static void handle_pragma_interface (cpp_reader *);
static void handle_pragma_implementation (cpp_reader *);
static void handle_pragma_java_exceptions (cpp_reader *);

static void init_operators (void);
static void copy_lang_type (tree);

/* A constraint that can be tested at compile time.  */
#define CONSTRAINT(name, expr) extern int constraint_##name [(expr) ? 1 : -1]

/* Functions and data structures for #pragma interface.

   `#pragma implementation' means that the main file being compiled
   is considered to implement (provide) the classes that appear in
   its main body.  I.e., if this is file "foo.cc", and class `bar'
   is defined in "foo.cc", then we say that "foo.cc implements bar".

   All main input files "implement" themselves automagically.

   `#pragma interface' means that unless this file (of the form "foo.h"
   is not presently being included by file "foo.cc", the
   CLASSTYPE_INTERFACE_ONLY bit gets set.  The effect is that none
   of the vtables nor any of the inline functions defined in foo.h
   will ever be output.

   There are cases when we want to link files such as "defs.h" and
   "main.cc".  In this case, we give "defs.h" a `#pragma interface',
   and "main.cc" has `#pragma implementation "defs.h"'.  */

struct impl_files
{
  const char *filename;
  struct impl_files *next;
};

static struct impl_files *impl_file_chain;

/* True if we saw "#pragma GCC java_exceptions".  */
bool pragma_java_exceptions;

void
cxx_finish (void)
{
  c_common_finish ();
}

/* A mapping from tree codes to operator name information.  */
operator_name_info_t operator_name_info[(int) MAX_TREE_CODES];
/* Similar, but for assignment operators.  */
operator_name_info_t assignment_operator_name_info[(int) MAX_TREE_CODES];

/* Initialize data structures that keep track of operator names.  */

#define DEF_OPERATOR(NAME, C, M, AR, AP) \
 CONSTRAINT (C, sizeof "operator " + sizeof NAME <= 256);
#include "operators.def"
#undef DEF_OPERATOR

static void
init_operators (void)
{
  tree identifier;
  char buffer[256];
  struct operator_name_info_t *oni;

#define DEF_OPERATOR(NAME, CODE, MANGLING, ARITY, ASSN_P)		    \
  sprintf (buffer, ISALPHA (NAME[0]) ? "operator %s" : "operator%s", NAME); \
  identifier = get_identifier (buffer);					    \
  IDENTIFIER_OPNAME_P (identifier) = 1;					    \
									    \
  oni = (ASSN_P								    \
	 ? &assignment_operator_name_info[(int) CODE]			    \
	 : &operator_name_info[(int) CODE]);				    \
  oni->identifier = identifier;						    \
  oni->name = NAME;							    \
  oni->mangled_name = MANGLING;						    \
  oni->arity = ARITY;

#include "operators.def"
#undef DEF_OPERATOR

  operator_name_info[(int) ERROR_MARK].identifier
    = get_identifier ("<invalid operator>");

  /* Handle some special cases.  These operators are not defined in
     the language, but can be produced internally.  We may need them
     for error-reporting.  (Eventually, we should ensure that this
     does not happen.  Error messages involving these operators will
     be confusing to users.)  */

  operator_name_info [(int) INIT_EXPR].name
    = operator_name_info [(int) MODIFY_EXPR].name;
  operator_name_info [(int) EXACT_DIV_EXPR].name = "(ceiling /)";
  operator_name_info [(int) CEIL_DIV_EXPR].name = "(ceiling /)";
  operator_name_info [(int) FLOOR_DIV_EXPR].name = "(floor /)";
  operator_name_info [(int) ROUND_DIV_EXPR].name = "(round /)";
  operator_name_info [(int) CEIL_MOD_EXPR].name = "(ceiling %)";
  operator_name_info [(int) FLOOR_MOD_EXPR].name = "(floor %)";
  operator_name_info [(int) ROUND_MOD_EXPR].name = "(round %)";
  operator_name_info [(int) ABS_EXPR].name = "abs";
  operator_name_info [(int) TRUTH_AND_EXPR].name = "strict &&";
  operator_name_info [(int) TRUTH_OR_EXPR].name = "strict ||";
  operator_name_info [(int) RANGE_EXPR].name = "...";
  operator_name_info [(int) UNARY_PLUS_EXPR].name = "+";

  assignment_operator_name_info [(int) EXACT_DIV_EXPR].name
    = "(exact /=)";
  assignment_operator_name_info [(int) CEIL_DIV_EXPR].name
    = "(ceiling /=)";
  assignment_operator_name_info [(int) FLOOR_DIV_EXPR].name
    = "(floor /=)";
  assignment_operator_name_info [(int) ROUND_DIV_EXPR].name
    = "(round /=)";
  assignment_operator_name_info [(int) CEIL_MOD_EXPR].name
    = "(ceiling %=)";
  assignment_operator_name_info [(int) FLOOR_MOD_EXPR].name
    = "(floor %=)";
  assignment_operator_name_info [(int) ROUND_MOD_EXPR].name
    = "(round %=)";
}

/* Initialize the reserved words.  */

void
init_reswords (void)
{
  unsigned int i;
  tree id;
  int mask = 0;

  if (cxx_dialect < cxx11)
    mask |= D_CXX11;
  if (!flag_concepts)
    mask |= D_CXX_CONCEPTS;
  if (!flag_tm)
    mask |= D_TRANSMEM;
  if (flag_no_asm)
    mask |= D_ASM | D_EXT;
  if (flag_no_gnu_keywords)
    mask |= D_EXT;

  /* The Objective-C keywords are all context-dependent.  */
  mask |= D_OBJC;

  ridpointers = ggc_cleared_vec_alloc<tree> ((int) RID_MAX);
  for (i = 0; i < num_c_common_reswords; i++)
    {
      if (c_common_reswords[i].disable & D_CONLY)
	continue;
      id = get_identifier (c_common_reswords[i].word);
      C_SET_RID_CODE (id, c_common_reswords[i].rid);
      ridpointers [(int) c_common_reswords[i].rid] = id;
      if (! (c_common_reswords[i].disable & mask))
	C_IS_RESERVED_WORD (id) = 1;
    }

  for (i = 0; i < NUM_INT_N_ENTS; i++)
    {
      char name[50];
      sprintf (name, "__int%d", int_n_data[i].bitsize);
      id = get_identifier (name);
      C_SET_RID_CODE (id, RID_FIRST_INT_N + i);
      C_IS_RESERVED_WORD (id) = 1;
    }
}

static void
init_cp_pragma (void)
{
  c_register_pragma (0, "vtable", handle_pragma_vtable);
  c_register_pragma (0, "unit", handle_pragma_unit);
  c_register_pragma (0, "interface", handle_pragma_interface);
  c_register_pragma (0, "implementation", handle_pragma_implementation);
  c_register_pragma ("GCC", "interface", handle_pragma_interface);
  c_register_pragma ("GCC", "implementation", handle_pragma_implementation);
  c_register_pragma ("GCC", "java_exceptions", handle_pragma_java_exceptions);
}

/* TRUE if a code represents a statement.  */

bool statement_code_p[MAX_TREE_CODES];

/* Initialize the C++ front end.  This function is very sensitive to
   the exact order that things are done here.  It would be nice if the
   initialization done by this routine were moved to its subroutines,
   and the ordering dependencies clarified and reduced.  */
bool
cxx_init (void)
{
  location_t saved_loc;
  unsigned int i;
  static const enum tree_code stmt_codes[] = {
   CTOR_INITIALIZER,	TRY_BLOCK,	HANDLER,
   EH_SPEC_BLOCK,	USING_STMT,	TAG_DEFN,
   IF_STMT,		CLEANUP_STMT,	FOR_STMT,
   RANGE_FOR_STMT,	WHILE_STMT,	DO_STMT,
   BREAK_STMT,		CONTINUE_STMT,	SWITCH_STMT,
   EXPR_STMT
  };

  memset (&statement_code_p, 0, sizeof (statement_code_p));
  for (i = 0; i < ARRAY_SIZE (stmt_codes); i++)
    statement_code_p[stmt_codes[i]] = true;

  saved_loc = input_location;
  input_location = BUILTINS_LOCATION;

  init_reswords ();
  init_tree ();
  init_cp_semantics ();
  init_operators ();
  init_method ();

  current_function_decl = NULL;

  class_type_node = ridpointers[(int) RID_CLASS];

  cxx_init_decl_processing ();

  if (c_common_init () == false)
    {
      input_location = saved_loc;
      return false;
    }

  init_cp_pragma ();

  init_repo ();

  input_location = saved_loc;
  return true;
}

/* Return nonzero if S is not considered part of an
   INTERFACE/IMPLEMENTATION pair.  Otherwise, return 0.  */

static int
interface_strcmp (const char* s)
{
  /* Set the interface/implementation bits for this scope.  */
  struct impl_files *ifiles;
  const char *s1;

  for (ifiles = impl_file_chain; ifiles; ifiles = ifiles->next)
    {
      const char *t1 = ifiles->filename;
      s1 = s;

      if (*s1 == 0 || filename_ncmp (s1, t1, 1) != 0)
	continue;

      while (*s1 != 0 && filename_ncmp (s1, t1, 1) == 0)
	s1++, t1++;

      /* A match.  */
      if (*s1 == *t1)
	return 0;

      /* Don't get faked out by xxx.yyy.cc vs xxx.zzz.cc.  */
      if (strchr (s1, '.') || strchr (t1, '.'))
	continue;

      if (*s1 == '\0' || s1[-1] != '.' || t1[-1] != '.')
	continue;

      /* A match.  */
      return 0;
    }

  /* No matches.  */
  return 1;
}



/* Parse a #pragma whose sole argument is a string constant.
   If OPT is true, the argument is optional.  */
static tree
parse_strconst_pragma (const char* name, int opt)
{
  tree result, x;
  enum cpp_ttype t;

  t = pragma_lex (&result);
  if (t == CPP_STRING)
    {
      if (pragma_lex (&x) != CPP_EOF)
	warning (0, "junk at end of #pragma %s", name);
      return result;
    }

  if (t == CPP_EOF && opt)
    return NULL_TREE;

  error ("invalid #pragma %s", name);
  return error_mark_node;
}

static void
handle_pragma_vtable (cpp_reader* /*dfile*/)
{
  parse_strconst_pragma ("vtable", 0);
  sorry ("#pragma vtable no longer supported");
}

static void
handle_pragma_unit (cpp_reader* /*dfile*/)
{
  /* Validate syntax, but don't do anything.  */
  parse_strconst_pragma ("unit", 0);
}

static void
handle_pragma_interface (cpp_reader* /*dfile*/)
{
  tree fname = parse_strconst_pragma ("interface", 1);
  struct c_fileinfo *finfo;
  const char *filename;

  if (fname == error_mark_node)
    return;
  else if (fname == 0)
    filename = lbasename (LOCATION_FILE (input_location));
  else
    filename = TREE_STRING_POINTER (fname);

  finfo = get_fileinfo (LOCATION_FILE (input_location));

  if (impl_file_chain == 0)
    {
      /* If this is zero at this point, then we are
	 auto-implementing.  */
      if (main_input_filename == 0)
	main_input_filename = LOCATION_FILE (input_location);
    }

  finfo->interface_only = interface_strcmp (filename);
  /* If MULTIPLE_SYMBOL_SPACES is set, we cannot assume that we can see
     a definition in another file.  */
  if (!MULTIPLE_SYMBOL_SPACES || !finfo->interface_only)
    finfo->interface_unknown = 0;
}

/* Note that we have seen a #pragma implementation for the key MAIN_FILENAME.
   We used to only allow this at toplevel, but that restriction was buggy
   in older compilers and it seems reasonable to allow it in the headers
   themselves, too.  It only needs to precede the matching #p interface.

   We don't touch finfo->interface_only or finfo->interface_unknown;
   the user must specify a matching #p interface for this to have
   any effect.  */

static void
handle_pragma_implementation (cpp_reader* /*dfile*/)
{
  tree fname = parse_strconst_pragma ("implementation", 1);
  const char *filename;
  struct impl_files *ifiles = impl_file_chain;

  if (fname == error_mark_node)
    return;

  if (fname == 0)
    {
      if (main_input_filename)
	filename = main_input_filename;
      else
	filename = LOCATION_FILE (input_location);
      filename = lbasename (filename);
    }
  else
    {
      filename = TREE_STRING_POINTER (fname);
      if (cpp_included_before (parse_in, filename, input_location))
	warning (0, "#pragma implementation for %qs appears after "
		 "file is included", filename);
    }

  for (; ifiles; ifiles = ifiles->next)
    {
      if (! filename_cmp (ifiles->filename, filename))
	break;
    }
  if (ifiles == 0)
    {
      ifiles = XNEW (struct impl_files);
      ifiles->filename = xstrdup (filename);
      ifiles->next = impl_file_chain;
      impl_file_chain = ifiles;
    }
}

/* Indicate that this file uses Java-personality exception handling.  */
static void
handle_pragma_java_exceptions (cpp_reader* /*dfile*/)
{
  tree x;
  if (pragma_lex (&x) != CPP_EOF)
    warning (0, "junk at end of #pragma GCC java_exceptions");

  choose_personality_routine (lang_java);
  pragma_java_exceptions = true;
}

/* Issue an error message indicating that the lookup of NAME (an
   IDENTIFIER_NODE) failed.  Returns the ERROR_MARK_NODE.  */

tree
unqualified_name_lookup_error (tree name)
{
  if (IDENTIFIER_OPNAME_P (name))
    {
      if (name != ansi_opname (ERROR_MARK))
	error ("%qD not defined", name);
    }
  else
    {
      if (!objc_diagnose_private_ivar (name))
	{
	  error ("%qD was not declared in this scope", name);
	  suggest_alternatives_for (location_of (name), name);
	}
      /* Prevent repeated error messages by creating a VAR_DECL with
	 this NAME in the innermost block scope.  */
      if (local_bindings_p ())
	{
	  tree decl;
	  decl = build_decl (input_location,
			     VAR_DECL, name, error_mark_node);
	  DECL_CONTEXT (decl) = current_function_decl;
	  push_local_binding (name, decl, 0);
	  /* Mark the variable as used so that we do not get warnings
	     about it being unused later.  */
	  TREE_USED (decl) = 1;
	}
    }

  return error_mark_node;
}

/* Like unqualified_name_lookup_error, but NAME is an unqualified-id
   used as a function.  Returns an appropriate expression for
   NAME.  */

tree
unqualified_fn_lookup_error (tree name)
{
  if (processing_template_decl)
    {
      /* In a template, it is invalid to write "f()" or "f(3)" if no
	 declaration of "f" is available.  Historically, G++ and most
	 other compilers accepted that usage since they deferred all name
	 lookup until instantiation time rather than doing unqualified
	 name lookup at template definition time; explain to the user what
	 is going wrong.

	 Note that we have the exact wording of the following message in
	 the manual (trouble.texi, node "Name lookup"), so they need to
	 be kept in synch.  */
      permerror (input_location, "there are no arguments to %qD that depend on a template "
		 "parameter, so a declaration of %qD must be available",
		 name, name);

      if (!flag_permissive)
	{
	  static bool hint;
	  if (!hint)
	    {
	      inform (input_location, "(if you use %<-fpermissive%>, G++ will accept your "
		     "code, but allowing the use of an undeclared name is "
		     "deprecated)");
	      hint = true;
	    }
	}
      return name;
    }

  return unqualified_name_lookup_error (name);
}

/* Wrapper around build_lang_decl_loc(). Should gradually move to
   build_lang_decl_loc() and then rename build_lang_decl_loc() back to
   build_lang_decl().  */

tree
build_lang_decl (enum tree_code code, tree name, tree type)
{
  return build_lang_decl_loc (input_location, code, name, type);
}

/* Build a decl from CODE, NAME, TYPE declared at LOC, and then add
   DECL_LANG_SPECIFIC info to the result.  */

tree
build_lang_decl_loc (location_t loc, enum tree_code code, tree name, tree type)
{
  tree t;

  t = build_decl (loc, code, name, type);
  retrofit_lang_decl (t);

  return t;
}

/* Add DECL_LANG_SPECIFIC info to T.  Called from build_lang_decl
   and pushdecl (for functions generated by the back end).  */

void
retrofit_lang_decl (tree t)
{
  struct lang_decl *ld;
  size_t size;
  int sel;

  if (DECL_LANG_SPECIFIC (t))
    return;

  if (TREE_CODE (t) == FUNCTION_DECL)
    sel = 1, size = sizeof (struct lang_decl_fn);
  else if (TREE_CODE (t) == NAMESPACE_DECL)
    sel = 2, size = sizeof (struct lang_decl_ns);
  else if (TREE_CODE (t) == PARM_DECL)
    sel = 3, size = sizeof (struct lang_decl_parm);
  else if (LANG_DECL_HAS_MIN (t))
    sel = 0, size = sizeof (struct lang_decl_min);
  else
    gcc_unreachable ();

  ld = (struct lang_decl *) ggc_internal_cleared_alloc (size);

  ld->u.base.selector = sel;

  DECL_LANG_SPECIFIC (t) = ld;
  if (current_lang_name == lang_name_cplusplus
      || decl_linkage (t) == lk_none)
    SET_DECL_LANGUAGE (t, lang_cplusplus);
  else if (current_lang_name == lang_name_c)
    SET_DECL_LANGUAGE (t, lang_c);
  else if (current_lang_name == lang_name_java)
    SET_DECL_LANGUAGE (t, lang_java);
  else
    gcc_unreachable ();

  if (GATHER_STATISTICS)
    {
      tree_node_counts[(int)lang_decl] += 1;
      tree_node_sizes[(int)lang_decl] += size;
    }
}

void
cxx_dup_lang_specific_decl (tree node)
{
  int size;
  struct lang_decl *ld;

  if (! DECL_LANG_SPECIFIC (node))
    return;

  if (TREE_CODE (node) == FUNCTION_DECL)
    size = sizeof (struct lang_decl_fn);
  else if (TREE_CODE (node) == NAMESPACE_DECL)
    size = sizeof (struct lang_decl_ns);
  else if (TREE_CODE (node) == PARM_DECL)
    size = sizeof (struct lang_decl_parm);
  else if (LANG_DECL_HAS_MIN (node))
    size = sizeof (struct lang_decl_min);
  else
    gcc_unreachable ();

  ld = (struct lang_decl *) ggc_internal_alloc (size);
  memcpy (ld, DECL_LANG_SPECIFIC (node), size);
  DECL_LANG_SPECIFIC (node) = ld;

  if (GATHER_STATISTICS)
    {
      tree_node_counts[(int)lang_decl] += 1;
      tree_node_sizes[(int)lang_decl] += size;
    }
}

/* Copy DECL, including any language-specific parts.  */

tree
copy_decl (tree decl)
{
  tree copy;

  copy = copy_node (decl);
  cxx_dup_lang_specific_decl (copy);
  return copy;
}

/* Replace the shared language-specific parts of NODE with a new copy.  */

static void
copy_lang_type (tree node)
{
  int size;
  struct lang_type *lt;

  if (! TYPE_LANG_SPECIFIC (node))
    return;

  if (TYPE_LANG_SPECIFIC (node)->u.h.is_lang_type_class)
    size = sizeof (struct lang_type);
  else
    size = sizeof (struct lang_type_ptrmem);
  lt = (struct lang_type *) ggc_internal_alloc (size);
  memcpy (lt, TYPE_LANG_SPECIFIC (node), size);
  TYPE_LANG_SPECIFIC (node) = lt;

  if (GATHER_STATISTICS)
    {
      tree_node_counts[(int)lang_type] += 1;
      tree_node_sizes[(int)lang_type] += size;
    }
}

/* Copy TYPE, including any language-specific parts.  */

tree
copy_type (tree type)
{
  tree copy;

  copy = copy_node (type);
  copy_lang_type (copy);
  return copy;
}

tree
cxx_make_type (enum tree_code code)
{
  tree t = make_node (code);

  /* Create lang_type structure.  */
  if (RECORD_OR_UNION_CODE_P (code)
      || code == BOUND_TEMPLATE_TEMPLATE_PARM)
    {
      struct lang_type *pi
          = (struct lang_type *) ggc_internal_cleared_alloc
	  (sizeof (struct lang_type));

      TYPE_LANG_SPECIFIC (t) = pi;
      pi->u.c.h.is_lang_type_class = 1;

      if (GATHER_STATISTICS)
	{
	  tree_node_counts[(int)lang_type] += 1;
	  tree_node_sizes[(int)lang_type] += sizeof (struct lang_type);
	}
    }

  /* Set up some flags that give proper default behavior.  */
  if (RECORD_OR_UNION_CODE_P (code))
    {
      struct c_fileinfo *finfo = \
	get_fileinfo (LOCATION_FILE (input_location));
      SET_CLASSTYPE_INTERFACE_UNKNOWN_X (t, finfo->interface_unknown);
      CLASSTYPE_INTERFACE_ONLY (t) = finfo->interface_only;
    }

  return t;
}

tree
make_class_type (enum tree_code code)
{
  tree t = cxx_make_type (code);
  SET_CLASS_TYPE_P (t, 1);
  return t;
}

/* Returns true if we are currently in the main source file, or in a
   template instantiation started from the main source file.  */

bool
in_main_input_context (void)
{
  struct tinst_level *tl = outermost_tinst_level();

  if (tl)
    return filename_cmp (main_input_filename,
			 LOCATION_FILE (tl->locus)) == 0;
  else
    return filename_cmp (main_input_filename, LOCATION_FILE (input_location)) == 0;
}
