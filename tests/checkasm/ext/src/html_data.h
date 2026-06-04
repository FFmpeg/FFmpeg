/*
 * Copyright © 2025, Niklas Haas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CHECKASM_HTML_DATA_H
#define CHECKASM_HTML_DATA_H

#ifdef __has_embed
  #define HAVE_HTML_DATA 1
#else
  #define HAVE_HTML_DATA 0
#endif

#if HAVE_HTML_DATA
  #ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc23-extensions"
  #endif
static const char checkasm_chart_js[] = {
  #embed "html_data/chart.min.js" suffix(, 0)
};

static const char checkasm_js[] = {
  #embed "html_data/checkasm.js" suffix(, 0)
};

static const char checkasm_css[] = {
  #embed "html_data/checkasm.css" suffix(, 0)
};

static const char checkasm_html_body[] = {
  #embed "html_data/body.html" suffix(, 0)
};

  #ifdef __clang__
    #pragma clang diagnostic pop
  #endif
#else /* !HAVE_HTML_DATA */

static const char checkasm_chart_js[]  = "";
static const char checkasm_js[]        = "";
static const char checkasm_css[]       = "";
static const char checkasm_html_body[] = "";

#endif

#endif /* CHECKASM_HTML_DATA_H */
