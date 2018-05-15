#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>

#include <linux/dma-buf.h>
#include "vdrm.h"

#include <uapi/vdrm_controller/v_controller_if.h>

#define CONTROLLER_NAME MODULE_NAME "-controller"
#define MAX_NUM_V_CONTROLLER_DEVICES MAX_NUM_VDRM_DEVICES

struct v_ctrl_device {
	struct cdev vdrm_ctrl_cdev;
	dev_t vdrm_ctrl_dev;
	struct device *vdrm_ctrl_device;
	int minor;

	struct drm_device *drmdev;
	struct list_head node;

	struct mutex open_lock;
	uint32_t open_count;
	struct list_head open_files;

	struct mutex gem_list_lock;
	uint32_t gem_handle;
	struct list_head gem_list; 
};

struct v_ctrl_file {
	struct v_ctrl_device *dev;
	struct list_head node;

	spinlock_t event_lock;
	struct list_head event_list;
	wait_queue_head_t event_wait;
	int event_space;

	struct mutex endpoints_lock;
	struct list_head endpoints;
	uint32_t provider_id;
};

struct v_ctrl_endpoint {
	struct v_ctrl_file *file;
	uint32_t provider_id;
	struct drm_plane *plane;

	struct list_head node;
};

struct v_ctrl_pending_event {
	struct v_ctrl_event *event;
	struct list_head node;
};

struct v_ctrl_gem {
	struct v_ctrl_device *dev;
	struct drm_gem_object *drm_bo;

	uint32_t gem_handle;

	struct dma_buf *dmabuf_object;

	struct list_head node;
};

static DEFINE_MUTEX(v_ctrl_lock);
static int minor;
static LIST_HEAD(v_ctrl_list);

static dev_t vdrm_ctrl_major;
static struct class *vdrm_ctrl_class;

typedef int v_ctrl_ioctl_t(struct v_ctrl_device *dev, void *data,
			struct v_ctrl_file *file_priv);

struct v_ctrl_ioctl_desc {
	unsigned int cmd;
	v_ctrl_ioctl_t *func;
	const char *name;
};

#define VDRMCTRL_IOCTL_DEF(ioctl, _func)	\
	[_IOC_NR(ioctl)] = {		\
		.cmd = ioctl,			\
		.func = _func,			\
		.name = #ioctl			\
	}

uint32_t v_controller_get_gem_handle(struct v_ctrl_gem *bo)
{
	return bo->gem_handle;
}

struct v_ctrl_gem *v_controller_create_gem(struct v_ctrl_device *dev, struct drm_gem_object *bo)
{
	struct v_ctrl_gem *v_bo;

	v_bo = kzalloc(sizeof(*v_bo), GFP_KERNEL);
	if(!v_bo)
		return NULL;

	v_bo->drm_bo = bo;
	v_bo->dev = dev;

	mutex_lock(&dev->gem_list_lock);
	dev->gem_handle++;
	v_bo->gem_handle = dev->gem_handle;
	list_add_tail(&v_bo->node, &dev->gem_list);
	mutex_unlock(&dev->gem_list_lock);

	return v_bo;
}

void v_controller_delete_gem(struct v_ctrl_gem *v_bo)
{
	mutex_lock(&v_bo->dev->gem_list_lock);
	list_del(&v_bo->node);
	mutex_unlock(&v_bo->dev->gem_list_lock);

	kfree(v_bo);
}

void v_controller_endpoint_flush(struct v_ctrl_endpoint *endpoint, struct drm_framebuffer *fb,
		uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
		uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh)
{
	struct v_ctrl_file *file_priv = endpoint->file;
	struct v_ctrl_pending_event *e;
	struct v_ctrl_event_new_buffer *event;
	unsigned long flags;

	e = kzalloc(sizeof(*e), GFP_ATOMIC);
	if (e == NULL)
		return;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if(!event)
		goto no_event;

	event->prov_id = endpoint->provider_id;
	if(fb) {
		event->valid = 1;
		event->drm_format = fb->pixel_format;
		event->width = fb->width;
		event->height = fb->height;
		v_framebuffer_populate_buffer_info(fb, event->v_gem_handle,
				event->pitches, event->offsets,
				V_CTRL_NUM_GEMS_PER_BUFFER);
	} else {
		event->valid = 0;
	}
	event->src_x = sx;
	event->src_y = sy;
	event->src_w = sw;
	event->src_h = sh;
	event->dst_x = dx;
	event->dst_y = dy;
	event->dst_w = dw;
	event->dst_h = dh;

	event->base.type = V_CTRL_EVENT_TYPE_NEW_BUFFER;
	event->base.length = sizeof(*event);

	e->event = &event->base;

	spin_lock_irqsave(&file_priv->event_lock, flags);

	if (file_priv->event_space < sizeof(*event)) {
		goto err_unlock;
	}

	file_priv->event_space -= sizeof(*event);
	list_add_tail(&e->node, &file_priv->event_list);
	wake_up(&file_priv->event_wait);

	spin_unlock_irqrestore(&file_priv->event_lock, flags);

	return;

err_unlock:
	spin_unlock_irqrestore(&file_priv->event_lock, flags);
	kfree(event);
no_event:
	kfree(e);

}

static int vdrm_ctrl_provider_create(struct v_ctrl_device *dev, void *data,
			struct v_ctrl_file *f_priv)
{
	struct v_ctrl_provider_create *c = data;
	struct v_ctrl_endpoint *ept;
	struct drm_plane *plane = NULL;
	int ret = 0;

	if(c->connection_type == V_CTRL_CONNECTION_TYPE_CRTC) {
		struct drm_crtc *search;
		drm_for_each_crtc(search, dev->drmdev) {
			if(search->base.id == c->object_id) {
				plane = search->primary;
				break;
			}
		}
		if(!plane)
			ret = -ENOENT;
	} else if(c->connection_type == V_CTRL_CONNECTION_TYPE_PLANE) {
		struct drm_plane *search;
		drm_for_each_plane(search, dev->drmdev) {
			if(search->base.id == c->object_id) {
				plane = search;
				break;
			}
		}
		if(!plane)
			ret = -ENOENT;
	} else {
		ret = -EINVAL;
	}

	if(ret)
		return ret;

	ept = kzalloc(sizeof(*ept), GFP_KERNEL);
	if(!ept)
		return -ENOMEM;

	ept->file = f_priv;
	ept->plane = plane;

	ret = v_plane_install_endpoint(ept->plane, ept);
	if(ret) {
		kfree(ept);
		return ret;
	}

	mutex_lock(&f_priv->endpoints_lock);
	f_priv->provider_id++;
	ept->provider_id = f_priv->provider_id;
	list_add_tail(&ept->node, &f_priv->endpoints);
	mutex_unlock(&f_priv->endpoints_lock);

	c->prov_id = ept->provider_id;

	return 0;
}

static int vdrm_ctrl_provider_destroy(struct v_ctrl_device *dev, void *data,
			struct v_ctrl_file *f_priv)
{
	struct v_ctrl_provider_destroy *d = data;
	struct v_ctrl_endpoint *ept, *eptt;
	int ret = -EINVAL;

	mutex_lock(&f_priv->endpoints_lock);
	list_for_each_entry_safe(ept, eptt, &f_priv->endpoints, node) {
		if(ept->provider_id == d->prov_id) {
			v_plane_uninstall_endpoint(ept->plane);
			list_del(&ept->node);
			kfree(ept);
			ret = 0;
		}
	}
	mutex_unlock(&f_priv->endpoints_lock);

	return ret;
}

static int vdrm_ctrl_provider_gem_to_dmabuf(struct v_ctrl_device *dev, void *data,
			struct v_ctrl_file *f_priv)
{
	struct v_ctrl_provider_gem_to_dmabuf *d = data;
	struct v_ctrl_gem *search, *v_gem = NULL;
	int ret = 0;
	struct dma_buf *dmabuf_object;

	if(d->flags & ~(O_CLOEXEC | O_RDWR))
		return -EINVAL;

	mutex_lock(&dev->drmdev->struct_mutex);

	mutex_lock(&dev->gem_list_lock);
	list_for_each_entry(search, &dev->gem_list, node) {
		if(search->gem_handle == d->v_gem_handle) {
			v_gem = search;
			break;
		}
	}
	mutex_unlock(&dev->gem_list_lock);

	if(!v_gem) {
		ret = -ENOENT;
		goto out;
	}

	if(!v_gem->dmabuf_object) {
		dmabuf_object = v_gem_prime_export(dev->drmdev, v_gem->drm_bo, d->flags);
		if(IS_ERR(dmabuf_object)) {
			ret = PTR_ERR(dmabuf_object);
			goto out;
		}
		v_gem->dmabuf_object = dmabuf_object;
		
	}

	get_dma_buf(v_gem->dmabuf_object);
	drm_gem_object_reference(v_gem->drm_bo);

	ret = dma_buf_fd(v_gem->dmabuf_object, d->flags);
	if(ret < 0)
		goto out_fd;

	d->fd = ret;
	d->size = v_gem->drm_bo->size;
	ret = 0;
	goto out;

out_fd:
	drm_gem_object_reference(v_gem->drm_bo);	
	dma_buf_put(v_gem->dmabuf_object);
out:
	mutex_unlock(&dev->drmdev->struct_mutex);

	return ret;
}

static const struct v_ctrl_ioctl_desc v_ctrl_ioctls[] = {
	VDRMCTRL_IOCTL_DEF(V_CTRL_IOCTL_PROVIDER_CREATE, vdrm_ctrl_provider_create),
	VDRMCTRL_IOCTL_DEF(V_CTRL_IOCTL_PROVIDER_DESTROY, vdrm_ctrl_provider_destroy),
	VDRMCTRL_IOCTL_DEF(V_CTRL_IOCTL_PROVIDER_GEM_TO_DMABUF, vdrm_ctrl_provider_gem_to_dmabuf),
};

#define VDRMCTRL_IOCTL_COUNT	ARRAY_SIZE(v_ctrl_ioctls)

static int vdrm_ctrl_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct v_ctrl_device *priv;
	struct v_ctrl_file *f_priv;

	priv = container_of(inode->i_cdev, struct v_ctrl_device, vdrm_ctrl_cdev);

	mutex_lock(&priv->open_lock);

	f_priv = kzalloc(sizeof(*f_priv), GFP_KERNEL);
	if(!f_priv) {
		ret = -ENOMEM;
		goto out;
	}

	f_priv->dev = priv;
	spin_lock_init(&f_priv->event_lock);
	INIT_LIST_HEAD(&f_priv->event_list);
	init_waitqueue_head(&f_priv->event_wait);
	f_priv->event_space = 4096;

	mutex_init(&f_priv->endpoints_lock);
	INIT_LIST_HEAD(&f_priv->endpoints);
	f_priv->provider_id = 0;

	priv->open_count++;
	list_add_tail(&f_priv->node, &priv->open_files);

	filp->private_data = f_priv;

out:
	mutex_unlock(&priv->open_lock);
	return ret;
}

static int vdrm_ctrl_release(struct inode *inode, struct file *filp)
{
	struct v_ctrl_file *f_priv = filp->private_data;
	struct v_ctrl_device *priv = f_priv->dev;
	unsigned long flags;
	struct v_ctrl_pending_event *e, *et;
	struct v_ctrl_endpoint *ept, *eptt;

	mutex_lock(&priv->open_lock);

	mutex_lock(&f_priv->endpoints_lock);
	list_for_each_entry_safe(ept, eptt, &f_priv->endpoints, node) {
		v_plane_uninstall_endpoint(ept->plane);
		list_del(&ept->node);
		kfree(ept);
	}
	mutex_unlock(&f_priv->endpoints_lock);

	spin_lock_irqsave(&f_priv->event_lock, flags);
	list_for_each_entry_safe(e, et, &f_priv->event_list, node) {
		list_del(&e->node);
		kfree(e->event);
		kfree(e);
	}
	spin_unlock_irqrestore(&f_priv->event_lock, flags);

	list_del(&f_priv->node);
	kfree(f_priv);
	filp->private_data = NULL;

	priv->open_count--;

	mutex_unlock(&priv->open_lock);

	return 0;
}

static ssize_t vdrm_ctrl_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset)
{
	struct v_ctrl_file *file_priv = filp->private_data;
	ssize_t ret = 0;

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	spin_lock_irq(&file_priv->event_lock);
	for (;;) {
		if (list_empty(&file_priv->event_list)) {
			if (ret)
				break;

			if (filp->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}

			spin_unlock_irq(&file_priv->event_lock);
			ret = wait_event_interruptible(file_priv->event_wait,
						       !list_empty(&file_priv->event_list));
			spin_lock_irq(&file_priv->event_lock);
			if (ret < 0)
				break;

			ret = 0;
		} else {
			struct v_ctrl_pending_event *e;

			e = list_first_entry(&file_priv->event_list,
					     struct v_ctrl_pending_event, node);
			if (e->event->length + ret > count)
				break;

			if (__copy_to_user_inatomic(buffer + ret,
						    e->event, e->event->length)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}

			file_priv->event_space += e->event->length;
			ret += e->event->length;
			list_del(&e->node);
			kfree(e->event);
			kfree(e);
		}
	}
	spin_unlock_irq(&file_priv->event_lock);

	return ret;
}

static unsigned int vdrm_ctrl_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct v_ctrl_file *file_priv = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &file_priv->event_wait, wait);

	if (!list_empty(&file_priv->event_list))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static long vdrm_ctrl_ioctl(struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	struct v_ctrl_file *file_priv = filp->private_data;
	struct v_ctrl_device *dev = file_priv->dev;
	const struct v_ctrl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	unsigned int size;
	char *kdata = NULL;
	char __user *buffer = (char __user *)arg;
	v_ctrl_ioctl_t *func;
	int retcode = -EINVAL;

	if(nr >= VDRMCTRL_IOCTL_COUNT)
		return -EINVAL;
	ioctl = &v_ctrl_ioctls[nr];

	if(cmd != ioctl->cmd)
		return -EINVAL;

	size = _IOC_SIZE(ioctl->cmd);
	cmd = ioctl->cmd;
	func = ioctl->func;
	if (!func)
		return -EINVAL;

	if ((cmd & IOC_IN) && !access_ok(VERIFY_READ, buffer, size))
		return -EFAULT;

	if ((cmd & IOC_OUT) && !access_ok(VERIFY_WRITE, buffer, size))
		return -EFAULT;

	if (cmd & (IOC_IN | IOC_OUT)) {
		kdata = kzalloc(size, GFP_KERNEL);
		if (!kdata) {
			return -ENOMEM;
		}
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, buffer, size) != 0) {
			retcode = -EFAULT;
			goto err_i1;
		}
	}

	retcode = func(dev, kdata, file_priv);

	if (cmd & IOC_OUT) {
		if (copy_to_user(buffer, kdata, size) != 0)
			retcode = -EFAULT;
	}

err_i1:
	kfree(kdata);
	return retcode;
}

static const struct file_operations vdrm_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = vdrm_ctrl_open,
	.release = vdrm_ctrl_release,
	.unlocked_ioctl = vdrm_ctrl_ioctl,
	.read = vdrm_ctrl_read,
	.poll = vdrm_ctrl_poll,
};

struct v_ctrl_device *v_controller_create_device(struct drm_device *dev)
{
	int ret;
	struct v_ctrl_device *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if(!priv) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&v_ctrl_lock);

	priv->drmdev = dev;
	priv->minor = minor;
	priv->vdrm_ctrl_dev = MKDEV(MAJOR(vdrm_ctrl_major), priv->minor);
	minor++;

	cdev_init(&priv->vdrm_ctrl_cdev, &vdrm_ctrl_fops);
	priv->vdrm_ctrl_cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->vdrm_ctrl_cdev, priv->vdrm_ctrl_dev, 1);
	if (ret) {
		pr_err("cdev_add failed: %d\n", ret);
		goto cdev_add_fail;
	}

	priv->vdrm_ctrl_device = device_create(vdrm_ctrl_class, NULL, priv->vdrm_ctrl_dev,
			NULL, CONTROLLER_NAME "-%d", priv->minor);
	if (IS_ERR(priv->vdrm_ctrl_device)) {
		ret = PTR_ERR(priv->vdrm_ctrl_device);
		pr_err("device_create failed: %d\n", ret);
		goto device_create_fail;
	}

	list_add_tail(&priv->node, &v_ctrl_list);

	mutex_init(&priv->open_lock);
	priv->open_count = 0;
	INIT_LIST_HEAD(&priv->open_files);

	mutex_init(&priv->gem_list_lock);
	priv->gem_handle = 0;
	INIT_LIST_HEAD(&priv->gem_list);

	mutex_unlock(&v_ctrl_lock);

	return priv;

device_create_fail:
	cdev_del(&priv->vdrm_ctrl_cdev);
cdev_add_fail:
	mutex_unlock(&v_ctrl_lock);
	kfree(priv);
out:
	return ERR_PTR(ret);
}

void v_controller_delete_device(struct v_ctrl_device *priv)
{
	mutex_lock(&v_ctrl_lock);
	list_del(&priv->node);
	device_destroy(vdrm_ctrl_class, priv->vdrm_ctrl_dev);
	cdev_del(&priv->vdrm_ctrl_cdev);
	mutex_unlock(&v_ctrl_lock);
	kfree(priv);
}

int v_controller_init()
{
	int ret;

	ret = alloc_chrdev_region(&vdrm_ctrl_major, 0,
			MAX_NUM_V_CONTROLLER_DEVICES,
			CONTROLLER_NAME);
	if (ret) {
		pr_err("alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}

	vdrm_ctrl_class = class_create(THIS_MODULE, CONTROLLER_NAME);
	if (IS_ERR(vdrm_ctrl_class)) {
		ret = PTR_ERR(vdrm_ctrl_class);
		pr_err("class_create failed: %d\n", ret);
		goto class_fail;
	}

	minor = 0;

	return 0;

class_fail:
	unregister_chrdev_region(vdrm_ctrl_major, 1);
	return ret;
}

void v_controller_fini()
{
	class_destroy(vdrm_ctrl_class);
	unregister_chrdev_region(vdrm_ctrl_major, 1);
}
