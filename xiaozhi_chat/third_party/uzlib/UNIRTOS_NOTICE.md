# uzlib for full-frame Emote playback

- Upstream: https://github.com/pfalcon/uzlib
- Pinned commit: 6d60d651a4499a64f2e5b21b4cc08d98cb84b5c1
- License: [zlib license and upstream notices](LICENSE).
- Only tinflate, tinfzlib, adler32 and crc32 are linked; no compressor/gzip/files/network.
- Local modifications in tinflate: reject reserved dynamic HLIT and negative
  literal/distance Huffman results before indexing, and explicit unsigned casts
  for validated nonnegative bounds. Paranoid checks enabled; warnings not hidden.
- Decoder input is the compiled SHA-pinned generated resource, not network data.
  The player enforces the zlib window, output size, palette indices, stream
  boundary, Adler32 and full-clip CRC32. No allocation or filesystem access.