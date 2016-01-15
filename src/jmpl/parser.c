/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/mm.h>
#include <iot/common/list.h>
#include <iot/common/debug.h>

#include "jmpl/jmpl.h"
#include "jmpl/parser.h"



static void free_reference(jmpl_ref_t *r)
{
    if (r == NULL)
        return;

    iot_free(r->ids);
    iot_free(r);
}


static jmpl_ref_t *parse_reference(char *val)
{
    jmpl_ref_t *r;
    int32_t    *ids, id;
    int         nid;
    char       *p, *b, *e, *n, *end, *dot, *idx;

    /*
     * Parse a JSON variable reference into an internal representation.
     *
     * Internally we represent variable references as a series of tagged
     * ids. Each id contains a tag and an index. The tag denotes what
     * type of information is encoded into the index: a string (field or
     * string index), or an integer index. Values of strings are interned
     * into a symbol table for fast lookup, and the symbol table index is
     * used as the index for id. Integer indices are used as such as the
     * index for id.
     */

    ids = NULL;
    nid = 0;

    p = val;
    while (p && *p) {
        iot_debug("@ '%s'", p);

        dot = strchr(p + 1, '.');
        idx = strchr(p + 1, '[');

        switch (*p) {
        case '.':
        default:
            b = (*p == '.' ? p + 1 : p);

            if (dot && idx) {
                if (dot < idx)
                    idx = NULL;
                else
                    dot = NULL;
            }

            if (dot)
                *dot = '\0';
            else if (idx)
                *idx = '\0';

            id = symtab_add(b, JMPL_SYMBOL_FIELD);

            if (id < 0)
                goto fail;

            iot_debug("symbol '%s' => 0x%x", b, id);

            if (dot) {
                *dot = '.';
                p    = dot;
            }
            else if (idx) {
                *idx = '[';
                p    = idx;
            }
            else
                p = NULL;

            break;

        case '[':
            b = p + 1;
            e = strchr(b, ']');

            if (e == NULL || e == b)
                goto invalid;

            n = e + 1;

            if (*b == '\'' || *b == '"') {
                e--;

                *e = '\0';
                id = symtab_add(b + 1, JMPL_SYMBOL_FIELD);
                iot_debug("symbol '%s' => 0x%x", b + 1, id);
                *e = *b;

                if (id < 0)
                    goto fail;

            }
            else {
                id = strtol(b, &end, 10);

                if (id < 0 || end != e)
                    goto invalid;

                iot_debug("index '%.*s' => 0x%x", (int)(e - b), b, id);
            }

            p = n;
            break;
        }

        if (iot_reallocz(ids, nid, nid + 1) == NULL)
            goto fail;

        ids[nid++] = id;
    }

    if (ids == NULL)
        goto invalid;

    r = iot_allocz(sizeof(*r));

    if (r == NULL)
        goto fail;

    r->ids = ids;
    r->nid = nid;

    return r;

 invalid:
    errno = EINVAL;
 fail:
    iot_free(ids);
    return NULL;
}


static inline void *jmpl_alloc(int type, size_t size)
{
    jmpl_any_t *jmpl;

    iot_debug("allocating instruction of type 0x%x, size %zd", type, size);

    jmpl = iot_allocz(size);

    if (jmpl == NULL)
        return NULL;

    jmpl->type = type;
    iot_list_init(&jmpl->hook);

    return jmpl;
}


static int parser_init(jmpl_parser_t *jp, const char *str)
{
    char *p, *end;

    iot_clear(jp);
    iot_list_init(&jp->templates);

    /*
     * Every JSON template file starts with the declaration of the
     * directive markers. The directive markers are used to delimit
     * template directives throughout the template. The directive
     * marker declaration is the first line of the template and is
     * a single line consisting of:
     *
     *   - the beginning marker,
     *   - the end marker,
     *   - an optional tabulation marker,
     *
     * All of these are separated from each other by whitespace.
     * single whitespace.
     *
     * Any character string is allowed as a marker with the following
     * limitations:
     *
     *   - a marker cannot contain any whitespace or a newline
     *   - a marker cannot be a substring of the other markers
     */

    str = skip_whitespace((char *)str, true);
    end = next_newline((char *)str);

    if (!str || !*str || !end || !*end)
        goto invalid;

    end++;

    jp->buf    = iot_strdup(end);
    jp->tokens = iot_allocz(strlen(end) + 1);
    jp->mbeg   = iot_strndup(str, end - str - 1);

    if (jp->buf == NULL || jp->tokens == NULL || jp->mbeg == NULL)
        goto nomem;

    p = jp->mbeg;
    p = next_whitespace(p, false);

    if (!*p)
        goto invalid;

    *p++ = '\0';

    p = skip_whitespace(p, false);

    if (!*p)
        goto invalid;

    jp->mend = p;

    p = next_whitespace(p, false);

    if (*p) {
        *p++ = '\0';

        p = skip_whitespace(p, false);

        if (*p)
            jp->mtab = p;

        p = next_whitespace(p, false);

        if (*p)
            *p = '\0';
    }

    jp->lbeg = strlen(jp->mbeg);
    jp->lend = strlen(jp->mend);
    jp->ltab = jp->mtab ? strlen(jp->mtab) : 0;

    jp->p = jp->buf;
    jp->t = jp->tokens;

    return 0;

 invalid:
    jp->error = "invalid directive markers, or no template data";
    errno = EINVAL;
 nomem:
    iot_free(jp->buf);
    iot_free(jp->tokens);
    iot_free(jp->mbeg);

    return -1;
}


static void parser_exit(jmpl_parser_t *jp)
{
    iot_free(jp->mbeg);
    iot_free(jp->buf);
    iot_free(jp->tokens);
}


static jmpl_insn_t *parse_ifset(jmpl_parser_t *jp);
static jmpl_insn_t *parse_if(jmpl_parser_t *jp);
static jmpl_insn_t *parse_foreach(jmpl_parser_t *jp);
static jmpl_insn_t *parse_subst(jmpl_parser_t *jp, char *val);
static jmpl_insn_t *parse_text(jmpl_parser_t *jp, char *val);
static void free_ifset(jmpl_ifset_t *jif);
static void free_if(jmpl_if_t *jif);
static void free_foreach(jmpl_for_t *jif);
static void free_text(jmpl_text_t *jt);
static void free_subst(jmpl_subst_t *jt);
static void free_expr(jmpl_expr_t *expr);
static void free_reference(jmpl_ref_t *r);


static void free_insn(jmpl_insn_t *insn)
{
    if (insn == NULL)
        return;

    switch (insn->any.type) {
    case JMPL_OP_IFSET:   free_ifset(&insn->ifset);     break;
    case JMPL_OP_IF:      free_if(&insn->ifelse);       break;
    case JMPL_OP_FOREACH: free_foreach(&insn->foreach); break;
    case JMPL_OP_TEXT:    free_text(&insn->text);       break;
    case JMPL_OP_SUBST:   free_subst(&insn->subst);     break;
    default:                                            break;
    }
}


static void free_instructions(iot_list_hook_t *l)
{
    iot_list_hook_t *p, *n;
    jmpl_insn_t     *insn;

    iot_list_foreach(l, p, n) {
        insn = iot_list_entry(p, typeof(*insn), any.hook);
        iot_list_delete(p);
        free_insn(insn);
    }
}


static void free_ifset(jmpl_ifset_t *jif)
{
    if (jif == NULL)
        return;

    free_reference(jif->test);
    free_instructions(&jif->tbranch);
    free_instructions(&jif->fbranch);
}


static jmpl_insn_t *parse_ifset(jmpl_parser_t *jp)
{
    iot_list_hook_t *branch;
    jmpl_ifset_t    *jif;
    jmpl_insn_t     *insn;
    char            *val;
    int              tkn, ebr;

    iot_debug("<if-set>");

    jif = jmpl_alloc(JMPL_OP_IFSET, sizeof(*jif));

    if (jif == NULL)
        return NULL;

    iot_list_init(&jif->tbranch);
    iot_list_init(&jif->fbranch);

    tkn = scan_next_token(jp, &val, SCAN_ID);

    if (tkn != JMPL_TKN_ID)
        goto missing_id;

    iot_debug("<id> '%s'", val);

    jif->test = parse_reference(val);

    if (jif->test == NULL)
        goto invalid_id;

    ebr = false;
    branch = &jif->tbranch;

    while ((tkn = scan_next_token(jp, &val, SCAN_IF_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_END:
            iot_debug("<end>");
            return 0;

        case JMPL_TKN_ELSE:
            iot_debug("<else>");

            if (ebr)
                goto unexpected_else;

            ebr = true;
            branch = &jif->fbranch;
            break;

        case JMPL_TKN_IFSET:
            insn = parse_ifset(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_IF:
            insn = parse_if(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_FOREACH:
            insn = parse_foreach(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);

            insn = parse_subst(jp, val);

            if (insn == NULL)
                goto invalid_reference;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);

            insn = parse_text(jp, val);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    return (jmpl_insn_t *)jif;

    return 0;

 missing_id:
 invalid_id:
 unexpected_else:
 unexpected_token:
 invalid_reference:

 parse_error:
    free_ifset(jif);
    return NULL;
}



static void free_expr(jmpl_expr_t *expr)
{
    if (expr == NULL)
        return;

    iot_free(expr);
}


static jmpl_expr_t *parse_expr(jmpl_parser_t *jp)
{
    jmpl_expr_t *expr;
    char        *val;
    int          tkn, lvl;

    expr = iot_allocz(sizeof(*expr));

    tkn = scan_next_token(jp, &val, SCAN_IF_EXPR);

    if (tkn != JMPL_TKN_OPEN)
        goto missing_open;

    lvl = 1;
    while (lvl > 0) {
        tkn = scan_next_token(jp, &val, SCAN_IF_EXPR);

        switch (tkn) {
        case JMPL_TKN_OPEN:
            lvl++;
            iot_debug("(");
            break;

        case JMPL_TKN_CLOSE:
            iot_debug(")");
            lvl--;
            break;

        case JMPL_TKN_STRING:
            iot_debug("<string> '%s'", val);
            break;

        case JMPL_TKN_AND:
            iot_debug("&&");
            break;

        case JMPL_TKN_OR:
            iot_debug("||");
            break;

        case JMPL_TKN_NOT:
            iot_debug("!");
            break;

        case JMPL_TKN_NEQ:
            iot_debug("!=");
            break;

        case JMPL_TKN_EQ:
            iot_debug("==");
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (!parse_subst(jp, val))
                goto invalid_reference;
            break;

        default:
            goto unexpected_token;
        }
    }

    return expr;

 missing_open:
 invalid_reference:
 unexpected_token:
    free_expr(expr);

    return NULL;
}


static void free_if(jmpl_if_t *jif)
{
    if (jif == NULL)
        return;

    free_expr(jif->test);
    free_instructions(&jif->tbranch);
    free_instructions(&jif->fbranch);
}


static jmpl_insn_t *parse_if(jmpl_parser_t *jp)
{
    iot_list_hook_t *branch;
    jmpl_if_t       *jif;
    jmpl_insn_t     *insn;
    char            *val;
    int              tkn, ebr;

    iot_debug("<if>");

    jif = jmpl_alloc(JMPL_OP_IF, sizeof(*jif));

    if (jif == NULL)
        return NULL;

    iot_list_init(&jif->tbranch);
    iot_list_init(&jif->fbranch);

    jif->test = parse_expr(jp);

    if (jif->test == NULL)
        goto invalid_expr;

    ebr = false;
    branch = &jif->tbranch;

    while ((tkn = scan_next_token(jp, &val, SCAN_IF_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_END:
            iot_debug("<end>");
            return 0;

        case JMPL_TKN_ELSE:
            iot_debug("<else>");

            if (ebr)
                goto unexpected_else;

            ebr = true;
            branch = &jif->fbranch;
            break;

        case JMPL_TKN_IFSET:
            insn = parse_ifset(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_IF:
            insn = parse_if(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_FOREACH:
            insn = parse_foreach(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);

            insn = parse_subst(jp, val);

            if (insn == NULL)
                goto invalid_reference;

            iot_list_append(branch, &insn->any.hook);
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);

            insn = parse_text(jp, val);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(branch, &insn->any.hook);
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    return (jmpl_insn_t *)jif;

 invalid_expr:
 unexpected_else:
 unexpected_token:
 invalid_reference:

 parse_error:
    free_if(jif);

    return NULL;
}


static void free_foreach(jmpl_for_t *jfor)
{
    if (jfor == NULL)
        return;

    free_reference(jfor->key);
    free_reference(jfor->val);
    free_instructions(&jfor->body);
}


static jmpl_insn_t *parse_foreach(jmpl_parser_t *jp)
{
    jmpl_for_t  *jfor;
    jmpl_insn_t *insn;
    char        *val, *colon;
    int          tkn;

    iot_debug("<foreach>");

    jfor = jmpl_alloc(JMPL_OP_FOREACH, sizeof(*jfor));

    if (jfor == NULL)
        return NULL;

    iot_list_init(&jfor->body);


    if ((tkn = scan_next_token(jp, &val, SCAN_ID)) != JMPL_TKN_ID)
        goto missing_id;

    iot_debug("<id> '%s'", val);

    colon = strchr(val, ':');

    if (colon == NULL || colon == val) {
        jfor->val = parse_reference(colon ? colon + 1 : val);

        if (jfor->val == NULL)
            goto invalid_valref;
    }
    else {
        *colon = '\0';
        jfor->key = parse_reference(val);
        *colon = ':';

        if (jfor->key == NULL)
            goto invalid_keyref;

        if (colon[1] != '\0') {
            jfor->val = parse_reference(colon + 1);

            if (jfor->val == NULL)
                goto invalid_valref;
        }
    }


    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_IN)
        goto missing_in;

    iot_debug("<in>");

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_SUBST)
        goto missing_inref;

    iot_debug("<subst> '%s'", val);

    jfor->in = parse_reference(val);

    if (jfor->in == NULL)
        goto invalid_inref;

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_DO)
        goto missing_do;

    iot_debug("<do>");

    while ((tkn = scan_next_token(jp, &val, SCAN_FOREACH_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_END:
            iot_debug("<end>");
            return 0;

        case JMPL_TKN_IFSET:
            insn = parse_ifset(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jfor->body, &insn->any.hook);
            break;

        case JMPL_TKN_IF:
            insn = parse_if(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jfor->body, &insn->any.hook);
            break;

        case JMPL_TKN_FOREACH:
            insn = parse_foreach(jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jfor->body, &insn->any.hook);
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);

            insn = parse_subst(jp, val);

            if (insn == NULL)
                goto invalid_reference;

            iot_list_append(&jfor->body, &insn->any.hook);
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);

            insn = parse_text(jp, val);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jfor->body, &insn->any.hook);
            break;

        case JMPL_TKN_EOF:
            goto unexpected_eof;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    return (jmpl_insn_t *)jfor;

 missing_id:
 invalid_keyref:
 invalid_valref:
 missing_in:
 invalid_inref:
 missing_inref:
 missing_do:
 invalid_reference:
 unexpected_eof:
 unexpected_token:

 parse_error:
    free_foreach(jfor);
    errno = EINVAL;

    return NULL;
}


static jmpl_insn_t *parse_escape(jmpl_parser_t *jp, char *val)
{
    jmpl_text_t *jt;

    IOT_UNUSED(jp);

    iot_debug("<escape> '%s'", val);

    if (val[0] != '\\' || val[1] == '\0')
        return NULL;

    switch (val[1]) {
    case 'n': val = "\n"; break;
    case 't': val = "\t"; break;
    case ' ': val = " ";  break;
    default:
        return NULL;
    }

    jt = iot_allocz(sizeof(*jt));

    if (jt == NULL)
        goto nomem;

    iot_list_init(&jt->hook);
    jt->type = JMPL_OP_TEXT;
    jt->text = iot_strdup(val);

    if (jt->text == NULL)
        goto nomem;

    return (jmpl_insn_t *)jt;

 nomem:
    iot_free(jt);
    return NULL;
}


static void free_subst(jmpl_subst_t *js)
{
    if (js == NULL)
        return;

    free_reference(js->ref);
    iot_free(js);
}


static jmpl_insn_t *parse_subst(jmpl_parser_t *jp, char *val)
{
    jmpl_subst_t *js;

    IOT_UNUSED(jp);

    if (val[0] == '\\')
        return parse_escape(jp, val);

    iot_debug("<subst> '%s'", val);

    js = iot_allocz(sizeof(*js));

    if (js == NULL)
        return NULL;

    iot_list_init(&js->hook);
    js->type = JMPL_OP_SUBST;
    js->ref  = parse_reference(val);

    if (js->ref == NULL)
        goto noref;

    return (jmpl_insn_t *)js;

 noref:
    iot_free(js);
    return NULL;
}


static void free_text(jmpl_text_t *jt)
{
    if (jt == NULL)
        return;

    iot_free(jt->text);
    iot_free(jt);
}


static jmpl_insn_t *parse_text(jmpl_parser_t *jp, char *val)
{
    jmpl_text_t *jt;

    IOT_UNUSED(jp);

    iot_debug("<text> '%s'", val);

    jt = iot_allocz(sizeof(*jt));

    if (jt == NULL)
        goto nomem;

    iot_list_init(&jt->hook);
    jt->type = JMPL_OP_TEXT;
    jt->text = iot_strdup(val);

    if (jt->text == NULL)
        goto nomem;

    return (jmpl_insn_t *)jt;

 nomem:
    iot_free(jt);
    return NULL;
}


jmpl_t *jmpl_parse(const char *str)
{
    jmpl_parser_t  jp;
    jmpl_t        *jmpl;
    jmpl_insn_t   *insn;
    char          *val;
    int            tkn;

    jmpl = iot_allocz(sizeof(*jmpl));

    if (jmpl == NULL)
        return NULL;

    jmpl->type = JMPL_OP_MAIN;
    iot_list_init(&jmpl->hook);

    if (parser_init(&jp, str) < 0)
        return NULL;

    iot_debug("begin marker: '%s'", jp.mbeg);
    iot_debug("  end marker: '%s'", jp.mend);
    iot_debug("  tab marker: '%s'", jp.mtab ? jp.mtab : "<none>");
    iot_debug("    template: %s"  , jp.buf);


    while ((tkn = scan_next_token(&jp, &val, SCAN_MAIN)) != JMPL_TKN_EOF) {
        switch (tkn) {
        case JMPL_TKN_ERROR:
        case JMPL_TKN_UNKNOWN:
            goto parse_error;

        case JMPL_TKN_IFSET:
            insn = parse_ifset(&jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jmpl->hook, &insn->any.hook);
            break;

        case JMPL_TKN_IF:
            insn = parse_if(&jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jmpl->hook, &insn->any.hook);
            break;

        case JMPL_TKN_FOREACH:
            insn = parse_foreach(&jp);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jmpl->hook, &insn->any.hook);
            break;

        case JMPL_TKN_SUBST:
            insn = parse_subst(&jp, val);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jmpl->hook, &insn->any.hook);
            break;

        case JMPL_TKN_TEXT:
            insn = parse_text(&jp, val);

            if (insn == NULL)
                goto parse_error;

            iot_list_append(&jmpl->hook, &insn->any.hook);
            break;

        default:
            goto parse_error;
        }
    }

    jmpl_dump(jmpl, stdout);

    return jmpl;

 parse_error:
    parser_exit(&jp);
    errno = EINVAL;
    return NULL;
}


static void dump_instructions(iot_list_hook_t *l, FILE *fp, int level);



static void indent(FILE *fp, int level)
{
    while (level-- > 0)
        fprintf(fp, "  ");
}


static void dump_reference(jmpl_ref_t *ref, FILE *fp)
{
    int tag, idx, id, i;
    const char *t;

    if (ref == NULL)
        return;

    for (i = 0, t = ""; i < ref->nid; i++, t = " ") {
        id  = ref->ids[i];
        tag = JMPL_SYMBOL_TAG(id);
        idx = JMPL_SYMBOL_IDX(id);

        switch (tag) {
        case JMPL_SYMBOL_INDEX:
            fprintf(fp, "%s0x%x/%d:[%d]", t, tag, idx, idx);
            break;
        case JMPL_SYMBOL_FIELD:
            fprintf(fp, "%s0x%x/%d:.%s", t, tag, idx, symtab_get(id));
            break;
        case JMPL_SYMBOL_STRING:
            fprintf(fp, "%s0x%x/%d:'%s'", t, tag, idx, symtab_get(id));
            break;
        default:
            fprintf(fp, "<invalid reference id:0x%x>", id);
            break;
        }
    }

    fflush(fp);
}


static void dump_expr(jmpl_expr_t *expr, FILE *fp, int level)
{
    indent(fp, level);
    fprintf(fp, "<expr>");
    fflush(fp);
}


static void dump_ifset(jmpl_ifset_t *jif, FILE *fp, int level)
{
    indent(fp, level);
    fprintf(fp, "<ifset> ");
    dump_reference(jif->test, fp);
    fprintf(fp, "\n");
    dump_instructions(&jif->tbranch, fp, level + 1);
    indent(fp, level);
    fprintf(fp, "<else>\n");
    dump_instructions(&jif->fbranch, fp, level + 1);
    indent(fp, level);
    fprintf(fp, "<end>\n");
}


static void dump_if(jmpl_if_t *jif, FILE *fp, int level)
{
    indent(fp, level);
    fprintf(fp, "<if> ");
    dump_expr(jif->test, fp, 0);
    fprintf(fp, "\n");
    dump_instructions(&jif->tbranch, fp, level + 1);
    indent(fp, level);
    fprintf(fp, "<else>\n");
    dump_instructions(&jif->fbranch, fp, level + 1);
    indent(fp, level);
    fprintf(fp, "<end>\n");
}


static void dump_foreach(jmpl_for_t *jfor, FILE *fp, int level)
{
    indent(fp, level);
    fprintf(fp, "<foreach> ");
    dump_reference(jfor->key, fp);
    fprintf(fp, ":");
    dump_reference(jfor->val, fp);
    fprintf(fp, " in ");
    dump_reference(jfor->in, fp);
    fprintf(fp, " do\n");
    dump_instructions(&jfor->body, fp, level + 1);
    indent(fp, level);
    fprintf(fp, "<end>\n");
}


static void dump_subst(jmpl_subst_t *js, FILE *fp, int level)
{
    indent(fp, level);
    fprintf(fp, "<subst> ");
    dump_reference(js->ref, fp);
    fprintf(fp, "\n");
}


static void dump_text(jmpl_text_t *jt, FILE *fp, int level)
{
    indent(fp, level);
    fprintf(fp, "<text> '%s'\n", jt->text);
}


static void dump_insn(jmpl_insn_t *insn, FILE *fp, int level)
{
    switch (insn->any.type) {
    case JMPL_OP_IFSET:   dump_ifset(&insn->ifset, fp, level);     break;
    case JMPL_OP_IF:      dump_if(&insn->ifelse, fp, level);       break;
    case JMPL_OP_FOREACH: dump_foreach(&insn->foreach, fp, level); break;
    case JMPL_OP_TEXT:    dump_text(&insn->text, fp, level);       break;
    case JMPL_OP_SUBST:   dump_subst(&insn->subst, fp, level);     break;
    default:                                                       break;
    }
}


static void dump_instructions(iot_list_hook_t *l, FILE *fp, int level)
{
    iot_list_hook_t *p, *n;
    jmpl_insn_t     *insn;

    iot_list_foreach(l, p, n) {
        insn = iot_list_entry(p, typeof(*insn), any.hook);
        dump_insn(insn, fp, level);
    }
}


void jmpl_dump(jmpl_t *jmpl, FILE *fp)
{
    dump_instructions(&jmpl->hook, fp, 0);
}