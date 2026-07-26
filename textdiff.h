/*
 * textdiff.h
 * Minimal changed span between two versions of a text buffer.
 *
 * The undo history stores what an edit changed rather than a copy of the whole
 * file, and this is how that change is derived: given the text before and after
 * an edit, it reports the one region that differs.  Keeping it a pure function
 * over plain byte arrays - no gap buffer, no editor state - is what makes it
 * testable on the host, which matters because a wrong answer here would corrupt
 * the user's text on undo.
 */
#ifndef TEXTDIFF_H_INCLUDED
#define TEXTDIFF_H_INCLUDED

/*
 * Locate the single region in which `a` (length `alen`) and `b` (length `blen`)
 * differ, by stripping their common prefix and common suffix.
 *
 * Writes to the out-params:
 *   *out_pos   offset where the two first differ (0 <= pos <= alen, blen)
 *   *out_alen  bytes of `a` in the differing region
 *   *out_blen  bytes of `b` in the differing region
 *
 * So `a` becomes `b` by replacing a[pos .. pos+*out_alen) with
 * b[pos .. pos+*out_blen).  Identical buffers yield all-zero lengths.  Both
 * regions are minimal: the prefix scan runs first and the suffix scan is
 * bounded so it can never overlap it, which is what keeps repeated characters
 * ("aa" becoming "aaa") from producing a nonsensical negative-length span.
 *
 * NUL bytes are ordinary data; lengths are explicit throughout.
 */
void buf_diff_span(const char *a, int alen, const char *b, int blen,
                   int *out_pos, int *out_alen, int *out_blen);

#endif /* TEXTDIFF_H_INCLUDED */
