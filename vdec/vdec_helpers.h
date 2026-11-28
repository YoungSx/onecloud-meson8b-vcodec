/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_HELPERS_H_
#define __MESON_VDEC_HELPERS_H_

#include "vdec.h"

/**
 * amvdec_set_canvases() - Map VB2 buffers to canvases
 *
 * @sess: current session
 * @reg_base: Registry bases of where to write the canvas indexes
 * @reg_num: number of contiguous registers after each reg_base (including it)
 */
int amvdec_set_canvases(struct amvdec_session *sess,
			u32 reg_base[], u32 reg_num[]);
int amvdec_canvas_alloc(struct amvdec_session *sess, u8 *canvas_id);

/* Helpers to read/write to the various IPs (DOS, PARSER) */
u32 amvdec_read_dos(struct amvdec_core *core, u32 reg);
void amvdec_write_dos(struct amvdec_core *core, u32 reg, u32 val);
void amvdec_write_dos_bits(struct amvdec_core *core, u32 reg, u32 val);
void amvdec_clear_dos_bits(struct amvdec_core *core, u32 reg, u32 val);
u32 amvdec_read_parser(struct amvdec_core *core, u32 reg);
void amvdec_write_parser(struct amvdec_core *core, u32 reg, u32 val);

u32 amvdec_am21c_body_size(u32 width, u32 height);
u32 amvdec_am21c_head_size(u32 width, u32 height);
u32 amvdec_am21c_size(u32 width, u32 height);

/**
 * amvdec_dst_buf_done_idx() - Signal that a buffer is done decoding
 *
 * @sess: current session
 * @buf_idx: hardware buffer index
 * @offset: VIFIFO bitstream offset corresponding to the buffer
 * @field: V4L2 interlaced field
 */
void amvdec_dst_buf_done_idx(struct amvdec_session *sess, u32 buf_idx,
			     u32 offset, u32 field);
void amvdec_dst_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf, u32 field);
void amvdec_dst_buf_done_offset(struct amvdec_session *sess,
				struct vb2_v4l2_buffer *vbuf,
				u32 offset, u32 field, bool allow_drop);
bool amvdec_timestamp_for_offset(struct amvdec_session *sess, u32 offset, u64 *timestamp);

/**
 * amvdec_add_ts() - Add a timestamp to the list
 *
 * @sess: current session
 * @ts: timestamp to add
 * @offset: offset in the VIFIFO where the associated packet was written
 * @flags the vb2_v4l2_buffer flags
 */
int amvdec_add_ts(struct amvdec_session *sess, u64 ts,
		  struct v4l2_timecode tc, u32 offset, u32 flags);
void amvdec_remove_ts(struct amvdec_session *sess, u64 ts);
void amvdec_drop_picture_offset(struct amvdec_session *sess, u32 offset);

/* Legacy ratio codes: 6 entries for MPEG-4/H.263, 14 for VC-1. */
void amvdec_set_par_from_ratio(struct amvdec_session *sess, u32 ratio, u32 idc_count);

/**
 * amvdec_set_par_from_dar() - Set Pixel Aspect Ratio from Display Aspect Ratio
 *
 * @sess: current session
 * @dar_num: numerator of the DAR
 * @dar_den: denominator of the DAR
 */
void amvdec_set_par_from_dar(struct amvdec_session *sess,
			     u32 dar_num, u32 dar_den);

/**
 * amvdec_src_change() - Notify new resolution/DPB size to the core
 *
 * @sess: current session
 * @width: picture width detected by the hardware
 * @height: picture height detected by the hardware
 * @dpb_size: Decoded Picture Buffer size (= amount of buffers for decoding)
 */
void amvdec_src_change(struct amvdec_session *sess, u32 width,
		       u32 height, u32 dpb_size);

/**
 * amvdec_abort() - Abort the current decoding session
 *
 * @sess: current session
 */
void amvdec_abort(struct amvdec_session *sess);
void amvdec_flush_capture_buffers(struct amvdec_session *sess);

/* Hand firmware buffer @fw_idx back to the firmware (recycle thread). */
void amvdec_recycle(struct amvdec_session *sess, u32 fw_idx);

/*
 * Meson8b frame pool (meson8b-capture.c): the decoder writes into pool
 * slots, and finished pictures are converted into CAPTURE buffers as
 * userspace queues them. See struct amvdec_frame_pool.
 */
bool amvdec_has_pool(const struct amvdec_session *sess);
extern const struct vb2_mem_ops amvdec_capture_memops;
void amvdec_pool_init(struct amvdec_session *sess);
int amvdec_pool_prepare(struct amvdec_session *sess, u32 count);
void amvdec_pool_give_all(struct amvdec_session *sess);
u32 amvdec_pool_count(struct amvdec_session *sess);
const struct amvdec_shadow *amvdec_pool_slot(struct amvdec_session *sess,
					     u32 slot);
int amvdec_pool_take(struct amvdec_session *sess);
void amvdec_pool_release(struct amvdec_session *sess, u32 slot);
void amvdec_pool_push(struct amvdec_session *sess, u32 slot,
		      const struct amvdec_picture *pic);
void amvdec_pool_capture_queued(struct amvdec_session *sess);
void amvdec_pool_end(struct amvdec_session *sess, enum amvdec_pool_end end);
void amvdec_pool_capture_stop(struct amvdec_session *sess);
void amvdec_pool_free(struct amvdec_session *sess);

/**
 * amvdec_pool_picture_done() - The picture in a pool slot is finished
 *
 * @sess: current session
 * @slot: pool slot holding the picture
 * @offset: VIFIFO bitstream offset of the picture, or -1 for the oldest
 *	    queued timestamp
 * @field: V4L2 interlaced field
 */
void amvdec_pool_picture_done(struct amvdec_session *sess, u32 slot,
			      u32 offset, u32 field);
#endif
