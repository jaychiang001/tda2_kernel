#include "vdrm.h"

#ifdef CONFIG_DEBUG_FS

static int gem_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct v_drm_private *priv = dev->dev_private;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "All Objects:\n");
	v_gem_describe_objects(&priv->obj_list, m);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int fb_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_framebuffer *fb;

	mutex_lock(&dev->mode_config.fb_lock);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		v_framebuffer_describe(fb, m);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}

static struct drm_info_list v_debugfs_list[] = {
	{"gem", gem_show, 0},
	{"fb", fb_show, 0},
};

int v_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(v_debugfs_list,
			ARRAY_SIZE(v_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install v_debugfs_list\n");
		return ret;
	}

	return ret;
}

void v_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(v_debugfs_list,
			ARRAY_SIZE(v_debugfs_list), minor);
}

#endif
