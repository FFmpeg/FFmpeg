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

#include <string.h>

#include "libavutil/avstring.h"

#include "ffmpeg.h"

static int nb_hw_devices;
static HWDevice **hw_devices;

static HWDevice *hw_device_get_by_type(enum AVHWDeviceType type)
{
    HWDevice *found = NULL;
    int i;
    for (i = 0; i < nb_hw_devices; i++) {
        if (hw_devices[i]->type == type) {
            if (found)
                return NULL;
            found = hw_devices[i];
        }
    }
    return found;
}

HWDevice *hw_device_get_by_name(const char *name)
{
    int i;
    for (i = 0; i < nb_hw_devices; i++) {
        if (!strcmp(hw_devices[i]->name, name))
            return hw_devices[i];
    }
    return NULL;
}

static HWDevice *hw_device_add(void)
{
    int err;
    err = av_reallocp_array(&hw_devices, nb_hw_devices + 1,
                            sizeof(*hw_devices));
    if (err) {
        nb_hw_devices = 0;
        return NULL;
    }
    hw_devices[nb_hw_devices] = av_mallocz(sizeof(HWDevice));
    if (!hw_devices[nb_hw_devices])
        return NULL;
    return hw_devices[nb_hw_devices++];
}

int hw_device_init_from_string(const char *arg, HWDevice **dev_out)
{
    // "type=name:device,key=value,key2=value2"
    // "type:device,key=value,key2=value2"
    // -> av_hwdevice_ctx_create()
    // "type=name@name"
    // "type@name"
    // -> av_hwdevice_ctx_create_derived()

    AVDictionary *options = NULL;
    char *type_name = NULL, *name = NULL, *device = NULL;
    enum AVHWDeviceType type;
    HWDevice *dev, *src;
    AVBufferRef *device_ref = NULL;
    int err;
    const char *errmsg, *p, *q;
    size_t k;

    k = strcspn(arg, ":=@");
    p = arg + k;

    type_name = av_strndup(arg, k);
    if (!type_name) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    type = av_hwdevice_find_type_by_name(type_name);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        errmsg = "unknown device type";
        goto invalid;
    }

    if (*p == '=') {
        k = strcspn(p + 1, ":@");

        name = av_strndup(p + 1, k);
        if (!name) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        if (hw_device_get_by_name(name)) {
            errmsg = "named device already exists";
            goto invalid;
        }

        p += 1 + k;
    } else {
        // Give the device an automatic name of the form "type%d".
        // We arbitrarily limit at 1000 anonymous devices of the same
        // type - there is probably something else very wrong if you
        // get to this limit.
        size_t index_pos;
        int index, index_limit = 1000;
        index_pos = strlen(type_name);
        name = av_malloc(index_pos + 4);
        if (!name) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        for (index = 0; index < index_limit; index++) {
            snprintf(name, index_pos + 4, "%s%d", type_name, index);
            if (!hw_device_get_by_name(name))
                break;
        }
        if (index >= index_limit) {
            errmsg = "too many devices";
            goto invalid;
        }
    }

    if (!*p) {
        // New device with no parameters.
        err = av_hwdevice_ctx_create(&device_ref, type,
                                     NULL, NULL, 0);
        if (err < 0)
            goto fail;

    } else if (*p == ':') {
        // New device with some parameters.
        ++p;
        q = strchr(p, ',');
        if (q) {
            device = av_strndup(p, q - p);
            if (!device) {
                err = AVERROR(ENOMEM);
                goto fail;
            }
            err = av_dict_parse_string(&options, q + 1, "=", ",", 0);
            if (err < 0) {
                errmsg = "failed to parse options";
                goto invalid;
            }
        }

        err = av_hwdevice_ctx_create(&device_ref, type,
                                     device ? device : p, options, 0);
        if (err < 0)
            goto fail;

    } else if (*p == '@') {
        // Derive from existing device.

        src = hw_device_get_by_name(p + 1);
        if (!src) {
            errmsg = "invalid source device name";
            goto invalid;
        }

        err = av_hwdevice_ctx_create_derived(&device_ref, type,
                                             src->device_ref, 0);
        if (err < 0)
            goto fail;
    } else {
        errmsg = "parse error";
        goto invalid;
    }

    dev = hw_device_add();
    if (!dev) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    dev->name = name;
    dev->type = type;
    dev->device_ref = device_ref;

    if (dev_out)
        *dev_out = dev;

    name = NULL;
    err = 0;
done:
    av_freep(&type_name);
    av_freep(&name);
    av_freep(&device);
    av_dict_free(&options);
    return err;
invalid:
    av_log(NULL, AV_LOG_ERROR,
           "Invalid device specification \"%s\": %s\n", arg, errmsg);
    err = AVERROR(EINVAL);
    goto done;
fail:
    av_log(NULL, AV_LOG_ERROR,
           "Device creation failed: %d.\n", err);
    av_buffer_unref(&device_ref);
    goto done;
}

void hw_device_free_all(void)
{
    int i;
    for (i = 0; i < nb_hw_devices; i++) {
        av_freep(&hw_devices[i]->name);
        av_buffer_unref(&hw_devices[i]->device_ref);
        av_freep(&hw_devices[i]);
    }
    av_freep(&hw_devices);
    nb_hw_devices = 0;
}

static enum AVHWDeviceType hw_device_match_type_by_hwaccel(enum HWAccelID hwaccel_id)
{
    int i;
    if (hwaccel_id == HWACCEL_NONE)
        return AV_HWDEVICE_TYPE_NONE;
    for (i = 0; hwaccels[i].name; i++) {
        if (hwaccels[i].id == hwaccel_id)
            return hwaccels[i].device_type;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

static enum AVHWDeviceType hw_device_match_type_in_name(const char *codec_name)
{
    const char *type_name;
    enum AVHWDeviceType type;
    for (type = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);
         type != AV_HWDEVICE_TYPE_NONE;
         type = av_hwdevice_iterate_types(type)) {
        type_name = av_hwdevice_get_type_name(type);
        if (strstr(codec_name, type_name))
            return type;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

int hw_device_setup_for_decode(InputStream *ist)
{
    enum AVHWDeviceType type;
    HWDevice *dev;
    int err;

    if (ist->hwaccel_device) {
        dev = hw_device_get_by_name(ist->hwaccel_device);
        if (!dev) {
            char *tmp;
            type = hw_device_match_type_by_hwaccel(ist->hwaccel_id);
            if (type == AV_HWDEVICE_TYPE_NONE) {
                // No match - this isn't necessarily invalid, though,
                // because an explicit device might not be needed or
                // the hwaccel setup could be handled elsewhere.
                return 0;
            }
            tmp = av_asprintf("%s:%s", av_hwdevice_get_type_name(type),
                              ist->hwaccel_device);
            if (!tmp)
                return AVERROR(ENOMEM);
            err = hw_device_init_from_string(tmp, &dev);
            av_free(tmp);
            if (err < 0)
                return err;
        }
    } else {
        if (ist->hwaccel_id != HWACCEL_NONE)
            type = hw_device_match_type_by_hwaccel(ist->hwaccel_id);
        else
            type = hw_device_match_type_in_name(ist->dec->name);
        if (type != AV_HWDEVICE_TYPE_NONE) {
            dev = hw_device_get_by_type(type);
            if (!dev) {
                hw_device_init_from_string(av_hwdevice_get_type_name(type),
                                           &dev);
            }
        } else {
            // No device required.
            return 0;
        }
    }

    if (!dev) {
        av_log(ist->dec_ctx, AV_LOG_WARNING, "No device available "
               "for decoder (device type %s for codec %s).\n",
               av_hwdevice_get_type_name(type), ist->dec->name);
        return 0;
    }

    ist->dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!ist->dec_ctx->hw_device_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

int hw_device_setup_for_encode(OutputStream *ost)
{
    enum AVHWDeviceType type;
    HWDevice *dev;

    type = hw_device_match_type_in_name(ost->enc->name);
    if (type != AV_HWDEVICE_TYPE_NONE) {
        dev = hw_device_get_by_type(type);
        if (!dev) {
            av_log(ost->enc_ctx, AV_LOG_WARNING, "No device available "
                   "for encoder (device type %s for codec %s).\n",
                   av_hwdevice_get_type_name(type), ost->enc->name);
            return 0;
        }
        ost->enc_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
        if (!ost->enc_ctx->hw_device_ctx)
            return AVERROR(ENOMEM);
        return 0;
    } else {
        // No device required.
        return 0;
    }
}

static int hwaccel_retrieve_data(AVCodecContext *avctx, AVFrame *input)
{
    InputStream *ist = avctx->opaque;
    AVFrame *output = NULL;
    enum AVPixelFormat output_format = ist->hwaccel_output_format;
    int err;

    if (input->format == output_format) {
        // Nothing to do.
        return 0;
    }

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to transfer data to "
               "output frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        goto fail;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

fail:
    av_frame_free(&output);
    return err;
}

int hwaccel_decode_init(AVCodecContext *avctx)
{
    InputStream *ist = avctx->opaque;

    ist->hwaccel_retrieve_data = &hwaccel_retrieve_data;

    return 0;
}
