#ifndef __MP4_TMPO_H__
#define __MP4_TMPO_H__

#include <cstddef>
#include <cstdint>
#include <cstring>

// Writes the iTunes `tmpo` atom - the one place in an MP4 file that players
// read a tempo from - which foobar2000 never writes. It stores the BPM field
// as a freeform "----:BPM" atom instead, visible to foobar2000 and to little
// else, and leaves any `tmpo` already in the file untouched beside it.
//
// This edits the file behind foobar2000's back, so it does as little as can
// be done and refuses everything else:
//
//   - a `tmpo` already there has its two bytes overwritten, and nothing else
//     in the file changes;
//   - otherwise a 26-byte `tmpo` is appended to the end of the tag list, and
//     only where that is also the end of `meta`, `udta` and `moov` and the
//     26 bytes can come out of a `free` box right behind it - or where moov
//     is the last thing in the file and the file can simply grow. Both keep
//     every byte of the audio where it was, so the chunk offset tables stay
//     true without being touched. That is the layout foobar2000 writes: tags
//     at the end of moov, then a few kilobytes of padding, then the audio.
//
// Anything else - a 64-bit box size on the path, tags that are not at the end
// of moov, no padding - is refused and left alone. Moving the audio would mean
// rewriting stco/co64 in every track, and that is a tag editor's job, not a
// BPM analyser's.
//
// No host in here, so foo_rubato_test can run it against byte buffers.

//! Positioned reads and writes on one open file.
struct mp4_io
{
	virtual ~mp4_io() {}
	virtual std::uint64_t size() = 0;
	virtual bool read(std::uint64_t offset, void * buffer, std::size_t bytes) = 0;
	virtual bool write(std::uint64_t offset, const void * buffer, std::size_t bytes) = 0;
};

enum mp4_tmpo_result
{
	mp4_tmpo_updated,        //!< an existing tmpo now holds the value
	mp4_tmpo_inserted,       //!< a new tmpo was added
	mp4_tmpo_unchanged,      //!< it already held the value
	mp4_tmpo_not_mp4,        //!< no moov/udta/meta/ilst to put it in
	mp4_tmpo_no_room,        //!< a layout this will not rewrite
	mp4_tmpo_io_error
};

inline const char * mp4_tmpo_result_name(mp4_tmpo_result r)
{
	switch (r)
	{
	case mp4_tmpo_updated:   return "updated";
	case mp4_tmpo_inserted:  return "inserted";
	case mp4_tmpo_unchanged: return "unchanged";
	case mp4_tmpo_not_mp4:   return "not an MP4 with a tag list";
	case mp4_tmpo_no_room:   return "no padding to write it into";
	default:                 return "read or write failed";
	}
}

namespace mp4_tmpo_detail
{
	inline std::uint32_t be32(const unsigned char * p)
	{
		return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
		       (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
	}

	inline void put32(unsigned char * p, std::uint32_t v)
	{
		p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16);
		p[2] = (unsigned char) (v >> 8);  p[3] = (unsigned char) v;
	}

	struct box
	{
		std::uint64_t pos = 0, size = 0;
		unsigned header = 8;
		char type[5] = { 0 };
		std::uint64_t end() const { return pos + size; }
	};

	//! The box at `pos`, which must end by `limit`. A 64-bit size is read so
	//! it can be stepped over, and flagged so it is never rewritten.
	inline bool read_box(mp4_io & io, std::uint64_t pos, std::uint64_t limit, box & b)
	{
		if (pos + 8 > limit) return false;
		unsigned char h[16];
		if (!io.read(pos, h, 8)) return false;
		b.pos = pos;
		b.size = be32(h);
		std::memcpy(b.type, h + 4, 4);
		b.type[4] = 0;
		b.header = 8;
		if (b.size == 1)
		{
			if (pos + 16 > limit || !io.read(pos + 8, h + 8, 8)) return false;
			b.size = (std::uint64_t(be32(h + 8)) << 32) | be32(h + 12);
			b.header = 16;
		}
		else if (b.size == 0)
		{
			b.size = limit - pos;
		}
		return b.size >= b.header && b.end() <= limit;
	}

	//! The first child of type `type` in [begin, end).
	inline bool find(mp4_io & io, std::uint64_t begin, std::uint64_t end,
	                 const char * type, box & out)
	{
		for (std::uint64_t pos = begin; pos + 8 <= end; )
		{
			box b;
			if (!read_box(io, pos, end, b)) return false;
			if (std::memcmp(b.type, type, 4) == 0) { out = b; return true; }
			pos = b.end();
		}
		return false;
	}

	inline bool rewrite_size(mp4_io & io, const box & b, std::uint64_t grow)
	{
		unsigned char h[4];
		put32(h, (std::uint32_t) (b.size + grow));
		return io.write(b.pos, h, 4);
	}
}

//! Puts `bpm` in the file's tmpo atom, creating one where that can be done
//! without moving anything. `bpm` is rounded by the caller; tmpo is 16 bits.
inline mp4_tmpo_result mp4_set_tmpo(mp4_io & io, std::uint16_t bpm)
{
	using namespace mp4_tmpo_detail;
	const std::uint64_t file_size = io.size();

	box moov, udta, meta, ilst;
	if (!find(io, 0, file_size, "moov", moov)) return mp4_tmpo_not_mp4;
	if (!find(io, moov.pos + moov.header, moov.end(), "udta", udta)) return mp4_tmpo_not_mp4;
	if (!find(io, udta.pos + udta.header, udta.end(), "meta", meta)) return mp4_tmpo_not_mp4;
	// meta is a full box: four bytes of version and flags before its children.
	if (!find(io, meta.pos + meta.header + 4, meta.end(), "ilst", ilst)) return mp4_tmpo_not_mp4;

	const unsigned char value[2] = { (unsigned char) (bpm >> 8), (unsigned char) bpm };

	box tmpo;
	if (find(io, ilst.pos + ilst.header, ilst.end(), "tmpo", tmpo))
	{
		box data;
		if (!find(io, tmpo.pos + tmpo.header, tmpo.end(), "data", data)) return mp4_tmpo_no_room;
		// data: header, 4 bytes of type, 4 of locale, then the value. Only the
		// two-byte integer iTunes writes is overwritten in place.
		if (data.size != data.header + 8 + 2) return mp4_tmpo_no_room;
		const std::uint64_t at = data.pos + data.header + 8;
		unsigned char old[2];
		if (!io.read(at, old, 2)) return mp4_tmpo_io_error;
		if (old[0] == value[0] && old[1] == value[1]) return mp4_tmpo_unchanged;
		return io.write(at, value, 2) ? mp4_tmpo_updated : mp4_tmpo_io_error;
	}

	// Appending: the tag list has to be the last thing in meta, meta in udta,
	// udta in moov, and none of them 64-bit, or growing it means moving
	// whatever follows.
	if (ilst.end() != meta.end() || meta.end() != udta.end() || udta.end() != moov.end())
		return mp4_tmpo_no_room;
	if (ilst.header != 8 || meta.header != 8 || udta.header != 8 || moov.header != 8)
		return mp4_tmpo_no_room;
	if (moov.size + 26 > 0xffffffffu) return mp4_tmpo_no_room;

	unsigned char atom[26] = { 0 };
	put32(atom, 26);            std::memcpy(atom + 4, "tmpo", 4);
	put32(atom + 8, 18);        std::memcpy(atom + 12, "data", 4);
	put32(atom + 16, 21);       // type 21: big-endian signed integer
	put32(atom + 20, 0);        // locale
	atom[24] = value[0];        atom[25] = value[1];

	const std::uint64_t at = moov.end();
	if (at == file_size)
	{
		// moov is last: the file grows and nothing after it moves, because
		// there is nothing after it.
		if (!io.write(at, atom, sizeof atom)) return mp4_tmpo_io_error;
	}
	else
	{
		box pad;
		if (!read_box(io, at, file_size, pad) || pad.header != 8 ||
		    (std::memcmp(pad.type, "free", 4) != 0 && std::memcmp(pad.type, "skip", 4) != 0))
			return mp4_tmpo_no_room;
		// Either the padding is used up exactly, or what is left of it is
		// still big enough to be a box.
		const std::uint64_t left = pad.size - 26;
		if (pad.size < 26 || (left != 0 && left < 8)) return mp4_tmpo_no_room;
		// The shrunken free header first, inside the old free box's payload
		// where it harms nothing; then the atom over the old header.
		if (left != 0)
		{
			unsigned char h[8];
			put32(h, (std::uint32_t) left);
			std::memcpy(h + 4, pad.type, 4);
			if (!io.write(at + 26, h, 8)) return mp4_tmpo_io_error;
		}
		if (!io.write(at, atom, sizeof atom)) return mp4_tmpo_io_error;
	}

	// And the four boxes that now hold 26 more bytes.
	if (!rewrite_size(io, ilst, 26) || !rewrite_size(io, meta, 26) ||
	    !rewrite_size(io, udta, 26) || !rewrite_size(io, moov, 26))
		return mp4_tmpo_io_error;
	return mp4_tmpo_inserted;
}

#endif // __MP4_TMPO_H__
