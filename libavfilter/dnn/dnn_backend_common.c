/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * DNN common functions different backends.
 */

#include "dnn_backend_common.h"

int ff_check_exec_params(void *ctx, DNNBackendType backend, DNNFunctionType func_type, DNNExecBaseParams *exec_params)
{
    if (!exec_params) {
        av_log(ctx, AV_LOG_ERROR, "exec_params is null when execute model.\n");
        return AVERROR(EINVAL);
    }

    if (!exec_params->in_frame) {
        av_log(ctx, AV_LOG_ERROR, "in frame is NULL when execute model.\n");
        return AVERROR(EINVAL);
    }

    if (!exec_params->out_frame) {
        av_log(ctx, AV_LOG_ERROR, "out frame is NULL when execute model.\n");
        return AVERROR(EINVAL);
    }

    if (exec_params->nb_output != 1 && backend != DNN_TF) {
        // currently, the filter does not need multiple outputs,
        // so we just pending the support until we really need it.
        avpriv_report_missing_feature(ctx, "multiple outputs");
        return AVERROR(EINVAL);
    }

    return 0;
}

DNNReturnType ff_dnn_fill_task(TaskItem *task, DNNExecBaseParams *exec_params, void *backend_model, int async, int do_ioproc) {
    if (task == NULL || exec_params == NULL || backend_model == NULL)
        return DNN_ERROR;
    if (do_ioproc != 0 && do_ioproc != 1)
        return DNN_ERROR;
    if (async != 0 && async != 1)
        return DNN_ERROR;

    task->do_ioproc = do_ioproc;
    task->async = async;
    task->input_name = exec_params->input_name;
    task->in_frame = exec_params->in_frame;
    task->out_frame = exec_params->out_frame;
    task->model = backend_model;
    task->nb_output = exec_params->nb_output;
    task->output_names = exec_params->output_names;

    return DNN_SUCCESS;
}

/**
 * Thread routine for async execution.
 * @param args pointer to DNNAsyncExecModule module
 */
static void *async_thread_routine(void *args)
{
    DNNAsyncExecModule *async_module = args;
    void *request = async_module->args;

    async_module->start_inference(request);
    async_module->callback(request);
    return NULL;
}

DNNReturnType ff_dnn_async_module_cleanup(DNNAsyncExecModule *async_module)
{
    if (!async_module) {
        return DNN_ERROR;
    }
#if HAVE_PTHREAD_CANCEL
    pthread_join(async_module->thread_id, NULL);
#endif
    async_module->start_inference = NULL;
    async_module->callback = NULL;
    async_module->args = NULL;
    return DNN_SUCCESS;
}

DNNReturnType ff_dnn_start_inference_async(void *ctx, DNNAsyncExecModule *async_module)
{
    int ret;

    if (!async_module) {
        av_log(ctx, AV_LOG_ERROR, "async_module is null when starting async inference.\n");
        return DNN_ERROR;
    }

#if HAVE_PTHREAD_CANCEL
    pthread_join(async_module->thread_id, NULL);
    ret = pthread_create(&async_module->thread_id, NULL, async_thread_routine, async_module);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to start async inference.\n");
        return DNN_ERROR;
    }
#else
    if (async_module->start_inference(async_module->args) != DNN_SUCCESS) {
        return DNN_ERROR;
    }
    async_module->callback(async_module->args);
#endif
    return DNN_SUCCESS;
}

DNNAsyncStatusType ff_dnn_get_async_result_common(Queue *task_queue, AVFrame **in, AVFrame **out)
{
    TaskItem *task = ff_queue_peek_front(task_queue);

    if (!task) {
        return DAST_EMPTY_QUEUE;
    }

    if (task->inference_done != task->inference_todo) {
        return DAST_NOT_READY;
    }

    *in = task->in_frame;
    *out = task->out_frame;
    ff_queue_pop_front(task_queue);
    av_freep(&task);

    return DAST_SUCCESS;
}
