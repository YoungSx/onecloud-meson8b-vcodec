// SPDX-License-Identifier: GPL-2.0+
/* Rate control against a synthetic encoder whose bits halve every 6 QP. */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
#define div64_u64(a, b) ((a) / (b))
#define div64_s64(a, b) ((a) / (s64)(b))
#include "../venc/rate-control.h"
#define printf(...) fprintf(stderr, __VA_ARGS__)

struct run { double average_kbps, peak_kbps; s32 min_qp, max_qp; };

/* @complexity(frame): P bits at QP 26; IDR pictures cost five times more. */
static struct run simulate(struct venc_rc *rc, unsigned int frames, unsigned int gop,
			   double (*complexity)(unsigned int), unsigned int from)
{
	double window[30] = {0}, sum = 0, total = 0, peak = 0;
	struct run run = { 0, 0, 99, -1 };
	s32 previous = rc->qp;
	rc->gop_size = gop;

	for (unsigned int frame = 0; frame < frames; frame++) {
		bool idr = frame % gop == 0;
		s32 qp = venc_rc_picture_qp(rc, idr);
		double bits = complexity(frame) * (idr ? 5 : 1) * pow(2, (26.0 - qp) / 6);
		u32 bytes = (u32)(bits / 8);

		assert(qp >= rc->qp_min && qp <= rc->qp_max);
		venc_rc_update(rc, bytes, qp, idr);
		/* upward jumps are unlimited only right after a scene cut */
		assert(rc->qp >= previous - VENC_RC_MAX_DOWN);
		assert(rc->fullness <= venc_rc_capacity(rc) && rc->fullness >= -venc_rc_capacity(rc) / 2);
		previous = rc->qp;
		sum += bytes * 8.0 - window[frame % 30];
		window[frame % 30] = bytes * 8.0;
		if (frame >= from) {
			total += bytes * 8.0;
			if (frame >= from + 29 && sum > peak)
				peak = sum;
			if (qp < run.min_qp) run.min_qp = qp;
			if (qp > run.max_qp) run.max_qp = qp;
		}
	}
	run.average_kbps = total * 30 / (frames - from) / 1000;
	run.peak_kbps = peak / 1000;
	return run;
}

static double steady(unsigned int frame) { (void)frame; return 60000; }
static double cut(unsigned int frame) { return frame < 150 ? 20000 : 80000; }
static double easy_then_hard(unsigned int frame) { return frame < 315 ? 2000 : 70000; }
static double cut_on_idr(unsigned int frame) { return frame < 300 ? 2000 : 70000; }

static void configure(struct venc_rc *rc, u64 bitrate)
{
	rc->bitrate = bitrate;
	rc->numerator = 1;
	rc->denominator = 30;
	rc->qp_min = 0;
	rc->qp_max = 51;
	rc->i_offset = 0;
	rc->gop_size = 30;
	venc_rc_reset(rc, 26);
}

int main(void)
{
	struct venc_rc rc;
	struct run run;

	for (s32 delta = -51; delta <= 51; delta++) {
		double exact = 1000000.0 * pow(2, delta / 6.0);

		assert(fabs(venc_rc_scale(1000000, delta) - exact) <= exact * 0.001 + 1);
	}
	/* steady content converges to the target, IDRs included */
	configure(&rc, 2000000);
	run = simulate(&rc, 300, 12, steady, 60);
	printf("steady: %.0f kbps, peak %.0f, QP %d..%d\n", run.average_kbps, run.peak_kbps,
	       run.min_qp, run.max_qp);
	assert(fabs(run.average_kbps - 2000) < 2000 * 0.05 && run.peak_kbps < 2000 * 1.3);
	/* a 4x cut re-converges within the next seconds and bounds the 1 s peak */
	configure(&rc, 2000000);
	run = simulate(&rc, 300, 30, cut, 150);
	printf("cut: %.0f kbps, peak %.0f, QP %d..%d\n", run.average_kbps, run.peak_kbps,
	       run.min_qp, run.max_qp);
	assert(fabs(run.average_kbps - 2000) < 2000 * 0.1 && run.peak_kbps < 2000 * 1.6);
	/* ten easy seconds must not bank credit that floods the hard part;
	 * only the unpredictable first hard picture overshoots */
	configure(&rc, 2000000);
	run = simulate(&rc, 600, 30, easy_then_hard, 315);
	printf("easy then hard: %.0f kbps, peak %.0f, QP %d..%d\n", run.average_kbps,
	       run.peak_kbps, run.min_qp, run.max_qp);
	assert(run.peak_kbps < 2000 * 2.5 && fabs(run.average_kbps - 2000) < 2000 * 0.15);
	/* a cut on an IDR: the IDR overshoots, the next GOP is back on target */
	configure(&rc, 2000000);
	run = simulate(&rc, 600, 30, cut_on_idr, 330);
	printf("cut on IDR: %.0f kbps, peak %.0f, QP %d..%d\n", run.average_kbps,
	       run.peak_kbps, run.min_qp, run.max_qp);
	assert(run.peak_kbps < 2000 * 1.3 && fabs(run.average_kbps - 2000) < 2000 * 0.1);
	/* All-IDR and short GOPs must track a reachable target without P history. */
	for (unsigned int gop = 1; gop <= 4; gop *= 2) {
		configure(&rc, 6000000);
		run = simulate(&rc, 600, gop, steady, 120);
		printf("gop %u: %.0f kbps, peak %.0f\n", gop,
		       run.average_kbps, run.peak_kbps);
		assert(fabs(run.average_kbps - 6000) < 6000 * 0.06);
	}
	/* Dense forced IDRs converge even when a long GOP was configured. */
	configure(&rc, 6000000);
	for (unsigned int frame = 0; frame < 300; frame++) {
		s32 qp = venc_rc_picture_qp(&rc, true);
		u32 bytes = (u32)(300000.0 * pow(2, (26.0 - qp) / 6) / 8);

		venc_rc_update(&rc, bytes, qp, true);
	}
	assert(rc.idr_run == 2 && rc.qp > 26);
	/* an unreachable target stops at the ceiling or a lower QP maximum */
	configure(&rc, 20000);
	run = simulate(&rc, 120, 12, steady, 0);
	assert(rc.qp == VENC_RC_QP_CEILING);
	configure(&rc, 20000);
	rc.qp_max = 30;
	run = simulate(&rc, 120, 12, steady, 0);
	assert(run.max_qp == 30 && rc.qp == 30);
	/* a minimum above the ceiling wins */
	configure(&rc, 20000);
	rc.qp_min = 45;
	venc_rc_reset(&rc, 26);
	assert(rc.qp == 45 && venc_rc_clamp_p(&rc, 51) == 45);
	/* extreme inputs stay in range: 100 Mbit/s, 1 MiB pictures, 1/240 s */
	configure(&rc, 100000000);
	rc.denominator = 240;
	for (int i = 0; i < 100; i++)
		venc_rc_update(&rc, 1 << 20, venc_rc_picture_qp(&rc, i % 4 == 0), i % 4 == 0);
	assert(rc.qp >= 0 && rc.qp <= 51);
	/* A pending update cannot alter the active picture's budget. */
	configure(&rc, 2000000);
	struct venc_rc config = rc;
	config.bitrate = 3000000;
	assert(venc_rc_budget(&rc) == 2000000 / 30);
	venc_rc_update(&rc, 10000, 26, false);
	u64 model = rc.p_complexity;
	venc_rc_configure(&rc, &config, false, 26);
	assert(rc.bitrate == 3000000 && rc.fullness == 0 && rc.p_complexity == model);
	config.numerator = 1001;
	config.denominator = 30000;
	venc_rc_configure(&rc, &config, false, 26);
	assert(!rc.p_complexity && !rc.i_complexity && !rc.fullness);
	assert(venc_rc_budget(&rc) == 100100);
	/* Recovery floor survives success and terminates repeated overflow. */
	struct venc_rc_recovery recovery = {0};
	venc_rc_overflow(&recovery, &rc, 32);
	assert(recovery.floor == 38 && recovery.failures == 1 && !recovery.terminal);
	venc_rc_overflow(&recovery, &rc, recovery.floor);
	venc_rc_overflow(&recovery, &rc, recovery.floor);
	assert(recovery.terminal && recovery.failures == 3);
	/* Measured windows use media time, include all bytes and remain bounded. */
	struct venc_rc_measurement measurement = {0};
	configure(&rc, 2000000);
	for (int frame = 0; frame < 300; frame++)
		venc_rc_measure(&measurement, &rc, 10000, 32);
	assert(measurement.count <= 31 && measurement.count >= 30);
	assert(measurement.current_bps >= 2399990 && measurement.current_bps <= 2400010);
	assert(measurement.saturated_frames == 300 && measurement.overshoots == 300);
	assert(measurement.bytes == 3000000);
	measurement = (struct venc_rc_measurement){0};
	rc.numerator = 1001;
	rc.denominator = 30000;
	for (int frame = 0; frame < 300; frame++)
		venc_rc_measure(&measurement, &rc, 10000, 26);
	assert(measurement.current_bps >= 2397590 && measurement.current_bps <= 2397620);
	assert(!measurement.saturated_frames);
	puts("rate control checks passed");
	return 0;
}
