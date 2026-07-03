/* test_diag_suggest.c — C2-2 did-you-mean unit tests (diag_suggest):
   unique near-miss hits; ties and over-threshold misses stay silent. */
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } \
} while(0)

/* Candidate iterator over a NULL-terminated string array. */
typedef struct { const char **p; } ArrIter;

static const char *arr_next(void *ctx)
{
    ArrIter *it = (ArrIter *)ctx;
    return *it->p ? *it->p++ : NULL;
}

static const char *suggest(const char *bad, const char **cands)
{
    ArrIter it = { cands };
    return diag_suggest(bad, arr_next, &it);
}

int main(void)
{
    /* Hit: transposition (Damerau), unique winner. */
    {
        const char *cands[] = { "width", "length", "height", NULL };
        const char *s = suggest("lenght", cands);
        ASSERT(s != NULL && strcmp(s, "length") == 0, "transposition hit");
    }
    /* Hit: single deletion. */
    {
        const char *cands[] = { "Vec", "Map", NULL };
        const char *s = suggest("Vecc", cands);
        ASSERT(s != NULL && strcmp(s, "Vec") == 0, "deletion hit");
    }
    /* Hit: substitution within threshold 2 for a long name. */
    {
        const char *cands[] = { "parallel_for", NULL };
        const char *s = suggest("paralel_for", cands);
        ASSERT(s != NULL && strcmp(s, "parallel_for") == 0, "long-name hit");
    }
    /* Tie between two distinct candidates: no suggestion. */
    {
        const char *cands[] = { "lena", "lenb", NULL };
        ASSERT(suggest("lenn", cands) == NULL, "tie must stay silent");
    }
    /* Duplicate spellings of the same name are not a tie. */
    {
        const char *cands[] = { "length", "length", NULL };
        const char *s = suggest("lenght", cands);
        ASSERT(s != NULL && strcmp(s, "length") == 0, "duplicate not a tie");
    }
    /* Over threshold: distance 3 with limit 2. */
    {
        const char *cands[] = { "omega", NULL };
        ASSERT(suggest("alpha", cands) == NULL, "over threshold stays silent");
    }
    /* Short names (len < 3 -> limit 0): never suggest. */
    {
        const char *cands[] = { "ax", NULL };
        ASSERT(suggest("ab", cands) == NULL, "short names never suggest");
    }
    /* A closer candidate later in the stream overrides an earlier tie:
       lengte/lengts tie at distance 2, then length wins uniquely at 1. */
    {
        const char *cands[] = { "lengte", "lengts", "length", NULL };
        const char *s = suggest("lenght", cands);
        ASSERT(s != NULL && strcmp(s, "length") == 0, "closer candidate wins over tie");
    }
    /* Empty candidate stream. */
    {
        const char *cands[] = { NULL };
        ASSERT(suggest("lenght", cands) == NULL, "empty stream");
    }

    printf("test_diag_suggest: all passed\n");
    return 0;
}
