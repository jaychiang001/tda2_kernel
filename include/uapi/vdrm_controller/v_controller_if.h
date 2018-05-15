#ifndef _V_CONTROLLER_IF_H_
#define _V_CONTROLLER_IF_H_

#if defined(__KERNEL__) || defined(__linux__)

#include <linux/types.h>
#include <asm/ioctl.h>

#else /* One of the BSDs */

#include <sys/ioccom.h>
#include <sys/types.h>
typedef int8_t   __s8;
typedef uint8_t  __u8;
typedef int16_t  __s16;
typedef uint16_t __u16;
typedef int32_t  __s32;
typedef uint32_t __u32;
typedef int64_t  __s64;
typedef uint64_t __u64;

#endif

#define V_CTRL_NUM_GEMS_PER_BUFFER			3

struct v_ctrl_event {
	__u32 type;
	__u32 length;
};

#define V_CTRL_EVENT_TYPE_NONE			0x1
#define V_CTRL_EVENT_TYPE_NEW_BUFFER		0x2

struct v_ctrl_event_none {
	struct v_ctrl_event base;
};

struct v_ctrl_event_new_buffer {
	struct v_ctrl_event base;

	__u32 prov_id;

	__u32 valid;
	__u32 drm_format;
	__u32 v_gem_handle[V_CTRL_NUM_GEMS_PER_BUFFER];
	__u32 pitches[V_CTRL_NUM_GEMS_PER_BUFFER];
	__u32 offsets[V_CTRL_NUM_GEMS_PER_BUFFER];
	__u32 width;
	__u32 height;

	__u32 src_x;
	__u32 src_y;
	__u32 src_w;
	__u32 src_h;
	__u32 dst_x;
	__u32 dst_y;
	__u32 dst_w;
	__u32 dst_h;
};

#define V_CTRL_CONNECTION_TYPE_CRTC		0x1
#define V_CTRL_CONNECTION_TYPE_PLANE		0x2

struct v_ctrl_provider_create {
	__u32 connection_type;
	__u32 object_id;
	__u32 prov_id;
};

struct v_ctrl_provider_destroy {
	__u32 prov_id;
};

struct v_ctrl_provider_gem_to_dmabuf {
	__u32 v_gem_handle;
	__u32 flags;
	__u32 fd;
	__u32 size;
};

#define V_CTRL_IOCTL_BASE			'd'
#define V_CTRL_IO(nr)				_IO(V_CTRL_IOCTL_BASE,nr)
#define V_CTRL_IOR(nr,type)			_IOR(V_CTRL_IOCTL_BASE,nr,type)
#define V_CTRL_IOW(nr,type)			_IOW(V_CTRL_IOCTL_BASE,nr,type)
#define V_CTRL_IOWR(nr,type)			_IOWR(V_CTRL_IOCTL_BASE,nr,type)

#define V_CTRL_PROVIDER_CREATE			0x0
#define V_CTRL_PROVIDER_DESTROY			0x1
#define V_CTRL_PROVIDER_GEM_TO_DMABUF		0x2

#define V_CTRL_IOCTL_PROVIDER_CREATE		V_CTRL_IOWR(V_CTRL_PROVIDER_CREATE, struct v_ctrl_provider_create)
#define V_CTRL_IOCTL_PROVIDER_DESTROY		V_CTRL_IOWR(V_CTRL_PROVIDER_DESTROY, struct v_ctrl_provider_destroy)
#define V_CTRL_IOCTL_PROVIDER_GEM_TO_DMABUF	V_CTRL_IOWR(V_CTRL_PROVIDER_GEM_TO_DMABUF, struct v_ctrl_provider_gem_to_dmabuf)
#endif
