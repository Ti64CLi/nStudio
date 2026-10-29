/*
 * nasmlink.h
 * How nStudio calls nasm, and the rules it shares with it.
 *
 * Each function here mirrors something nasm does - the command line it parses,
 * the name it gives the program it builds, the way it reads and resolves an
 * INCLUDE - so they are kept apart from the editor: the unit tests cover them
 * on the host, and integration/ checks them against nasm itself.  A change
 * here, or on nasm's side of any of these rules, must be made on both.
 */
#ifndef NASMLINK_H_INCLUDED
#define NASMLINK_H_INCLUDED

#include <stddef.h>

/* Where nasm writes its JSON diagnostics for nStudio to read back (see
   asmdiag.h for the contract).  Lives in the shared Ndless config directory,
   which already holds nstudio.cfg. */
#define NASMLINK_DIAG_FILE "/documents/ndless/nstudio_diag.json"

/*
 * Where nasm will put the program it builds from `src`.
 *
 * This mirrors nasm's own make_outpath() exactly: every dot-extension is
 * stripped from the file name - it cuts at the FIRST dot, not the last - and
 * ".tns" is appended, so /documents/foo/bar.asm.tns becomes
 * /documents/foo/bar.tns.  nasm derives this itself and offers no way to
 * override it from the command line.  A results report names the program it
 * built, but Ctrl+R needs the name without assembling, and nasm 1.2 reports no
 * name at all, so this copy of the rule stays; keep the two in step.
 *
 * Returns 0 when no usable path exists: an unsaved buffer, a path too long to
 * extend, or a result identical to the source - the last being exactly the case
 * nasm itself refuses to build, so nothing was produced to run.  A name with no
 * dot simply gains ".tns", matching nasm rather than second-guessing it.
 */
int nasmlink_output_path(const char *src, char *out, size_t outsz);

/*
 * The arguments nStudio passes to nl_exec() to assemble a file.  nl_exec takes
 * only the arguments after the program name, so argv[0] here is nasm's
 * argv[1]:
 *
 *   <source> [user args...] --no-ui --report=2 [--format=<format>]
 *            --diagnostics-file=<path> --fb <address>
 *
 * nasm 1.3 then shows nothing and writes a results report; an older nasm
 * ignores those three options and asks and shows as it always has (see
 * asmdiag.h).  The user's extra arguments are split on blanks (no quoting), at
 * most 16 of them, and come first, so nStudio's own options win any clash.
 * `format` is "zehn" or "prg", or NULL to leave the choice to nasm.  `fb` is
 * the caller's framebuffer, which an older nasm copies so the editor stays
 * visible behind its dialogs; it is passed as a decimal address because
 * nl_exec runs nasm in nStudio's own address space.
 */
#define NASMLINK_INJECTED 6 /* the most options nStudio adds itself */
#define NASMLINK_MAX_ARGS (1 + 16 + NASMLINK_INJECTED) /* + source, user */

typedef struct {
  char *argv[NASMLINK_MAX_ARGS];
  int argc;
  char source[512];
  char user_args[128]; /* the user's arguments, split in place */
  char format_arg[16]; /* "--format=<format>" */
  char diag_arg[600];  /* "--diagnostics-file=<path>" */
  char fb_arg[32];     /* the framebuffer address, in decimal */
} NasmInvocation;

/* Fill `inv` for assembling `src`.  Returns 0 when the source or diagnostics
   path is too long to pass whole (nothing truncated is ever passed), or the
   format is not one nasm knows. */
int nasmlink_invocation(NasmInvocation *inv, const char *src,
                        const char *user_args, const char *diag_path,
                        const char *format, const void *fb);

/*
 * If `line` is an INCLUDE (or GET) directive, copy its operand into `out` and
 * return its length; otherwise return 0.  The line is read as nasm reads it:
 * the comment starts at the first ';' outside "..." or a 'c' literal, a word
 * in column 0 is a label, the directive is matched in any case, and the
 * operand is the rest of the line, trimmed.  nasm takes it verbatim - no
 * quotes are removed and no extension is added - so on the calculator the
 * directive names the file in full, ".tns" and all.
 */
int nasmlink_include_operand(const char *line, char *out, int outsz);

/*
 * Resolve an INCLUDE operand against the file containing the directive, as
 * nasm's abspath() does: an absolute path stands, a leading "./" is skipped
 * and each leading "../" climbs one directory; anything else in the operand
 * is kept as written.  Returns 0 if the result would not fit, rather than
 * forming a truncated path that would then be reported as missing.
 */
int nasmlink_resolve_include(const char *includer, const char *operand,
                             char *out, int outsz);

#endif /* NASMLINK_H_INCLUDED */
