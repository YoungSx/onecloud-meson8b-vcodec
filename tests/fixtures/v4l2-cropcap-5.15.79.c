// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Video capture interface for Linux version 2
 *
 * A generic framework to process V4L2 ioctl commands.
 *
 * Authors:	Alan Cox, <alan@lxorguk.ukuu.org.uk> (version 1)
 *              Mauro Carvalho Chehab <mchehab@kernel.org> (version 2)
 */


/* Minimal read-only selection/CROPCAP regression fixture from Linux 5.15.79. */
static int v4l_g_selection(const struct v4l2_ioctl_ops *ops,
			   struct file *file, void *fh, void *arg)
{
	struct v4l2_selection *p = arg;
	u32 old_type = p->type;
	int ret;

	if (p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ops->vidioc_g_selection(file, fh, p);
	p->type = old_type;
	return ret;
}

static int v4l_cropcap(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_cropcap *p = arg;
	struct v4l2_selection s = { .type = p->type };
	int ret = 0;

	/* setting trivial pixelaspect */
	p->pixelaspect.numerator = 1;
	p->pixelaspect.denominator = 1;

	if (s.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (s.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	/*
	 * The determine_valid_ioctls() call already should ensure
	 * that this can never happen, but just in case...
	 */
	if (WARN_ON(!ops->vidioc_g_selection))
		return -ENOTTY;

	if (ops->vidioc_g_pixelaspect)
		ret = ops->vidioc_g_pixelaspect(file, fh, s.type,
						&p->pixelaspect);

	/*
	 * Ignore ENOTTY or ENOIOCTLCMD error returns, just use the
	 * square pixel aspect ratio in that case.
	 */
	if (ret && ret != -ENOTTY && ret != -ENOIOCTLCMD)
		return ret;

	/* Use g_selection() to fill in the bounds and defrect rectangles */

	/* obtaining bounds */
	if (V4L2_TYPE_IS_OUTPUT(p->type))
		s.target = V4L2_SEL_TGT_COMPOSE_BOUNDS;
	else
		s.target = V4L2_SEL_TGT_CROP_BOUNDS;

	if (test_bit(V4L2_FL_QUIRK_INVERTED_CROP, &vfd->flags))
		s.target = s.target == V4L2_SEL_TGT_COMPOSE_BOUNDS ?
			V4L2_SEL_TGT_CROP_BOUNDS : V4L2_SEL_TGT_COMPOSE_BOUNDS;

	ret = v4l_g_selection(ops, file, fh, &s);
	if (ret)
		return ret;
	p->bounds = s.r;

	/* obtaining defrect */
	if (s.target == V4L2_SEL_TGT_COMPOSE_BOUNDS)
		s.target = V4L2_SEL_TGT_COMPOSE_DEFAULT;
	else
		s.target = V4L2_SEL_TGT_CROP_DEFAULT;

	ret = v4l_g_selection(ops, file, fh, &s);
	if (ret)
		return ret;
	p->defrect = s.r;

	return 0;
}
