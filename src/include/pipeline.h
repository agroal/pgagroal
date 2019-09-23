/*
 * Copyright (C) 2019 Red Hat
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGAGROAL_PIPELINE_H
#define PGAGROAL_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <worker.h>

#include <ev.h>
#include <stdlib.h>

#define PIPELINE_PERFORMANCE 0

typedef void* (*initialize)(void*);
typedef void (*start)(struct worker_io*);
typedef void (*callback)(struct ev_loop *, struct ev_io *, int);
typedef void (*stop)(struct worker_io*);
typedef void (*destroy)(void*);

/** @struct
 * Define the structure for a pipeline
 */
struct pipeline
{
   initialize initialize; /**< The initialize function for the pipeline */
   start start;           /**< The start function */
   callback client;       /**< The callback for the client */
   callback server;       /**< The callback for the server */
   stop stop;             /**< The stop function */
   destroy destroy;       /**< The destroy function for the pipeline */
};

/**
 * Get the performance pipeline
 * @return The structure
 */
struct pipeline performance_pipeline();

#ifdef __cplusplus
}
#endif

#endif
