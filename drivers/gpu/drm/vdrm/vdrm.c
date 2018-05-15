#include <linux/module.h>

#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include "vdrm.h"

#define DRIVER_NAME		MODULE_NAME
#define DRIVER_DESC		"Virtual DRM"
#define DRIVER_DATE		"20110917"
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

struct v_plane_info {
	struct drm_plane *plane;

	struct list_head link;
};

struct v_crtc_info {
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_plane *plane;
	struct drm_encoder *encoder;

	struct list_head link;
};

struct v_atomic_state_commit {
	struct work_struct work;
	struct drm_device *dev;
	struct drm_atomic_state *state;
};

static int objects_lookup(struct drm_device *dev,
		struct drm_file *filp, uint32_t pixel_format,
		struct drm_gem_object **bos, uint32_t *handles)
{
	int i, n = drm_format_num_planes(pixel_format);

	for (i = 0; i < n; i++) {
		bos[i] = drm_gem_object_lookup(dev, filp, handles[i]);
		if (!bos[i])
			goto fail;

	}

	return 0;

fail:
	while (--i > 0)
		drm_gem_object_unreference_unlocked(bos[i]);

	return -ENOENT;
}

static void v_atomic_wait_for_completion(struct drm_device *dev,
					    struct drm_atomic_state *old_state)
{
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc *crtc;
	unsigned int i;
	int ret;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		ret = v_crtc_wait_pending(crtc);

		if (!ret)
			dev_warn(dev->dev,
				 "atomic complete timeout (pipe %u)!\n", i);
	}
}

static void v_atomic_complete(struct v_atomic_state_commit *commit)
{
	struct drm_device *dev = commit->dev;
	struct drm_atomic_state *old_state = commit->state;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);
	drm_atomic_helper_commit_planes(dev, old_state, 0);
	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	v_atomic_wait_for_completion(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);

	drm_atomic_state_free(old_state);

	kfree(commit);
}

static void v_atomic_work(struct work_struct *work)
{
	struct v_atomic_state_commit *commit =
		container_of(work, struct v_atomic_state_commit, work);

	v_atomic_complete(commit);
}

static void vdrm_setup_probe(struct drm_device *dev)
{
	struct drm_connector *connector;

	mutex_lock(&dev->mode_config.mutex);
	drm_for_each_connector(connector, dev) {
		drm_helper_probe_single_connector_modes(connector,
				dev->mode_config.max_width,
				dev->mode_config.max_height);
	}
	mutex_unlock(&dev->mode_config.mutex);
}

static struct drm_framebuffer *v_device_fb_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *bos[4];
	struct drm_framebuffer *fb;
	int ret;

	dbg_function_entry(dev->dev, device, 0);

	ret = objects_lookup(dev, file, mode_cmd->pixel_format,
			bos, mode_cmd->handles);
	if (ret)
		return ERR_PTR(ret);

	fb = v_framebuffer_init(dev, mode_cmd, bos);
	if (IS_ERR(fb)) {
		int i, n = drm_format_num_planes(mode_cmd->pixel_format);
		for (i = 0; i < n; i++)
			drm_gem_object_unreference_unlocked(bos[i]);
		return fb;
	}
	return fb;
}

static void v_device_output_poll_changed(struct drm_device *dev)
{
	dbg_function_entry(dev->dev, device, 0);
}

static int v_device_atomic_check(struct drm_device *dev,
			    struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i, count = 0;

	dbg_function_entry(dev->dev, device, 0);

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		count++;
	}

	if(count > 1)
		return -EINVAL;

	return drm_atomic_helper_check(dev, state);

}

static int v_device_atomic_commit(struct drm_device *dev,
			      struct drm_atomic_state *state, bool async)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct v_atomic_state_commit *commit;
	int i;
	int ret;
	unsigned long flags;
	struct v_drm_private *priv = dev->dev_private;

	dbg_function_entry(dev->dev, device, 0);

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if(v_crtc_is_pending(crtc))
			return -EINVAL;
	}

	/* Allocate the commit object. */
	commit = kzalloc(sizeof(*commit), GFP_KERNEL);
	if (commit == NULL)
		return -ENOMEM;

	INIT_WORK(&commit->work, v_atomic_work);
	commit->dev = dev;
	commit->state = state;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret) {
		kfree(commit);
		return ret;
	}

	spin_lock_irqsave(&dev->event_lock, flags);
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if(crtc_state && crtc_state->event) {
			list_add_tail(&crtc_state->event->base.link,
				      &priv->unconsumed_events);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_atomic_helper_swap_state(dev, state);

	if (async)
		schedule_work(&commit->work);
	else
		v_atomic_complete(commit);

	return 0;
}

static struct drm_atomic_state *v_device_atomic_state_alloc(struct drm_device *dev)
{
	struct drm_atomic_state *state;

	dbg_function_entry(dev->dev, device, 0);

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	if (drm_atomic_state_init(dev, state) < 0) {
		kfree(state);
		return NULL;
	}
	return state;
}

static void v_device_atomic_state_clear(struct drm_atomic_state *state)
{
	dbg_function_entry(state->dev->dev, device, 0);

	drm_atomic_state_default_clear(state);
}

static void v_device_atomic_state_free(struct drm_atomic_state *state)
{
	dbg_function_entry(state->dev->dev, device, 0);

	drm_atomic_state_default_release(state);
	kfree(state);
}

static const struct drm_mode_config_funcs v_mode_config_funcs = {
	.fb_create = v_device_fb_create,
	.output_poll_changed = v_device_output_poll_changed,
	.atomic_check = v_device_atomic_check,
	.atomic_commit = v_device_atomic_commit,
	.atomic_state_alloc = v_device_atomic_state_alloc,
	.atomic_state_clear = v_device_atomic_state_clear,
	.atomic_state_free = v_device_atomic_state_free,
};

static void plane_info_delete_all(struct drm_device *dev)
{
	struct v_plane_info *plane_info, *temp;
	struct v_drm_private *priv = dev->dev_private;

	list_for_each_entry_safe(plane_info, temp, &priv->plane_infos, link) {
		list_del(&plane_info->link);
		v_plane_fini(dev, plane_info->plane);
		kfree(plane_info);
	}
}

static void crtc_info_delete_all(struct drm_device *dev)
{
	struct v_crtc_info *crtc_info, *temp;
	struct v_drm_private *priv = dev->dev_private;

	list_for_each_entry_safe(crtc_info, temp, &priv->crtc_infos, link) {
		list_del(&crtc_info->link);
		v_crtc_fini(dev, crtc_info->crtc);
		v_plane_fini(dev, crtc_info->plane);
		v_connector_fini(dev, crtc_info->connector);
		v_encoder_fini(dev, crtc_info->encoder);
		kfree(crtc_info);
	}
}

static bool create_new_crtc(struct drm_device *dev, uint32_t id, struct device_node *np)
{
	struct v_drm_private *priv = dev->dev_private;
	struct v_crtc_info *crtc_info;

	crtc_info = kzalloc(sizeof(*crtc_info), GFP_KERNEL);
	if(!crtc_info) {
		return false;
	}

	crtc_info->encoder = v_encoder_init(dev, id);
	if (!crtc_info->encoder) {
		dev_err(dev->dev, "could not create encoder: %u\n", id);
		goto encoder_fail;
	}

	crtc_info->connector = v_connector_init(dev, id, crtc_info->encoder, np);
	if (!crtc_info->connector) {
		dev_err(dev->dev, "could not create connector: %u\n", id);
		goto connector_fail;
	}

	crtc_info->plane = v_plane_init(dev, id, (1 << id), DRM_PLANE_TYPE_PRIMARY, np);
	if (!crtc_info->plane) {
		dev_err(dev->dev, "could not create primary plane: %u\n", id);
		goto plane_fail;
	}

	crtc_info->crtc = v_crtc_init(dev, id, crtc_info->plane, np);
	if (!crtc_info->crtc) {
		dev_err(dev->dev, "could not create crtc: %d\n", id);
		goto crtc_fail;
	}

	list_add_tail(&crtc_info->link, &priv->crtc_infos);

	return true;

crtc_fail:
	v_plane_fini(dev, crtc_info->plane);
plane_fail:
	v_connector_fini(dev, crtc_info->connector);
connector_fail:
	v_encoder_fini(dev, crtc_info->encoder);
encoder_fail:
	kfree(crtc_info);
	return false;
}

static int vdrm_modeset_init(struct drm_device *dev)
{
	int i;
	struct v_drm_private *priv = dev->dev_private;
	struct device_node *np = dev->dev->of_node;
	struct device_node *child = NULL;

	drm_mode_config_init(dev);

	INIT_LIST_HEAD(&priv->crtc_infos);
	INIT_LIST_HEAD(&priv->plane_infos);

	i = 0;
	while((child = of_get_next_child(np, child)) != NULL) {
		if(of_device_is_compatible(child, "ti,dra7-vdrm-crtc")) {
			if(!create_new_crtc(dev, i, child)) {
				of_node_put(child);
				goto out_fail;
			}
			i++;
			priv->num_crtcs++;
		}
	}

	child = NULL;
	i = 0;
	while((child = of_get_next_child(np, child)) != NULL) {
		if(of_device_is_compatible(child, "ti,dra7-vdrm-plane")) {
			struct of_phandle_args crtcs;
			int rc;
			int count = 0;
			uint32_t crtc_mask = 0;
			struct v_plane_info *plane_info;
			struct drm_plane *plane;
			struct drm_crtc *crtc;

			plane_info = kzalloc(sizeof(*plane_info), GFP_KERNEL);
			if(!plane_info) {
				of_node_put(child);
				goto plane_out_fail;
			}

			do {
				rc = of_parse_phandle_with_args(child, "supported-crtcs", NULL, count, &crtcs);
				if(rc)
					break;
				count++;
				drm_for_each_crtc(crtc, dev) {
					if(crtc_for_device_node(crtc, crtcs.np))
						crtc_mask |= (1 << drm_crtc_index(crtc));
				}
				of_node_put(crtcs.np);
			} while(1);

			if(!count) {
				of_node_put(child);
				kfree(plane_info);
				goto plane_out_fail;
			}

			plane = v_plane_init(dev,
					i + priv->num_crtcs,
					crtc_mask, DRM_PLANE_TYPE_OVERLAY, child);
			if(!plane) {
				of_node_put(child);
				kfree(plane_info);
				goto plane_out_fail;
			}

			plane_info->plane = plane;
			list_add_tail(&plane_info->link, &priv->plane_infos);
			i++;
		}
	}

	dev->mode_config.min_width = 16;
	dev->mode_config.min_height = 16;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	dev->mode_config.funcs = &v_mode_config_funcs;

	drm_mode_config_reset(dev);

	return 0;

plane_out_fail:
	plane_info_delete_all(dev);
out_fail:
	crtc_info_delete_all(dev);
	drm_mode_config_cleanup(dev);
	return -ENOMEM;

}

static void vdrm_modeset_fini(struct drm_device *dev)
{
	plane_info_delete_all(dev);
	crtc_info_delete_all(dev);
	drm_mode_config_cleanup(dev);
}

static int dev_load(struct drm_device *dev, unsigned long flags)
{
	int ret;
	struct v_drm_private *priv;
	struct v_crtc_info *crtc_info;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev->dev_private = priv;

	spin_lock_init(&priv->list_lock);
	INIT_LIST_HEAD(&priv->obj_list);
	INIT_LIST_HEAD(&priv->unconsumed_events);

	ret = vdrm_modeset_init(dev);
	if(ret) {
		dev->dev_private = NULL;
		kfree(priv);
		return ret;
	}

	ret = drm_vblank_init(dev, priv->num_crtcs);
	if (ret)
		dev_warn(dev->dev, "could not init vblank\n");

	list_for_each_entry(crtc_info, &priv->crtc_infos, link)
		drm_crtc_vblank_off(crtc_info->crtc);

	vdrm_setup_probe(dev);

	dev_set_drvdata(dev->dev, dev);

	drm_kms_helper_poll_init(dev);

	dev->irq_enabled = true;

	priv->vctrl = v_controller_create_device(dev);

	return 0;
}

static int dev_unload(struct drm_device *dev)
{
	struct v_drm_private *priv = dev->dev_private;

	v_controller_delete_device(priv->vctrl);

	drm_kms_helper_poll_fini(dev);
	vdrm_modeset_fini(dev);
	drm_vblank_cleanup(dev);

	kfree(dev->dev_private);
	dev->dev_private = NULL;
	dev_set_drvdata(dev->dev, NULL);

	return 0;
}

static int dev_open(struct drm_device *dev, struct drm_file *file)
{
	file->driver_priv = NULL;

	return 0;
}

static void dev_lastclose(struct drm_device *dev)
{
	drm_mode_config_reset(dev);
}

static void dev_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct v_drm_private *priv = dev->dev_private;
	struct drm_pending_event *event;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	list_for_each_entry(event, &priv->unconsumed_events, link) {
		if (event->file_priv == file) {
			file->event_space += event->event->length;
			event->file_priv = NULL;
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

}

static void dev_postclose(struct drm_device *dev, struct drm_file *file)
{
}

static int v_irq_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	return 0;
}

static void v_irq_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
}

static const struct vm_operations_struct v_gem_vm_ops = {
	.fault = v_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations vdriver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.unlocked_ioctl = drm_ioctl,
	.release = drm_release,
	.mmap = v_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
};

static struct drm_driver v_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM  | DRIVER_PRIME | DRIVER_ATOMIC,
	.load = dev_load,
	.unload = dev_unload,
	.open = dev_open,
	.lastclose = dev_lastclose,
	.preclose = dev_preclose,
	.postclose = dev_postclose,
	.set_busid = drm_platform_set_busid,
	.get_vblank_counter = drm_vblank_no_hw_counter,
	.enable_vblank = v_irq_enable_vblank,
	.disable_vblank = v_irq_disable_vblank,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = v_debugfs_init,
	.debugfs_cleanup = v_debugfs_cleanup,
#endif
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = v_gem_prime_export,
	.gem_prime_import = v_gem_prime_import,
	.gem_free_object = v_gem_free_object,
	.gem_vm_ops = &v_gem_vm_ops,
	.dumb_create = v_gem_dumb_create,
	.dumb_map_offset = v_gem_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.ioctls = NULL,
	.num_ioctls = 0,
	.fops = &vdriver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int pdev_probe(struct platform_device *device)
{
	return drm_platform_init(&v_drm_driver, device);
}

static int pdev_remove(struct platform_device *device)
{
	drm_put_dev(platform_get_drvdata(device));

	return 0;
}

static const struct of_device_id v_drm_of_match[] = {
	{ .compatible = "ti,dra7-vdrm", },
	{},
};

static struct platform_driver pdev = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = v_drm_of_match,
	},
	.probe = pdev_probe,
	.remove = pdev_remove,
};

static struct platform_driver * const drivers[] = {
	&pdev,
};

static int __init v_drm_init(void)
{
	int ret;

	ret = v_controller_init();
	if(ret)
		return ret;

	ret = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if(ret)
		goto drv_register_fail;

	return 0;

drv_register_fail:
	v_controller_fini();
	return ret;
}

static void __exit v_drm_fini(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
	v_controller_fini();
}

module_init(v_drm_init);
module_exit(v_drm_fini);

MODULE_AUTHOR("Rob Clark <rob@ti.com>");
MODULE_DESCRIPTION("Virtual DRM Display Driver");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL v2");
