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

#include "avi_reader.h"
#include "output.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
	constexpr uint32_t FourCC(const char (&id)[5]) {
		return uint32_t(uint8_t(id[0])) | uint32_t(uint8_t(id[1])) << 8 |
			uint32_t(uint8_t(id[2])) << 16 | uint32_t(uint8_t(id[3])) << 24;
	}

	std::string FourCCName(uint32_t id) {
		std::string name;
		for (int i = 0; i < 4; ++i) {
			char c = static_cast<char>((id >> (8 * i)) & 0xFF);
			name += (c >= 32 && c < 127) ? c : '?';
		}
		return name;
	}

	uint16_t GetU16(const std::vector<uint8_t>& data, size_t pos) {
		return pos + 2 <= data.size() ? uint16_t(data[pos] | data[pos + 1] << 8) : 0;
	}

	uint32_t GetU32(const std::vector<uint8_t>& data, size_t pos) {
		return pos + 4 <= data.size() ? uint32_t(GetU16(data, pos)) | uint32_t(GetU16(data, pos + 2)) << 16 : 0;
	}

	uint8_t Clamp8(int value) {
		return static_cast<uint8_t>(std::min(255, std::max(0, value)));
	}

	// BT.601 with the video range (Y from 16 to 235), as the YUV codecs of Windows
	void YuvToRgba(int y, int u, int v, uint8_t* out) {
		int c = (y - 16) * 298 + 128;
		int d = u - 128;
		int e = v - 128;
		out[0] = Clamp8((c + 409 * e) >> 8);
		out[1] = Clamp8((c - 100 * d - 208 * e) >> 8);
		out[2] = Clamp8((c + 516 * d) >> 8);
		out[3] = 255;
	}

	constexpr uint16_t WAVE_FORMAT_MPEGLAYER3 = 0x55;
}

AviReader::AviReader(Filesystem_Stream::InputStream stream) : stream(std::move(stream)) {
	// An AVI file is a RIFF "AVI " chunk, OpenDML adds RIFF "AVIX" chunks with more of the movie
	uint32_t offset = 0;
	while (Seek(offset)) {
		uint32_t id = ReadU32();
		uint32_t size = ReadU32();
		uint32_t form = ReadU32();
		if (!this->stream || id != FourCC("RIFF") || (form != FourCC("AVI ") && form != FourCC("AVIX"))) {
			break;
		}
		if (offset == 0 && form != FourCC("AVI ")) {
			break;
		}
		ReadHeaders(offset + 8 + size);
		offset += 8 + size + (size & 1);
	}

	if (streams.empty()) {
		error = "not an AVI movie";
		return;
	}

	if (video_stream >= 0 && video == Video::None) {
		const auto& format = streams[video_stream].format;
		uint32_t compression = GetU32(format, 16);
		video_error = fmt::format("AVI video format {} {}-bit is not supported (only uncompressed video)",
			compression == 0 ? "RGB" : FourCCName(compression), GetU16(format, 14));
	}
	if (!HasVideo() && !HasAudio()) {
		error = video_error.empty() ? "no video nor sound" : video_error;
	}
}

void AviReader::ReadHeaders(uint32_t end) {
	uint32_t offset = static_cast<uint32_t>(stream.tellg());
	while (offset + 8 <= end && Seek(offset)) {
		uint32_t id = ReadU32();
		uint32_t size = ReadU32();
		if (!stream) {
			break;
		}
		if (id == FourCC("LIST")) {
			uint32_t list = ReadU32();
			if (list == FourCC("hdrl")) {
				ReadHeaders(offset + 8 + size);
			} else if (list == FourCC("strl")) {
				ReadStreamHeaders(offset + 8 + size);
			} else if (list == FourCC("movi")) {
				ReadChunks(offset + 8 + size);
			}
		}
		offset += 8 + size + (size & 1);
	}
}

void AviReader::ReadStreamHeaders(uint32_t end) {
	StreamInfo info;
	uint32_t offset = static_cast<uint32_t>(stream.tellg());
	while (offset + 8 <= end && Seek(offset)) {
		uint32_t id = ReadU32();
		uint32_t size = ReadU32();
		if (!stream) {
			break;
		}
		if (id == FourCC("strh") && size >= 32) {
			std::vector<uint8_t> header(size);
			if (ReadBytes(header.data(), size)) {
				info.type = GetU32(header, 0);
				info.scale = GetU32(header, 20);
				info.rate = GetU32(header, 24);
			}
		} else if (id == FourCC("strf")) {
			info.format.resize(size);
			if (!ReadBytes(info.format.data(), size)) {
				info.format.clear();
			}
		}
		offset += 8 + size + (size & 1);
	}

	int index = static_cast<int>(streams.size());
	streams.push_back(info);

	if (info.type == FourCC("vids") && video_stream < 0 && info.format.size() >= 40) {
		video_stream = index;
		const auto& format = info.format;
		width = static_cast<int32_t>(GetU32(format, 4));
		int32_t format_height = static_cast<int32_t>(GetU32(format, 8));
		bits = GetU16(format, 14);
		uint32_t compression = GetU32(format, 16);
		height = std::abs(format_height);
		frame_duration = info.rate > 0 ? int64_t(info.scale) * 1000000 / info.rate : 0;

		if (compression == FourCC("IYUV") || compression == FourCC("I420")) {
			video = Video::Planar420;
		} else if (compression == FourCC("YV12")) {
			video = Video::Planar420VU;
		} else if (compression == FourCC("YUY2") || compression == FourCC("YUYV") || compression == FourCC("YUNV")) {
			video = Video::YUYV;
		} else if (compression == FourCC("UYVY") || compression == FourCC("UYNV") || compression == FourCC("HDYC")) {
			video = Video::UYVY;
		} else if (compression == 0 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) {
			video = Video::RGB;
			bottom_up = format_height > 0;
			if (bits == 8) {
				uint32_t header_size = GetU32(format, 0);
				uint32_t colors = GetU32(format, 32);
				colors = (colors == 0 || colors > 256) ? 256 : colors;
				for (uint32_t i = 0; i < colors; ++i) {
					size_t pos = header_size + i * 4; // B, G, R, 0
					if (pos + 4 <= format.size()) {
						uint8_t rgba[4] = { format[pos + 2], format[pos + 1], format[pos], 255 };
						std::memcpy(&palette[i], rgba, 4);
					}
				}
			}
		}
		if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || frame_duration <= 0) {
			video = Video::None;
		}
	} else if (info.type == FourCC("auds") && audio_stream < 0 && info.format.size() >= 16) {
		audio_stream = index;
	}
}

void AviReader::ReadChunks(uint32_t end) {
	uint32_t offset = static_cast<uint32_t>(stream.tellg());
	while (offset + 8 <= end && Seek(offset)) {
		uint32_t id = ReadU32();
		uint32_t size = ReadU32();
		if (!stream) {
			break;
		}
		if (id == FourCC("LIST")) {
			// "rec " lists group the chunks of a moment
			ReadChunks(offset + 8 + size);
		} else {
			// the chunk id is the stream number in two digits and the kind of data: "00dc", "01wb"...
			char first = static_cast<char>(id & 0xFF);
			char second = static_cast<char>((id >> 8) & 0xFF);
			if (first >= '0' && first <= '9' && second >= '0' && second <= '9') {
				int number = (first - '0') * 10 + (second - '0');
				if (number == video_stream) {
					frames.push_back({ offset + 8, size });
				} else if (number == audio_stream) {
					audio.push_back({ offset + 8, size });
				}
			}
		}
		offset += 8 + size + (size & 1);
	}
}

bool AviReader::Seek(uint32_t offset) {
	stream.clear();
	stream.seekg(offset, std::ios_base::beg);
	return static_cast<bool>(stream);
}

bool AviReader::ReadBytes(void* dst, uint32_t size) {
	stream.read(static_cast<char*>(dst), size);
	return static_cast<uint32_t>(stream.gcount()) == size;
}

uint32_t AviReader::ReadU32() {
	uint8_t bytes[4] = {};
	if (!ReadBytes(bytes, 4)) {
		return 0;
	}
	return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

const std::string& AviReader::GetError() const {
	return error;
}

const std::string& AviReader::GetVideoError() const {
	return video_error;
}

bool AviReader::HasVideo() const {
	return video != Video::None && !frames.empty();
}

int AviReader::GetWidth() const {
	return width;
}

int AviReader::GetHeight() const {
	return height;
}

bool AviReader::HasAudio() const {
	return audio_stream >= 0 && !audio.empty();
}

int64_t AviReader::GetDuration() const {
	int64_t video_length = frame_duration * static_cast<int64_t>(frames.size());
	int64_t audio_length = 0;
	if (HasAudio()) {
		uint32_t bytes_per_second = GetU32(streams[audio_stream].format, 8);
		if (bytes_per_second > 0) {
			int64_t bytes = 0;
			for (const auto& chunk : audio) {
				bytes += chunk.size;
			}
			audio_length = bytes * 1000000 / bytes_per_second;
		}
	}
	return std::max(video_length, audio_length);
}

bool AviReader::IsDone(int64_t time) const {
	return time >= GetDuration();
}

bool AviReader::ReadFrame(int64_t time, uint8_t* rgba) {
	if (!HasVideo()) {
		return false;
	}
	int index = static_cast<int>(std::min<int64_t>(time / frame_duration, static_cast<int64_t>(frames.size()) - 1));
	if (index == frame_index) {
		return false;
	}
	// the frames in between are skipped
	frame_index = index;
	return DecodeFrame(index, rgba);
}

bool AviReader::DecodeFrame(int index, uint8_t* rgba) {
	const Chunk& chunk = frames[index];
	if (chunk.size == 0) {
		return false;
	}

	size_t pixels = static_cast<size_t>(width) * height;
	int chroma_width = (width + 1) / 2;
	int chroma_height = (height + 1) / 2;
	size_t stride = 0;
	size_t needed = 0;
	switch (video) {
		case Video::Planar420:
		case Video::Planar420VU:
			needed = pixels + 2 * static_cast<size_t>(chroma_width) * chroma_height;
			break;
		case Video::YUYV:
		case Video::UYVY:
			stride = static_cast<size_t>(chroma_width) * 4;
			needed = stride * height;
			break;
		case Video::RGB:
			stride = ((static_cast<size_t>(width) * bits + 31) / 32) * 4;
			needed = stride * height;
			break;
		case Video::None:
			return false;
	}
	if (chunk.size < needed) {
		return false;
	}

	chunk_data.resize(needed);
	if (!Seek(chunk.offset) || !ReadBytes(chunk_data.data(), static_cast<uint32_t>(needed))) {
		return false;
	}
	const uint8_t* data = chunk_data.data();

	if (video == Video::Planar420 || video == Video::Planar420VU) {
		const uint8_t* u_plane = data + pixels;
		const uint8_t* v_plane = u_plane + static_cast<size_t>(chroma_width) * chroma_height;
		if (video == Video::Planar420VU) {
			std::swap(u_plane, v_plane);
		}
		for (int y = 0; y < height; ++y) {
			const uint8_t* y_row = data + static_cast<size_t>(y) * width;
			const uint8_t* u_row = u_plane + static_cast<size_t>(y / 2) * chroma_width;
			const uint8_t* v_row = v_plane + static_cast<size_t>(y / 2) * chroma_width;
			uint8_t* out = rgba + static_cast<size_t>(y) * width * 4;
			for (int x = 0; x < width; ++x) {
				YuvToRgba(y_row[x], u_row[x / 2], v_row[x / 2], out + x * 4);
			}
		}
	} else if (video == Video::YUYV || video == Video::UYVY) {
		int y0 = video == Video::YUYV ? 0 : 1;
		int u = video == Video::YUYV ? 1 : 0;
		for (int y = 0; y < height; ++y) {
			const uint8_t* row = data + y * stride;
			uint8_t* out = rgba + static_cast<size_t>(y) * width * 4;
			for (int x = 0; x < width; ++x) {
				const uint8_t* pair = row + (x / 2) * 4;
				YuvToRgba(pair[y0 + (x & 1) * 2], pair[u], pair[u + 2], out + x * 4);
			}
		}
	} else {
		for (int y = 0; y < height; ++y) {
			const uint8_t* row = data + (bottom_up ? height - 1 - y : y) * stride;
			uint8_t* out = rgba + static_cast<size_t>(y) * width * 4;
			for (int x = 0; x < width; ++x, out += 4) {
				switch (bits) {
					case 8:
						std::memcpy(out, &palette[row[x]], 4);
						break;
					case 16: { // RGB555
						int value = row[x * 2] | row[x * 2 + 1] << 8;
						int r = (value >> 10) & 31, g = (value >> 5) & 31, b = value & 31;
						out[0] = static_cast<uint8_t>(r << 3 | r >> 2);
						out[1] = static_cast<uint8_t>(g << 3 | g >> 2);
						out[2] = static_cast<uint8_t>(b << 3 | b >> 2);
						out[3] = 255;
						break;
					}
					default: { // 24 or 32-bit: B, G, R(, unused)
						const uint8_t* pixel = row + x * (bits / 8);
						out[0] = pixel[2];
						out[1] = pixel[1];
						out[2] = pixel[0];
						out[3] = 255;
						break;
					}
				}
			}
		}
	}
	return true;
}

std::vector<uint8_t> AviReader::ReadAudio() {
	std::vector<uint8_t> samples;
	if (!HasAudio()) {
		return samples;
	}
	for (const auto& chunk : audio) {
		size_t pos = samples.size();
		samples.resize(pos + chunk.size);
		if (!Seek(chunk.offset) || !ReadBytes(samples.data() + pos, chunk.size)) {
			samples.resize(pos);
		}
	}

	const auto& format = streams[audio_stream].format;
	if (GetU16(format, 0) == WAVE_FORMAT_MPEGLAYER3) {
		return samples;
	}
	// the wave format of the AVI is the "fmt " chunk of a WAV file
	return MakeWav(format, samples);
}
