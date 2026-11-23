// SPDX-License-Identifier: GPL-2.0+
/* Host boundary for driver.inc, extracted unchanged from production sources.
 * Firmware supplies synthetic identities, offsets and PTS, not decoded pixels.
 * Work and dequeue steps are deliberately delayed but run on one host thread.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int irqreturn_t;
#include "dos_regs.h"
#include "meson8b-pool.h"

#define BIT(n) (1u << (n))
#define GENMASK(h, l) ((~0u >> (31 - (h))) & (~0u << (l)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define EXPORT_SYMBOL_GPL(x)
#define dev_dbg(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
#define kfree(p) free(p)
#define msecs_to_jiffies(n) (n)
#define time_is_after_jiffies64(t) ((t) > now)
#define VDEC_REVISION_MESON8B 1
#define STATUS_RUNNING 1
#define STATUS_NEEDS_RESUME 2
#define VB2_BUF_STATE_DONE 1
#define VB2_BUF_STATE_ERROR 2
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define IRQ_WAKE_THREAD 2
#define NPICTURES 300
#define NBUFFERS 10

struct list_head { struct list_head *next, *prev; };
#define list_for_each_entry(p, h, m) \
	for (p = container_of((h)->next, __typeof__(*p), m); \
	     &p->m != (h); p = container_of(p->m.next, __typeof__(*p), m))
#define list_for_each_entry_safe(p, n, h, m) \
	for (p = container_of((h)->next, __typeof__(*p), m), \
	     n = container_of(p->m.next, __typeof__(*p), m); &p->m != (h); \
	     p = n, n = container_of(n->m.next, __typeof__(*n), m))
static void list_del(struct list_head *p)
{
	p->prev->next = p->next;
	p->next->prev = p->prev;
}
#define spin_lock_irqsave(p, f) ((void)(p), (f) = 0)
#define spin_unlock_irqrestore(p, f) ((void)(p), (void)(f))

struct work_struct { bool pending; };
struct vb2_buffer {
	u64 timestamp;
	u32 type, num_planes, bytesused[2];
	void *vb2_queue;
	int identity;
	u32 width, height;
};
struct vb2_v4l2_buffer {
	struct vb2_buffer vb2_buf;
	u32 sequence, flags, timecode, field;
};
struct amvdec_vb2_buffer { struct vb2_v4l2_buffer vb; bool eos; int state; };
struct amvdec_geometry {
	u32 width, height, reference_width, reference_height, crop_left, crop_top;
};
struct amvdec_picture {
	struct amvdec_geometry geo;
	u64 timestamp;
	u32 flags, timecode, field;
	bool restart, eos;
};
struct amvdec_shadow { int identity; u32 width, height; };
enum amvdec_pool_end { AMVDEC_POOL_RUNNING, AMVDEC_POOL_FLUSH, AMVDEC_POOL_LAST };
struct amvdec_frame_pool {
	int lock, delivery_lock;
	struct work_struct work;
	struct meson8b_pool state;
	struct amvdec_picture picture[MESON8B_POOL_MAX];
	struct amvdec_shadow slot[MESON8B_POOL_MAX];
	enum amvdec_pool_end end;
};
struct amvdec_timestamp {
	struct list_head list;
	u32 offset, used_count, flags, tc;
	u64 ts;
};
struct amvdec_session;
struct amvdec_codec_ops {
	void (*drain)(struct amvdec_session *);
	void (*resume)(struct amvdec_session *);
	void (*recycle)(void);
	void (*capture_queued)(struct amvdec_session *);
	irqreturn_t (*isr)(struct amvdec_session *);
	irqreturn_t (*threaded_isr)(struct amvdec_session *);
};
struct amvdec_format { struct amvdec_codec_ops *codec_ops; };
struct platform { int revision; };
struct amvdec_core {
	struct platform *platform;
	struct amvdec_session *cur_sess;
	void *dev, *dev_dec;
	u32 pool_waiting_peak;
};
struct amvdec_session {
	struct amvdec_core *core;
	struct amvdec_format *fmt_out;
	struct amvdec_frame_pool pool;
	struct list_head timestamps;
	int ts_spinlock, esparser_queued_bufs, should_stop, status, fh, recycle_wq;
	int streamon_cap, streamon_out, ctrl_min_buf_capture;
	u64 last_irq_jiffies;
	bool irq_active;
	u32 width, height, reference_width, reference_height, crop_left, crop_top;
	u32 sequence_cap, dpb_size, num_dst_bufs, fw_idx_to_vb2_idx[32];
	void *m2m_ctx;
	struct work_struct esparser_queue_work;
};

static struct amvdec_session session;
static struct amvdec_vb2_buffer buffers[NBUFFERS];
static struct vb2_v4l2_buffer *queued[NBUFFERS], *done[NBUFFERS];
static unsigned int qhead, qlen, dhead, dlen;
static u32 registers[0x10000 / 4];
static u64 now, finish_at;
static unsigned int firmware, copied, returned, dequeued, events, errors, recycled;
static unsigned int expected, random_state;
static bool copy_irq;

static void trace(const char *stage, int identity)
{
	printf("%llu %-12s id=%d irq=%u pool=%u copy=%u dq=%u eos=%u\n",
	       (unsigned long long)now, stage, identity, firmware,
	       session.pool.state.len, copied, dequeued, events);
}
static void mutex_lock(int *lock) { assert(!*lock); *lock = 1; }
static void mutex_unlock(int *lock) { assert(*lock); *lock = 0; }
static int atomic_read(int *p) { return *p; }
static void atomic_dec(int *p) { assert(*p > 0); --*p; }
static bool list_empty(struct list_head *p) { return p->next == p; }
#define list_first_entry(h, t, m) container_of((h)->next, t, m)
static void wake_up_interruptible(int *p) { (void)p; }
static u64 get_jiffies_64(void) { return now; }
static void msleep(unsigned int ms);
static void amvdec_pool_work(struct work_struct *work);
static void codec_h264_frames_ready(struct amvdec_session *, u32);
static void vdec_vb2_buf_finish(struct vb2_buffer *);
static irqreturn_t vdec_isr(int irq, void *data);
static irqreturn_t vdec_threaded_isr(int irq, void *data);
void amvdec_pool_push(struct amvdec_session *, u32, const struct amvdec_picture *);

static void schedule_work(struct work_struct *work) { work->pending = true; }
static void cancel_work_sync(struct work_struct *work) { work->pending = false; }
static u32 amvdec_read_dos(struct amvdec_core *core, u32 reg)
{
	assert(!(reg % 4) && reg / 4 < ARRAY_SIZE(registers));
	return registers[reg / 4];
}
static void amvdec_write_dos(struct amvdec_core *core, u32 reg, u32 val)
{
	assert(!(reg % 4) && reg / 4 < ARRAY_SIZE(registers));
	registers[reg / 4] = val;
}
static bool amvdec_has_pool(struct amvdec_session *s)
{
	return s->core->platform->revision == VDEC_REVISION_MESON8B;
}
static u32 amvdec_pool_count(struct amvdec_session *s) { return s->pool.state.count; }
static void amvdec_abort(struct amvdec_session *s) { assert(!"unexpected abort"); }
static void amvdec_recycle(struct amvdec_session *s, u32 slot)
{
	assert(!(s->pool.state.waiting & BIT(slot)));
	recycled++;
}
static u32 amvdec_get_output_size_wh(u32 w, u32 h) { return w * h; }
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *ctx)
{
	struct vb2_v4l2_buffer *b;
	if (!qlen)
		return NULL;
	b = queued[qhead];
	qhead = (qhead + 1) % NBUFFERS;
	qlen--;
	return b;
}
static void vb2_set_plane_payload(struct vb2_buffer *b, unsigned int p, u32 n)
{
	assert(p < 2);
	b->bytesused[p] = n;
}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b, int state)
{
	struct amvdec_vb2_buffer *v = container_of(b, struct amvdec_vb2_buffer, vb);
	assert(dlen < NBUFFERS && v->state == 1);
	v->state = state == VB2_BUF_STATE_DONE ? 2 : 3;
	if (state == VB2_BUF_STATE_ERROR)
		errors++;
	else if (b->vb2_buf.bytesused[0])
		returned++;
	done[(dhead + dlen++) % NBUFFERS] = b;
	trace(state == VB2_BUF_STATE_DONE ? "CAPTURE-done" : "CAPTURE-error",
	      b->vb2_buf.identity);
	printf("  CAPTURE pts=%llu seq=%u flags=%x eos=%u bytes=%u/%u\n",
	       (unsigned long long)b->vb2_buf.timestamp, b->sequence, b->flags,
	       v->eos, b->vb2_buf.bytesused[0], b->vb2_buf.bytesused[1]);
}
static void v4l2_m2m_last_buffer_done(void *ctx, struct vb2_v4l2_buffer *b)
{
	b->flags |= V4L2_BUF_FLAG_LAST;
	v4l2_m2m_buf_done(b, VB2_BUF_STATE_DONE);
}
static struct amvdec_session *vb2_get_drv_priv(void *q) { return q; }
static struct vb2_v4l2_buffer *to_vb2_v4l2_buffer(struct vb2_buffer *b)
{
	return container_of(b, struct vb2_v4l2_buffer, vb2_buf);
}
static void v4l2_event_queue_fh(int *fh, const struct v4l2_event *ev)
{
	if (ev->type == V4L2_EVENT_EOS) {
		events++;
		trace("EOS", -1);
	} else {
		assert(ev->type == V4L2_EVENT_SOURCE_CHANGE);
		trace("SOURCE_CHANGE", -1);
	}
}
static void v4l2_ctrl_s_ctrl(int ctrl, u32 n) { assert(!"non-pool path"); }
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove_by_idx(void *ctx, u32 i)
{
	assert(!"non-pool path"); return NULL;
}
static void amvdec_dst_buf_done_offset(struct amvdec_session *s,
	struct vb2_v4l2_buffer *v, u32 o, u32 f, bool drop) { assert(!"non-pool path"); }
static void amvdec_dst_buf_done(struct amvdec_session *s,
	struct vb2_v4l2_buffer *v, u32 f) { assert(!"non-pool path"); }
static void issue(unsigned int first, unsigned int count, bool reverse, bool threaded);
static int amvdec_meson8b_copy_capture(struct amvdec_session *s,
	const struct amvdec_geometry *geo, const struct amvdec_shadow *shadow,
	struct vb2_buffer *b)
{
	unsigned int slot = shadow - s->pool.slot;
	assert(!s->pool.lock && s->pool.delivery_lock);
	assert(s->pool.state.waiting & BIT(slot));
	assert(geo->width == shadow->width && geo->height == shadow->height);
	b->identity = shadow->identity;
	b->width = geo->width;
	b->height = geo->height;
	if (copy_irq) {
		copy_irq = false;
		/* Conversion is slow; another firmware batch arrives in the gap. */
		now += 125;
		issue(3, 3, false, true);
		assert(s->pool.state.waiting & BIT(slot));
	}
	copied++;
	trace("copy", b->identity);
	return 0;
}

#include "driver.inc"

static irqreturn_t firmware_isr(struct amvdec_session *s) { return IRQ_WAKE_THREAD; }
static irqreturn_t firmware_thread(struct amvdec_session *s)
{
	codec_h264_frames_ready(s, registers[AV_SCRATCH_0 / 4]);
	return IRQ_HANDLED;
}
static void resume(struct amvdec_session *s) { assert(!"unexpected resume"); }
static void recycle_available(void) {}
static void msleep(unsigned int ms)
{
	now += ms;
	if (finish_at && now >= finish_at) {
		finish_at = 0;
		assert(vdec_threaded_isr(0, session.core) == IRQ_HANDLED);
		trace("IRQ-complete", -1);
	}
}
static void init(void)
{
	static struct platform platform = { VDEC_REVISION_MESON8B };
	static struct amvdec_codec_ops ops = {
		.isr = firmware_isr, .threaded_isr = firmware_thread,
		.recycle = recycle_available, .resume = resume,
	};
	static struct amvdec_format format = { &ops };
	static struct amvdec_core core;
	memset(&session, 0, sizeof(session));
	memset(&core, 0, sizeof(core));
	memset(buffers, 0, sizeof(buffers));
	memset(registers, 0, sizeof(registers));
	core.platform = &platform;
	core.cur_sess = &session;
	session.core = &core;
	session.fmt_out = &format;
	session.timestamps.next = session.timestamps.prev = &session.timestamps;
	session.streamon_cap = session.streamon_out = 1;
	session.status = STATUS_RUNNING;
	session.width = 320;
	session.height = 240;
	assert(!meson8b_pool_reset(&session.pool.state, 6));
	meson8b_pool_give_all(&session.pool.state);
	qhead = qlen = dhead = dlen = 0;
	firmware = copied = returned = dequeued = events = errors = recycled = 0;
	expected = 0;
	now = 1000;
	finish_at = 0;
	copy_irq = false;
}
static void queue(unsigned int index)
{
	struct amvdec_vb2_buffer *v = &buffers[index];
	assert(qlen < NBUFFERS && !v->state && !events);
	v->vb = (struct vb2_v4l2_buffer){ .vb2_buf = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.num_planes = 2, .vb2_queue = &session, .identity = -1,
	} };
	v->eos = false;
	v->state = 1;
	queued[(qhead + qlen++) % NBUFFERS] = &v->vb;
	amvdec_pool_capture_queued(&session);
}
static void queue_many(unsigned int count)
{
	for (unsigned int i = 0; i < count; i++)
		queue(i);
}
static void timestamps(unsigned int count, unsigned int tail)
{
	for (unsigned int i = 0; i < count; i++) {
		unsigned int pts = i;
		struct amvdec_timestamp *p = calloc(1, sizeof(*p));
		assert(p);
		if (i >= count - tail)
			pts = count - 1 - (i - (count - tail));
		p->offset = 0x12340000u + i * 256;
		p->ts = pts * 1000 + 1;
		p->list.prev = session.timestamps.prev;
		p->list.next = &session.timestamps;
		session.timestamps.prev->next = &p->list;
		session.timestamps.prev = &p->list;
		session.esparser_queued_bufs++;
	}
}
static void issue(unsigned int first, unsigned int count, bool reverse, bool threaded)
{
	assert(count <= 6);
	for (unsigned int i = 0; i < count; i++) {
		u32 offset = 0x12340000u + (first + i) * 256;
		u32 reg = (AV_SCRATCH_A + i / 2 * 4) / 4;
		u32 shift = i % 2 * 16;
		u32 slot = (first + i) % 6;
		assert(!(session.pool.state.waiting & BIT(slot)));
		registers[(AV_SCRATCH_1 + i * 4) / 4] = (offset << 16) | slot;
		registers[reg] = (registers[reg] & ~(0xffffu << shift)) |
				((offset >> 16) << shift);
		session.pool.slot[slot] = (struct amvdec_shadow){
			.identity = reverse ? first + count - i - 1 : first + i,
			.width = session.width, .height = session.height,
		};
	}
	registers[AV_SCRATCH_0 / 4] = count << 8;
	firmware += count;
	assert(vdec_isr(0, session.core) == IRQ_WAKE_THREAD);
	assert(session.irq_active);
	trace("IRQ-enter", first);
	if (threaded) {
		assert(vdec_threaded_isr(0, session.core) == IRQ_HANDLED);
		assert(!session.irq_active);
		trace("IRQ-complete", first);
		for (unsigned int i = 0; i < session.pool.state.len; i++) {
			u32 slot = session.pool.state.queue[
				(session.pool.state.head + i) % MESON8B_POOL_MAX];
			struct amvdec_picture *p = &session.pool.picture[slot];
			printf("  POOL slot=%u pts=%llu geo=%ux%u last=%u eos=%u\n",
			       slot, (unsigned long long)p->timestamp, p->geo.width,
			       p->geo.height, !!(p->flags & V4L2_BUF_FLAG_LAST), p->eos);
		}
	}
}
static void work(void)
{
	if (session.pool.work.pending) {
		session.pool.work.pending = false;
		amvdec_pool_work(&session.pool.work);
		assert(!session.pool.lock && !session.pool.delivery_lock);
	}
}
static void dequeue(bool requeue)
{
	struct vb2_v4l2_buffer *b;
	struct amvdec_vb2_buffer *v;
	bool eos;
	if (!dlen)
		return;
	b = done[dhead];
	dhead = (dhead + 1) % NBUFFERS;
	dlen--;
	v = container_of(b, struct amvdec_vb2_buffer, vb);
	assert(v->state == 2 || v->state == 3);
	eos = v->eos;
	assert(!events);
	if (v->state == 2 && b->vb2_buf.bytesused[0]) {
		assert(b->vb2_buf.identity == (int)expected);
		assert(b->vb2_buf.timestamp == expected * 1000 + 1);
		assert(b->vb2_buf.bytesused[0] == b->vb2_buf.width * b->vb2_buf.height);
		expected++;
		dequeued++;
	}
	trace(b->flags & V4L2_BUF_FLAG_LAST ? "DQBUF-LAST" : "DQBUF", b->vb2_buf.identity);
	vdec_vb2_buf_finish(&b->vb2_buf);
	assert(events == (unsigned int)eos);
	/* Repeated finish must never duplicate the event. */
	vdec_vb2_buf_finish(&b->vb2_buf);
	assert(events == (unsigned int)eos);
	v->state = 0;
	if (requeue && !eos)
		queue(v - buffers);
}
static unsigned int next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}
static void pump(void)
{
	unsigned int tries = 10000;
	while ((session.pool.state.len || dlen || session.pool.work.pending) && tries--) {
		now++;
		if (next_random() % 4)
			work();
		if (next_random() % 3)
			dequeue(true);
	}
	assert(tries && !session.pool.state.len && !dlen && !errors);
}
static void release_queued(void)
{
	session.streamon_cap = 0;
	amvdec_pool_capture_stop(&session);
	while (dlen) {
		struct vb2_v4l2_buffer *b = done[dhead];
		dhead = (dhead + 1) % NBUFFERS;
		dlen--;
		container_of(b, struct amvdec_vb2_buffer, vb)->state = 0;
	}
	while (!list_empty(&session.timestamps)) {
		struct amvdec_timestamp *p = list_first_entry(&session.timestamps,
			struct amvdec_timestamp, list);
		list_del(&p->list);
		free(p);
	}
}
static void stream(unsigned int capture, unsigned int batch)
{
	init();
	assert(capture && capture <= NBUFFERS && batch && batch <= 6);
	timestamps(NPICTURES, batch);
	queue_many(capture);
	for (unsigned int first = 0; first < NPICTURES; first += batch) {
		bool tail = first + batch == NPICTURES;
		session.should_stop = tail;
		issue(first, batch, tail, true);
		assert(!events);
		pump();
	}
	assert(firmware == NPICTURES && copied == firmware && returned == firmware);
	assert(dequeued == firmware && recycled == firmware && events == 1);
	assert(!session.esparser_queued_bufs && list_empty(&session.timestamps));
	printf("PASS stream capture=%u batch=%u firmware=%u copy=%u dq=%u eos=%u\n",
	       capture, batch, firmware, copied, dequeued, events);
	release_queued();
}
int main(int argc, char **argv)
{
	assert(argc >= 2);
	random_state = argc > 4 ? strtoul(argv[4], NULL, 10) : 1;
	assert(random_state);
	if (!strcmp(argv[1], "stream")) {
		assert(argc >= 4);
		stream(strtoul(argv[2], NULL, 10), strtoul(argv[3], NULL, 10));
		return 0;
	}
	init();
	if (!strcmp(argv[1], "copy-irq")) {
		timestamps(6, 0);
		queue_many(4);
		issue(0, 3, false, true);
		copy_irq = true;
		pump();
		assert(firmware == 6 && copied == 6 && dequeued == 6 && !events);
	} else if (!strcmp(argv[1], "wait")) {
		timestamps(1, 0);
		queue_many(1);
		session.should_stop = 1;
		issue(0, 1, false, false);
		now = 1200; /* hard IRQ was 200 ms ago; threaded handler has not run */
		finish_at = 1250;
		assert(!vdec_wait_inactive(&session));
		assert(now == 1300 && !session.irq_active && session.pool.state.len == 1);
		assert(!copied && !events);
		pump();
		assert(dequeued == 1 && events == 1);
	} else if (!strcmp(argv[1], "source-change")) {
		timestamps(2, 0);
		issue(0, 1, false, true);
		session.status = STATUS_NEEDS_RESUME;
		issue(1, 1, false, true);
		amvdec_src_change(&session, 640, 480, 6);
		/* Both queued pictures retain 320x240; the change LAST has no EOS. */
		queue_many(1);
		pump();
		assert(dequeued == 2 && !events && session.width == 640);
		assert(session.sequence_cap == 0);
	} else if (!strcmp(argv[1], "cancel")) {
		timestamps(3, 0);
		queue_many(1);
		issue(0, 3, false, true);
		session.streamon_cap = 0;
		amvdec_pool_capture_stop(&session);
		work();
		assert(!copied && !dequeued && !events && errors == 1);
		assert(!session.pool.state.len && !session.pool.work.pending);
		release_queued();
		/* A fresh session cannot inherit a cancelled picture or EOS marker. */
		stream(1, 6);
		puts("PASS cancel/reopen");
		return 0;
	} else {
		assert(!"unknown scenario");
	}
	printf("PASS %s firmware=%u copy=%u dq=%u eos=%u\n",
	       argv[1], firmware, copied, dequeued, events);
	release_queued();
	return 0;
}
