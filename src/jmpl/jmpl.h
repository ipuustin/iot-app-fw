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

#ifndef __JMPL_H__
#define __JMPL_H__

#include <iot/common/macros.h>
#include <iot/common/json.h>

/**
 * \addtogroup JSONTemplate
 * @{
 *
 * @file jmpl.h
 *
 * @brief JSON Templates.
 */

/**
 * @brief Opaque type for JSON data.
 */
typedef iot_json_t json_t;


/**
 * @brief Opaque type for a JSON template.
 */
typedef struct jmpl_s jmpl_t;


/**
 * @brief Load a JSON template from a file.
 *
 * Loads a JSON template from the given file.
 *
 * @param [in] path  file to load JSON template from
 *
 * @return Returns the loaded JSON template or NULL upon error in which case
 *         errno is also set.
 */
jmpl_t *jmpl_load_template(const char *path);


/**
 * @brief Load JSON data from a file.
 *
 * Loads JSON data from the given file.
 *
 * @param [in] path  file to load JSON data from
 *
 * @return Returns the loaded JSON data or NULL upon error in which case
 *         errno is also set.
 */
json_t *jmpl_load_json(const char *path);


/**
 * @brief Parse a JSON template.
 *
 * Parse the given string into a JSON template.
 *
 * @param [in] str  the JSON template string to parse
 *
 * @return Returns the parsed JSON template or NULL upon error in which case
 *         errno is also set.
 */
jmpl_t *jmpl_parse(const char *str);


/**
 * @brief Evaluate a JSON template.
 *
 * Evaluate a JSON template in the context of the given JSON data.
 *
 * @param [in] jmpl  JSON template
 * @param [in] json  JSON data
 *
 * @return Returns the string the template evaluates to in the context of
 *         the given data. Return NULL upon failure in which case errno is
 *         also set.
 */
char *jmpl_eval(jmpl_t *jmpl, json_t *json);




#endif /* __JMPL_H__ */
