#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mode.h>
#include <drm/drm_plane_helper.h>

#include "vdrm.h"

struct v_crtc_plane_data {
	struct drm_plane *plane;
	struct drm_framebuffer *fb;
	uint32_t dx, dy, dw, dh;
	uint32_t sx, sy, sw, sh;
	struct list_head link;
};

struct v_crtc {
	struct drm_crtc base;
	uint32_t id;

	spinlock_t vsync_lock;
	struct hrtimer vsync_timer;
	uint64_t nsec_to_vsync;

	struct list_head planes;

	bool disable_in_progress;
	bool enabled;

	bool event_registered;
	bool should_register_event;

	wait_queue_head_t pending_wait;

	struct device_node *device_node;
};

#define to_v_crtc(x) container_of(x, struct v_crtc, base)

void v_crtc_add_plane_update(struct drm_crtc *crtc, struct drm_plane *plane,
		struct drm_framebuffer *fb,
		uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
		uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh)
{
	struct v_crtc_plane_data *p;
	struct v_crtc *v_crtc = to_v_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);
	p = kzalloc(sizeof(*p), GFP_ATOMIC);
	if(!p)
		goto out;
	p->plane = plane;
	p->fb = fb;
	p->dx = dx;
	p->dy = dy;
	p->dw = dw;
	p->dh = dh;
	p->sx = sx;
	p->sy = sy;
	p->sw = sw;
	p->sh = sh;

	list_add_tail(&p->link, &v_crtc->planes);
out:
	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);
}

bool crtc_for_device_node(struct drm_crtc *crtc, struct device_node *np)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	if(v_crtc->device_node == np)
		return true;

	return false;
}

static void v_crtc_disable_locked(struct drm_crtc *crtc)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	if(v_crtc->should_register_event == true) {
		v_crtc->event_registered = true;
		v_crtc->should_register_event = false;
	}

	v_crtc->disable_in_progress = true;
}

bool v_crtc_is_pending(struct drm_crtc *crtc)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);
	ret = v_crtc->event_registered;
	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);

	return ret;
}

int v_crtc_wait_pending(struct drm_crtc *crtc)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	return wait_event_timeout(v_crtc->pending_wait,
				  v_crtc_is_pending(crtc) == false,
				  msecs_to_jiffies(250));
}

static enum hrtimer_restart vsync_timer(struct hrtimer *timer)
{
	enum hrtimer_restart ret = HRTIMER_RESTART;
	struct v_crtc *v_crtc = container_of(timer, struct v_crtc, vsync_timer);
	struct drm_crtc *crtc = &v_crtc->base;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;
	struct drm_pending_vblank_event *event;
	struct v_crtc_plane_data *plane_data, *temp;
	uint32_t plane_mask;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;

	dbg_function_entry(dev->dev, crtc, v_crtc->id);

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);

	plane_mask = crtc->state->plane_mask;
	list_for_each_entry_safe(plane_data, temp, &v_crtc->planes, link) {
		list_del(&plane_data->link);
		v_plane_flush(plane_data->plane, plane_data->fb,
					plane_data->dx, plane_data->dy,
					plane_data->dw, plane_data->dh,
					plane_data->sx, plane_data->sy,
					plane_data->sw, plane_data->sh,
					false);
		plane_mask &= ~(1 << drm_plane_index(plane_data->plane));
		kfree(plane_data);
	}

	drm_for_each_plane_mask(plane, dev, plane_mask) {
		if(plane->state->crtc == crtc) {
			plane_state = plane->state;
			v_plane_flush(plane, plane_state->fb,
					plane_state->crtc_x, plane_state->crtc_y,
					plane_state->crtc_w, plane_state->crtc_h,
					plane_state->src_x >> 16,
					plane_state->src_y >> 16,
					plane_state->src_w >> 16,
					plane_state->src_h >> 16,
					true);
		}
	}

	drm_handle_vblank(dev, drm_crtc_index(crtc));

	event = crtc->state->event;

	if (event) {
		spin_lock_irqsave(&dev->event_lock, flags);
		list_del(&event->base.link);

		if (event->base.file_priv)
			drm_crtc_send_vblank_event(crtc, event);
		else
			event->base.destroy(&event->base);
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	if(v_crtc->event_registered) {
		v_crtc->event_registered = false;
		wake_up(&v_crtc->pending_wait);
	}

	if(v_crtc->disable_in_progress) {
		ret = HRTIMER_NORESTART;
		drm_crtc_vblank_off(crtc);
		v_crtc->disable_in_progress = false;
		v_crtc->enabled = false;
	} else {
		hrtimer_forward_now(&v_crtc->vsync_timer, ns_to_ktime(v_crtc->nsec_to_vsync));
	}

	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);

	return ret;
}


#define to_v_crtc(x) container_of(x, struct v_crtc, base)

static void v_crtc_save(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static void v_crtc_restore(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static void v_crtc_reset(struct drm_crtc *crtc)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);
	unsigned long flags;

	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);
	if(v_crtc->enabled)
		v_crtc_disable_locked(crtc);
	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);

	drm_atomic_helper_crtc_reset(crtc);
}

static int v_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv, uint32_t handle, uint32_t width, uint32_t height)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return 0;
}

static int v_crtc_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv, uint32_t handle, uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return 0;
}

static int v_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return 0;
}

static void v_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b, uint32_t start, uint32_t size)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	drm_atomic_helper_legacy_gamma_set(crtc, r, g, b, start, size);
}

static void v_crtc_destroy(struct drm_crtc *crtc)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	drm_crtc_cleanup(crtc);
	kfree(v_crtc);
}

static int v_crtc_set_config(struct drm_mode_set *set)
{
	dbg_function_entry(set->crtc->dev->dev, crtc, (to_v_crtc(set->crtc))->id);

	return drm_atomic_helper_set_config(set);
}

static int v_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb, struct drm_pending_vblank_event *event, uint32_t flags)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return drm_atomic_helper_page_flip(crtc, fb, event, flags);
}

static int v_crtc_set_property(struct drm_crtc *crtc, struct drm_property *property, uint64_t val)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return drm_atomic_helper_crtc_set_property(crtc, property, val);
}

static struct drm_crtc_state *v_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return drm_atomic_helper_crtc_duplicate_state(crtc);
}

static void v_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	drm_atomic_helper_crtc_destroy_state(crtc, state);
}

static int v_crtc_atomic_set_property(struct drm_crtc *crtc, struct drm_crtc_state *state, struct drm_property *property, uint64_t val)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return -EINVAL;
}

static int v_crtc_atomic_get_property(struct drm_crtc *crtc, const struct drm_crtc_state *state, struct drm_property *property, uint64_t *val)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return -EINVAL;
}

static void v_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static void v_crtc_prepare(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static void v_crtc_commit(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static bool v_crtc_mode_fixup(struct drm_crtc *crtc, const struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return true;
}

static int v_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode, int x, int y, struct drm_framebuffer *old_fb)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return drm_helper_crtc_mode_set(crtc, mode, adjusted_mode, x, y, old_fb);
}

static void v_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static int v_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y, struct drm_framebuffer *old_fb)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return drm_helper_crtc_mode_set_base(crtc, x, y, old_fb);
}

static int v_crtc_mode_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb, int x, int y, uint32_t mode_set_atomic_flag)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return drm_helper_crtc_mode_set_base(crtc, x, y, fb);
}

static void v_crtc_load_lut(struct drm_crtc *crtc)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);
}

static void v_crtc_disable(struct drm_crtc *crtc)
{
	unsigned long flags;
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);
	v_crtc_disable_locked(crtc);
	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);
}

static void v_crtc_enable(struct drm_crtc *crtc)
{
	unsigned long flags;
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);

	if(v_crtc->should_register_event == true) {
		v_crtc->event_registered = true;
		v_crtc->should_register_event = false;
	}

	v_crtc->nsec_to_vsync = div64_u64(1000000000ull,
			(uint64_t)crtc->state->adjusted_mode.vrefresh);

	v_crtc->enabled = true;
	hrtimer_start(&v_crtc->vsync_timer,
			ns_to_ktime(v_crtc->nsec_to_vsync), HRTIMER_MODE_REL);

	drm_crtc_vblank_on(crtc);
	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);
}

static int v_crtc_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	return 0;
}

static void v_crtc_atomic_begin(struct drm_crtc *crtc, struct drm_crtc_state *old_crtc_state)
{
	unsigned long flags;
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);

	WARN_ON(v_crtc->event_registered == true);
	v_crtc->should_register_event = true;

	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);

}

static void v_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_crtc_state *old_crtc_state)
{
	unsigned long flags;
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	dbg_function_entry(crtc->dev->dev, crtc, (to_v_crtc(crtc))->id);

	spin_lock_irqsave(&v_crtc->vsync_lock, flags);

	if(v_crtc->should_register_event == true) {
		v_crtc->event_registered = true;
		v_crtc->should_register_event = false;
	}

	spin_unlock_irqrestore(&v_crtc->vsync_lock, flags);
}

static const struct drm_crtc_funcs v_crtc_funcs = {
	.save = v_crtc_save,
	.restore = v_crtc_restore,
	.reset = v_crtc_reset,
	.cursor_set = v_crtc_cursor_set,
	.cursor_set2 = v_crtc_cursor_set2,
	.cursor_move = v_crtc_cursor_move,
	.gamma_set = v_crtc_gamma_set,
	.destroy = v_crtc_destroy,
	.set_config = v_crtc_set_config,
	.page_flip = v_crtc_page_flip,
	.set_property = v_crtc_set_property,
	.atomic_duplicate_state = v_crtc_atomic_duplicate_state,
	.atomic_destroy_state = v_crtc_atomic_destroy_state,
	.atomic_set_property = v_crtc_atomic_set_property,
	.atomic_get_property = v_crtc_atomic_get_property,
};

static const struct drm_crtc_helper_funcs v_crtc_helper_funcs = {
	.dpms = v_crtc_dpms,
	.prepare = v_crtc_prepare,
	.commit = v_crtc_commit,
	.mode_fixup = v_crtc_mode_fixup,
	.mode_set = v_crtc_mode_set,
	.mode_set_nofb = v_crtc_mode_set_nofb,
	.mode_set_base = v_crtc_mode_set_base,
	.mode_set_base_atomic = v_crtc_mode_set_base_atomic,
	.load_lut = v_crtc_load_lut,
	.disable = v_crtc_disable,
	.enable = v_crtc_enable,
	.atomic_check = v_crtc_atomic_check,
	.atomic_begin = v_crtc_atomic_begin,
	.atomic_flush = v_crtc_atomic_flush,
};

struct drm_crtc *v_crtc_init(struct drm_device *dev, uint32_t id,
		struct drm_plane *plane, struct device_node *np)
{
	struct drm_crtc *crtc = NULL;
	struct v_crtc *v_crtc;
	int ret;

	v_crtc = kzalloc(sizeof(*v_crtc), GFP_KERNEL);
	if (!v_crtc)
		return NULL;

	v_crtc->id = id;
	hrtimer_init(&v_crtc->vsync_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	v_crtc->vsync_timer.function = vsync_timer;
	spin_lock_init(&v_crtc->vsync_lock);
	INIT_LIST_HEAD(&v_crtc->planes);

	init_waitqueue_head(&v_crtc->pending_wait);

	crtc = &v_crtc->base;

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&v_crtc_funcs);
	if (ret < 0)
		goto err;

	drm_crtc_helper_add(crtc, &v_crtc_helper_funcs);

	v_crtc->device_node = np;
	of_node_get(np);

	return crtc;

err:
	kfree(v_crtc);
	return NULL;
}

void v_crtc_fini(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct v_crtc *v_crtc = to_v_crtc(crtc);

	of_node_put(v_crtc->device_node);
	v_crtc_destroy(crtc);
}
