#if !defined(_RSYS_SOUNDDECODE_H_)
#define _RSYS_SOUNDDECODE_H_

/* MacPhoenix: sound sample formats to the mixer's own, 8-bit offset-binary
   mono (sound.cpp mixes nothing else), for the Movie Toolbox's sound media
   (movies.cpp; docs/executor/QuickTime.md, section 12).

   The formats of QuickTime's sound sample descriptions (Inside Macintosh:
   QuickTime, "Sound Sample Descriptions"; Apple's QuickTime File Format):
   'raw ' (offset binary), 'twos' (signed, big-endian), 'sowt' (signed,
   little-endian), 'NONE' (raw at 8 bits, twos at 16), 'ulaw' and 'alaw'
   (ITU-T G.711), 'ima4' (IMA ADPCM, the IMA's Recommended Practices for
   Enhancing Digital Audio Compatibility, in Apple's packets: a 2-byte
   header, the predictor's high 9 bits and the step index, then 64 4-bit
   codes, low nibble first; a packet a channel for each 64 frames). MACE
   ('MAC3', 'MAC6') is not decoded: no published description. */

#include <cstddef>
#include <cstdint>

namespace Executor
{
/* frames in one packet of the format (ima4: 64, MACE: 6, others 1) */
uint32_t ROMlib_sound_frames_per_packet(uint32_t format);

/* bytes of one packet, all channels */
uint32_t ROMlib_sound_bytes_per_packet(uint32_t format, int channels, int bits);

/* whether ROMlib_decode_sound knows the format */
bool ROMlib_sound_format_known(uint32_t format, int bits);

/* frames frames, from skip frames into the packets at in (in_bytes of
   them), to out as 8-bit offset binary, the channels' mean; false (out
   left silent) for a format it doesn't know or too few bytes */
bool ROMlib_decode_sound(uint32_t format, int channels, int bits,
                         const uint8_t *in, size_t in_bytes,
                         uint32_t skip, uint32_t frames, uint8_t *out);
}

#endif /* !_RSYS_SOUNDDECODE_H_ */
