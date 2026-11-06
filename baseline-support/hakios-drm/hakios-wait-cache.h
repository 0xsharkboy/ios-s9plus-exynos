/* Private BO completion facts for the pinned, sequential HakiosMali queue.
 * This never infers completion from time or skips an unfinished job. Callers
 * register only CreateBO allocations; IOSurface imports must be forgotten so
 * their kernel WAIT_BO cache invalidation always runs. Errors, sequence
 * regressions and ambiguous concurrent returns permanently select fallback.
 * All access, including lifecycle changes during an RPC, is synchronized.
 */
#ifndef HAKIOS_WAIT_CACHE_H
#define HAKIOS_WAIT_CACHE_H
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define HWC_HANDLES 4096u
struct hwc_slot { uint64_t seq; bool owned, known; };
struct hwc_cache {
   pthread_mutex_t mutex;
   uint64_t epoch, completed, submitted, hits;
   unsigned active_submits;
   bool disabled;
   struct hwc_slot slots[HWC_HANDLES];
};
#define HWC_INITIALIZER { .mutex = PTHREAD_MUTEX_INITIALIZER }
struct hwc_ticket { uint64_t epoch; bool active; };
struct hwc_query { uint64_t epoch, seq, hit_count; uint32_t handle; bool eligible, hit; };

static inline struct hwc_slot *
hwc_slot(struct hwc_cache *c, uint32_t handle)
{
   return handle && handle <= HWC_HANDLES ? &c->slots[handle - 1] : NULL;
}

static inline void
hwc_disable(struct hwc_cache *c)
{
   pthread_mutex_lock(&c->mutex);
   c->disabled = true;
   pthread_mutex_unlock(&c->mutex);
}

static inline bool
hwc_available(struct hwc_cache *c)
{
   pthread_mutex_lock(&c->mutex);
   bool result = !c->disabled;
   pthread_mutex_unlock(&c->mutex);
   return result;
}

static inline void
hwc_created(struct hwc_cache *c, uint32_t handle)
{
   pthread_mutex_lock(&c->mutex);
   struct hwc_slot *s = hwc_slot(c, handle);
   ++c->epoch;
   if (s) *s = (struct hwc_slot){ .owned = true, .known = true };
   pthread_mutex_unlock(&c->mutex);
}

static inline void
hwc_forget(struct hwc_cache *c, uint32_t handle)
{
   pthread_mutex_lock(&c->mutex);
   struct hwc_slot *s = hwc_slot(c, handle);
   ++c->epoch;
   if (s) memset(s, 0, sizeof(*s));
   pthread_mutex_unlock(&c->mutex);
}

/* Disable fast answers before handing any new submission to the kernel. */
static inline struct hwc_ticket
hwc_submit_begin(struct hwc_cache *c)
{
   pthread_mutex_lock(&c->mutex);
   struct hwc_ticket t = { .epoch = c->epoch, .active = !c->disabled };
   if (t.active) ++c->active_submits;
   pthread_mutex_unlock(&c->mutex);
   return t;
}

static inline void
hwc_submit_end(struct hwc_cache *c, struct hwc_ticket t,
               const uint32_t *handles, uint32_t count, uint64_t seq, bool success)
{
   if (!t.active) return;
   pthread_mutex_lock(&c->mutex);
   assert(c->active_submits);
   --c->active_submits;
   if (!c->disabled) {
      /* A late/out-of-order RPC return is allowed, but not optimized. This
       * also protects against a device timeline reset. Never guess an epoch. */
      if (!success || !seq || seq <= c->submitted || seq <= c->completed ||
          count > HWC_HANDLES || (count && !handles)) {
         c->disabled = true;
      } else {
         c->submitted = seq;
         for (uint32_t i = 0; i < count; ++i) {
            struct hwc_slot *s = hwc_slot(c, handles[i]);
            if (s && s->owned) {
               if (t.epoch != c->epoch) s->known = false;
               else { s->seq = seq; s->known = true; }
            }
         }
      }
   }
   pthread_mutex_unlock(&c->mutex);
}

static inline struct hwc_query
hwc_wait_query(struct hwc_cache *c, uint32_t handle)
{
   struct hwc_query q = { .handle = handle };
   pthread_mutex_lock(&c->mutex);
   struct hwc_slot *s = hwc_slot(c, handle);
   if (!c->disabled && !c->active_submits && s && s->owned && s->known) {
      q.eligible = true; q.epoch = c->epoch; q.seq = s->seq;
      q.hit = s->seq <= c->completed;
      if (q.hit) q.hit_count = ++c->hits;
   }
   pthread_mutex_unlock(&c->mutex);
   return q;
}

/* Only after an actual successful kernel WAIT_BO. A changed lifecycle or
 * another submission means this snapshot cannot establish a completion fact. */
static inline void
hwc_wait_succeeded(struct hwc_cache *c, struct hwc_query q)
{
   if (!q.eligible || q.hit) return;
   pthread_mutex_lock(&c->mutex);
   struct hwc_slot *s = hwc_slot(c, q.handle);
   if (!c->disabled && !c->active_submits && q.epoch == c->epoch &&
       s && s->owned && s->known && s->seq == q.seq && q.seq > c->completed)
      c->completed = q.seq;
   pthread_mutex_unlock(&c->mutex);
}

/* Only after a successful kernel wait for this exact global queue sequence. */
static inline void
hwc_sequence_succeeded(struct hwc_cache *c, uint64_t seq)
{
   pthread_mutex_lock(&c->mutex);
   if (!c->disabled && seq > c->completed) c->completed = seq;
   pthread_mutex_unlock(&c->mutex);
}
#endif
