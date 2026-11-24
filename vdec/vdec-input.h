/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON_VDEC_INPUT_H
#define MESON_VDEC_INPUT_H

/* OUTPUT bytesused includes data_offset; validate their bounds at buf_prepare. */
static inline u32 amvdec_input_size(struct vb2_buffer *vb)
{
	return vb2_get_plane_payload(vb, 0) - vb->planes[0].data_offset;
}

static inline unsigned long amvdec_input_capacity(struct vb2_buffer *vb)
{
	return vb2_plane_size(vb, 0) - vb->planes[0].data_offset;
}

static inline u8 *amvdec_input_data(struct vb2_buffer *vb)
{
	u8 *data = vb2_plane_vaddr(vb, 0);

	return data ? data + vb->planes[0].data_offset : NULL;
}

static inline void amvdec_input_set_size(struct vb2_buffer *vb, u32 size)
{
	vb2_set_plane_payload(vb, 0, vb->planes[0].data_offset + size);
}

#endif
