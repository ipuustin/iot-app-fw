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

#include "generator.h"

#define FN_TRUNCATE "truncate"
#define FN_CONCAT   "concat"

static int fn_truncate(smpl_t *smpl, int argc, smpl_value_t *argv,
                       smpl_value_t *rv, void *user_data);

static int fn_concat(smpl_t *smpl, int argc, smpl_value_t *argv,
                     smpl_value_t *rv, void *user_data);

static void register_functions(void)
{
    static int registered = 0;

    if (registered)
        return;

    if (smpl_register_function(FN_TRUNCATE, fn_truncate, NULL) < 0) {
        log_error("Failed to register template function '%s'", FN_TRUNCATE);
        exit(1);
    }

    if (smpl_register_function(FN_CONCAT, fn_concat, NULL) < 0) {
        log_error("Failed to register template function '%s'", FN_CONCAT);
        exit(1);
    }

    registered = 1;
}


int template_load(generator_t *g)
{
    char **errors;

    register_functions();

    g->template = smpl_load_template(g->path_template, &errors);

    if (g->template != NULL)
        return 0;
    else {
        int i;

        log_error("Failed to load template file '%s'.", g->path_template);

        if (errors != NULL) {
            for (i = 0; errors[i] != NULL; i++)
                log_error("error: %s", errors[i]);
            smpl_errors_free(errors);
        }

        return -1;
    }
}


void template_destroy(generator_t *g)
{
    smpl_free_template(g->template);
    g->template = NULL;
}


int template_eval(service_t *s)
{
    char **errors;

    s->output = smpl_evaluate(s->g->template, s->data, &errors);

    if (s->output != NULL) {
        if (errors != NULL) {
            int i;

            log_error("template for %s / %s evaluated with errors:",
                      s->provider, s->app);

            if (errors != NULL) {
                for (i = 0; errors[i] != NULL; i++)
                    log_error("error: %s", errors[i]);
                smpl_errors_free(errors);
            }
        }

        return 0;
    }
    else {
        int i;

        log_error("Failed to generate service file for %s / %s.",
                  s->provider, s->app);

        if (errors != NULL) {
            for (i = 0; errors[i] != NULL; i++)
                log_error("error: %s", errors[i]);
            smpl_errors_free(errors);
        }

        return -1;
    }
}


static int fn_truncate(smpl_t *smpl, int argc, smpl_value_t *argv,
                       smpl_value_t *rv, void *user_data)
{
    int32_t  lim, len;
    char    *str;
    char    *result;

    IOT_UNUSED(user_data);

    if (rv == NULL)
        goto invalid_retval;

    if (argc != 2)
        goto invalid_argc;

    if (argv[0].type != SMPL_VALUE_STRING || argv[1].type != SMPL_VALUE_INTEGER)
        goto invalid_argv;

    str = argv[0].str;
    len = strlen(str);
    lim = argv[1].i32;

    if (lim > len)
        lim = len;

    result = smpl_allocz(lim + 1);

    if (result == NULL)
        goto nomem;

    strncpy(result, str, lim);
    rv->type    = SMPL_VALUE_STRING;
    rv->str     = result;
    rv->dynamic = 1;

    return 0;

 invalid_retval:
    smpl_fail(-1, smpl, EINVAL,
              "%s() called without a return value", FN_TRUNCATE);

 invalid_argc:
    rv->type = SMPL_VALUE_UNKNOWN;
    smpl_fail(-1, smpl, EINVAL,
              "%s() expects %d arguments, %d given", FN_TRUNCATE, 2, argc);

 invalid_argv:
    rv->type = SMPL_VALUE_UNKNOWN;
    smpl_fail(-1, smpl, EINVAL,
              "%s() expects a string and an integer argument", FN_TRUNCATE);

 nomem:
    rv->type = SMPL_VALUE_UNKNOWN;
    return -1;
}


static int fn_concat(smpl_t *smpl, int argc, smpl_value_t *argv,
                     smpl_value_t *rv, void *user_data)
{
    smpl_value_t *arg;
    char         *p, buf[4096];
    int           l, n, i;

    IOT_UNUSED(user_data);

    if (rv == NULL)
        goto invalid_retval;

    p = buf;
    l = sizeof(buf) - 1;

    for (i = 0, arg = argv; i < argc; i++, arg++) {
        switch (arg->type) {
        case SMPL_VALUE_STRING:
            n = snprintf(p, l, "%s", arg->str);
            break;
        case SMPL_VALUE_INTEGER:
            n = snprintf(p, l, "%d", arg->i32);
            break;
        case SMPL_VALUE_DOUBLE:
            n = snprintf(p, l, "%f", arg->dbl);
            break;
        case SMPL_VALUE_UNSET:
            n = 0;
            break;
        default:
            goto invalid_arg;
        }

        if (n >= l)
            goto overflow;

        p += n;
        l -= n;
    }

    *p = '\0';
    rv->type    = SMPL_VALUE_STRING;
    rv->str     = smpl_strdup(buf);
    rv->dynamic = 1;

    if (rv->str == NULL)
        goto nomem;

    return 0;

 invalid_retval:
    smpl_fail(-1, smpl, EINVAL,
              "%s() called without a return value", FN_CONCAT);

 invalid_arg:
    rv->type = SMPL_VALUE_UNKNOWN;
    smpl_fail(-1, smpl, EINVAL,
              "%s() expects string, integer, or double arguments", FN_CONCAT);

 overflow:
    rv->type = SMPL_VALUE_UNKNOWN;
    smpl_fail(-1, smpl, ENOBUFS, "%s() run out of buffer space", FN_CONCAT);

 nomem:
    rv->type = SMPL_VALUE_UNKNOWN;
    return -1;
}
