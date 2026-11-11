#ifndef _OS_REFCNT_INTERNAL_H
#define _OS_REFCNT_INTERNAL_H

struct os_refcnt {
	os_ref_atomic_t ref_count;
#if OS_REFCNT_DEBUG
	struct os_refgrp *ref_group;
#endif
};

#if OS_REFCNT_DEBUG

__options_closed_decl(os_refgrp_flags_t, uint64_t, {
	OS_REFGRP_F_NONE           = 0x0,
	OS_REFGRP_F_ALWAYS_ENABLED = 0x1,
});

struct os_refgrp {
	const char *grp_name;
	os_ref_atomic_t grp_children;  /* number of refcount objects in group */
	os_ref_atomic_t grp_count;     /* current reference count of group */
	_Atomic uint64_t grp_retain_total;
	_Atomic uint64_t grp_release_total;
	struct os_refgrp *grp_parent;
	void *grp_log;                 /* refcount logging context */
	uint64_t grp_flags;            /* Unused for now. */
};

#endif

# define OS_REF_ATOMIC_INITIALIZER 0
#if OS_REFCNT_DEBUG
# define OS_REF_INITIALIZER { .ref_count = OS_REF_ATOMIC_INITIALIZER, .ref_group = NULL }
#else
# define OS_REF_INITIALIZER { .ref_count = OS_REF_ATOMIC_INITIALIZER }
#endif

__BEGIN_DECLS

#if OS_REFCNT_DEBUG
# define os_ref_if_debug(x, y) x
#else
# define os_ref_if_debug(x, y) y
#endif

void os_ref_init_count_external(os_ref_atomic_t *, struct os_refgrp *, os_ref_count_t);
void os_ref_retain_external(os_ref_atomic_t *, struct os_refgrp *);
void os_ref_retain_locked_external(os_ref_atomic_t *, struct os_refgrp *);
os_ref_count_t os_ref_release_external(os_ref_atomic_t *, struct os_refgrp *,
    memory_order release_order, memory_order dealloc_order);
os_ref_count_t os_ref_release_relaxed_external(os_ref_atomic_t *, struct os_refgrp *);
os_ref_count_t os_ref_release_barrier_external(os_ref_atomic_t *, struct os_refgrp *);
os_ref_count_t os_ref_release_locked_external(os_ref_atomic_t *, struct os_refgrp *);
bool os_ref_retain_try_external(os_ref_atomic_t *, struct os_refgrp *);

/* For now, the internal and external variants are identical */
#define os_ref_init_count_internal      os_ref_init_count_external
#define os_ref_retain_internal          os_ref_retain_external
#define os_ref_retain_locked_internal   os_ref_retain_locked_external
#define os_ref_release_internal         os_ref_release_external
#define os_ref_release_barrier_internal os_ref_release_barrier_external
#define os_ref_release_relaxed_internal os_ref_release_relaxed_external
#define os_ref_release_locked_internal  os_ref_release_locked_external
#define os_ref_retain_try_internal      os_ref_retain_try_external

static inline void
os_ref_init_count(struct os_refcnt *rc, struct os_refgrp * __unused grp, os_ref_count_t count)
{
#if OS_REFCNT_DEBUG
	rc->ref_group = grp;
#endif
	os_ref_init_count_internal(&rc->ref_count, os_ref_if_debug(rc->ref_group, NULL), count);
}

static inline void
os_ref_retain(struct os_refcnt *rc)
{
	os_ref_retain_internal(&rc->ref_count, os_ref_if_debug(rc->ref_group, NULL));
}

static inline os_ref_count_t
os_ref_release_locked(struct os_refcnt *rc)
{
	return os_ref_release_locked_internal(&rc->ref_count, os_ref_if_debug(rc->ref_group, NULL));
}

static inline void
os_ref_retain_locked(struct os_refcnt *rc)
{
	os_ref_retain_internal(&rc->ref_count, os_ref_if_debug(rc->ref_group, NULL));
}

static inline bool
os_ref_retain_try(struct os_refcnt *rc)
{
	return os_ref_retain_try_internal(&rc->ref_count, os_ref_if_debug(rc->ref_group, NULL));
}

__deprecated_msg("inefficient codegen, prefer os_ref_release / os_ref_release_relaxed")
static inline os_ref_count_t OS_WARN_RESULT
os_ref_release_explicit(struct os_refcnt *rc, memory_order release_order, memory_order dealloc_order)
{
	return os_ref_release_internal(&rc->ref_count, os_ref_if_debug(rc->ref_group, NULL),
	           release_order, dealloc_order);
}

#if OS_REFCNT_DEBUG
# define os_refgrp_initializer(name, parent, flags) \
	 { \
	        .grp_name =          (name), \
	        .grp_children =      (0u), \
	        .grp_count =         (0u), \
	        .grp_retain_total =  (0u), \
	        .grp_release_total = (0u), \
	        .grp_parent =        (parent), \
	        .grp_log =           NULL, \
	        .grp_flags =         flags, \
	}

# define os_refgrp_decl_flags(qual, var, name, parent, flags) \
	qual struct os_refgrp __attribute__((section("__DATA,__refgrps"))) var =  \
	    os_refgrp_initializer(name, parent, flags)

# define os_refgrp_decl(qual, var, name, parent) \
	os_refgrp_decl_flags(qual, var, name, parent, OS_REFGRP_F_NONE)

# define os_refgrp_decl_extern(var) \
	extern struct os_refgrp var

/* Create a default group based on the init() callsite if no explicit group
 * is provided. */
# define os_ref_init_count(rc, grp, count) ({ \
	        os_refgrp_decl(static, __grp, __func__, NULL); \
	        (os_ref_init_count)((rc), (grp) ? (grp) : &__grp, (count)); \
	})

#else /* OS_REFCNT_DEBUG */

# define os_refgrp_decl(qual, var, name, parent) extern struct os_refgrp var __attribute__((unused))
# define os_refgrp_decl_extern(var) os_refgrp_decl(, var, ,)
# define os_ref_init_count(rc, grp, count) (os_ref_init_count)((rc), NULL, (count))

#endif /* OS_REFCNT_DEBUG */

__abortlike
static inline void
os_ref_panic_live(void *rc)
{
	panic("os_refcnt: unexpected release of final reference (rc=%p)\n", rc);
	__builtin_unreachable();
}

static inline os_ref_count_t OS_WARN_RESULT
os_ref_release(struct os_refcnt *rc)
{
	return os_ref_release_barrier_internal(&rc->ref_count,
	           os_ref_if_debug(rc->ref_group, NULL));
}

static inline os_ref_count_t OS_WARN_RESULT
os_ref_release_relaxed(struct os_refcnt *rc)
{
	return os_ref_release_relaxed_internal(&rc->ref_count,
	           os_ref_if_debug(rc->ref_group, NULL));
}

static inline void
os_ref_release_live(struct os_refcnt *rc)
{
	if (__improbable(os_ref_release(rc) == 0)) {
		os_ref_panic_live(rc);
	}
}

static inline os_ref_count_t
os_ref_get_count_internal(os_ref_atomic_t *rc)
{
	return atomic_load_explicit(rc, memory_order_relaxed);
}

static inline os_ref_count_t
os_ref_get_count(struct os_refcnt *rc)
{
	return os_ref_get_count_internal(&rc->ref_count);
}


__END_DECLS

#endif /* _OS_REFCNT_INTERNAL_H */
