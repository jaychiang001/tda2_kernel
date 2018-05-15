#include <linux/dma-buf.h>

#include "vdrm.h"

struct v_gem_object {
	struct drm_gem_object base;

	dma_addr_t paddr;
	void *token;
	struct dma_attrs dma_attrs;

	struct sg_table *sgt;

	struct v_ctrl_gem *vctrl_gem;

	struct list_head node;
};


#define to_v_bo(x) container_of(x, struct v_gem_object, base)

uint32_t v_gem_get_v_ctrl_gem_handle(struct drm_gem_object *bo)
{
	struct v_gem_object *v_obj = to_v_bo(bo);
	return v_controller_get_gem_handle(v_obj->vctrl_gem);
}

#ifdef CONFIG_DEBUG_FS

void v_gem_describe_objects(struct list_head *list, struct seq_file *m)
{
	struct v_gem_object *v_obj;
	int count = 0;
	size_t size = 0;
	uint64_t off;

	list_for_each_entry(v_obj, list, node) {
		struct drm_gem_object *obj = &v_obj->base;
		seq_printf(m, "   ");

		off = drm_vma_node_start(&obj->vma_node);
		seq_printf(m, "%p: %2d (%2d) %08llx %pad %d\n",
				v_obj, obj->name, obj->refcount.refcount.counter,
				off, &v_obj->paddr, obj->size);

		count++;
		size += obj->size;
	}

	seq_printf(m, "Total %d objects, %zu bytes\n", count, size);
}

#endif

int v_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int ret;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct drm_device *dev = obj->dev;
	struct v_gem_object *v_obj = to_v_bo(obj);
	pgoff_t pgoff = ((unsigned long)vmf->virtual_address - vma->vm_start) >> PAGE_SHIFT;
	unsigned long pfn = (v_obj->paddr >> PAGE_SHIFT) + pgoff;

	/* Make sure we don't parallel update on a fault, nor move or remove
	 * something from beneath our feet
	 */
	mutex_lock(&dev->struct_mutex);
	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);
	mutex_unlock(&dev->struct_mutex);

	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

int v_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;
	uint64_t l_offset;
	int ret = 0;

	/* GEM does all our handle to object mapping */
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL)
		return -ENOENT;

	mutex_lock(&obj->dev->struct_mutex);
	ret = drm_gem_create_mmap_offset_size(obj, obj->size);
	if (ret) {
		l_offset = 0;
		goto out;
	}

	l_offset = drm_vma_node_offset_addr(&obj->vma_node);

out:
	*offset = l_offset;
	mutex_unlock(&obj->dev->struct_mutex);
	drm_gem_object_unreference_unlocked(obj);
	return 0;
}

/** We override mainly to fix up some of the vm mapping flags.. */
int v_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret) {
		return ret;
	}

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));

	return 0;
}

void v_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct v_drm_private *priv = dev->dev_private;
	struct v_gem_object *v_obj = to_v_bo(obj);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	spin_lock(&priv->list_lock);
	list_del(&v_obj->node);
	spin_unlock(&priv->list_lock);

	v_controller_delete_gem(v_obj->vctrl_gem);

	if (obj->import_attach) {
		drm_prime_gem_destroy(obj, v_obj->sgt);
	} else {
		dma_free_attrs(dev->dev, obj->size,
				v_obj->token, v_obj->paddr,
				&v_obj->dma_attrs);
	}

	drm_gem_object_release(obj);

	kfree(v_obj);
}

int v_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args)
{
	struct v_drm_private *priv = dev->dev_private;
	struct v_gem_object *v_obj;
	struct drm_gem_object *obj;
	int ret;

	args->pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	args->size = PAGE_ALIGN(args->pitch * args->height);

	v_obj = kzalloc(sizeof(*v_obj), GFP_KERNEL);
	if (!v_obj)
		return -ENOMEM;

	obj = &v_obj->base;

	drm_gem_private_object_init(dev, obj, args->size);

	init_dma_attrs(&v_obj->dma_attrs);
	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &v_obj->dma_attrs);
	if(NULL == (v_obj->token = dma_alloc_attrs(dev->dev, obj->size,
						 &v_obj->paddr,
						 GFP_KERNEL,
						 &v_obj->dma_attrs))) {
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	v_obj->vctrl_gem = v_controller_create_gem(priv->vctrl, obj);
	if(!v_obj->vctrl_gem) {
		ret = -ENOMEM;
		goto err_v_gem;
	}

	ret = drm_gem_handle_create(file, obj, &args->handle);
	if (ret)
		goto err_handle;

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(obj);

	spin_lock(&priv->list_lock);
	list_add_tail(&v_obj->node, &priv->obj_list);
	spin_unlock(&priv->list_lock);

	return 0;

err_handle:
	v_controller_delete_gem(v_obj->vctrl_gem);
err_v_gem:
	dma_free_attrs(dev->dev, obj->size, v_obj->token,
			v_obj->paddr, &v_obj->dma_attrs);
err_dma_alloc:
	kfree(v_obj);
	return ret;
}

struct drm_gem_object *v_gem_new_dmabuf(struct drm_device *dev, size_t size,
					   struct sg_table *sgt)
{
	struct v_drm_private *priv = dev->dev_private;
	struct v_gem_object *v_obj;
	struct drm_gem_object *obj;

	if (sgt->orig_nents != 1)
		return ERR_PTR(-EINVAL);

	mutex_lock(&dev->struct_mutex);
	v_obj = kzalloc(sizeof(*v_obj), GFP_KERNEL);
	if (!v_obj) {
		obj = ERR_PTR(-ENOMEM);
		goto out;
	}

	obj = &v_obj->base;

	drm_gem_private_object_init(dev, obj, PAGE_ALIGN(size));

	v_obj->sgt = sgt;
	v_obj->paddr = sg_dma_address(sgt->sgl);

	v_obj->vctrl_gem = v_controller_create_gem(priv->vctrl, obj);
	if(!v_obj->vctrl_gem) {
		obj = ERR_PTR(-ENOMEM);
		kfree(v_obj);
		goto out;
	}

	spin_lock(&priv->list_lock);
	list_add_tail(&v_obj->node, &priv->obj_list);
	spin_unlock(&priv->list_lock);

out:
	mutex_unlock(&dev->struct_mutex);

	return obj;
}

static struct sg_table *v_gem_map_dma_buf(
		struct dma_buf_attachment *attachment,
		enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attachment->dmabuf->priv;
	struct v_gem_object *v_obj = to_v_bo(obj);
	struct sg_table *sg;
	int ret;

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sg, 1, GFP_KERNEL);
	if (ret)
		goto out;

	sg_init_table(sg->sgl, 1);
	sg_dma_len(sg->sgl) = obj->size;
	sg_set_page(sg->sgl, pfn_to_page(PFN_DOWN(v_obj->paddr)), obj->size, 0);
	sg_dma_address(sg->sgl) = v_obj->paddr;

	return sg;
out:
	kfree(sg);
	return ERR_PTR(ret);
}

static void v_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
		struct sg_table *sg, enum dma_data_direction dir)
{
	sg_free_table(sg);
	kfree(sg);
}

static void v_gem_dmabuf_release(struct dma_buf *buffer)
{
	struct drm_gem_object *obj = buffer->priv;
	drm_gem_object_unreference_unlocked(obj);
}


static int v_gem_dmabuf_begin_cpu_access(struct dma_buf *buffer,
		enum dma_data_direction dir)
{
	return 0;
}

static void v_gem_dmabuf_end_cpu_access(struct dma_buf *buffer,
		enum dma_data_direction dir)
{
}


static void *v_gem_dmabuf_kmap_atomic(struct dma_buf *buffer,
		unsigned long page_num)
{
	struct drm_gem_object *obj = buffer->priv;
	struct v_gem_object *v_obj = to_v_bo(obj);
	return kmap_atomic(pfn_to_page(page_num + PFN_DOWN(v_obj->paddr)));
}

static void v_gem_dmabuf_kunmap_atomic(struct dma_buf *buffer,
		unsigned long page_num, void *addr)
{
	kunmap_atomic(addr);
}

static void *v_gem_dmabuf_kmap(struct dma_buf *buffer,
		unsigned long page_num)
{
	struct drm_gem_object *obj = buffer->priv;
	struct v_gem_object *v_obj = to_v_bo(obj);
	return kmap(pfn_to_page(page_num + PFN_DOWN(v_obj->paddr)));
}

static void v_gem_dmabuf_kunmap(struct dma_buf *buffer,
		unsigned long page_num, void *addr)
{
	kunmap(addr);
}

static int v_gem_dmabuf_mmap(struct dma_buf *buffer,
		struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = buffer->priv;
	int ret = 0;

	if (WARN_ON(!obj->filp))
		return -EINVAL;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	return 0;
}

static struct dma_buf_ops v_dmabuf_ops = {
	.map_dma_buf = v_gem_map_dma_buf,
	.unmap_dma_buf = v_gem_unmap_dma_buf,
	.release = v_gem_dmabuf_release,
	.begin_cpu_access = v_gem_dmabuf_begin_cpu_access,
	.end_cpu_access = v_gem_dmabuf_end_cpu_access,
	.kmap_atomic = v_gem_dmabuf_kmap_atomic,
	.kunmap_atomic = v_gem_dmabuf_kunmap_atomic,
	.kmap = v_gem_dmabuf_kmap,
	.kunmap = v_gem_dmabuf_kunmap,
	.mmap = v_gem_dmabuf_mmap,
};

struct dma_buf *v_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &v_dmabuf_ops;
	exp_info.size = obj->size;
	exp_info.flags = flags;
	exp_info.priv = obj;

	return dma_buf_export(&exp_info);
}

struct drm_gem_object *v_gem_prime_import(struct drm_device *dev,
					     struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;
	struct sg_table *sgt;
	int ret;

	if (dma_buf->ops == &v_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_reference(obj);
			return obj;
		}
	}

	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	obj = v_gem_new_dmabuf(dev, dma_buf->size, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	obj->import_attach = attach;

	return obj;

fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}
