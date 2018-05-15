#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "vdrm.h"

struct v_connector {
	struct drm_connector base;
	uint32_t id;
	struct drm_encoder *encoder;
	struct vdrm_timings timings;

};

#define to_v_connector(x) container_of(x, struct v_connector, base)

static void copy_timings_vdrm_to_drm(struct drm_display_mode *mode,
		struct vdrm_timings *timings)
{
	mode->clock = timings->pixelclock / 1000;

	mode->hdisplay = timings->x_res;
	mode->hsync_start = mode->hdisplay + timings->hfp;
	mode->hsync_end = mode->hsync_start + timings->hsw;
	mode->htotal = mode->hsync_end + timings->hbp;

	mode->vdisplay = timings->y_res;
	mode->vsync_start = mode->vdisplay + timings->vfp;
	mode->vsync_end = mode->vsync_start + timings->vsw;
	mode->vtotal = mode->vsync_end + timings->vbp;

	mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
}

static void copy_timings_drm_to_vdrm(struct vdrm_timings *timings,
		struct drm_display_mode *mode)
{
	timings->pixelclock = mode->clock * 1000;

	timings->x_res = mode->hdisplay;
	timings->hfp = mode->hsync_start - mode->hdisplay;
	timings->hsw = mode->hsync_end - mode->hsync_start;
	timings->hbp = mode->htotal - mode->hsync_end;

	timings->y_res = mode->vdisplay;
	timings->vfp = mode->vsync_start - mode->vdisplay;
	timings->vsw = mode->vsync_end - mode->vsync_start;
	timings->vbp = mode->vtotal - mode->vsync_end;

}

static int v_connector_dpms(struct drm_connector *connector, int mode)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return drm_atomic_helper_connector_dpms(connector, mode);
}

static void v_connector_save(struct drm_connector *connector)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);
}

static void v_connector_restore(struct drm_connector *connector)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);
}

static void v_connector_reset(struct drm_connector *connector)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	drm_atomic_helper_connector_reset(connector);
}

static enum drm_connector_status v_connector_detect(
		struct drm_connector *connector, bool force)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return connector_status_connected;

}

static int v_connector_fill_modes(struct drm_connector *connector, uint32_t max_width, uint32_t max_height)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return drm_helper_probe_single_connector_modes(connector, max_width, max_height);
}

static int v_connector_set_property(struct drm_connector *connector, struct drm_property *property, uint64_t val)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return -EINVAL;
}

static void v_connector_destroy(struct drm_connector *connector)
{
	struct v_connector *v_connector = to_v_connector(connector);

	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(v_connector);
}

static void v_connector_force(struct drm_connector *connector)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);
}

static struct drm_connector_state *v_connector_atomic_duplicate_state(struct drm_connector *connector)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return drm_atomic_helper_connector_duplicate_state(connector);
}

static void v_connector_atomic_destroy_state(struct drm_connector *connector, struct drm_connector_state *state)
{
	/*
	 * atomic_destroy_state for connectors is
	 * sent with connector == NULL. This causes an
	 * OOPS with dbg_function_entry
	 */
	//dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	drm_atomic_helper_connector_destroy_state(connector, state);
}

static int v_connector_atomic_set_property(struct drm_connector *connector, struct drm_connector_state *state, struct drm_property *property, uint64_t val)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return -EINVAL;
}

static int v_connector_atomic_get_property(struct drm_connector *connector, const struct drm_connector_state *state, struct drm_property *property, uint64_t *val)
{
	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return -EINVAL;
}

static int v_connector_get_modes(struct drm_connector *connector)
{
	struct v_connector *v_connector = to_v_connector(connector);
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;

	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	mode = drm_mode_create(dev);
	copy_timings_vdrm_to_drm(mode, &v_connector->timings);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static int v_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct vdrm_timings timings = {0};
	int ret = MODE_BAD;
	struct drm_display_mode *new_mode;
	struct drm_device *dev = connector->dev;
	struct v_connector *v_connector = to_v_connector(connector);
	int r = 0;

	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	copy_timings_drm_to_vdrm(&timings, mode);
	mode->vrefresh = drm_mode_vrefresh(mode);

	if (0 == memcmp(&timings, &v_connector->timings, sizeof(struct vdrm_timings)))
		ret = MODE_OK;
	else
		r = -EINVAL;

	if(r) {
		new_mode = drm_mode_duplicate(dev, mode);
		new_mode->clock = timings.pixelclock / 1000;
		new_mode->vrefresh = 0;
		if (mode->vrefresh == drm_mode_vrefresh(new_mode))
			ret = MODE_OK;
		drm_mode_destroy(dev, new_mode);
	}

	return ret;
}

static struct drm_encoder *v_connector_attached_encoder(
		struct drm_connector *connector)
{
	struct v_connector *v_connector = to_v_connector(connector);

	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return v_connector->encoder;
}

static struct drm_encoder *v_connector_atomic_best_encoder(struct drm_connector *connector, struct drm_connector_state *connector_state)
{
	struct v_connector *v_connector = to_v_connector(connector);

	dbg_function_entry(connector->dev->dev, connector, (to_v_connector(connector))->id);

	return v_connector->encoder;
}

static const struct drm_connector_funcs v_connector_funcs = {
	.dpms = v_connector_dpms,
	.save = v_connector_save,
	.restore = v_connector_restore,
	.reset = v_connector_reset,
	.detect = v_connector_detect,
	.fill_modes = v_connector_fill_modes,
	.set_property = v_connector_set_property,
	.destroy = v_connector_destroy,
	.force = v_connector_force,
	.atomic_duplicate_state = v_connector_atomic_duplicate_state,
	.atomic_destroy_state = v_connector_atomic_destroy_state,
	.atomic_set_property = v_connector_atomic_set_property,
	.atomic_get_property = v_connector_atomic_get_property,
};

static const struct drm_connector_helper_funcs v_connector_helper_funcs = {
	.get_modes = v_connector_get_modes,
	.mode_valid = v_connector_mode_valid,
	.best_encoder = v_connector_attached_encoder,
	.atomic_best_encoder = v_connector_atomic_best_encoder,
};

/* initialize connector */
struct drm_connector *v_connector_init(struct drm_device *dev,
		uint32_t id, struct drm_encoder *encoder,
		struct device_node *np)
{
	struct drm_connector *connector = NULL;
	struct v_connector *v_connector;
	uint32_t x, y, fps;
	uint32_t total_x = 0, total_y = 0;

	v_connector = kzalloc(sizeof(struct v_connector), GFP_KERNEL);
	if (!v_connector)
		return NULL;

	if(of_property_read_u32(np, "x-res", &x)) {
		kfree(v_connector);
		return NULL;
	}

	if(of_property_read_u32(np, "y-res", &y)) {
		kfree(v_connector);
		return NULL;
	}

	if(of_property_read_u32(np, "refresh", &fps)) {
		kfree(v_connector);
		return NULL;
	}

	v_connector->timings.x_res = x;
	total_x += v_connector->timings.x_res;
	v_connector->timings.hfp = ((v_connector->timings.x_res/100 > 0) ? (v_connector->timings.x_res/100) : 1);
	total_x += v_connector->timings.hfp;
	v_connector->timings.hbp = ((v_connector->timings.hfp/2 > 0) ? (v_connector->timings.hfp/2) : 1);
	total_x += v_connector->timings.hbp;
	v_connector->timings.hsw = ((v_connector->timings.hbp/2 > 0) ? (v_connector->timings.hbp/2) : 1);
	total_x += v_connector->timings.hsw;

	v_connector->timings.y_res = y;
	total_y += v_connector->timings.y_res;
	v_connector->timings.vfp = ((v_connector->timings.y_res/100 > 0) ? (v_connector->timings.y_res/100) : 1);
	total_y += v_connector->timings.vfp;
	v_connector->timings.vbp = ((v_connector->timings.vfp/2 > 0) ? (v_connector->timings.vfp/2) : 1);
	total_y += v_connector->timings.vbp;
	v_connector->timings.vsw = ((v_connector->timings.vbp/2 > 0) ? (v_connector->timings.vbp/2) : 1);
	total_y += v_connector->timings.vsw;

	v_connector->timings.pixelclock = (fps * total_x * total_y);

	v_connector->encoder = encoder;
	v_connector->id = id;

	connector = &v_connector->base;

	drm_connector_init(dev, connector, &v_connector_funcs,
				DRM_MODE_CONNECTOR_VIRTUAL);
	drm_connector_helper_add(connector, &v_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_register(connector);

	drm_mode_connector_attach_encoder(connector, encoder);

	return connector;
}

void v_connector_fini(struct drm_device *dev, struct drm_connector *connector)
{
	v_connector_destroy(connector);
}
