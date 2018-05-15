#ifndef __V_DRV_H__
#define __V_DRV_H__

#include <drm/drmP.h>
#include <drm/drm_gem.h>

#define MODULE_NAME			"vdrm"
#define MAX_NUM_VDRM_DEVICES		(4)

struct v_ctrl_device;
struct v_ctrl_endpoint;
struct v_ctrl_gem;

struct v_drm_private {

	/* lock for obj_list below */
	spinlock_t list_lock;

	/* list of GEM objects: */
	struct list_head obj_list;

	struct v_ctrl_device *vctrl;

	struct list_head crtc_infos;
	struct list_head plane_infos;

	uint32_t num_crtcs;

	struct list_head unconsumed_events;
};

struct vdrm_timings
{
	u16 x_res;
	u16 y_res;
	u32 pixelclock;
	u16 hsw;
	u16 hfp;
	u16 hbp;
	u16 vsw;
	u16 vfp;
	u16 vbp;
};

#define V_PLANE_MAX_FORMATS (32)

#if 0
#if 0
#define dbg_function_entry(dev, name, id) printk(KERN_INFO "[%s:" #name "-%d] %s\n", dev_name(dev), id, __func__)
#else
#define dbg_function_entry(dev, name, id) printk(KERN_DEBUG "[%s:" #name "-%d] %s\n", dev_name(dev), id, __func__)
#endif
#else
#define dbg_function_entry(dev, name, id)
#endif

#ifdef CONFIG_DEBUG_FS
int v_debugfs_init(struct drm_minor *minor);
void v_debugfs_cleanup(struct drm_minor *minor);
void v_gem_describe_objects(struct list_head *list, struct seq_file *m);
void v_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
#endif

int v_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int v_gem_mmap(struct file *filp, struct vm_area_struct *vma);

void v_gem_free_object(struct drm_gem_object *obj);

int v_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
int v_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);

struct dma_buf *v_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags);
struct drm_gem_object *v_gem_prime_import(struct drm_device *dev,
		struct dma_buf *buffer);
uint32_t v_gem_get_v_ctrl_gem_handle(struct drm_gem_object *bo);


int v_controller_init(void);
void v_controller_fini(void);
struct v_ctrl_device *v_controller_create_device(struct drm_device *dev);
void v_controller_delete_device(struct v_ctrl_device *vctrl);
void v_controller_endpoint_flush(struct v_ctrl_endpoint *endpoint, struct drm_framebuffer *fb,
		uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
		uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh);
struct v_ctrl_gem *v_controller_create_gem(struct v_ctrl_device *dev, struct drm_gem_object *bo);
void v_controller_delete_gem(struct v_ctrl_gem *v_bo);
uint32_t v_controller_get_gem_handle(struct v_ctrl_gem *bo);

struct drm_encoder *v_encoder_init(struct drm_device *dev, uint32_t id);
struct drm_connector *v_connector_init(struct drm_device *dev, uint32_t id, struct drm_encoder *encoder, struct device_node *np);
struct drm_plane *v_plane_init(struct drm_device *dev, uint32_t id, uint32_t crtc_bitmask, enum drm_plane_type type, struct device_node *np);
struct drm_crtc *v_crtc_init(struct drm_device *dev, uint32_t id, struct drm_plane *plane, struct device_node *np);

void v_crtc_fini(struct drm_device *dev, struct drm_crtc *crtc);
void v_plane_fini(struct drm_device *dev, struct drm_plane *plane);
void v_connector_fini(struct drm_device *dev, struct drm_connector *connector);
void v_encoder_fini(struct drm_device *dev, struct drm_encoder *encoder);

struct drm_framebuffer *v_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
int v_framebuffer_pin(struct drm_framebuffer *fb);
void v_framebuffer_unpin(struct drm_framebuffer *fb);
void v_framebuffer_populate_buffer_info(struct drm_framebuffer *fb,
		uint32_t *handles, uint32_t *pitches, uint32_t *offsets,
		uint32_t array_sz);

void vdrm_get_supported_formats(uint32_t *array, uint32_t *n);
bool vdrm_check_supported_formats(uint32_t format);

bool v_crtc_is_pending(struct drm_crtc *crtc);
int v_crtc_wait_pending(struct drm_crtc *crtc);
void v_crtc_add_plane_update(struct drm_crtc *crtc, struct drm_plane *plane,
		struct drm_framebuffer *fb,
		uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
		uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh);
bool crtc_for_device_node(struct drm_crtc *crtc, struct device_node *np);

int v_plane_install_endpoint(struct drm_plane *plane, struct v_ctrl_endpoint *endpoint);
void v_plane_uninstall_endpoint(struct drm_plane *plane);
void v_plane_flush(struct drm_plane *plane, struct drm_framebuffer *fb,
		uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
		uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh,
		bool new_flush);
#endif
