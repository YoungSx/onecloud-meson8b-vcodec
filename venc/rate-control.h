/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Frame-level H.264 rate control for the HCODEC encoder.
 *
 * The microcode quantizes each picture with the QP the host programs; it has
 * no rate control of its own. After each picture the host updates a leaky
 * bucket of one second and a P-picture complexity estimate (bits normalized
 * to QP 26, assuming bits halve every 6 QP), then picks the next QP whose
 * estimate drains the bucket over one second. State is bounded, so long
 * easy or hard runs do not wind up, and a scene cut resets the estimate.
 *
 * Plain integer C shared with tests/test_rate_control.c.
 */
#ifndef MESON_VENC_RATE_CONTROL_H
#define MESON_VENC_RATE_CONTROL_H

#ifdef __KERNEL__
#include <linux/math64.h>
#include <linux/types.h>
#endif

#define VENC_RC_REFERENCE_QP	26
#define VENC_RC_MAX_DOWN	2	/* QP steps per picture toward finer */
#define VENC_RC_MAX_UP		6	/* QP steps per picture toward coarser */
/*
 * Past QP 32 this microcode skips fewer macroblocks (its forced-skip SAD
 * thresholds do not scale with QP), so P pictures stop shrinking: at 720p
 * QP 32/36/40, static content takes 766/1063/1738 kbit/s and scene content
 * 7478/8001/7587, while PSNR falls 6 dB. Rate control never raises the P QP
 * past this; fixed QPs may still use the whole range.
 */
#define VENC_RC_QP_CEILING	32

struct venc_rc {
	u64 bitrate;			/* bits per second, > 0 */
	u32 numerator, denominator;	/* seconds per picture */
	s32 qp_min, qp_max;
	s32 i_offset;			/* IDR QP relative to the P QP */
	u32 gop_size;			/* 1 is all-IDR; 0 has no periodic IDRs */

	s64 fullness;			/* bits above budget, in [-capacity/2, capacity] */
	u64 p_complexity, i_complexity;	/* 0 until the first such picture */
	s32 qp;				/* QP of the next P picture */
	u32 idr_run;			/* consecutive IDRs, including forced ones */
};

/* 2^(n/6) in Q16 for n = 0..5 */
static const u32 venc_rc_step[6] = { 65536, 73562, 82570, 92682, 104032, 116772 };

/* value * 2^(delta/6), for |delta| <= 51 */
static inline u64 venc_rc_scale(u64 value, s32 delta)
{
	s32 octaves = delta >= 0 ? delta / 6 : -((5 - delta) / 6);
	u64 scaled = value * venc_rc_step[delta - octaves * 6];

	return octaves >= 0 ? (scaled << octaves) >> 16 : scaled >> (16 - octaves);
}

static inline u64 venc_rc_budget(const struct venc_rc *rc)
{
	u64 bits = div64_u64(rc->bitrate * rc->numerator, rc->denominator);

	return bits ? bits : 1;
}

static inline s64 venc_rc_capacity(const struct venc_rc *rc)
{
	u64 budget = venc_rc_budget(rc);

	return rc->bitrate > 2 * budget ? rc->bitrate : 2 * budget;
}

/* Finest QP whose estimate fits @target bits. */
static inline s32 venc_rc_qp_for(const struct venc_rc *rc, u64 complexity, u64 target)
{
	s32 qp;

	for (qp = rc->qp_min; qp < rc->qp_max; qp++)
		if (venc_rc_scale(complexity, VENC_RC_REFERENCE_QP - qp) <= target)
			break;
	return qp;
}

static inline s32 venc_rc_clamp(const struct venc_rc *rc, s32 qp)
{
	return qp < rc->qp_min ? rc->qp_min : qp > rc->qp_max ? rc->qp_max : qp;
}

/* P QPs rate control may choose: the QP range, capped at the ceiling */
static inline s32 venc_rc_clamp_p(const struct venc_rc *rc, s32 qp)
{
	s32 ceiling = rc->qp_max < VENC_RC_QP_CEILING ? rc->qp_max : VENC_RC_QP_CEILING;

	if (ceiling < rc->qp_min)
		ceiling = rc->qp_min;
	return qp < rc->qp_min ? rc->qp_min : qp > ceiling ? ceiling : qp;
}

/* Restart from @qp: the bucket and both estimates are dropped. */
static inline void venc_rc_reset(struct venc_rc *rc, s32 qp)
{
	rc->fullness = 0;
	rc->p_complexity = 0;
	rc->i_complexity = 0;
	rc->idr_run = 0;
	rc->qp = venc_rc_clamp_p(rc, qp);
}

/* Apply pending controls at a picture boundary; history belongs to this config. */
static inline void venc_rc_configure(struct venc_rc *rc,
				     const struct venc_rc *config,
				     bool restart, s32 seed)
{
	bool period_changed = rc->numerator != config->numerator ||
			      rc->denominator != config->denominator;
	bool rate_changed = rc->bitrate != config->bitrate;

	rc->bitrate = config->bitrate;
	rc->numerator = config->numerator;
	rc->denominator = config->denominator;
	rc->qp_min = config->qp_min;
	rc->qp_max = config->qp_max;
	rc->i_offset = config->i_offset;
	rc->gop_size = config->gop_size;
	if (restart || period_changed) {
		venc_rc_reset(rc, seed);
	} else {
		if (rate_changed)
			rc->fullness = 0;
		rc->qp = rc->gop_size == 1 || rc->idr_run > 1 ? venc_rc_clamp(rc, rc->qp) :
			 venc_rc_clamp_p(rc, rc->qp);
	}
}

static inline u64 venc_rc_target(const struct venc_rc *rc)
{
	u64 budget = venc_rc_budget(rc);
	s64 target = (s64)budget - div64_s64(rc->fullness * rc->numerator,
					  rc->denominator);

	if (target < (s64)(budget / 8))
		target = budget / 8;
	else if (target > (s64)(2 * budget))
		target = 2 * budget;
	return target > 0 ? target : 1;
}

static inline s32 venc_rc_picture_qp(const struct venc_rc *rc, bool idr)
{
	s32 qp, floor;

	if (!idr)
		return venc_rc_clamp_p(rc, rc->qp);
	qp = venc_rc_clamp(rc, rc->qp + rc->i_offset);
	/* A prediction, not a peak guarantee. All-IDR uses a per-picture budget. */
	if (rc->i_complexity) {
		u64 target = rc->gop_size == 1 || rc->idr_run > 1 ?
			venc_rc_target(rc) : (u64)venc_rc_capacity(rc) / 3;

		floor = venc_rc_qp_for(rc, rc->i_complexity, target);
		if (qp < floor)
			qp = floor;
	}
	return qp;
}

static inline u64 venc_rc_average(u64 average, u64 sample)
{
	/* a scene cut makes the old average meaningless */
	if (!average || sample > 3 * average)
		return sample;
	return (3 * average + sample) / 4;
}

/* Account a finished picture coded at @qp and choose the next P QP. */
static inline void venc_rc_update(struct venc_rc *rc, u32 bytes, s32 qp, bool idr)
{
	u64 bits = (u64)bytes * 8, budget = venc_rc_budget(rc);
	u64 complexity = venc_rc_scale(bits, qp - VENC_RC_REFERENCE_QP);
	s64 capacity = venc_rc_capacity(rc);
	u64 target, model;
	bool cut = !idr && rc->p_complexity && complexity > 3 * rc->p_complexity;
	s32 next;

	rc->fullness += (s64)bits - (s64)budget;
	if (rc->fullness > capacity)
		rc->fullness = capacity;
	else if (rc->fullness < -capacity / 2)
		rc->fullness = -capacity / 2;
	if (idr) {
		if (rc->idr_run < 2)
			rc->idr_run++;
		/* a cut on an IDR scales the P estimate by the same factor */
		if (rc->i_complexity && complexity > 3 * rc->i_complexity && rc->p_complexity) {
			rc->p_complexity = div64_u64(rc->p_complexity * complexity,
						     rc->i_complexity);
			cut = true;
		}
		rc->i_complexity = venc_rc_average(rc->i_complexity, complexity);
	} else {
		rc->idr_run = 0;
		rc->p_complexity = venc_rc_average(rc->p_complexity, complexity);
	}
	model = rc->gop_size == 1 || rc->idr_run > 1 ?
		rc->i_complexity : rc->p_complexity;
	if (!model)
		return;

	/* drain the bucket over one second */
	target = venc_rc_target(rc);
	next = venc_rc_qp_for(rc, model, target);
	if (rc->gop_size == 1 || rc->idr_run > 1)
		next -= rc->i_offset;
	/* After a cut the estimate is fresh, so follow it at once. */
	if (next < rc->qp - VENC_RC_MAX_DOWN)
		next = rc->qp - VENC_RC_MAX_DOWN;
	else if (!cut && next > rc->qp + VENC_RC_MAX_UP)
		next = rc->qp + VENC_RC_MAX_UP;
	rc->qp = rc->gop_size == 1 || rc->idr_run > 1 ?
		venc_rc_clamp(rc, next) : venc_rc_clamp_p(rc, next);
}

/* Overflow recovery is separate from the normal P-QP prediction ceiling. */
#define VENC_RC_MAX_OVERFLOWS 3
struct venc_rc_recovery {
	s32 floor;
	u32 failures;
	bool terminal;
};

static inline void venc_rc_overflow(struct venc_rc_recovery *r,
				   const struct venc_rc *rc, s32 qp)
{
	r->failures++;
	r->floor = venc_rc_clamp(rc, qp + VENC_RC_MAX_UP);
	r->terminal = r->failures >= VENC_RC_MAX_OVERFLOWS || qp >= rc->qp_max;
}

#define VENC_RC_WINDOW_FRAMES 240
struct venc_rc_measurement {
	u32 bits[VENC_RC_WINDOW_FRAMES];
	u64 ns[VENC_RC_WINDOW_FRAMES];
	u32 head, count;
	u64 window_bits, window_ns;
	u64 frames, bytes, peak_bps, current_bps;
	u64 saturated_frames, saturated_ns, streak_ns, overshoots, overflows;
	u64 target_bps;
	s32 qp;
};

static inline void venc_rc_measure(struct venc_rc_measurement *m,
				   const struct venc_rc *rc, u32 bytes, s32 qp)
{
	u64 ns = div64_u64((u64)rc->numerator * 1000000000u, rc->denominator);
	u32 index, ceiling = rc->gop_size == 1 || rc->idr_run > 1 ?
		rc->qp_max : (rc->qp_max < VENC_RC_QP_CEILING ?
			      rc->qp_max : VENC_RC_QP_CEILING);

	if (!ns)
		ns = 1;
	while (m->count && (m->count == VENC_RC_WINDOW_FRAMES ||
		m->window_ns + ns - m->ns[m->head] >= 1000000000u)) {
		m->window_ns -= m->ns[m->head];
		m->window_bits -= m->bits[m->head];
		m->head = (m->head + 1) % VENC_RC_WINDOW_FRAMES;
		m->count--;
	}
	index = (m->head + m->count++) % VENC_RC_WINDOW_FRAMES;
	m->bits[index] = bytes * 8;
	m->ns[index] = ns;
	m->window_bits += bytes * 8;
	m->window_ns += ns;
	m->frames++;
	m->bytes += bytes;
	m->target_bps = rc->bitrate;
	m->qp = qp;
	m->current_bps = div64_u64(m->window_bits * 1000000000u, m->window_ns);
	if (m->current_bps > m->peak_bps)
		m->peak_bps = m->current_bps;
	if (qp >= (s32)ceiling) {
		m->saturated_frames++;
		m->saturated_ns += ns;
		m->streak_ns += ns;
	} else {
		m->streak_ns = 0;
	}
	if (m->current_bps > rc->bitrate)
		m->overshoots++;
}

#endif
