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

#include <stdio.h>
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define MAX_CHANNELS 512


typedef struct DownsampleContext {
    const AVClass *class;
    char *args;
    AVChannelLayout out_channel_layout;
    double gain[MAX_CHANNELS];

    int nb_output_channels;
    int channel_map[MAX_CHANNELS];
    struct SwrContext *swr;
} DownsampleContext;


static int query_formats(AVFilterContext *ctx)
{
    DownsampleContext *downsample = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterChannelLayouts *layouts;
    int ret;

    /* libswr supports any sample and packing formats */
    if ((ret = ff_set_common_formats(ctx, ff_all_formats(AVMEDIA_TYPE_AUDIO))) < 0)
        return ret;

    if ((ret = ff_set_common_all_samplerates(ctx)) < 0)
        return ret;

    // inlink supports any channel layout
    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0)
        return ret;

    // outlink supports only requested output channel layout
    layouts = NULL;
    if ((ret = ff_add_channel_layout(&layouts, &downsample->out_channel_layout)) < 0)
        return ret;
    return ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts);
}

static int config_props(AVFilterLink *link)
{
	int ret,i,j,r;
	AVFilterContext *ctx = link->dst;
	DownsampleContext *downsample = ctx->priv;


	if (link->ch_layout.nb_channels > MAX_CHANNELS) {
		av_log(ctx, AV_LOG_ERROR,
				"af_pan supports a maximum of %d channels. "
				"Feel free to ask for a higher limit.\n", MAX_CHANNELS);
		return AVERROR_PATCHWELCOME;
	}

	// init libswresample context
	ret = swr_alloc_set_opts2(&downsample->swr,
	                              &downsample->out_channel_layout, link->format, link->sample_rate,
	                              &link->ch_layout, link->format, link->sample_rate,
	                              0, ctx);


	if (ret < 0)
		return AVERROR(ENOMEM);

	// Filter we are using is not using pure gains;
	int n = link->ch_layout.nb_channels;
	for (j=0; j<link->ch_layout.nb_channels;j++)
	{
		downsample->gain[j] = (double)(1.0/(double)n);
	}
	av_log(ctx,AV_LOG_INFO, "DOWNSAMPLED GAINS\n");
	swr_set_matrix(downsample->swr, downsample->gain, MAX_CHANNELS);
	r = swr_init(downsample->swr);
	if (r < 0)
		return r;
	return 0;
}

//static int filter_frame(AVFilterLink *inlink, AVFrame *in)
static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
	int ret;
	int n = insamples->nb_samples;
	AVFilterLink *const outlink = inlink->dst->outputs[0];
	AVFrame *outsamples = ff_get_audio_buffer(outlink, n);
	DownsampleContext *downsample = inlink->dst->priv;

	if (!outsamples) {
		av_frame_free(&insamples);
		return AVERROR(ENOMEM);
	}
	swr_convert(downsample->swr, outsamples->extended_data, n,
				(void *)insamples->extended_data, n);
	av_frame_copy_props(outsamples, insamples);
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
	outsamples->channel_layout = outlink->channel_layout;
	outsamples->channels = outlink->ch_layout.nb_channels;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
	if ((ret = av_channel_layout_copy(&outsamples->ch_layout, &outlink->ch_layout)) < 0)
		return ret;

	ret = ff_filter_frame(outlink, outsamples);
	av_frame_free(&insamples);
	return ret;
//    av_log(NULL, AV_LOG_INFO, "Init");
//    int ret;
//    AVFilterLink *outlink = inlink->dst->outputs[0];
//    AVChannelLayout out_channel_layout;
//    struct SwrContext *swr;
//    double *gain;
////     Get Input Layout
//    // Set Swr
//    // Use SWR to resample input and store in output
//    ret = av_channel_layout_from_string(&out_channel_layout, "1c");
//    av_log(NULL, AV_LOG_INFO, "Got Channel layout");
//    if (ret<0){
//        goto fail;
//    }
//
//    // init libswresample context
//    gain = (double *)malloc(in->ch_layout.nb_channels * sizeof(double));
//    int i;
//    for (i=0;i<in->ch_layout.nb_channels;i++)
//    	gain[i]=1.0;
//    ret = swr_alloc_set_opts2(&swr,
//                              &out_channel_layout, in->format, in->sample_rate,
//                              &in->ch_layout, in->format, in->sample_rate,
//                              0, NULL);
//    swr_set_matrix(swr, gain, in->ch_layout.nb_channels);
//    int r = swr_init(swr);
//	if (r < 0)
//		return r;
//	av_log(NULL, AV_LOG_INFO, "Created SWR");
//
//    av_opt_set_int(swr, "uch", out_channel_layout.nb_channels, 0);
//    // Notsure what this do
//    int channel_map[] = {0};
//    swr_set_channel_mapping(swr, channel_map);
//
//    if (ret < 0)
//        return AVERROR(ENOMEM);
//
//    int n = in->nb_samples;
//
//    AVFrame *out = ff_get_audio_buffer(outlink,n);
//
//
//    if (!out) {
//        ret = AVERROR(ENOMEM);
//        goto fail;
//    }
//
//    swr_convert(swr, out->extended_data, n,
//    		(void *)in->extended_data,n);
//
//
//    ret = av_frame_copy_props(out, in);
//    if (ret < 0)
//        goto fail;
//
//    ret = av_frame_copy(out, in);
//    if (ret < 0)
//        goto fail;
//
//#if FF_API_OLD_CHANNEL_LAYOUT
//FF_DISABLE_DEPRECATION_WARNINGS
//    out->channel_layout = outlink->channel_layout;
//    out->channels = outlink->ch_layout.nb_channels;
//FF_ENABLE_DEPRECATION_WARNINGS
//#endif
//    if ((ret = av_channel_layout_copy(&out->ch_layout, &outlink->ch_layout)) < 0)
//        return ret;
//
//    av_frame_free(&in);
//
//    return ff_filter_frame(outlink, out);
//fail:
//    av_frame_free(&in);
//    av_frame_free(&out);
//    return ret;
}
static av_cold int init(AVFilterContext *ctx)
{
	DownsampleContext *const downsample = ctx->priv;
	char *args = av_strdup(downsample->args);
	av_channel_layout_from_string(&downsample->out_channel_layout, "1c");
	downsample->nb_output_channels = 1;
	return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
	DownsampleContext *downsample = ctx->priv;
	swr_free(&downsample->swr);
}

#define OFFSET(x) offsetof(DownsampleContext, x)

static const AVOption downsample_options[] = {
    { "args", NULL, OFFSET(args), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { NULL }
};

AVFILTER_DEFINE_CLASS(downsample);

static const AVFilterPad downsample_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
		.config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad downsample_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_downsample = {
    .name          = "downsample",
    .description   = NULL_IF_CONFIG_SMALL("Copy the input audio unchanged to the output."),
	.priv_size 	   = sizeof(DownsampleContext),
	.priv_class = &downsample_class,
	.init = init,
	.uninit = uninit,
//    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(downsample_inputs),
    FILTER_OUTPUTS(downsample_outputs),
	FILTER_QUERY_FUNC(query_formats),
};
