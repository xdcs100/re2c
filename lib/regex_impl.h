#ifndef _RE2C_LIB_REGEX_IMPL_
#define _RE2C_LIB_REGEX_IMPL_

#include "regex.h"
#include <stddef.h>
#include <map>
#include <vector>
#include <queue>

#include "src/dfa/determinization.h"
#include "src/nfa/nfa.h"


namespace re2c {
namespace libre2c {

typedef std::vector<tag_info_t> tag_path_t;

struct conf_t
{
    nfa_state_t *state;
    uint32_t origin;
    int32_t thist;

    inline conf_t(): state(NULL), origin(0), thist(HROOT) {}
    inline conf_t(nfa_state_t *s, uint32_t o, int32_t h)
        : state(s), origin(o), thist(h) {}
    inline conf_t(const conf_t &c, nfa_state_t *s)
        : state(s), origin(c.origin), thist(c.thist) {}
    inline conf_t(const conf_t &c, nfa_state_t *s, int32_t h)
        : state(s), origin(c.origin), thist(h) {}
};

struct ran_or_fin_t
{
    inline bool operator()(const conf_t &c);
};

typedef std::vector<conf_t> confset_t;
typedef confset_t::iterator confiter_t;
typedef confset_t::const_iterator cconfiter_t;
typedef confset_t::const_reverse_iterator rcconfiter_t;

template<typename history_type_t>
struct simctx_t
{
    typedef libre2c::conf_t conf_t;
    typedef std::vector<conf_t> confset_t;
    typedef confset_t::iterator confiter_t;
    typedef confset_t::const_iterator cconfiter_t;
    typedef confset_t::reverse_iterator rconfiter_t;
    typedef confset_t::const_reverse_iterator rcconfiter_t;
    typedef history_type_t history_t;

    const nfa_t &nfa;
    const nfa_t *nfa0;
    const size_t nsub;
    const int flags;

    history_t history;
    int32_t hidx;

    uint32_t step;

    size_t rule;

    const char *cursor;
    const char *marker;

    regoff_t *offsets1;
    regoff_t *offsets2;
    regoff_t *offsets3;
    bool *done;

    int32_t *newprectbl;
    int32_t *oldprectbl;
    size_t oldprecdim;
    histleaf_t *histlevel;
    std::vector<uint32_t> sortcores;
    std::vector<uint32_t> fincount;
    std::vector<int32_t> worklist;
    std::vector<cconfiter_t> stateiters;

    confset_t reach;
    confset_t state;
    std::vector<nfa_state_t*> gor1_topsort;
    std::vector<nfa_state_t*> gor1_linear;
    std::vector<nfa_state_t*> gtop_heap_storage;
    cmp_gtop_t gtop_cmp;
    gtop_heap_t gtop_heap;
    closure_stats_t dc_clstats;

    simctx_t(const nfa_t &nfa, const nfa_t *nfa0, size_t re_nsub, int flags);
    ~simctx_t();
    FORBID_COPY(simctx_t);
};

// tag history for Kuklewicz disambiguation (POSIX semantics)
struct khistory_t
{
    struct node_t {
        tag_info_t info;
        hidx_t pred;

        inline node_t(tag_info_t info, hidx_t pred)
            : info(info), pred(pred) {}
    };

    std::vector<node_t> nodes;
    std::vector<int32_t> path1;
    std::vector<int32_t> path2;

    inline khistory_t(): nodes(), path1(), path2() { init(); }
    inline void init();
    inline node_t &node(hidx_t i) { return nodes[static_cast<uint32_t>(i)]; }
    inline const node_t &node(hidx_t i) const { return nodes[static_cast<uint32_t>(i)]; }
    template<typename ctx_t> inline hidx_t link(ctx_t &ctx
        , const typename ctx_t::conf_t &conf);
    template<typename ctx_t> static int32_t precedence(ctx_t &ctx
        , const typename ctx_t::conf_t &x, const typename ctx_t::conf_t &y
        , int32_t &prec1, int32_t &prec2);
    FORBID_COPY(khistory_t);
};

typedef simctx_t<phistory_t> psimctx_t;
typedef simctx_t<lhistory_t> lsimctx_t;
typedef simctx_t<zhistory_t> pzsimctx_t;
typedef simctx_t<zhistory_t> lzsimctx_t;
typedef simctx_t<khistory_t> ksimctx_t;

int regexec_dfa(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
int regexec_nfa_posix(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
int regexec_nfa_posix_trie(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
int regexec_nfa_posix_backward(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
int regexec_nfa_posix_kuklewicz(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
int regexec_nfa_leftmost(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);
int regexec_nfa_leftmost_trie(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);

template<typename history_t>
simctx_t<history_t>::simctx_t(const nfa_t &nfa, const nfa_t *nfa0, size_t re_nsub, int flags)
    : nfa(nfa)
    , nfa0(nfa0)
    , nsub(2 * (re_nsub - 1))
    , flags(flags)
    , history()
    , hidx(HROOT)
    , step(0)
    , rule(Rule::NONE)
    , cursor(NULL)
    , marker(NULL)
    , offsets1(NULL)
    , offsets2(NULL)
    , offsets3(NULL)
    , done(NULL)
    , newprectbl(NULL)
    , oldprectbl(NULL)
    , oldprecdim(0)
    , histlevel(NULL)
    , sortcores()
    , fincount()
    , worklist()
    , stateiters()
    , reach()
    , state()
    , gor1_topsort()
    , gor1_linear()
    , gtop_heap_storage()
    , gtop_cmp()
    , gtop_heap(gtop_cmp, gtop_heap_storage)
    , dc_clstats()
{
    const size_t
        ntags = nfa.tags.size(),
        nstates = nfa.size,
        ncores = nfa.ncores;

    state.reserve(nstates);
    reach.reserve(nstates);

    done = new bool[nsub];

    if (!(flags & REG_TRIE)) {
        offsets1 = new regoff_t[nsub * ncores];
        offsets2 = new regoff_t[nsub * ncores];
        offsets3 = new regoff_t[nsub];
    }
    if (!(flags & REG_LEFTMOST) && !(flags & REG_TRIE)) {
        const size_t dim = (flags & REG_KUKLEWICZ) ? ntags : ncores;
        newprectbl = new int32_t[ncores * dim];
        oldprectbl = new int32_t[ncores * dim];
        histlevel = new histleaf_t[ncores];
        sortcores.reserve(ncores);
        fincount.resize(ncores + 1);
        worklist.reserve(nstates);
    }
    if (flags & REG_KUKLEWICZ) {
        stateiters.reserve(ncores);
    }

    if (flags & REG_GTOP) {
        gtop_heap_storage.reserve(nstates);
    }
    else {
        gor1_topsort.reserve(nstates);
        gor1_linear.reserve(nstates);
    }
}

template<typename history_t>
simctx_t<history_t>::~simctx_t()
{
    delete[] done;
    if (!(flags & REG_TRIE)) {
        delete[] offsets1;
        delete[] offsets2;
        delete[] offsets3;
    }
    if (!(flags & REG_LEFTMOST) && !(flags & REG_TRIE)) {
        delete[] newprectbl;
        delete[] oldprectbl;
        delete[] histlevel;
    }
    if (flags & REG_BACKWARD) {
        delete &nfa0->charset;
        delete &nfa0->rules;
        delete &nfa0->tags;
        delete nfa0;
    }
}

template<typename history_t>
void init(simctx_t<history_t> &ctx, const char *string)
{
    ctx.reach.clear();
    ctx.state.clear();
    ctx.history.init();
    ctx.hidx = HROOT;
    ctx.step = 0;
    ctx.rule = Rule::NONE;
    ctx.cursor = ctx.marker = string;
    ctx.sortcores.clear();
    DASSERT(ctx.worklist.empty());
    DASSERT(ctx.gor1_topsort.empty());
    DASSERT(ctx.gor1_linear.empty());
    DASSERT(ctx.gtop_heap.empty());
}

template<typename history_t>
int finalize(const simctx_t<history_t> &ctx, const char *string, size_t nmatch,
    regmatch_t pmatch[])
{
    if (ctx.rule == Rule::NONE) {
        return REG_NOMATCH;
    }

    regmatch_t *m = pmatch;
    m->rm_so = 0;
    m->rm_eo = ctx.marker - string - 1;

    const std::vector<Tag> &tags = ctx.nfa.tags;
    size_t todo = nmatch * 2;
    bool *done = ctx.done;
    memset(done, 0, ctx.nsub * sizeof(bool));

    for (int32_t i = ctx.hidx; todo > 0 && i != HROOT; ) {
        const typename history_t::node_t &n = ctx.history.node(i);
        const Tag &tag = tags[n.info.idx];
        const size_t t = tag.ncap;
        if (!fictive(tag) && t < nmatch * 2 && !done[t]) {
            done[t] = true;
            --todo;
            const regoff_t off = n.info.neg ? -1
                : static_cast<regoff_t>(n.step);
            m = &pmatch[t / 2 + 1];
            if (t % 2 == 0) {
                m->rm_so = off;
            }
            else {
                m->rm_eo = off;
            }
        }
        i = n.pred;
    }

    return 0;
}

bool ran_or_fin_t::operator()(const conf_t &c)
{
    switch (c.state->type) {
        case nfa_state_t::RAN:
        case nfa_state_t::FIN: return true;
        default: return false;
    }
}

void khistory_t::init()
{
    nodes.clear();
    nodes.push_back(node_t(NOINFO, -1));
}

template<typename ctx_t>
hidx_t khistory_t::link(ctx_t &/* ctx */, const typename ctx_t::conf_t &conf)
{
    const int32_t i = static_cast<int32_t>(nodes.size());
    nodes.push_back(node_t(conf.state->tag.info, conf.thist));
    return i;
}

} // namespace libre2c
} // namespace re2c

#endif // _RE2C_LIB_REGEX_IMPL_
