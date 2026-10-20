/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "movie_decoder_ffmpeg.h"

#ifdef HAVE_FFMPEG

#include <algorithm>
#include <cstdio>
#include "filefinder.h"
#include "filesystem_stream.h"
#include "output.h"
#include "string_view.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

/** A movie file read by FFmpeg through the Player's file system (archives included) */
struct MovieDecoderFFmpeg::Input {
	Filesystem_Stream::InputStream stream;
	AVIOContext* io = nullptr;
	AVFormatContext* format = nullptr;

	~Input() {
		if (format) {
			avformat_close_input(&format);
		}
		if (io) {
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
	}

	bool Open(std::string_view file) {
		stream = FileFinder::OpenMovie(file);
		if (!stream) {
			return false;
		}
		constexpr int buffer_size = 64 * 1024;
		auto* buffer = static_cast<uint8_t*>(av_malloc(buffer_size));
		io = avio_alloc_context(buffer, buffer_size, 0, this, &Read, nullptr, &Seek);
		format = avformat_alloc_context();
		if (!io || !format) {
			return false;
		}
		format->pb = io;
		format->flags |= AVFMT_FLAG_CUSTOM_IO;
		// the name helps FFmpeg to find the format
		std::string name = ToString(stream.GetName());
		if (avformat_open_input(&format, name.c_str(), nullptr, nullptr) < 0) {
			return false; // format was freed
		}
		return avformat_find_stream_info(format, nullptr) >= 0;
	}

	static int Read(void* opaque, uint8_t* buf, int size) {
		auto& stream = static_cast<Input*>(opaque)->stream;
		stream.read(reinterpret_cast<char*>(buf), size);
		int read = static_cast<int>(stream.gcount());
		return read > 0 ? read : AVERROR_EOF;
	}

	static int64_t Seek(void* opaque, int64_t offset, int whence) {
		auto& stream = static_cast<Input*>(opaque)->stream;
		if (whence & AVSEEK_SIZE) {
			return static_cast<int64_t>(stream.GetSize());
		}
		whence &= ~AVSEEK_FORCE;
		stream.clear();
		stream.seekg(offset, whence == SEEK_CUR ? std::ios_base::cur : (whence == SEEK_END ? std::ios_base::end : std::ios_base::beg));
		return stream ? static_cast<int64_t>(stream.tellg()) : -1;
	}
};

namespace {
	AVCodecContext* OpenCodec(AVStream* stream, const AVCodec* codec) {
		if (!codec) {
			return nullptr;
		}
		AVCodecContext* context = avcodec_alloc_context3(codec);
		if (!context) {
			return nullptr;
		}
		context->thread_count = 0; // as many as the CPU has
		if (avcodec_parameters_to_context(context, stream->codecpar) < 0 || avcodec_open2(context, codec, nullptr) < 0) {
			avcodec_free_context(&context);
		}
		return context;
	}
}

MovieDecoderFFmpeg::MovieDecoderFFmpeg(std::string_view file, int width, int height) {
	static bool log_level_set = false;
	if (!log_level_set) {
		av_log_set_level(AV_LOG_ERROR);
		log_level_set = true;
	}

	frame = av_frame_alloc();
	shown = av_frame_alloc();
	packet = av_packet_alloc();

	input = std::make_unique<Input>();
	if (!input->Open(file)) {
		error = "not a movie FFmpeg reads";
		return;
	}
	AVFormatContext* format = input->format;

	const AVCodec* codec = nullptr;
	video_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (video_index >= 0 && (format->streams[video_index]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
		video_index = -1; // the cover of a music file
	}
	if (video_index >= 0) {
		video = OpenCodec(format->streams[video_index], codec);
		if (!video) {
			Output::Warning("Movie {}: FFmpeg has no decoder for its video ({})", file,
				avcodec_get_name(format->streams[video_index]->codecpar->codec_id));
			video_index = -1;
		}
	}
	// only the pictures are read from this file: the sound comes from another reading of it
	for (unsigned i = 0; i < format->nb_streams; ++i) {
		format->streams[i]->discard = static_cast<int>(i) == video_index ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
	}

	DecodeAudio(file);
	if (video_index < 0 && audio.empty()) {
		error = "no video nor sound FFmpeg decodes";
		return;
	}

	if (video_index >= 0) {
		this->width = width > 0 ? width : video->width;
		this->height = height > 0 ? height : video->height;
		AVRational rate = av_guess_frame_rate(format, format->streams[video_index], nullptr);
		frame_step = rate.num > 0 ? av_rescale(AV_TIME_BASE, rate.den, rate.num) : AV_TIME_BASE / 25;
	}
	duration = format->duration != AV_NOPTS_VALUE ? format->duration : 0;
}

MovieDecoderFFmpeg::~MovieDecoderFFmpeg() {
	sws_freeContext(scaler);
	avcodec_free_context(&video);
	av_frame_free(&frame);
	av_frame_free(&shown);
	av_packet_free(&packet);
}

void MovieDecoderFFmpeg::DecodeAudio(std::string_view file) {
	Input in;
	if (!in.Open(file)) {
		return;
	}
	const AVCodec* codec = nullptr;
	int index = av_find_best_stream(in.format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (index < 0) {
		return;
	}
	AVCodecContext* context = OpenCodec(in.format->streams[index], codec);
	if (!context) {
		Output::Warning("Movie {}: FFmpeg has no decoder for its sound ({})", file,
			avcodec_get_name(in.format->streams[index]->codecpar->codec_id));
		return;
	}
	for (unsigned i = 0; i < in.format->nb_streams; ++i) {
		in.format->streams[i]->discard = static_cast<int>(i) == index ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
	}

	// 16-bit samples, mono or stereo, at the rate of the movie: the audio decoders resample
	int channels = std::min(context->ch_layout.nb_channels, 2);
	int rate = context->sample_rate;
	AVChannelLayout layout;
	av_channel_layout_default(&layout, channels);
	SwrContext* resampler = nullptr;
	if (channels <= 0 || rate <= 0 ||
			swr_alloc_set_opts2(&resampler, &layout, AV_SAMPLE_FMT_S16, rate,
				&context->ch_layout, context->sample_fmt, rate, 0, nullptr) < 0 ||
			swr_init(resampler) < 0) {
		swr_free(&resampler);
		avcodec_free_context(&context);
		return;
	}

	std::vector<uint8_t> samples;
	int frame_bytes = channels * 2;
	auto convert = [&](const uint8_t** data, int count) {
		int out = swr_get_out_samples(resampler, count);
		if (out <= 0) {
			return;
		}
		size_t pos = samples.size();
		samples.resize(pos + static_cast<size_t>(out) * frame_bytes);
		uint8_t* dst = samples.data() + pos;
		int converted = swr_convert(resampler, &dst, out, data, count);
		samples.resize(pos + static_cast<size_t>(std::max(converted, 0)) * frame_bytes);
	};
	auto receive = [&]() {
		while (avcodec_receive_frame(context, frame) == 0) {
			convert(const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
			av_frame_unref(frame);
		}
	};
	while (av_read_frame(in.format, packet) >= 0) {
		if (packet->stream_index == index && avcodec_send_packet(context, packet) >= 0) {
			receive();
		}
		av_packet_unref(packet);
	}
	avcodec_send_packet(context, nullptr);
	receive();
	convert(nullptr, 0);

	swr_free(&resampler);
	avcodec_free_context(&context);

	if (samples.empty()) {
		return;
	}
	audio_duration = static_cast<int64_t>(samples.size() / frame_bytes) * AV_TIME_BASE / rate;

	// WAVEFORMATEX of 16-bit PCM
	uint32_t bytes_per_second = static_cast<uint32_t>(rate * frame_bytes);
	std::vector<uint8_t> format = {
		1, 0, static_cast<uint8_t>(channels), 0,
		static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8), static_cast<uint8_t>(rate >> 16), static_cast<uint8_t>(rate >> 24),
		static_cast<uint8_t>(bytes_per_second), static_cast<uint8_t>(bytes_per_second >> 8),
		static_cast<uint8_t>(bytes_per_second >> 16), static_cast<uint8_t>(bytes_per_second >> 24),
		static_cast<uint8_t>(frame_bytes), 0, 16, 0
	};
	audio = MakeWav(format, samples);
}

const std::string& MovieDecoderFFmpeg::GetError() const {
	return error;
}

bool MovieDecoderFFmpeg::HasVideo() const {
	return video_index >= 0;
}

int MovieDecoderFFmpeg::GetWidth() const {
	return width;
}

int MovieDecoderFFmpeg::GetHeight() const {
	return height;
}

bool MovieDecoderFFmpeg::HasAudio() const {
	return !audio.empty();
}

std::vector<uint8_t> MovieDecoderFFmpeg::ReadAudio() {
	return std::move(audio);
}

bool MovieDecoderFFmpeg::IsDone(int64_t time) const {
	int64_t length = std::max(duration, audio_duration);
	if (duration > 0) {
		return time >= length;
	}
	// a length the file doesn't give: until the last picture and the end of the sound
	return (video_index < 0 || video_ended) && time >= audio_duration;
}

bool MovieDecoderFFmpeg::DecodeVideoFrame() {
	AVStream* stream = input->format->streams[video_index];
	while (true) {
		int ret = avcodec_receive_frame(video, frame);
		if (ret == 0) {
			int64_t pts = frame->best_effort_timestamp;
			if (pts == AV_NOPTS_VALUE) {
				frame_time += frame_step;
			} else {
				if (stream->start_time != AV_NOPTS_VALUE) {
					pts -= stream->start_time;
				}
				frame_time = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
			}
			frame_waiting = true;
			return true;
		}
		if (ret != AVERROR(EAGAIN) || flushing) {
			video_ended = true;
			return false;
		}
		if (av_read_frame(input->format, packet) < 0) {
			// the end of the file: the decoder gives the pictures it keeps
			avcodec_send_packet(video, nullptr);
			flushing = true;
			continue;
		}
		if (packet->stream_index == video_index) {
			avcodec_send_packet(video, packet);
		}
		av_packet_unref(packet);
	}
}

bool MovieDecoderFFmpeg::ReadFrame(int64_t time, uint8_t* rgba) {
	if (video_index < 0) {
		return false;
	}

	// the last picture due, the ones before it are skipped
	bool due = false;
	while (frame_waiting || (!video_ended && DecodeVideoFrame())) {
		if (frame_time > time) {
			break;
		}
		av_frame_unref(shown);
		av_frame_move_ref(shown, frame);
		frame_waiting = false;
		due = true;
	}
	if (!due) {
		return false;
	}

	scaler = sws_getCachedContext(scaler, shown->width, shown->height, static_cast<AVPixelFormat>(shown->format),
		width, height, AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr);
	if (!scaler) {
		return false;
	}
	// YUV to RGB as the video says, BT.709 for HD pictures that don't say
	int matrix = SWS_CS_DEFAULT;
	if (shown->colorspace == AVCOL_SPC_BT709 || (shown->colorspace == AVCOL_SPC_UNSPECIFIED && shown->height > 576)) {
		matrix = SWS_CS_ITU709;
	} else if (shown->colorspace == AVCOL_SPC_BT2020_NCL || shown->colorspace == AVCOL_SPC_BT2020_CL) {
		matrix = SWS_CS_BT2020;
	}
	sws_setColorspaceDetails(scaler, sws_getCoefficients(matrix), shown->color_range == AVCOL_RANGE_JPEG,
		sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);

	uint8_t* dst[4] = { rgba, nullptr, nullptr, nullptr };
	int dst_stride[4] = { width * 4, 0, 0, 0 };
	sws_scale(scaler, shown->data, shown->linesize, 0, shown->height, dst, dst_stride);
	return true;
}

#endif
