#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "vdrm.h"

struct v_encoder {
	struct drm_encoder base;
	uint32_t id;
};

#define to_v_encoder(x) container_of(x, struct v_encoder, base)

static void v_encoder_reset(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static void v_encoder_destroy(struct drm_encoder *encoder)
{
	struct v_encoder *v_encoder = to_v_encoder(encoder);

	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);

	drm_encoder_cleanup(encoder);
	kfree(v_encoder);
}

static void v_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static void v_encoder_save(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static void v_encoder_restore(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static bool v_encoder_mode_fixup(struct drm_encoder *encoder, const struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);

	return true;
}

static void v_encoder_prepare(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static void v_encoder_commit(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static void v_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static struct drm_crtc *v_encoder_get_crtc(struct drm_encoder *encoder)
{
	struct v_encoder *v_encoder = to_v_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc;

	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);

	drm_for_each_crtc(crtc, dev)
	{
		if(drm_crtc_index(crtc) == v_encoder->id)
			return crtc;
	}

	return NULL;
}

static enum drm_connector_status v_encoder_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);

	return connector_status_connected;
}

static void v_encoder_disable(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static void v_encoder_enable(struct drm_encoder *encoder)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
}

static int v_encoder_atomic_check(struct drm_encoder *encoder,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	dbg_function_entry(encoder->dev->dev, encoder, (to_v_encoder(encoder))->id);
	return 0;
}

static const struct drm_encoder_funcs v_encoder_funcs = {
	.reset = v_encoder_reset,
	.destroy = v_encoder_destroy,
};

static const struct drm_encoder_helper_funcs v_encoder_helper_funcs = {
	.dpms = v_encoder_dpms,
	.save = v_encoder_save,
	.restore = v_encoder_restore,
	.mode_fixup = v_encoder_mode_fixup,
	.prepare = v_encoder_prepare,
	.commit = v_encoder_commit,
	.mode_set = v_encoder_mode_set,
	.get_crtc = v_encoder_get_crtc,
	.detect = v_encoder_detect,
	.disable = v_encoder_disable,
	.enable = v_encoder_enable,
	.atomic_check = v_encoder_atomic_check,
};

struct drm_encoder *v_encoder_init(struct drm_device *dev, uint32_t id)
{
	struct drm_encoder *encoder = NULL;
	struct v_encoder *v_encoder;

	v_encoder = kzalloc(sizeof(*v_encoder), GFP_KERNEL);
	if (!v_encoder)
		return NULL;

	v_encoder->id = id;

	encoder = &v_encoder->base;

	drm_encoder_init(dev, encoder, &v_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL);
	drm_encoder_helper_add(encoder, &v_encoder_helper_funcs);

	encoder->possible_crtcs = (1 << id);

	return encoder;
}

void v_encoder_fini(struct drm_device *dev, struct drm_encoder *encoder)
{
	v_encoder_destroy(encoder);
}
