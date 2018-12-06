/* Generated by re2c 1.1.1 on Thu Dec  6 21:59:06 2018 */

#include <string.h>

namespace re2c {

#define YYMAXFILL 18


struct ScannerState
{
    enum lexer_state_t {LEX_NORMAL, LEX_FLEX_NAME};
    lexer_state_t lexer_state;

    size_t BSIZE;
    char *bot, *lim, *cur, *mar, *ctx, *tok, *ptr, *pos, *eof;
    char *yyt1;

    ptrdiff_t tchar;
    uint32_t cline;

    inline ScannerState()
        : lexer_state (LEX_NORMAL)
        , BSIZE(8192)
        , bot(new char[BSIZE + YYMAXFILL])
        , lim(bot + BSIZE)
        , cur(lim)
        , mar(lim)
        , ctx(lim)
        , tok(lim)
        , ptr(lim)
        , pos(lim)
        , eof(NULL)
        , yyt1(lim)
        , tchar(0)
        , cline(1)
    {
        memset(lim, 0, YYMAXFILL);
    }

    inline ~ScannerState() { delete[] bot; }

    inline void shift_ptrs(ptrdiff_t offs)
    {
        lim += offs;
        cur += offs;
        mar += offs;
        ctx += offs;
        tok += offs;
        ptr += offs;
        pos += offs;
        yyt1 += offs;
    }

    FORBID_COPY(ScannerState);
};
} // namespace re2c
