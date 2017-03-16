/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post
    Copyright (C) 2017 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "rawspeedconfig.h"

#ifdef HAVE_JPEG

#include "decompressors/JpegDecompressor.h"
#include "common/Common.h"                // for uchar8, uint32, ushort16
#include "common/Memory.h"                // for alignedFree, alignedMalloc...
#include "common/Point.h"                 // for iPoint2D
#include "decoders/RawDecoderException.h" // for ThrowRDE
#include "io/ByteStream.h"                // for ByteStream
#include <algorithm>                      // for min
#include <cstdio>                         // for size_t
#include <memory>                         // for unique_ptr
#include <vector>                         // for vector

#ifndef HAVE_JPEG_MEM_SRC
#include "io/IOException.h" // for ThrowIOE
#endif

extern "C" {
#include <jpeglib.h> // for jpeg_source_mgr, jpeg_decompress_struct
// IWYU pragma: no_include <jconfig.h>
// IWYU pragma: no_include <jmorecfg.h>
}

using namespace std;

namespace RawSpeed {

#ifdef HAVE_JPEG_MEM_SRC

#define JPEG_MEMSRC(A, B, C) jpeg_mem_src(A, B, C)

#else

#define JPEG_MEMSRC(A, B, C) jpeg_mem_src_int(A, B, C)
/* Read JPEG image from a memory segment */

static void init_source(j_decompress_ptr cinfo) {}
static boolean fill_input_buffer(j_decompress_ptr cinfo) {
  auto* src = (struct jpeg_source_mgr*)cinfo->src;
  return (boolean) !!src->bytes_in_buffer;
}
static void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
  auto* src = (struct jpeg_source_mgr*)cinfo->src;

  if (num_bytes > (int)src->bytes_in_buffer)
    ThrowIOE("read out of buffer");
  if (num_bytes > 0) {
    src->next_input_byte += (size_t)num_bytes;
    src->bytes_in_buffer -= (size_t)num_bytes;
  }
}
static void term_source(j_decompress_ptr cinfo) {}
static void jpeg_mem_src_int(j_decompress_ptr cinfo,
                             const unsigned char* buffer, long nbytes) {
  struct jpeg_source_mgr* src;

  if (cinfo->src == nullptr) { /* first time for this JPEG object? */
    cinfo->src = (struct jpeg_source_mgr*)(*cinfo->mem->alloc_small)(
        (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));
  }

  src = (struct jpeg_source_mgr*)cinfo->src;
  src->init_source = init_source;
  src->fill_input_buffer = fill_input_buffer;
  src->skip_input_data = skip_input_data;
  src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->term_source = term_source;
  src->bytes_in_buffer = nbytes;
  src->next_input_byte = (const JOCTET*)buffer;
}

#endif

METHODDEF(void)
my_error_throw(j_common_ptr cinfo) { ThrowRDE("JPEG decoder error!"); }

struct JpegDecompressor::JpegDecompressStruct : jpeg_decompress_struct {
  struct jpeg_error_mgr jerr;
  JpegDecompressStruct() {
    jpeg_create_decompress(this);

    err = jpeg_std_error(&jerr);
    jerr.error_exit = my_error_throw;
  }
  ~JpegDecompressStruct() { jpeg_destroy_decompress(this); }
};

void JpegDecompressor::decode(uint32 offX,
                              uint32 offY) { /* Each slice is a JPEG image */
  struct JpegDecompressStruct dinfo;

  vector<JSAMPROW> buffer(1);

  JPEG_MEMSRC(&dinfo, input.getData(input.getRemainSize()),
              input.getRemainSize());

  if (JPEG_HEADER_OK != jpeg_read_header(&dinfo, static_cast<boolean>(true)))
    ThrowRDE("Unable to read JPEG header");

  jpeg_start_decompress(&dinfo);
  if (dinfo.output_components != (int)mRaw->getCpp())
    ThrowRDE("Component count doesn't match");
  int row_stride = dinfo.output_width * dinfo.output_components;

  unique_ptr<uchar8[], decltype(&alignedFree)> complete_buffer(
      (uchar8*)alignedMallocArray<16>(dinfo.output_height, row_stride),
      alignedFree);
  while (dinfo.output_scanline < dinfo.output_height) {
    buffer[0] = (JSAMPROW)(
        &complete_buffer[(size_t)dinfo.output_scanline * row_stride]);
    if (0 == jpeg_read_scanlines(&dinfo, &buffer[0], 1))
      ThrowRDE("JPEG Error while decompressing image.");
  }
  jpeg_finish_decompress(&dinfo);

  // Now the image is decoded, and we copy the image data
  int copy_w = min(mRaw->dim.x - offX, dinfo.output_width);
  int copy_h = min(mRaw->dim.y - offY, dinfo.output_height);
  for (int y = 0; y < copy_h; y++) {
    uchar8* src = &complete_buffer[(size_t)row_stride * y];
    auto* dst = (ushort16*)mRaw->getData(offX, y + offY);
    for (int x = 0; x < copy_w; x++) {
      for (int c = 0; c < dinfo.output_components; c++)
        *dst++ = (*src++);
    }
  }
}

} // namespace RawSpeed

#else

#pragma message                                                                \
    "JPEG is not present! Lossy JPEG compression will not be supported!"

#endif
