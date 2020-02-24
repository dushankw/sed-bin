#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

#include "operations.h"
#include "status.h"

static int expand_replace(
    char *replace_expanded,
    const char *pattern_space,
    const char *replace,
    const regmatch_t *pmatch) {
  const int replace_len = strlen(replace);
  bool found_backslash = false;
  int replace_expanded_index = 0;
  for (int replace_index = 0; replace_index < replace_len; ++replace_index) {
    const char replace_char = replace[replace_index];
    switch (replace_char) {
      case '\\':
        // double backslash case
        if (found_backslash) {
          replace_expanded[replace_expanded_index++] = '\\';
        }
        found_backslash = !found_backslash;
        break;
      case '&':
        if (!found_backslash) {
          const int so = pmatch[0].rm_so;
          const int eo = pmatch[0].rm_eo;
          memmove(
            replace_expanded + replace_expanded_index,
            pattern_space + so,
            eo
          );
          replace_expanded_index += eo - so;
        } else {
          replace_expanded[replace_expanded_index++] = replace_char;
          found_backslash = 0;
        }
        break;
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (found_backslash) {
          const char back_ref_index = replace_char - '0';
          const int so = pmatch[back_ref_index].rm_so;
          if (so == -1) {
            return -1;
          }
          const int eo = pmatch[back_ref_index].rm_eo;
          memmove(
            replace_expanded + replace_expanded_index,
            pattern_space + so,
            eo
          );
          replace_expanded_index += eo - so;
          found_backslash = 0;
        } else {
          replace_expanded[replace_expanded_index++] = replace_char;
        }
        break;
      default:
        replace_expanded[replace_expanded_index++] = replace_char;
        if (found_backslash) {
          // ignore for now, at some point it might be nice to handle \x, \o, \n
          // etc.
          found_backslash = 0;
        }
        break;
    }
  }
  return replace_expanded_index;
}

static int substitution(
    regex_t *regex,
    Status *status,
    char *pattern_space,
    const char *pattern,
    const char *replace
    )
{
  regmatch_t pmatch[MAX_MATCHES];
  if (regexec(regex, pattern_space, MAX_MATCHES, pmatch, 0)) {
    regfree(regex);
    return 0;
  }

  status->sub_success = true;

  const int pattern_space_len = strlen(pattern_space);
  const int so = pmatch[0].rm_so; // start offset
  if (so != -1 && pmatch[0].rm_eo == 0 ) {
    // Emtpy match, example: echo foo bar | sed 's/[^ ]*/yo/g'
    // Here foo is replaced by yo, then we move to ' bar', which is an empty
    // match, the regex matches but does not eat any char, in that case we can
    // just go to the next char an try a new match.
    return 1;
  }
  const int eo = pmatch[0].rm_eo; // end offset

  char replace_expanded[512]; // TODO abitrary size, might be too small
  const int replace_expanded_len =
    expand_replace(replace_expanded, pattern_space, replace, pmatch);

  int po;
  int ro;

  for (po = so, ro = 0; po < eo && ro < replace_expanded_len; ++po, ++ro) {
    pattern_space[po] = replace_expanded[ro];
  }

  if (po < eo) {
    // Matched part was longer than replaced part, let's shift the rest to the
    // left.
    memmove(
      pattern_space + po,
      pattern_space + eo,
      pattern_space_len - po
    );
    return po;
  } else if (ro < replace_expanded_len) {
    memmove(
      pattern_space + eo + replace_expanded_len - ro,
      pattern_space + eo,
      pattern_space_len - eo
    );
    memmove(
      pattern_space + eo,
      replace_expanded + ro,
      replace_expanded_len - ro
    );

    pattern_space[pattern_space_len + replace_expanded_len - (eo - so)] = 0;
    return so + replace_expanded_len;
  }
  return eo;
}

void s(
  Status *status,
  const char *pattern,
  const char *replace,
  const int opts)
{
  regex_t regex;

  // FIXME we should compile only once, both loops and each line processed can
  // lead to compiling the same regex many times
  if (regcomp(&regex, pattern, 0)) {
    regfree(&regex);
    assert(false);
  }

  // TODO nth/p/w opts
  const bool opt_g = opts & S_OPT_G;

  char *pattern_space = status->pattern_space;
  int pattern_offset = 0;
  do {
    pattern_offset = substitution(
      &regex,
      status,
      pattern_space,
      pattern,
      replace
    );
    pattern_space += pattern_offset;
  } while (opt_g && pattern_space[0] && pattern_offset);
  regfree(&regex);
}

void x(Status *status) {
  char *pattern_space = status->pattern_space;
  char *hold_space = status->hold_space;
  status->pattern_space = hold_space;
  status->hold_space = pattern_space;
}

void g(Status *status) {
  char *pattern_space = status->pattern_space;
  const char *hold_space = status->hold_space;
  memcpy(
    pattern_space,
    hold_space,
    strlen(hold_space)
  );
}

void h(Status *status) {
  const char *pattern_space = status->pattern_space;
  char *hold_space = status->hold_space;
  memcpy(
    hold_space,
    pattern_space,
    strlen(pattern_space)
  );
}

void p(const Status *status) {
  const char *pattern_space = status->pattern_space;
  puts(pattern_space);
}