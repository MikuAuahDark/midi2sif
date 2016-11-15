// MIDI2SIF beatmap converter

#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>

#ifdef WIN32
#include <io.h>
#endif

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <map>

struct EventInformation
{
protected:
	EventInformation() {}

public:
	uint32_t tick;
	uint32_t order;
	int32_t meta;
	std::string data;
	int32_t pos;
	int32_t channel;
	bool note_on;

	EventInformation(uint32_t tick, int32_t meta, std::string data): order(0), pos(-1), channel(-1), note_on(false)
	{
		this->tick = tick;
		this->order = order;
		this->meta = meta;
		this->data = data;
	}
	EventInformation(uint32_t tick, bool note_on, int32_t pos, int32_t channel): order(0), meta(-1), data("")
	{
		this->tick = tick;
		this->order = order;
		this->note_on = note_on;
		this->pos = pos;
		this->channel = channel;
	}

	bool operator <(const EventInformation& ei) const
	{
		return tick < ei.tick || (tick == ei.tick && order < ei.order);
	}
};

struct SIFBeatmap
{
	enum Attribute
	{
		ATTR_SMILE = 1,
		ATTR_PURE = 2,
		ATTR_COOL = 3
	};

	enum EffectType
	{
		NORMAL_NOTE = 1,
		TOKEN_NOTE = 2,
		LONG_NOTE = 3,
		STAR_NOTE = 4
	};

	enum NotePos
	{
		PosR4 = 1,
		PosR3 = 2,
		PosR2 = 3,
		PosR1 = 4,
		PosC = 5,
		PosL1 = 6,
		PosL2 = 7,
		PosL3 = 8,
		PosL4 = 9
	};

	double timing_sec;
	Attribute notes_attribute;
	EffectType effect;
	double effect_value;
	NotePos position;

	bool operator<(const SIFBeatmap& b) const
	{
		return timing_sec < b.timing_sec;
	}

	operator std::string()
	{
		char temp[128];

		sprintf(temp,
			"{"
			"\"timing_sec\": %.4f,"
			"\"notes_attribute\": %d,"
			"\"notes_level\": 1,"
			"\"effect\": %d,"
			"\"effect_value\": %.4f,"
			"\"position\": %d"
			"}",
			timing_sec, notes_attribute, effect, effect == SIFBeatmap::LONG_NOTE ? effect_value : 2.0, position
		);

		return std::string(temp);
	}
};

struct TemporaryLNQueue
{
	double timing_sec;
	SIFBeatmap::Attribute attribute;
	SIFBeatmap::NotePos position;

	TemporaryLNQueue() {}
	TemporaryLNQueue(double a, SIFBeatmap::Attribute b, SIFBeatmap::NotePos c): timing_sec(a), attribute(b), position(c) {}
};

uint32_t ReadVarInt(FILE* stream, size_t* decrementer = NULL)
{
	int32_t temp;
	uint32_t out = 0;

	do
	{
		if((temp = fgetc(stream)) == EOF)
			throw std::runtime_error("Unexpected EOF");

		if(decrementer) (*decrementer)--;

		out = (out << 7) | (temp & 0x7F);
	}
	while ((temp & 0x80) != 0);

	return out;
}

inline uint32_t STR2DWORD_BE(const void* x)
{
	const uint8_t* str = (const uint8_t*)x;
	return ((str[0] << 24) | (str[1] << 16) | (str[2] << 8) | str[3]);
}

inline uint16_t STR2WORD_BE(const void* x)
{
	const uint8_t* str = (const uint8_t*)x;
	return ((str[0] << 8) | str[1]);
}

void InsertEvent(std::map<uint32_t, std::vector<EventInformation> >& raw, EventInformation& event)
{
	if(raw.count(event.tick))
	{
		std::vector<EventInformation>& eilist = raw[event.tick];

		event.order = eilist.size();
		eilist.push_back(event);
	}
	else
	{
		std::vector<EventInformation> temp;

		event.order = 0;
		temp.push_back(event);
		raw[event.tick] = temp;
	}
}

std::vector<SIFBeatmap> midi2sif(FILE* midi_stream)
{
	char temp_buffer[32];

	if(fread(temp_buffer, 1, 4, midi_stream) != 4)
	{
		unexpected_eof:
		throw std::runtime_error("Unexpected EOF");
	}

	if(memcmp(temp_buffer, "MThd", 4))
		throw std::runtime_error("Not MIDI");

	if(fread(temp_buffer, 1, 4, midi_stream) != 4)
		goto unexpected_eof;

	if((STR2DWORD_BE(temp_buffer)) != 6)
		throw std::runtime_error("Header size not 6");

	uint32_t mtrk_count;
	uint32_t ppqn;
	double tempo = 120.0;
	std::map<uint32_t, std::vector<EventInformation> > raw_event_list;

	if(fread(temp_buffer, 1, 6, midi_stream) != 6)
		goto unexpected_eof;

	mtrk_count = STR2WORD_BE(temp_buffer + 2);
	ppqn = STR2WORD_BE(temp_buffer + 4);

	if(ppqn > 32767)
		throw std::runtime_error("MTC is not supported");

	for(uint32_t i = 0; i < mtrk_count; i++)
	{
		if(fread(temp_buffer, 1, 8, midi_stream) != 8)
			goto unexpected_eof;

		size_t mtrk_len = STR2DWORD_BE(temp_buffer + 4);
		uint32_t timing_total = 0;

		if(memcmp(temp_buffer, "MTrk", 4))
		{
			while(mtrk_len > 0)
			{
				size_t res = fread(temp_buffer, 1, std::min(mtrk_len, size_t(32)), midi_stream);

				if(res == 0)
					goto unexpected_eof;

				mtrk_len -= res;
			}

			continue;
		}

		while(mtrk_len > 0)
		{
			uint32_t timing = ReadVarInt(midi_stream, &mtrk_len);
			int32_t event_byte = fgetc(midi_stream);
			int32_t event_type = event_byte >> 4;

			if(event_byte == EOF)
				goto unexpected_eof;

			mtrk_len--;
			timing_total += timing;

			if(event_type == 8)
			{
				int32_t notepos = fgetc(midi_stream);

				if(notepos == EOF || fgetc(midi_stream) == EOF)
					goto unexpected_eof;

				mtrk_len -= 2;

				EventInformation temp(timing_total, false, notepos, event_byte & 0xF);
				InsertEvent(raw_event_list, temp);
			}
			else if(event_type == 9)
			{
				int32_t notepos = fgetc(midi_stream);

				if(notepos == EOF || fgetc(midi_stream) == EOF)
					goto unexpected_eof;
				
				mtrk_len -= 2;

				EventInformation temp(timing_total, true, notepos, event_byte & 0xF);
				InsertEvent(raw_event_list, temp);
			}
			else if(event_byte == 255)
			{
				int32_t meta_id = fgetc(midi_stream);

				if(meta_id == EOF)
					goto unexpected_eof;

				mtrk_len--;

				size_t data_size = ReadVarInt(midi_stream, &mtrk_len);
				char* data = new char[data_size];

				if(fread(data, 1, data_size, midi_stream) != data_size)
				{
					delete[] data;
					goto unexpected_eof;
				}

				mtrk_len -= data_size;

				EventInformation temp(timing_total, meta_id, std::string(data, data_size));
				InsertEvent(raw_event_list, temp);

				delete[] data;
			}
			else if(event_byte == 240 || event_byte == 247)
			{
				int32_t temp;

				do
				{
					temp = fgetc(midi_stream);

					if(temp == EOF)
						goto unexpected_eof;

					mtrk_len--;
				}
				while(temp != 247);
			}
			else if(fread(temp_buffer, 1, 2, midi_stream) != 2)
				goto unexpected_eof;
			else
				mtrk_len -= 2;
		}
	}

	size_t lowest_tick = 0xFFFFFFFFU;
	size_t highest_tick = 0;

	std::vector<EventInformation> event_list;

	for(std::map<uint32_t, std::vector<EventInformation> >::iterator i = raw_event_list.begin(); i != raw_event_list.end(); i++)
	{
		size_t tick = i->first;
		std::vector<EventInformation>& eis = i->second;

		lowest_tick = std::min(tick, lowest_tick);
		highest_tick = std::max(tick, highest_tick);

		while(eis.size() > 0)
		{
			event_list.push_back(eis.back());
			eis.pop_back();
		}
	}

	std::sort(event_list.begin(), event_list.end());

	int32_t top_index = 0;
	int32_t bottom_index = 127;

	for(std::vector<EventInformation>::iterator i = event_list.begin(); i != event_list.end(); i++)
	{
		EventInformation& x = *i;

		if(x.pos != (-1))
		{
			top_index = std::max(top_index, x.pos);
			bottom_index = std::min(bottom_index, x.pos);
		}
	}

	int32_t mid_index = top_index - bottom_index + 1;

	if(mid_index > 9 || mid_index % 2 == 0)
		throw std::runtime_error("Failed to analyze note position. Make sure you only use 9 note keys or odd amount of note keys");

	if(mid_index != 9 && mid_index % 2 == 1)
	{
		mid_index = (top_index + bottom_index) / 2;
		top_index = mid_index + 4;
		bottom_index = mid_index - 4;
	}

	std::vector<SIFBeatmap> sif_beatmap;
	std::map<SIFBeatmap::NotePos, TemporaryLNQueue> longnote_queue;
	double current_timing_sec = 0.0;
	double add_timing_sec = 60.0 / ppqn / tempo;

	for(size_t tick = lowest_tick; tick <= highest_tick; tick++)
	{
		EventInformation ei = *event_list.begin();

		while(tick >= ei.tick)
		{
			event_list.erase(event_list.begin());

			if(ei.meta == 81)
			{
				tempo = 0.0;

				for(std::string::iterator i = ei.data.begin(); i != ei.data.end(); i++)
					tempo = tempo * 256.0 + uint8_t(*i);

				tempo = floor(600000000 / tempo) / 10;
				add_timing_sec = 60.0 / ppqn / tempo;
			}
			else if(ei.pos != (-1))
			{
				SIFBeatmap::NotePos position = SIFBeatmap::NotePos(ei.pos - bottom_index + 1);
				SIFBeatmap::Attribute attri = SIFBeatmap::Attribute(ei.channel >> 2);
				SIFBeatmap::EffectType effect = SIFBeatmap::EffectType((ei.channel & 0x3) + 1);

				if(attri > 0)
				{
					if(ei.note_on)
					{
						if(effect == SIFBeatmap::LONG_NOTE)
						{
							if(longnote_queue.count(position))
							{
								char temp[32];

								sprintf(temp, "another note in pos %d is in queue", int(position));
								throw std::runtime_error(temp);
							}
							else
								longnote_queue[position] = TemporaryLNQueue(current_timing_sec, attri, position);
						}
						else
						{
							SIFBeatmap beatmap = {current_timing_sec, attri, effect, 2.0, position};
							sif_beatmap.push_back(beatmap);
						}
					}
					else if(ei.note_on == false && effect == SIFBeatmap::LONG_NOTE)
					{
						if(longnote_queue.count(position))
						{
							TemporaryLNQueue queue = longnote_queue[position];
							longnote_queue.erase(longnote_queue.find(position));

							SIFBeatmap beatmap = {queue.timing_sec, attri, SIFBeatmap::LONG_NOTE, current_timing_sec - queue.timing_sec, position};
							sif_beatmap.push_back(beatmap);
						}
						else
						{
							char temp[32];

							sprintf(temp, "queue for pos %d is empty", int(position));
							throw std::runtime_error(temp);
						}
					}
				}
			}

			if(event_list.size() > 0)
				ei = *event_list.begin();
			else
				ei.tick = 0xFFFFFFFFU;
		}

		current_timing_sec += add_timing_sec;
	}

	std::sort(sif_beatmap.begin(), sif_beatmap.end());

	return sif_beatmap;
}

int main(int argc, char* argv[])
{
	FILE* input_file = stdin;
	FILE* output_file = stdout;

#ifdef WIN32
	_setmode(0, 0x8000);
	_setmode(1, 0x8000);
#endif

	fputs("MIDI2SIF converter\n", stderr);

	if(argc >= 2 && memcmp(argv[1], "--help", 7) == 0)
	{
		fputs("Usage: MIDI2SIF [input=stdin] [output=stdout]\n", stderr);

		return 0;
	}

	if(argc >= 2)
	{
		input_file = fopen(argv[1], "rb");

		if(input_file == NULL)
		{
			perror(argv[1]);

			return 1;
		}
	}

	std::vector<SIFBeatmap> beatmaps;

	try
	{
		beatmaps = midi2sif(input_file);
	}
	catch(std::exception& e)
	{
		fprintf(stderr, "Error: %s", e.what());

		return (-1);
	}

	if(input_file != stdin)
		fclose(input_file);

	if(argc >= 3)
	{
		output_file = fopen(argv[2], "wb");

		if(output_file == NULL)
		{
			perror(argv[2]);
			fprintf(stderr, "Writing to stdout...");

			output_file = stdout;
		}
	}

	if(fwrite("[\n", 1, 2, output_file) != 2)
	{
		write_fail:
		perror("Cannot write");
		return (-1);
	}

	std::vector<SIFBeatmap>::iterator last_val = beatmaps.end() - 1;
	for(std::vector<SIFBeatmap>::iterator i = beatmaps.begin(); i != beatmaps.end(); i++)
	{
		std::string str = *i;

		if(i != last_val)
			str += ",\n";

		size_t length = str.length();

		if(fwrite(str.c_str(), 1, length, output_file) != length)
			goto write_fail;
	}

	if(fwrite("]", 1, 1, output_file) != 1)
		goto write_fail;

	if(output_file != stdout)
		fclose(output_file);

	return 0;
}
