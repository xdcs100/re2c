#include "lib/lex.h"
#include "lib/regex.h"
#include "lib/regex_impl.h"
#include "src/options/opt.h"
#include "src/debug/debug.h"
#include "src/dfa/closure_leftmost.h"
#include "src/dfa/determinization.h"
#include "src/nfa/nfa.h"


namespace re2c {
namespace libre2c {

static void reach_on_symbol(lsimctx_t &, uint32_t);
static void update_offsets(lsimctx_t &ctx, const conf_t &c, uint32_t id);

int regexec_nfa_leftmost(const regex_t *preg, const char *string
    , size_t nmatch, regmatch_t pmatch[], int)
{
    lsimctx_t &ctx = *static_cast<lsimctx_t*>(preg->simctx);
    init(ctx, string);

    // root state can be non-core, so we pass zero as origin to avoid checks
    const conf_t c0(ctx.nfa.root, 0, HROOT);
    ctx.reach.push_back(c0);

    for (;;) {
        closure_leftmost_dfs(ctx);
        const uint32_t sym = static_cast<uint8_t>(*ctx.cursor++);
        if (ctx.state.empty() || sym == 0) break;
        reach_on_symbol(ctx, sym);
    }

    for (cconfiter_t i = ctx.state.begin(), e = ctx.state.end(); i != e; ++i) {
        nfa_state_t *s = i->state;
        s->clos = NOCLOS;
        if (s->type == nfa_state_t::FIN) {
            update_offsets(ctx, *i, NONCORE);
        }
    }

    if (ctx.rule == Rule::NONE) {
        return REG_NOMATCH;
    }

    regmatch_t *m = pmatch;
    m->rm_so = 0;
    m->rm_eo = ctx.marker - string - 1;
    const size_t n = std::min(ctx.nsub, 2 * nmatch);
    for (size_t t = 0; t < n; ++t) {
        const regoff_t off = ctx.offsets3[t];
        if (t % 2 == 0) {
            ++m;
            m->rm_so = off;
        }
        else {
            m->rm_eo = off;
        }
    }

    return 0;
}

void reach_on_symbol(lsimctx_t &ctx, uint32_t sym)
{
    const confset_t &state = ctx.state;
    confset_t &reach = ctx.reach;
    DASSERT(reach.empty());

    // in reverse, so that future closure DFS has states in stack order
    uint32_t j = 0;
    for (rcconfiter_t i = state.rbegin(), e = state.rend(); i != e; ++i) {
        nfa_state_t *s = i->state;
        s->clos = NOCLOS;

        if (s->type == nfa_state_t::RAN) {
            for (const Range *r = s->ran.ran; r; r = r->next()) {
                if (r->lower() <= sym && sym < r->upper()) {
                    conf_t c(s->ran.out, j, HROOT);
                    reach.push_back(c);
                    update_offsets(ctx, *i, j);
                    ++j;
                    break;
                }
            }
        }
        else if (s->type == nfa_state_t::FIN) {
            update_offsets(ctx, *i, NONCORE);
        }
    }

    std::swap(ctx.offsets1, ctx.offsets2);
    ctx.history.init();
    ++ctx.step;
}

void update_offsets(lsimctx_t &ctx, const conf_t &c, uint32_t id)
{
    const size_t nsub = ctx.nsub;
    bool *done = ctx.done;
    nfa_state_t *s = c.state;
    regoff_t *o;

    if (s->type == nfa_state_t::FIN) {
        ctx.marker = ctx.cursor;
        ctx.rule = 0;
        o = ctx.offsets3;
    }
    else {
        o = ctx.offsets1 + id * nsub;
    }

    memcpy(o, ctx.offsets2 + c.origin * nsub, nsub * sizeof(regoff_t));
    memset(done, 0, nsub * sizeof(bool));

    for (int32_t i = c.thist; i != HROOT; ) {
        const lhistory_t::node_t &n = ctx.history.node(i);
        const size_t t = n.info.idx;
        if (!done[t]) {
            done[t] = true;
            o[t] = n.info.neg ? -1 : static_cast<regoff_t>(ctx.step);
        }
        i = n.pred;
    }
}

} // namespace libre2
} // namespace re2c

